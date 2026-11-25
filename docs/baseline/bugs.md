# OS_proj7-dev 基线全仓库缺陷诊断与修复清单（`bugs.md`）

本文档以仓库**原始未改动基线（`e5fcf65` / `4b1dda0`）**为唯一审计对象，汇总全仓库深度只读审计发现的全部真实代码缺陷（Bug），供按清单自主逐项排查与修复。文档与姊妹篇 `traps.md` 严格解耦：本卷专注**基线中真实存在的编译、运行、并发、内存安全、文件系统语义及测试框架缺陷**；微内核化迁移的架构约束与工程红线见 `traps.md`。

- **严重级别**：`P0`（阻断构建、内核 Panic、死锁、内存/磁盘静默损坏、跨进程 UAF 或核心测试假阳性放行崩溃）｜ `P1`（高危并发竞态、边界绕过、资源泄漏、W^X 失效、错误语义失真）｜ `P2`（畸形输入边界、规范兼容性、性能退化或脚本健壮性）

---

## 一、缺陷分类总览

| 类别                           | P0     | P1     | P2    | 合计     |
|:---------------------------- |:------:|:------:|:-----:|:------:|
| **一、双架构 HAL、虚拟内存与中断/驱动层**    | 4      | 9      | 1     | 14     |
| **二、VFS、三大文件系统与块缓冲层**        | 7      | 10     | 2     | 19     |
| **三、进程管理、ELF 加载、管道、锁原语与控制台** | 1      | 8      | 2     | 11     |
| **四、构建系统、用户态程序与测试框架**        | 4      | 7      | 2     | 13     |
| **总计（全部为原始基线待修项）**           | **16** | **34** | **7** | **57** |

---

## 二、双架构 HAL、虚拟内存与中断/驱动层（14 项）

### - [ ] [BUG-HAL-01]（原 BUG-20）LoongArch64 副核未初始化页表与异常入口 CSR，调度即跳 `0x0` 死持锁

- **位置**：`src/kernel/main.c:35-43`、`src/kernel/arch/loongarch/entry.S:14`、`src/kernel/arch/loongarch/vm.c:21-40`、`src/kernel/arch/loongarch/trap.c:48-65`
- **严重级别**：`P0` ｜ **核验方式**：静态核验
- **现象与根因**：`entry.S` 将 `TLBRENTRY` 清零；仅 CPU 0 调用的 `kvminit()` 与 `trapinit()` 写入了 `PGDL`/`PWCL`/`PWCH`/`ECFG`/`TCFG`/`EENTRY`/`TLBRENTRY`/`MERRENTRY`。副核调用的 `kvminithart()` 仅执行 `tlbinit()`，`trapinithart()` 为空函数。副核进入 `scheduler()` 持 `p->lock` 切至 `KSTACK` 触发 TLB 重填时直接跳向 `0x0` 并永久死持锁。
- **修复方案**：保存全局 `kernel_pagetable`，在 `kvminithart()` 补齐 `PGDL`/`PWCL`/`PWCH` + `tlbinit()`，在 `trapinithart()` 补齐 `ECFG`/`TCFG`/`EENTRY`/`TLBRENTRY`/`MERRENTRY`（注意不要在 `trapinithart()` 内过早 `intr_on()`，见 `BUG-HAL-02`）。

### - [ ] [BUG-HAL-02] LoongArch64 `trapinit()` 在初始化完成前过早开中断，存在 `virtio_disk_watchdog()` 空指针 Panic

- **位置**：`src/kernel/arch/loongarch/trap.c:48-60, 214-219`、`src/kernel/main.c:23-35`、`src/kernel/virtio_disk.c:499-520`
- **严重级别**：`P0` ｜ **核验方式**：静态核验
- **现象与根因**：CPU 0 在 `main.c:23` 调用 `trapinit()` 时即开启 `CSR_TCFG_EN | CSR_TCFG_PER` 定时器并调用 `intr_on()`（L59），此时 `hal_disk_init()`（`main.c:31`）尚未执行，`disk.used == NULL`。若时钟中断提前到达，`devintr()` 调用 `virtio_disk_watchdog()` → `virtio_disk_pending()` 直接解引用 `disk.used->idx` 崩溃。当前仅因默认时钟周期长达 167.8 ms 而侥幸未触发，一旦缩短周期（`BUG-HAL-13`）将稳定崩溃。
- **修复方案**：移除 `trapinit()`（以及后续迁入 `trapinithart()` 时）中的 `intr_on()`，与 RISC-V 统一由 `scheduler()` 首次开中断；并在 `virtio_disk_pending()` 增加 `if(disk.used == 0) return 0;`。

### - [ ] [BUG-HAL-03] LoongArch64 全部内核 MMIO 走 `DMWIN0`（`MAT=1` 可缓存），未启用非缓存窗口

- **位置**：`src/kernel/arch/loongarch/entry.S:8-11`、`src/kernel/memlayout.h:3-19`、`src/kernel/arch/loongarch/apic.c:12-32`、`src/kernel/arch/loongarch/virtio_pci.c:55-88`
- **严重级别**：`P0`（真机/带缓存仿真器）/ `P1`（QEMU TCG） ｜ **核验方式**：静态核验
- **现象与根因**：`entry.S:8` 将 `DMWIN0` 设为 `0x9000000000000011`（`MAT=01` 一致可缓存）并关闭 `DMWIN1`，但 `memlayout.h` 将 `UART0`、`LS7A_PCH_REG_BASE`、`PCI_ECAM`、`PCI_MMIO_BASE` 全部挂在 `DMWIN_MASK` 下。真机上轮询设备寄存器会命中陈旧缓存行并合并写；微内核用户态以 `MAT=0` 映射同一 MMIO 页还会引发 Cache Aliasing。
- **修复方案**：在 `entry.S` 开启 `DMWIN1 = 0x8000000000000001`（`PLV0=1, MAT=0` SUC），定义 `DMWIN_UC_MASK 0x8000000000000000UL`，将所有 MMIO 基址改挂 `DMWIN_UC_MASK`。

### - [ ] [BUG-HAL-04] LoongArch64 `kfree()` 下界错用 `end` 而非 `RAMBASE`，放行 2.2 GiB 非 RAM/MMIO 空洞

- **位置**：`src/kernel/kalloc.c:30-35, 51-60`、`src/kernel/memlayout.h:21-24`、`src/kernel/arch/loongarch/kernel.ld:6, 47`
- **严重级别**：`P0` ｜ **核验方式**：静态核验
- **现象与根因**：LoongArch64 内核链接于 `0x9000000000200000`（`end` 紧随其后），而 `kinit()` 实际管理的内存池位于 `[RAMBASE, PHYSTOP)`（`0x9000000090000000` 起 128 MiB）。`kfree()` 第 55 行仍检查 `(char*)pa < end`，导致落入 `[end, RAMBASE)` 空洞（覆盖全部 MMIO 寄存器）的地址被误传 `kfree()` 时不会 Panic，反而执行 `memset(pa, 1, PGSIZE)` 写毁硬件寄存器并挂入空闲链表。
- **修复方案**：在 `kfree()` 中针对 `ARCH_LOONGARCH` 使用 `(uint64)pa < RAMBASE || (uint64)pa >= PHYSTOP`。

### - [ ] [BUG-HAL-05]（原 BUG-06）LoongArch64 `usertrap()` 无锁读写 `p->killed`，`kerneltrap()` 无锁读 `p->state`

- **位置**：`src/kernel/arch/loongarch/trap.c:85, 101, 104, 158`
- **严重级别**：`P1` ｜ **核验方式**：静态核验
- **现象与根因**：LoongArch64 `usertrap()` 直接读写 `p->killed` 而未调用加锁接口 `killed(p)`/`setkilled(p)`；`kerneltrap()` 直接无锁检查 `myproc()->state == RUNNING`，在 `CPUS=3` 并发 `kill()` 下存在数据竞争。
- **修复方案**：统一改为 `killed(p)` / `setkilled(p)`，并参照 RISC-V 移除 `kerneltrap()` 中的无锁 `state` 判断。

### - [ ] [BUG-HAL-06] `hal_irq_claim()` / `hal_irq_complete()` 使用 32 位 `int` 截断 LoongArch64 64 位 EXTIOI 位图

- **位置**：`src/kernel/defs.h:30-31`、`src/kernel/hal.c:24-34`、`src/kernel/arch/loongarch/hal.c:23-40`、`src/kernel/trap.c:194`
- **严重级别**：`P1` ｜ **核验方式**：静态核验
- **现象与根因**：LoongArch64 `extioi_claim()` 返回 64 位 `uint64` 位图，经 HAL 层的 `int` 返回值与参数传递时会静默截断高 32 位 IRQ（编号 `>= 32` 的外设中断全丢）。
- **修复方案**：将双架构 `hal_irq_claim()` / `hal_irq_complete()` 及调用处变量统一升级为 `uint64`。

### - [ ] [BUG-HAL-07] LoongArch64 `devintr()` 对 EXTIOI 位图使用互斥 `if / else if`，并发中断漏处理且未知位永不清除

- **位置**：`src/kernel/arch/loongarch/trap.c:186-212`
- **严重级别**：`P1` ｜ **核验方式**：静态核验
- **现象与根因**：EXTIOI 返回多比特位图，但 `devintr()` 用 `if (irq & UART0) ... else if (irq & VIRTIO0) ... else if (irq) ...` 互斥处理。UART0 与 VIRTIO0 同时置位时磁盘中断会被饿死；未知位与已知位同时置位时永远不会调用 `hal_irq_complete()`，触发持续中断风暴。
- **修复方案**：改为独立 `if` 分支并累计 `handled` 掩码，末尾对 `irq & ~handled` 打印告警并执行 `hal_irq_complete(irq & ~handled)`。

### - [ ] [BUG-HAL-08]（原 BUG-22 串口部分）LoongArch64 `UART0` 边沿触发存在 RX 与 TX 丢中断，且 `uartputc_sync()` 无锁竞争 `THR`

- **位置**：`src/kernel/arch/loongarch/apic.c:14`、`src/kernel/arch/loongarch/trap.c:188-200`、`src/kernel/uart.c:81-162`
- **严重级别**：`P1` ｜ **核验方式**：静态核验
- **现象与根因**：`UART0_IRQ` 在 LS7A APIC 中配置为边沿触发，若新 RX 字节或 TX 空边沿落在 `uartintr()` 与 `hal_irq_complete()` 之间，中断永久丢失导致读/写控制台进程卡死；此外 `uartputc_sync()` 不持 `tx_lock` 直接写 `THR`，与 `uartwrite()` 竞争发送寄存器。
- **修复方案**：新增 `uart_rx_pending()` 与 `uart_tx_pending()`，在 `devintr()` 中以 `do { uartintr(); hal_irq_complete(...); } while(uart_rx_pending() || uart_tx_pending());` 闭环排空；非 panic 路径下同步 `uartputc_sync()` 与 `tx_lock`。

### - [ ] [BUG-HAL-09] LoongArch64 `tlbrefill.S` 未检查空页目录项，缺页遍历会读取物理地址 `0x0` 页作为伪页表

- **位置**：`src/kernel/arch/loongarch/tlbrefill.S:6-22`
- **严重级别**：`P1` ｜ **核验方式**：静态核验
- **现象与根因**：`lddir` 不检查目录项是否为 0。访问未分配中间页目录的懒分配或非法地址时，`lddir` 读出 `0x0`，后续在 `CRMD.DA=1` 物理直访下直接读取物理地址 `0x0..0xfff` 并装入 TLB。若物理页 0 存有非零数据即造成越权访问。
- **修复方案**：在 `tlbrefill.S` 每次 `lddir` 后判断 `beqz $t0, 1f`，将 `TLBRELO0/1` 清零后 `tlbfill`（或使用静态全零哨兵页）。

### - [ ] [BUG-HAL-10] LoongArch64 页表权限位 `PTE_NX`/`PTE_NR`/`PTE_RPLV` 被 32 位 `int`/`uint` 截断，用户栈/堆全员 RWX

- **位置**：`src/kernel/arch/loongarch/loongarch.h:305-315`、`src/kernel/arch/loongarch/vm.c:41, 100, 173, 192, 224-225, 276, 285-289`、`src/kernel/exec.c:13-21`、`src/kernel/arch/loongarch/trap.c:40-44`
- **严重级别**：`P1` ｜ **核验方式**：静态核验
- **现象与根因**：`PTE_NX = (1UL << 62)` 等高 3 位权限在 `mappages`/`kvmmap`/`uvmalloc`/`flags2perm`（32 位 `int`）与 `uvmcopy`（32 位 `uint flags`）中被静默截断为 0；且 `loongarch.h:309` 令 `PTE_X = 0`，`la_page_fault()` 放行取指缺页 `LA_ECODE_PIF`。导致 LoongArch64 所有数据段、栈、堆、懒分配页及用户态 MMIO 均丧失 W^X 保护。
- **修复方案**：双架构页表权限参数与 `uvmcopy` `flags` 统一改为 `uint64`；LoongArch64 对非可执行映射显式置 `PTE_NX`，并从 `la_page_fault()` 移除 `LA_ECODE_PIF`。

### - [ ] [BUG-HAL-11] 双架构 `copyin` / `copyinstr` / `copyout` 缺页处理与 `MAXVA` 上界校验不一致

- **位置**：`src/kernel/vm.c:386-425`、`src/kernel/arch/loongarch/vm.c:312-340`
- **严重级别**：`P1` ｜ **核验方式**：静态核验
- **现象与根因**：(1) RISC-V `copyin()` 缺页时调用 `vmfault(pagetable, va0, 0)` 传入了写标志 `0` 而非读标志 `1`；(2) RISC-V `copyinstr()` 完全未调用 `vmfault()`，导致将懒分配内存中的字符串路径传给 `open`/`exec` 等系统调用直接失败；(3) LoongArch64 `copyout()` 缺少 `if(va0 >= MAXVA) return -1;` 校验。
- **修复方案**：修正 RISC-V `copyin()` 的 `vmfault(..., 1)`、为 `copyinstr()` 补齐 `vmfault(..., 1)`，并为 LoongArch64 `copyout()` 补齐 `va0 >= MAXVA` 检查。

### - [ ] [BUG-HAL-12] 双架构 `uvmunmap()` / `uvmdealloc()` 解除映射后未刷新 TLB，存在缩容后 UAF 隐患

- **位置**：`src/kernel/vm.c:193-212, 249-260`、`src/kernel/arch/loongarch/vm.c:125-146, 234-245`
- **严重级别**：`P1`（启用 ASID 或共享页时升为 `P0`） ｜ **核验方式**：静态核验
- **现象与根因**：`uvmunmap(..., do_free=1)` 清空 PTE 并 `kfree()` 物理页后未刷新 TLB，目前仅靠返回用户态全刷 TLB 掩盖。一旦启用 ASID 或多核共享内存，陈旧 TLB 将导致跨进程 UAF。
- **修复方案**：在 `uvmunmap()` 清除有效 PTE 后执行精确页 TLB 失效（RISC-V `sfence.vma` / LoongArch `invtlb`），跨核共享页配合 TLB Shootdown IPI。

### - [ ] [BUG-HAL-13]（原 BUG-21）LoongArch64 默认时钟周期达 167.8 ms，且双架构 `wakeup()` 缺失核间 IPI 唤醒 `idle` 副核

- **位置**：`src/kernel/arch/loongarch/trap.c:53`、`src/kernel/proc.c:468-471, 592-605`
- **严重级别**：`P1` ｜ **核验方式**：静态核验
- **现象与根因**：100 MHz 稳定计数器下 `tcfg = 0x1000000UL` 对应周期 **167.8 ms**；`wakeup()` 不发 IPI，处于 `idle 0`/`wfi` 的副核最坏需干等 167.8 ms 才能响应跨核唤醒。
- **修复方案**：先修复 `BUG-HAL-02`，再将龙芯 `TCFG` 缩至 ~10 ms（按比例换算逻辑 `ticks`），并在双架构引入轻量 IPI 唤醒。

### - [ ] [BUG-HAL-14] `virtio_disk.c` Watchdog 无锁读共享状态、缺 `id` 校验，`virtio_pci.c` 缺 64 位写拆分与 MMIO 屏障

- **位置**：`src/kernel/virtio_disk.c:476-488, 499-520`、`src/kernel/arch/loongarch/virtio_pci.c:219-315`
- **严重级别**：`P2` ｜ **核验方式**：静态核验
- **现象与根因**：`virtio_disk_watchdog()` 非原子读 `disk.vdisk_lock.locked` 且无锁读 `disk.used_idx != disk.used->idx`；`virtio_disk_intr()` 未校验 `id < NUM && disk.info[id].b != 0`；`virtio_pci.c` 对 64 位队列地址直接 64 位 MMIO 写且缺 `__sync_synchronize()`。
- **修复方案**：`watchdog` 改用非阻塞 `try_acquire`；`virtio_disk_intr` 增检 `id`/`b`；`virtio_pci` 拆分为低/高 32 位写并补齐内存屏障。

---

## 三、VFS、三大文件系统与块缓冲层（19 项）

### - [ ] [BUG-FS-01] `iput()` 释放 ext2/FAT32 孤儿节点与 `ext2_iget`/`fat32_iget` 无锁快路径存在状态撕裂与 UAF 竞态

- **位置**：`src/kernel/fs.c:329-330, 374-399`、`src/kernel/ext2.c:233-240, 1130-1135`、`src/kernel/fat32.c:266-275, 428-438, 976-985`
- **严重级别**：`P0` ｜ **核验方式**：静态核验
- **现象与根因**：`iput()` 在持 `ip->lock` 期间调用 `vfs_itrunc(ip)`，此时 `ip->ref == 1` 且 `ip->valid == 1`。`ext2_itrunc`/`fat32_itrunc` 先将 `inum`/首簇归还位图/FAT（FAT32 立刻将 `next_free` 回退至此簇），之后 `fs.c:386` 才清 `ip->valid = 0`。并发 `create()` 复用该 `inum`/簇后，`ext2_iget`/`fat32_iget` 走无锁快路径跳过 `ip->lock` 拿到濒死 inode，随后被清 `valid=0` 与 `addrs[0]=0`（而 `ilock()` 对非 `FS_XV6` 不重读磁盘）。
- **修复方案**：在 `iput()` 调用 `vfs_itrunc(ip)` **之前**（持 `ip->lock` 时）执行 `__atomic_store_n(&ip->valid, 0, __ATOMIC_RELEASE)` 失效快路径。

### - [ ] [BUG-FS-02] `ext2_find_dirent()` 整结构体拷贝 263B `ext2_dirent` 触发内核栈越界读，且目录遍历缺 `name_len` 校验

- **位置**：`src/kernel/ext2.c:87-93, 616-675, 700-725, 990-1010`
- **严重级别**：`P0` ｜ **核验方式**：静态核验
- **现象与根因**：`struct ext2_dirent` 含 `char name[255]` 共 263B。`ext2_find_dirent()` 对栈上 `buf[BSIZE]` 偏移 `pos` 处的条目直接整结构体赋值 `*out = *de;`，当 `pos > 761` 时发生最多 251B 内核栈越界读；此外各目录扫描函数均未检查 `8 + de->name_len <= de->rec_len`。
- **修复方案**：改用 `memmove` 按 `ext2_min(de->rec_len, sizeof(*out))`（或 8B 头 + `name_len`）安全拷贝，并在所有目录循环增加 `8 + de->name_len > de->rec_len` 拒绝条件。

### - [ ] [BUG-FS-03] `ext2` 持缓冲区锁调用分配/释放器违反锁序，叠加 `NBUF == LOGBLOCKS == 30` 可致 `bget: no buffers` Panic 或死锁

- **位置**：`src/kernel/ext2.c:102-105, 400-590, 1080-1125`、`src/kernel/param.h:12-14`、`src/kernel/bio.c:77-88`、`src/kernel/log.c:184-191, 221-234`
- **严重级别**：`P0` ｜ **核验方式**：静态核验
- **现象与根因**：`ext2_bmap`/`ext2_bmap_double`/`ext2_itrunc` 先持间接块 `b->lock` 再调 `ext2_alloc_block`/`ext2_free_block` 拿 `fs->lock` 与位图/超级块 `b->lock`（违反 `inode->lock -> fs->lock -> b->lock` 锁序）；`ext2_free_block`/`ext2_free_inode` 持位图 `bp->lock` 写超级块与 GD，使 `ext2_itrunc` 最多同时钉住 4~5 个缓冲区。全局 `NBUF = 30 == LOGBLOCKS` 且 `log_write()` 会 `bpin`，高并发下直接耗尽 `bcache` 触发 `panic("bget: no buffers")`，畸形块号自指还会自死锁。
- **修复方案**：释放器写回位图后先 `brelse(bp)` 再写超级块与 GD；`ext2_bmap`/`ext2_itrunc` 消除持 `b->lock` 调分配/释放器；扩大 `NBUF` 至 `LOGBLOCKS + 32` 以上。

### - [ ] [BUG-FS-04]（原 BUG-18）FAT32 删除后仍打开的文件保留已释放目录项位置，后续写入覆写复用槽位

- **位置**：`src/kernel/fat32.c:550-594, 953, 1142-1156`
- **严重级别**：`P0` ｜ **核验方式**：静态核验
- **现象与根因**：`fat32_unlink()` 删除目录项并清零 `nlink`，但未清零 `ip->addrs[1]`（目录槽磁盘偏移），且 `fat32_sync_inode()` 未检查 `ip->nlink == 0`。若同目录新建文件复用该槽位，旧匿名 FD 的 `fat32_writei()` → `fat32_sync_inode()` 会直接把旧文件长度与首簇写回新文件的目录项；同时 `fat32_remove_entry()` 与 `fat32_sync_inode()` 未传播 I/O 失败。
- **修复方案**：`fat32_unlink()` 成功删除目录项后清零 `ip->addrs[1] = 0`，`fat32_sync_inode()` 入口加 `if(ip->addrs[1] == 0 || ip->nlink == 0) return 0;`，并严格传播读写错误。

### - [ ] [BUG-FS-05] `vfs_umount()` 按指针比对挂载点且未拦截活跃子挂载，父挂载卸载致子挂载悬空孤儿

- **位置**：`src/kernel/vfs.c:296-325`
- **严重级别**：`P0` ｜ **核验方式**：静态核验
- **现象与根因**：`vfs_umount()` 查找挂载项时仅比较 `mountpoint == mp` 指针，且在置 `MOUNT_UNMOUNTING` 前未检查是否存在挂在当前挂载之下的活跃子挂载（如 `/mnt/nest` 上挂了 fat32 时尝试 `umount /mnt`）。若仅靠 `vfs_mount_busy()` 拦截或指针不一致，存在状态切换窗口与父卸载后子挂载悬空风险。
- **修复方案**：统一按 `(dev, inum)` 匹配挂载点，并在 `mtable.lock` 保护下、切换 `MOUNT_UNMOUNTING` 之前显式扫描拒绝存在任意子挂载（`mountpoint->mnt == mnt || parent->mnt == mnt`）的卸载。

### - [ ] [BUG-FS-06] 向已 `unlink` 的孤儿目录（`dp->nlink == 0`）执行 `create`/`link` 导致目录“复活”与子项泄漏；`ext2_link` 无锁读字段

- **位置**：`src/kernel/sysfile.c:274-290`、`src/kernel/vfs.c:27-50`、`src/kernel/ext2.c:842-860, 950-986`、`src/kernel/fat32.c:1025-1055`
- **严重级别**：`P0` ｜ **核验方式**：静态核验
- **现象与根因**：孤儿目录被 `cwd` 持有时 `dp->nlink == 0`。向其新建文件会在 `iput(dp)` 释放目录块时永久泄漏子 inode；向其 `mkdir` 会执行 `dp->nlink++`（`0 -> 1`）导致已删除目录错误“复活”永不回收。此外 `ext2_link()` 在持锁前无锁读 `ip->type`/`dp->type`，且 `xv6_link()`/`ext2_link()` 均未检查 `ip->nlink < 1` 与 `dp->nlink < 1`。
- **修复方案**：在 `create()`、`ext2_create()`、`fat32_create()` 中拦截 `dp->nlink < 1`；在 `xv6_link()` 与 `ext2_link()` 持锁后校验 `ip->nlink >= 1`、`dp->type == T_DIR && dp->nlink >= 1`，并用 `ext2_find_dirent` 替代 `ext2_lookup` 查重。

### - [ ] [BUG-FS-07] `ext2_bmap()` 与 `ext2_bmap_double()` 在下层数据块分配失败时未回滚释放新分配的间接块

- **位置**：`src/kernel/ext2.c:495-591, 794-809`
- **严重级别**：`P0` ｜ **核验方式**：静态核验
- **现象与根因**：空间耗尽时，`ext2_bmap`/`ext2_bmap_double` 成功分配一级/二级间接块后若数据块分配返回 `0`，未回滚释放刚分配的空间接块。当调用方为 `ext2_dir_add()`（失败直接返回 `-1` 不调用 `ext2_sync_inode`）时，新分配的间接块未落盘并在 inode 淘汰后永久泄漏。
- **修复方案**：跟踪本次新分配的 `new_ind`/`new_outer`/`new_inner` 标记，下层分配失败时逆序清零上层槽位并调用 `ext2_free_block()` 回滚释放。

### - [ ] [BUG-FS-08] `fileread()` / `filewrite()` 未拦截负数长度 `n < 0`，目录读取无符号回绕绕过检查

- **位置**：`src/kernel/file.c:107-142`
- **严重级别**：`P1` ｜ **核验方式**：静态核验
- **现象与根因**：`sys_read`/`sys_write` 传入有符号 `int n`（如 `-1`），`fileread()` 传给 `vfs_readi(..., uint n)` 后回绕为 `0xFFFFFFFF`，在 `ext2_readi`/`fat32_readi` 的目录分支绕过长度校验向用户态写入 16B 目录项；对管道/控制台 `filewrite(..., -1)` 则返回 `0` 而非 `-1`。
- **修复方案**：在 `fileread()` 与 `filewrite()` 入口统一增加 `if(n < 0) return -1;`。

### - [ ] [BUG-FS-09] `ext2_writei()` / `fat32_writei()` 先分配块/簇再 `either_copyin()` 致非法指针泄漏磁盘块，且缺 `off > ip->size` 校验

- **位置**：`src/kernel/ext2.c:1035-1064`、`src/kernel/fat32.c:925-960`
- **严重级别**：`P1` ｜ **核验方式**：静态核验
- **现象与根因**：循环内先 `ext2_bmap(..., 1)`/`fat32_cluster_at(..., 1)` 分配块/簇，后调 `either_copyin()`。用户传入非法指针（`badwrite`）时直接 `return -1` 跳过 `sync_inode`，新分配块/簇永久泄漏且白耗磁盘 I/O；两函数均未检 `off > ip->size`，`ext2_writei` 未检 `ip->type != T_FILE` 且忽略 `ext2_sync_inode` 返回值。
- **修复方案**：先校验 `ip->type != T_FILE || off > ip->size || off + n < off`；循环内先 `either_copyin` 验证用户地址合法后再分配块/簇（**注意**：`ext2_writei` 应直接操作 `bread()` 缓冲或仅用单个 `BSIZE` 栈缓冲，**切勿在 4KB 内核栈上同时分配两个 `BSIZE` 数组**以防爆栈），并检查 `sync_inode` 返回值。

### - [ ] [BUG-FS-10] `ext2_find_dirent()` 缺失 14 字节长文件名截断别名匹配，导致超长文件名无法 `unlink`

- **位置**：`src/kernel/ext2.c:639-676`
- **严重级别**：`P1` ｜ **核验方式**：静态核验
- **现象与根因**：`ext2_lookup()` 支持 `> DIRSIZ`（14 字节）长名的截断别名查找（如 `verylongfilename.txt`），但 `ext2_find_dirent()` 仅匹配 `name_len <= DIRSIZ`，导致长名文件可读不可删，且 `ext2_link()` 重名检测失效。
- **修复方案**：在 `ext2_find_dirent()` 补齐与 `ext2_lookup()` 对称的 14 字节截断别名回退匹配。

### - [ ] [BUG-FS-11] `fat32_unlink()` 不支持 LFN 长文件名删除且遗留孤儿 LFN 槽位；`fat32_create()` 漏检 LFN 重名且回滚无锁

- **位置**：`src/kernel/fat32.c:1025-1155`
- **严重级别**：`P1` ｜ **核验方式**：静态核验
- **现象与根因**：(1) `fat32_unlink()` 仅查 8.3 短名，无法按 LFN（或 14B 截断别名）删除，按短名删也会遗留前置 LFN 碎片槽；(2) `fat32_create()` 仅用 `fat32_find_entry(dp, name83)` 查重，漏检同目录已存在的 LFN 同名文件；(3) `fat32_create()` 失败回滚时无锁调用 `fat32_write_fat(fs, clus, 0)` 且未回退 `fs->next_free`。
- **修复方案**：`fat32_unlink()` 跟踪并清理关联 LFN 槽（**注意先删 8.3 短名槽再删 LFN 槽**，避免掉电后文件退化为 8.3 别名）；`fat32_create()` 补齐 LFN 查重并改用加锁的 `fat32_free_chain(fs, clus)` 回滚。

### - [ ] [BUG-FS-12] `fat32_lookup(dp, "..")` 无法解析标准 FAT32 一级子目录中簇号为 `0` 的 `".."` 目录项

- **位置**：`src/kernel/fat32.c:1005-1020`
- **严重级别**：`P1` ｜ **核验方式**：静态核验
- **现象与根因**：按 Microsoft FAT32 规范，根目录直属子目录的 `".."` 条目首簇号必须为 `0`。原 `fat32_lookup()` 找到 `".."` 后直接调用 `fat32_iget()`，被 `if(clus < 2) return 0;` 拦截导致查找失败。
- **修复方案**：当 `".."` 条目的 `fat32_first_cluster(&de) == 0` 且 `dp->mnt->root != 0` 时，直接返回 `idup(dp->mnt->root)`。

### - [ ] [BUG-FS-13] FAT32 拒绝 `first_cluster == 0` 的合法空文件，导致 0 字节文件变幽灵项

- **位置**：`src/kernel/fat32.c:420-425, 895-905, 1040-1055`
- **严重级别**：`P1` ｜ **核验方式**：静态核验
- **现象与根因**：宿主工具创建的 0 字节 FAT32 文件首簇号为 `0`。`fat32_iget()` 拒绝 `clus < 2` 返回 `0`，`fat32_readi()` 输出 `xde.inum = 0` 被 `ls` 跳过，导致 0B 文件不可列、不可开、不可删，且 LFN 空文件会绕过基于 `fat32_iget` 的查重。
- **修复方案**：为空文件分配基于 `entry_off` 的合成非零 `inum`，首次写入时按需分配首簇并回写目录项；查重直接比对目录项。

### - [ ] [BUG-FS-14] `fat32_root_inode()` 硬编码 `inum = 2` 冲突，`fat32_cluster_at()`/`fat32_find_free_entry()` 改 FAT 链缺 `fs->lock`

- **位置**：`src/kernel/fat32.c:294-311, 441-460, 535-547`
- **严重级别**：`P1` ｜ **核验方式**：静态核验
- **现象与根因**：`fat32_root_inode()` 硬编码 `iget(mnt->dev, FAT32_ROOT_INO)`（`2`）而非 `fat32fs.root_clus`，当镜像 `root_clus != 2` 且簇 2 分配给普通文件时发生 `itable` 编号碰撞；`fat32_cluster_at()` 与 `fat32_find_free_entry()` 扩展簇链写 FAT 表及失败回滚均未持有 `fs->lock`，且未回退 `next_free`。
- **修复方案**：`fat32_root_inode()` 改用 `iget(mnt->dev, fat32fs.root_clus)`；FAT 链接与回滚全部纳入 `acquiresleep(&fs->lock)` 保护。

### - [ ] [BUG-FS-15] `vfs_mount()` 未校验 `dev` 白名单与独占，`virtio_disk_rw()` 未知 `dev` 静默回落 `ROOTDEV`

- **位置**：`src/kernel/vfs.c:146, 207-225`、`src/kernel/virtio_disk.c:375-380`
- **严重级别**：`P1` ｜ **核验方式**：静态核验
- **现象与根因**：`vfs_mount()` 仅检查 `dev <= 0`，允许 `dev == ROOTDEV (1)`、非法 `dev` 或重复挂载同一 `dev`；`virtio_disk_rw()` 对 `EXT2DEV`/`FAT32DEV` 之外的所有 `dev` 静默回落 `blockno += 0` 读写 `ROOTDEV`，造成 `bcache`/`itable` 跨设备缓存别名与根卷损坏。
- **修复方案**：`vfs_mount()` 仅放行已注册非根设备白名单且校验 `dev` 独占；`virtio_disk_rw()` 对未知 `b->dev` 拒绝或 Panic。

### - [ ] [BUG-FS-16] `ext2_mount()` / `fat32_mount()` 缺短读、移位 UB、整数溢出与分区上界校验；FAT 链遍历缺环路防护

- **位置**：`src/kernel/ext2.c:370-480, 1158-1210`、`src/kernel/fat32.c:121-132, 185-198, 294-315, 596-660`
- **严重级别**：`P1` ｜ **核验方式**：静态核验
- **现象与根因**：(1) 两挂载函数 `block_read_bytes()` 仅检 `< 0` 非 `!= sizeof(...)`；(2) `ext2_mount()` 未检 `s_log_block_size > 2`（移位 UB）、`s_blocks_count <= s_first_data_block`（`ext2_alloc_block` 下溢）、`s_blocks_count <= EXT2SIZE`、`inode_size` 对齐及组描述符块号合法性；`ext2_alloc/free` 缺 `block_size * 8` 位图容量与块/inode 上下界校验；(3) `fat32_mount()` 未检 `root_clus > nclus + 1`，多处 32 位乘法/加法可溢出且失败前污染全局 `fat32fs`；`fat32_zero_cluster()` 缺簇号范围检查；(4) 所有 FAT 链遍历缺步数上限，遇到 2 簇环路即内核死循环。
- **修复方案**：收紧短读为 `!= sizeof(...)`，补齐全部超级块/BPB/组描述符/位图边界与 `uint64` 防溢出检查，`fat32_mount` 使用局部结构体，所有 FAT 链循环加 `hops++ >= fs->nclus` 熔断。

### - [ ] [BUG-FS-17] `block_read_bytes()` / `block_write_bytes()` 缺 32 位 `off + n` 溢出检查；`ext2_read/write_dinode` 未检短读/短写

- **位置**：`src/kernel/block.c:16-54`、`src/kernel/ext2.c:152-178, 842, 932, 935`
- **严重级别**：`P1` ｜ **核验方式**：静态核验
- **现象与根因**：`block_read/write_bytes` 若 `off + n` 32 位回绕会静默读写扇区 0（覆盖 MBR/超级块）；`ext2_read/write_dinode` 仅检 `< 0` 非 `== sizeof(*di)`；多处 `ext2_sync_inode()` 与 `ext2_create` 末尾 `ext2_iget()` 忽略失败返回值。
- **修复方案**：`block_read/write_bytes` 增加 `if(off + n < off) return -1;`；`dinode` 读写严格校验字节数等于 `sizeof(*di)`；传播 `ext2_sync_inode` 与 `ext2_iget` 失败并回滚。

### - [ ] [BUG-FS-18] FAT32 LFN 状态机 `seq == 0` 负索引越界、`de->name[13]` 越界 UB 及 `seq == 1` 未清 `expect`

- **位置**：`src/kernel/fat32.c:59, 720-756`
- **严重级别**：`P2` ｜ **核验方式**：静态核验
- **现象与根因**：`fat32_lfn_feed()` 未校验 `seq <= 0 || seq > 20`（`seq == 0` 时 `pos = -13` 负索引越界）；通过 `de->name[13]` 访问 11 字节数组属 C 未定义行为；`seq == 1` 未清 `st->expect` 允许重复 `seq == 1` 槽覆写文件名。
- **修复方案**：定义 `struct fat32_lfn_dirent`，校验 `1 <= seq <= 20`，`seq == 1` 时同步置 `st->expect = 0`。

### - [ ] [BUG-FS-19] FAT32 `readdir` 16 位截断 `inum`、根目录子项 `..` 违背规范；`ext2_sync_inode` 覆写权限位且大文件写 `O(N^2)` 退化

- **位置**：`src/kernel/fat32.c:895-910, 1085-1090`、`src/kernel/fs.h:56-59`、`src/kernel/ext2.c:195-201, 277, 297-317`
- **严重级别**：`P2` ｜ **核验方式**：静态核验
- **现象与根因**：`dirent.inum` 为 16 位 `ushort`，65536 倍数簇号截断为 0 被 `ls` 隐藏；`fat32_create` 为根目录子项写 `..` 填入 `root_clus` 而非规范 `0`；`ext2_sync_inode` 强制覆写 `i_mode` 权限位（丢 `+x`）且每次写入重读整棵双间接树统计 `i_blocks` 呈 `O(N^2)` 退化。
- **修复方案**：避免 `xde.inum` 截断为 0；根目录子项 `..` 写簇号 `0`；`ext2_sync_inode` 保留低 12 位权限并增量维护 `i_blocks`。

---

## 四、进程管理、ELF 加载、管道、锁原语与控制台（11 项）

### - [ ] [BUG-CORE-01] `kkill(0)` 毒化 `UNUSED` 进程槽位且 `allocproc()` 未清零 `p->killed`

- **位置**：`src/kernel/proc.c:126-151, 611-629`
- **严重级别**：`P0` ｜ **核验方式**：静态核验
- **现象与根因**：`freeproc()` 回收槽位置 `p->pid = 0; p->state = UNUSED;`。`kkill(0)` 未过滤 `pid <= 0` 与 `UNUSED`，会命中首个空闲槽位并置 `p->killed = 1`；而 `allocproc()` 复用槽位时未重置 `p->killed = 0`，导致下一次 `fork()` 或服务拉起的新进程一出生即被误杀。
- **修复方案**：`kkill()` 入口加 `if(pid <= 0) return -1;` 且循环加 `p->state != UNUSED`；`allocproc()` 显式清零 `p->killed = 0; p->xstate = 0; p->chan = 0;`。

### - [ ] [BUG-CORE-02] `sys_sbrk()` 懒分配与双架构 `uvmalloc()` 缺失 `>= TRAPFRAME` 地址上界校验

- **位置**：`src/kernel/sysproc.c:45-61`、`src/kernel/vm.c:218-235`、`src/kernel/arch/loongarch/vm.c:173-190`
- **严重级别**：`P1` ｜ **核验方式**：静态核验
- **现象与根因**：`sys_sbrk()` 懒分配仅检 `addr + n < addr`，未限制 `addr + n >= TRAPFRAME`；双架构 `uvmalloc()` 也未拦截 `newsz >= TRAPFRAME`。将 `p->sz` 推高至 `>= TRAPFRAME` 后，进程退出 `uvmfree()` 会覆盖高地址映射或遍历 `>= MAXVA` 触发 `panic("walk")`。
- **修复方案**：在 `sys_sbrk()` 懒分配分支增加 `addr + n >= TRAPFRAME` 拦截，在双架构 `uvmalloc()` 入口增加 `if(newsz >= TRAPFRAME) return 0;`。

### - [ ] [BUG-CORE-03] `growproc(int n)` 负增量下溢回绕，超额缩容静默成功而非返回 `-1`

- **位置**：`src/kernel/proc.c:250-265`、`src/kernel/sysproc.c:40-63`
- **严重级别**：`P1` ｜ **核验方式**：静态核验
- **现象与根因**：`n < 0` 且 `(uint64)(-(int64)n) > sz` 时，`sz + n` 无符号下溢回绕接近 `2^64`，`uvmdealloc()` 因 `newsz >= oldsz` 直接返回 `oldsz`，`sys_sbrk()` 静默返回旧地址而非 `-1`（`n == INT_MIN` 亦存在 `-n` 有符号溢出）。
- **修复方案**：`uint64 dec = (uint64)(-(int64)n); if(dec > sz) return -1; sz = uvmdealloc(p->pagetable, sz, sz - dec);`。

### - [ ] [BUG-CORE-04] `kexec()` 缺失段重叠、`ph.off` 溢出、栈溢出、`int off` 截断及 `elf.entry` 合法性校验

- **位置**：`src/kernel/exec.c:30, 76-112, 157-161`
- **严重级别**：`P1` ｜ **核验方式**：静态核验
- **现象与根因**：(1) 未检 `ph.vaddr < sz`，乱序/重叠 ELF 使 `uvmalloc` 不映射 `[ph.vaddr, sz)`，随后 `loadseg()` 触发 `panic("loadseg: address should exist")`；(2) 未检 `ph.off + ph.filesz < ph.off` 与 `sz + (USERSTACK+1)*PGSIZE < sz`；(3) `int off = elf.phoff` 截断高 32 位且有符号加法可溢出；(4) 未校验至少存在一个 `PT_LOAD` 段且 `elf.entry` 落在可执行段内。
- **修复方案**：`off` 改为 `uint64` 并检 `elf.phentsize == sizeof(ph)`；补齐 `ph.vaddr < sz`、`ph.off + ph.filesz < ph.off`、栈加法溢出与 `elf.entry` 落入可执行段校验。

### - [ ] [BUG-CORE-05] `pipewrite()` / `piperead()` 持自旋锁调 `copyin`/`copyout`，出错时丢字节并误报 `0`（EOF）

- **位置**：`src/kernel/pipe.c:82-102, 112-129`
- **严重级别**：`P1` ｜ **核验方式**：静态核验
- **现象与根因**：持自旋锁 `pi->lock`（关中断）期间逐字节拷贝，懒分配页触发 `vmfault()` → `kalloc()` 延长关中断并固化 `pi->lock -> kmem.lock` 依赖；`piperead()` 先 `pi->nread++` 再 `copyout`，拷贝失败永久丢字节且首字节失败返回 `0` 伪装 EOF（`pipewrite` 首字节失败亦返 `0`）。
- **修复方案**：改用栈上小缓冲批拷贝并在释放 `pi->lock` 后执行 `copyin`/`copyout`（或失败回退 `nread`），`i == 0` 拷贝失败返回 `-1`。

### - [ ] [BUG-CORE-06] `consoleread()` 持中断锁 `copyout` 丢字符、`consolewrite()` 吞错、`uartwrite()` 睡眠不可被 `kill` 打断

- **位置**：`src/kernel/console.c:59-75, 91-130`、`src/kernel/uart.c:81-99`
- **严重级别**：`P1` ｜ **核验方式**：静态核验
- **现象与根因**：`consoleread()` 持中断锁 `cons.lock` 先推进 `cons.r++` 再 `either_copyout`，非法/只读地址丢字符且误报 EOF，缺页阻塞 RX 中断；`consolewrite()` 首字节拷贝失败返 `0`；`uartwrite()` 睡眠循环未检 `killed(myproc())`。
- **修复方案**：释放 `cons.lock` 后再 `either_copyout`（或失败回退 `cons.r--` 且返 `-1`）；`consolewrite` 首字节失败返 `-1`；`uartwrite` 增加 `killed(myproc())` 退出分支。

### - [ ] [BUG-CORE-07] `consoleintr()` 中断态 `^P` 持 `cons.lock` 调 `procdump()` 致长时间关中断与死锁风险

- **位置**：`src/kernel/console.c:140-183`、`src/kernel/uart.c:107-124`、`src/kernel/printf.c:64-134`
- **严重级别**：`P1` ｜ **核验方式**：静态核验
- **现象与根因**：中断上下文持 `cons.lock` 直接调 `procdump()` 轮询打印最多 64 行，引入 `cons.lock -> pr.lock` 依赖并致 16550A RX FIFO 溢出；另一核同时 `panic()` 时 `uartputc_sync()` 死循环使 `cons.lock` 永不释放。
- **修复方案**：`^P` 分支先释放 `cons.lock` 再调 `procdump()`（微内核下由 `consdrvd` 通过诊断系统调用触发）。

### - [ ] [BUG-CORE-08] `sleeplock.c` 缺失自重入检测与释放者权限校验

- **位置**：`src/kernel/sleeplock.c:21-52`
- **严重级别**：`P1` ｜ **核验方式**：静态核验
- **现象与根因**：`acquiresleep()` 未检 `lk->locked && lk->pid == myproc()->pid`，同进程误重入直接静默死锁；`releasesleep()` 未检 `!lk->locked || lk->pid != myproc()->pid`，误释放会静默破坏互斥。
- **修复方案**：补齐自重入 Panic、释放者归属校验与 `myproc() != 0` 断言。

### - [ ] [BUG-CORE-09] `kwait()` 持全局 `wait_lock` + `pp->lock`（关中断）执行 `copyout()` 与全量 `freeproc()`

- **位置**：`src/kernel/proc.c:387-413`
- **严重级别**：`P1` ｜ **核验方式**：静态核验
- **现象与根因**：同时持有双自旋锁期间执行 `copyout()` 与 `freeproc()`（释放 100 MiB `sbrkmuch` 页表并逐页 `memset`），关中断霸占全局 `wait_lock` 数十至数百毫秒，阻塞全机 `fork`/`exit`/`wait` 并丢中断。
- **修复方案**：锁内仅摘取 `pagetable`/`sz`/`trapframe`/`xstate` 并转换槽位状态，释放双锁后再无锁开中断释放页表与 `copyout()`。

### - [ ] [BUG-CORE-10] `uvmdealloc()` 中 `int npages` 在 LoongArch64 47 位空间下存在 32 位截断

- **位置**：`src/kernel/arch/loongarch/vm.c:234-245`、`src/kernel/vm.c:249-260`
- **严重级别**：`P2` ｜ **核验方式**：静态核验
- **现象与根因**：LoongArch64 `MAXVA = 1L << 47` 跨度可达 `2^35` 页，`int npages` 缩容超 8 TiB 时溢出为负并符号扩展致 `uvmunmap()` `panic("walk")`。
- **修复方案**：双架构 `uvmdealloc()` 中的 `int npages` 统一改为 `uint64`。

### - [ ] [BUG-CORE-11] `syscall()` 截断 64 位 `a7` 系统调用号且 `allocpid()` 无溢出回绕保护

- **位置**：`src/kernel/syscall.c:138-145`、`src/kernel/proc.c:15, 95-105`
- **严重级别**：`P2` ｜ **核验方式**：静态核验
- **现象与根因**：`int num = p->trapframe->a7` 截断高 32 位，使 `0x100000001` 被误放行为 `SYS_fork`；`nextpid` 单调递增无溢出与存活 PID 避让。
- **修复方案**：`num` 改为 `uint64`；`allocpid()` 增加上限安全回绕与活跃 PID 查重。

---

## 五、构建系统、用户态程序与测试框架（13 项）

### - [ ] [BUG-TEST-01]（原 BUG-15）GCC 16 下 `usertests.c` 未读局部变量 `fsblocks` 触发 `-Werror` 阻断双架构镜像构建

- **位置**：`src/user/usertests.c:2346, 2370`
- **严重级别**：`P0` ｜ **核验方式**：**实测**（GCC 16 双架构构建均报错）
- **现象与根因**：`fsfull()` 定义并递增 `fsblocks` 但从未读取，在 `-Wall -Werror` 下触发 `-Werror=unused-but-set-variable` 阻断 `fs.img` 构建。
- **修复方案**：删除无用的 `fsblocks` 声明与自增（不放宽 `-Werror`）。

### - [ ] [BUG-TEST-02]（原 BUG-16）`run_qemu_case.py` 收尾阶段忽略 `--reject`，内核 Panic 仍可返回 `PASS`

- **位置**：`tests/runner/run_qemu_case.py:165-183`
- **严重级别**：`P0` ｜ **核验方式**：**实测**
- **现象与根因**：仅在命令循环内判定 `args.reject`，对最后一条命令之后 `drain(0.5)` 与 `--final-wait` 期间输出的 `panic:`/`kerneltrap` 未覆盖 `result = "FAIL"`。
- **修复方案**：在最终日志快照上统一重新判定 `args.reject`、`args.expect` 与 `args.expect_count`。

### - [ ] [BUG-TEST-03] `tests/smp/cmds.sh` 并发写 3 个 1 MiB `bigfile` 超出 `ext2.img` 剩余容量，导致 `tests/smp` 实测失败

- **位置**：`tests/smp/cmds.sh:11-15`、`src/Makefile:233, 240-246`、`src/kernel/param.h:9`
- **严重级别**：`P0` ｜ **核验方式**：**实测**（三核并发写均在 ~960 KiB 报 `write failed`，`Result: FAIL`）+ 算术核验
- **现象与根因**：`ext2.img` 总计 `4096` 块（4 MiB），扣除元数据与预置 1 MiB `hostbig.bin` 仅剩 **~2,920 块（~2.85 MiB）**。`cmds.sh` 在 `CPUS=3` 下并发启动 3 个 1 MiB `bigfile`（需 **3,087 块**），并行推进时必现 ENOSPC；且后续 `bigfile /mnt/a verify` 与后台 `&` 写入存在竞态。
- **修复方案**：同步扩大 `EXT2_BLOCKS` 与 `EXT2SIZE` 至 `8192`（8 MiB）或为 `bigfile` 增加尺寸参数，并将后台 `&` 改为显式同步的并发测例。

### - [ ] [BUG-TEST-04] `run_qemu_case.py` 的 `wait_for_idle_prompt()` 在后台输出交错、QEMU 早退或 Panic 时死等至超时

- **位置**：`tests/runner/run_qemu_case.py:98-115, 122-163`
- **严重级别**：`P0` ｜ **核验方式**：**实测** + 静态核验
- **现象与根因**：`endswith("$")` + 0.5s 静默判定在后台 `&` 输出覆盖 `$ ` 后永不匹配；后台静默写盘超 0.5s 又误判空闲抢跑；循环内不检查 `qemu.poll()` 也不实时早停 `args.reject`；`stdin.write()` 未捕 `BrokenPipeError` 跳过 `FAIL` 汇总。
- **修复方案**：循环内增加 `qemu.poll()` 与 `args.reject` 实时早停；捕获 `BrokenPipeError` 转为 `FAIL`；用命令尾哨兵标记替代脆弱的末尾 `$` 检测。

### - [ ] [BUG-TEST-05] `run_qemu_case.py` 退出清理无 `SIGKILL` 升级且 `make` 先退时漏杀孤儿 QEMU

- **位置**：`tests/runner/run_qemu_case.py:197-212`
- **严重级别**：`P1` ｜ **核验方式**：静态核验
- **现象与根因**：仅在 `qemu.poll() is None` 时发 `SIGTERM`，`make` 先退而子 `qemu-system-*` 存活时跳过清理；无 `SIGKILL` 升级与 `wait()` 回收。
- **修复方案**：无条件对进程组 `qemu.pid` 发 `SIGTERM` → 短等待 → 升级 `SIGKILL` 并 `wait()`。

### - [ ] [BUG-TEST-06]（原 BUG-17 扩展）`tests/full_usertests.sh` 全局 `pkill` 误杀其他 QEMU，且存在超时、漏检与吞日志缺陷

- **位置**：`tests/full_usertests.sh:25-67`
- **严重级别**：`P1` ｜ **核验方式**：静态核验
- **现象与根因**：(1) `pkill -f "qemu-system"` 误杀同宿主机其他 QEMU 会话；(2) 轮询窗口 `750s < 800s` 提前误杀慢速测试；(3) 未早停 `SOME TESTS FAILED` / `FAILED -- lost`；(4) 终检漏掉 `kerneltrap`；(5) 编译输出重定向 `/dev/null` 吞构建日志。
- **修复方案**：改用 `setsid` 进程组隔离清理；对齐轮询时长 `165 × 5s`，补全失败早停、`kerneltrap` 终检及构建日志文件。

### - [ ] [BUG-TEST-07]（原 TRAP-13 代码错扩展）`src/test-xv6.py` 存在语法/运行时崩溃、硬编码 RISC-V、泄漏 QEMU 与阻塞超时失效

- **位置**：`src/test-xv6.py:21-114, 190-198`
- **严重级别**：`P1` ｜ **核验方式**：静态核验
- **现象与根因**：(1) `self.out` 拼写错（应为 `self.output`）、`os.exit(1)` 不存在（应为 `sys.exit(1)`）、`error()` 引用未定义 `regexps`；(2) 硬编码不传 `ARCH=` 导致 LoongArch 基线测试失效；(3) `stop()` 只杀 `make` 泄漏孤儿 QEMU；(4) 阻塞 `os.read()` 使 `deadline` 超时失效；(5) 吞掉构建 `CalledProcessError`；(6) `monitor()` 每秒重印全历史呈 `O(N^2)` 膨胀；`-q` 覆盖单测参数。
- **修复方案**：逐一修正上述拼写/异常与 `TEST_ARCH` 传递，改用独立会话进程组清理 + `select` 非阻塞读 + 增量行偏移打印。

### - [ ] [BUG-TEST-08] `src/user/logstress.c` 存在 1,500 字节 `.bss` 缓冲区越界写与越界读

- **位置**：`src/user/logstress.c:9-11, 17, 31-33`
- **严重级别**：`P1` ｜ **核验方式**：静态核验
- **现象与根因**：`buf` 声明为 `BUFSZ = 500` 字节，但以 `SZ = 2000` 执行 `memset`/`write`，越界 1,500 字节，紧邻 `.bss` 页尾时直接段错误。
- **修复方案**：统一令 `BUFSZ = SZ = 2000`。

### - [ ] [BUG-TEST-09] 多套测试脚本存在“命令回显假阳性”、多实例仅检 1 次及 `bigfile`/`unlinkopen` 错误前缀不一致漏拦截

- **位置**：`tests/{ext2,fat32,persist,semantics,smp}/run.sh`、`src/user/bigfile.c:23,31,47,94`、`src/user/unlinkopen.c:20,24,29,34`
- **严重级别**：`P1` ｜ **核验方式**：实测 + 静态核验
- **现象与根因**：串口回显输入命令使 `--expect "ext2-first"` 等断言在 `echo` 时即误通过；`semantics` 的 `unlinkopen`（2 次）与 `smp` 的 `bigfile`（2/4 次）未用 `--expect-count`；`bigfile.c`/`unlinkopen.c` 多处失败分支未含 `FAIL` 绕过 `--reject`。
- **修复方案**：改用 `--expect-count` 或整行匹配，统一工具失败输出为 `<tool>: FAIL`。

### - [ ] [BUG-TEST-10] 5 套测试脚本未启用 `--prompt-wait`，长命令期间塞满 128B UART 缓冲致丢字符

- **位置**：`tests/{ext2,fat32,persist,public_vfs,semantics}/run.sh`、`src/kernel/console.c:48`
- **严重级别**：`P1` ｜ **核验方式**：实测 + 静态核验
- **现象与根因**：固定 `--step-wait 1.0` 盲发命令，在慢速环境/LoongArch/微内核下极易塞满 128B 串口缓冲并静默截断后续命令。
- **修复方案**：统一启用 `--prompt-wait --cmd-timeout 180`。

### - [ ] [BUG-TEST-11] `test1.c` 读错返 `-1` 死循环，`test2.c`/`stressfs.c`/`umountstress.c` 吞错导致假阳性

- **位置**：`src/user/test1.c:30-38,60-63`、`src/user/test2.c:18,54-64,74`、`src/user/stressfs.c:26-46`、`src/user/umountstress.c:15-38,76-79,95`
- **严重级别**：`P1` ｜ **核验方式**：静态核验
- **现象与根因**：`test1.c` 在 `read()` 返 `-1` 时进入 `while(-1)` 死循环；`test2.c` 缺 `O_TRUNC` 且不比对内容；`stressfs.c` 全程不检返回值；`umountstress.c` 在 0 次成功卸载/打开时仍报 `UMOUNT_STRESS_OK`。
- **修复方案**：修正 `test1` 循环为 `> 0`，为 `test2`/`stressfs`/`umountstress` 补齐严格返回值断言、内容校验与最小成功次数门槛。

### - [ ] [BUG-TEST-12]（原 BUG-19 扩展）`vfsbench.c` 提前 EOF 仍报预设字节数，且 `vfsbench`/`unlinkopen`/`bigfile` 存校验与参数盲区

- **位置**：`src/user/vfsbench.c:21-77`、`src/user/unlinkopen.c:33-50`、`src/user/bigfile.c:85-97`
- **严重级别**：`P2` ｜ **核验方式**：静态核验
- **现象与根因**：`vfsbench` 不累计实际读字节 `got` 且不校验内容、`atoi(argv[3])*1024` 可溢出；`unlinkopen` 未覆盖 `BUG-FS-04`（unlink 后继续写 + 槽位复用）；`bigfile` 对非法 `mode` 静默按 `keep` 通过。
- **修复方案**：累计并校验 `got == total` 及内容模式；补齐 `unlinkopen` 写扩展回归与 `bigfile` 参数校验。

### - [ ] [BUG-TEST-13] 构建系统脚本缺陷：`$(ARCH_STAMP)` 漏删 3 级目录、`bc` 浮点版本号误判、`check_env.sh` 漏检依赖、`mkfat32.c` `fsinfo` 偏差

- **位置**：`src/Makefile:75-78, 120-125, 190-191, 315-320`、`tests/check_env.sh:27-44`、`Makefile:37-39, 389`、`src/mkfs/mkfat32.c:192-193`
- **严重级别**：`P2` ｜ **核验方式**：静态核验
- **现象与根因**：`$(ARCH_STAMP)` 漏删 `kernel/arch/loongarch/*.{o,d}`；`check-qemu-version` 用 `bc` 浮点比版本号（`7.10 < 7.2` 误报）；`check_env.sh` 未检 `bc`/`perl`；`Makefile` `env.log` 硬编码 RISC-V；`mkfat32.c` `fsinfo` 未扣 LFN 夹具占用的 2 个簇。
- **修复方案**：补齐 `*/*/*/*.{o,d}` 清理、改用 `sort -V`/`python3` 判版本、补全 `check_env.sh`/`env.log` 与 `mkfat32.c` 计数。

---

## 六、推荐修复顺序（面向原始基线）

1. **阶段 1 — 构建与双架构启动止血（先打通验证闭环）**：`BUG-TEST-01`（GCC 16 编译）、`BUG-HAL-01`（龙芯副核 CSR）、`BUG-HAL-02`（移除龙芯 `trapinit` 过早 `intr_on`）、`BUG-HAL-04`（龙芯 `kfree` 下界）、`BUG-TEST-02/03/04`（判分器与 `tests/smp` 镜像空间）。
2. **阶段 2 — 内核内存安全与并发 P0 根治**：`BUG-CORE-01`（`kkill(0)`）、`BUG-FS-01`（`iput` 快路径 UAF）、`BUG-FS-02`（`ext2_find_dirent` 栈越界）、`BUG-FS-03`（`ext2` 锁序与 `NBUF`）、`BUG-FS-04/05/06/07`（FAT32 匿名槽、`umount` 子挂载、孤儿目录、`ext2_bmap` 泄漏）、`BUG-HAL-03`（龙芯 `DMWIN1` 非缓存 MMIO）。
3. **阶段 3 — 双架构一致性、语义完整与测试可信度（P1）**：`BUG-HAL-05` ~ `BUG-HAL-13`、`BUG-FS-08` ~ `BUG-FS-17`、`BUG-CORE-02` ~ `BUG-CORE-09`、`BUG-TEST-05` ~ `BUG-TEST-11`。
4. **阶段 4 — 边界健壮性与构建打磨（P2）**：`BUG-HAL-14`、`BUG-FS-18/19`、`BUG-CORE-10/11`、`BUG-TEST-12/13`。
