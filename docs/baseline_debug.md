# 基线探索与问题诊断记录（`baseline_debug.md`）

本文档记录在启动 **xv6 微内核化重构** 之前，对仓库基线代码（xv6-riscv rev5 + 2026 北航扩展版 VFS/HAL/LoongArch64）、双架构硬件抽象层及测试框架进行深度探索时发现的**现有缺陷、隐蔽陷阱与微内核迁移约束**。所有问题均按模块编号归档，并在架构设计阶段逐一给出规避方案。

---

## 一、环境与代码基线快照

- **基线提交**：`e5fcf65` (`chore: 初始化xv6-microkernel基线`)
- **工具链实测**（经 [check_env.sh](../tests/check_env.sh) 验证）：
  - RISC-V：`riscv64-linux-gnu-gcc`、`qemu-system-riscv64`（均已就绪）
  - LoongArch64：`loongarch64-linux-gnu-gcc`、`qemu-system-loongarch64`（均已就绪）
  - 宿主镜像工具：`mkfs.ext2`、`debugfs`、`python3`、`make`（均已就绪）
- **内核代码体量（总计 11,471 LOC）**：
  - 文件系统与块缓冲层：~4,193 LOC（`ext2.c` 1,216 / `fat32.c` 1,183 / `fs.c` 785 / `vfs.c` 507 / `log.c` 237 / `bio.c` 153 / `block.c` 53 / 头文件 ~59）
  - 设备驱动层：~1,193 LOC（`virtio_disk.c` 520 / `arch/loongarch/virtio_pci.c` 315 / `console.c` 196 / `uart.c` 162）
  - 文件描述符 / 管道 / 文件系统调用层：~845 LOC（`sysfile.c` 535 / `file.c` 180 / `pipe.c` 130）
  - 核心内核与双架构 HAL：~5,240 LOC

---

## 二、虚拟内存与跨地址空间拷贝陷阱

### [BUG/TRAP-01] `vmfault()` 隐式耦合 `myproc()`，跨进程拷贝将误判地址上限与页表
- **位置**：
  - RISC-V：[src/kernel/vm.c#L452-L473](../src/kernel/vm.c#L452-L473)
  - LoongArch64：[src/kernel/arch/loongarch/vm.c#L204-L231](../src/kernel/arch/loongarch/vm.c#L204-L231)
- **现象与根因**：
  - `copyin(pagetable, ...)` 与 `copyout(pagetable, ...)` 虽然显式接收 `pagetable` 参数，但在缺页时调用的 `vmfault(pagetable, va, read)` 内部却硬编码使用 `struct proc *p = myproc()`，以 `p->sz` 检查越界，并（在 RISC-V 中）直接向 `p->pagetable` 写入新映射。
  - 在微内核下，当用户态服务进程（如 `vfsd`、`ext2d`）调用跨进程授权拷贝 `ipc_copy()` 访问客户端的懒分配内存页（`usertests` 中的 `sbrkarg`、`rwsbrk` 用例）时，`myproc()` 是**服务端进程**而非**客户端进程**，会导致错误拒绝合法懒分配页或将页误映射到服务端页表！
- **规避措施**：
  - 在双架构 `vm.c` 中新增 `vmfault_proc(struct proc *target, uint64 va, int read)` 及 `copyin_proc` / `copyout_proc`，跨空间授权拷贝强制绑定目标客户端 `struct proc *`。

### [TRAP-02] 用户态驱动映射 MMIO/DMA 后，进程退出会触发 `kfree` / `freewalk` Panic
- **位置**：
  - RISC-V：[src/kernel/vm.c#L263-L288](../src/kernel/vm.c#L263-L288)、[src/kernel/kalloc.c#L51-L56](../src/kernel/kalloc.c#L51-L56)
  - LoongArch64：[src/kernel/arch/loongarch/vm.c#L247-L269](../src/kernel/arch/loongarch/vm.c#L247-L269)
- **现象与根因**：
  - `proc_freepagetable()` 调用 `uvmfree(pagetable, sz)` 仅卸载 `[0, sz)` 并对页内物理地址执行 `kfree()`，随后 `freewalk()` 遇到任何残留叶子 PTE 都会 `panic("freewalk: leaf")`。
  - 如果设备驱动（`blkdrvd`/`consdrvd`）将 MMIO 地址（如 `0x10000000`、`0x1fe00000`）映射到用户态：
    1. 若落在 `[0, sz)`，进程退出时 `kfree(mmio_pa)` 会直接触发 `panic("kfree")`；
    2. 若落在 `sz` 之上但退出时未主动 unmap，`freewalk()` 会触发 `panic("freewalk: leaf")`。
- **规避措施**：
  - 设备映射统一放置在 `sz` 之上的受控高地址区（`DEV_MMIO_BASE` / `DEV_DMA_BASE`，低于 `TRAPFRAME`），并在 `struct proc` 中记录 `devmaps[]`；`proc_freepagetable()` 退出清理时对 MMIO 执行 `uvmunmap(..., do_free=0)`、对 DMA 执行 `uvmunmap(..., do_free=1)`，且 `kfork()` 禁止继承设备映射。

---

## 三、双架构（RISC-V vs LoongArch64）底层差异与基线缺陷

### [TRAP-03] LoongArch64 MMIO 非页对齐、缓存属性（`MAT`）及 DMA 地址掩码
- **位置**：
  - [src/kernel/memlayout.h#L1-L29](../src/kernel/memlayout.h#L1-L29)
  - [src/kernel/arch/loongarch/loongarch.h#L299-L315](../src/kernel/arch/loongarch/loongarch.h#L299-L315)
  - [src/kernel/virtio_disk.c#L40-L48](../src/kernel/virtio_disk.c#L40-L48)
- **发现与分析**：
  1. LoongArch64 的 `UART0` 物理地址为 `0x1fe001e0`，**非 4KB 对齐**（页内偏移 `0x1e0`）。`dev_map()` 必须按 `PGROUNDDOWN(pa)` 映射整页，并向驱动返回 `user_va + (pa & (PGSIZE-1))`。
  2. LoongArch64 页表项中 `PTE_MAT = (1L << 4)` 表示**一致可缓存（Coherent Cached）**；映射 MMIO 设备寄存器到用户态时**必须清除 `PTE_MAT`**（即 `MAT=0`，强序非缓存），同时保留 `PTE_P | PTE_W | PTE_D | PTE_PLV | PTE_V`。
  3. LoongArch64 上 `kalloc()` 返回的是带直接映射窗口前缀 `DMWIN_MASK (0x9000000000000000)` 的内核虚拟地址，而 VirtIO DMA 需要纯物理地址。`dma_alloc()` 返回给用户态 `blkdrvd` 的 DMA 物理地址必须剥离 `DMWIN_MASK`（`pa & ~DMWIN_MASK`）。
  4. **内存布局利好**：LoongArch64 内核镜像链接于 `0x9000000000200000`，而 `kalloc()` 内存池位于 `0x9000000090000000` 起 128MB（见 [arch/loongarch/kernel.ld](../src/kernel/arch/loongarch/kernel.ld) 与 [kalloc.c](../src/kernel/kalloc.c#L30-L34)），因此 BootFS 嵌入 `.rodata` 在 LoongArch64 上完全不消耗 `kalloc()` 可用页。

### [TRAP-04] LoongArch64 Legacy INTx 电平中断丢失风险
- **位置**：[src/kernel/arch/loongarch/trap.c#L192-L216](../src/kernel/arch/loongarch/trap.c#L192-L216)、[src/kernel/virtio_disk.c#L493-L520](../src/kernel/virtio_disk.c#L493-L520)
- **现象与根因**：
  - 在 QEMU `loongarch64` 的 `virtio-blk-pci` (`vectors=0`) 下，采用传统电平中断（INTx）经 LS7A APIC -> EXTIOI 路由。若新的完成事件在清除上一次中断的窗口期内到达，中断控制器链路可能吞掉中断，导致等待磁盘完成的进程永久休眠。
  - 基线内核通过“驱动 ack 后循环重检 `virtio_disk_pending()`”叠加“时钟中断调用 `virtio_disk_watchdog()`”规避该问题。
- **规避措施**：
  - 微内核化后，内核 `devintr()` 在收到 `VIRTIO0_IRQ` 时先在中断控制器侧屏蔽/完成并向 `blkdrvd` 投递异步 `NOTIFY_IRQ`；`blkdrvd` 处理完 used ring 并调用 `irq_ack()` 后，必须再次复检 `used_idx != used->idx`；同时内核定时器在存在未完成块请求时周期投递轻量级 `NOTIFY_WATCHDOG` 兜底。

### [TRAP-05] LoongArch64 `uservec`/`userret` 每次陷入均全量刷 TLB，放大多跳 IPC 开销
- **位置**：[src/kernel/arch/loongarch/uservec.S#L52](../src/kernel/arch/loongarch/uservec.S#L52)、[src/kernel/arch/loongarch/uservec.S#L59](../src/kernel/arch/loongarch/uservec.S#L59)
- **现象与分析**：
  - 基线 LoongArch64 未启用按进程 `ASID` 隔离，每次 `uservec` / `userret` 都执行 `invtlb 0x0, $zero, $zero` 全量刷新 TLB。
  - 在微内核下，若每次 512B 读写都触发多跳无缓存 IPC，在 LoongArch64 QEMU 下的开销会被成倍放大，可能导致 `full_usertests.sh`（800s 超时）或 `mount_storm` 变慢。
- **规避措施**：
  - 必须在各用户态文件系统服务（`xv6fsd`/`ext2d`/`fat32d`）内通过 `libfs` 维护写穿 LRU 块缓存（大幅削减向 `blkdrvd` 的 IPC 频次），并将读写分片保持在 4KB 粒度；如后续基准测试仍需提速，可进一步为 LoongArch64 引入按 `pid` 的低成本 ASID（`w_csr_asid`）优化。

### [BUG-06] LoongArch64 `trap.c` 存在无锁访问 `p->killed` 的 SMP 数据竞争
- **位置**：[src/kernel/arch/loongarch/trap.c#L85-L105](../src/kernel/arch/loongarch/trap.c#L85-L105)
- **现象**：RISC-V 的 [trap.c](../src/kernel/trap.c#L57-L80) 使用加锁函数 `killed(p)` / `setkilled(p)`，而 LoongArch64 版本直接读写 `p->killed`，在 `CPUS=3` 与并发 `kill()`/故障注入下存在数据竞争。
- **规避措施**：重构微内核陷阱路径时统一改为 `killed(p)` / `setkilled(p)`。

### [TRAP-07] LoongArch64 `virtio_pci` 重启时的 PCI BAR 幂等分配
- **位置**：[src/kernel/arch/loongarch/virtio_pci.c#L52](../src/kernel/arch/loongarch/virtio_pci.c#L52)、[src/kernel/arch/loongarch/virtio_pci.c#L109-L152](../src/kernel/arch/loongarch/virtio_pci.c#L109-L152)
- **分析**：当 `blkdrvd` 崩溃并重启后，QEMU 虚拟 PCI 设备的 BAR 寄存器已保留首次分配的地址。重启后的 `blkdrvd` 若保持确定性 `next_mmio` 起点或复用已配置的合法 BAR 地址，再写入 `device_status = 0`（等待读回 0）完成 VirtIO 硬复位，即可干净重置设备队列而无需重启系统。

---

## 四、VFS 与三大文件系统语义耦合问题

### [TRAP-08] 嵌套挂载点的 `unlink` 保护存在“FS 回调 VFS”逆向依赖
- **位置**：
  - [src/kernel/ext2.c#L904-L908](../src/kernel/ext2.c#L904-L908)
  - [src/kernel/fat32.c#L1139-L1143](../src/kernel/fat32.c#L1139-L1143)
  - 测试覆盖：[tests/mount_lifecycle/cmds.sh](../tests/mount_lifecycle/cmds.sh)（在 `/mnt/nest` 上嵌套挂载 fat32 后尝试 `rm /mnt/nest` 必须失败）
- **现象与根因**：
  - 宏内核中，`ext2_unlink` 和 `fat32_unlink` 直接回调 `vfs_mountpoint(ip)` 判断目标目录是否是另一文件系统的挂载点。
  - 迁出至用户态后，`ext2d`/`fat32d` 不能反向调用 `vfsd`（否则形成 `vfsd -> ext2d -> vfsd` 循环调用死锁）。
- **规避措施**：
  - 将挂载点占用检查**前置到 `vfsd`**：`vfsd` 在收到 `unlink(path)` 时，先解析父目录并 `lookup` 目标节点，确认 `vfs_mountpoint(target) == 0` 后，再向对应 FS 服务下发 `FS_OP_UNLINK`（同时由 `vfsd` 事件循环自然串行化 `mount`/`unlink`，杜绝 TOCTOU 竞争）。

### [TRAP-09] Unlink-While-Open（已删除但仍打开文件）的跨进程引用计数协同
- **位置**：
  - [src/kernel/fs.c#L374-L400](../src/kernel/fs.c#L374-L400)
  - [src/kernel/ext2.c#L922-L925](../src/kernel/ext2.c#L922-L925)、[src/kernel/ext2.c#L1128-L1132](../src/kernel/ext2.c#L1128-L1132)
  - [src/kernel/fat32.c#L1147-L1150](../src/kernel/fat32.c#L1147-L1150)
  - 测试覆盖：[tests/semantics/cmds.sh](../tests/semantics/cmds.sh)（`unlinkopen /mnt/uop.txt`、`unlinkopen /fat/uop.txt`）
- **现象与根因**：
  - 文件被打开后再 `unlink`，其数据块/簇链与 inode 必须持续存活直到最后一个 FD `close()` 触发 `iput()`。
- **规避措施**：
  - VFS-to-FS IPC 协议引入显式 **Vnode 引用生命周期**：`LOOKUP`/`CREATE`/`MOUNT` 在 FS 服务本地持有 `ip->ref`；`vfsd` 维护全局 Vnode 缓存，仅当该 `(dev, inum)` 在 `vfsd` 中的引用计数归零时才向 FS 服务发送 `FS_OP_RELEASE(inum)`，从而 100% 保留原有 `iput()` 延迟回收语义。

### [TRAP-10] FAT32 无磁盘 inode 号，重启恢复依赖目录项偏移 `entry_off`
- **位置**：[src/kernel/fat32.c#L406-L425](../src/kernel/fat32.c#L406-L425)、[src/kernel/fat32.c#L581-L594](../src/kernel/fat32.c#L581-L594)
- **现象与根因**：
  - `fat32.c` 以首簇号 `clus` 作为 `inum`，并在内存 `ip->addrs[1]` 中缓存目录项磁盘字节偏移 `entry_off`，写文件更新 `size` 时必须依赖 `entry_off` 回写目录项。
  - 若 `fat32d` 崩溃重启，仅凭 `(dev, inum)` 无法直接反查 `entry_off`。
- **规避措施**：
  - `vfsd` 的文件表与检查点中为打开文件保存规范路径（或相对挂载点路径，用于重启后重解析）；`fat32d` 重启后，对已挂载路径的新请求及具名打开文件可通过路径重放恢复 `entry_off`；对已 `unlink` 的匿名文件则安全返回 `-1` 而避免误写磁盘。

### [TRAP-11] `kexec()` 路径回退规则与直接向新页表物理页加载 ELF
- **位置**：[src/kernel/exec.c#L40-L57](../src/kernel/exec.c#L40-L57)、[src/kernel/exec.c#L176-L197](../src/kernel/exec.c#L176-L197)
- **现象与分析**：
  1. `kexec()` 在相对路径不含 `'/'` 且查找失败时，会自动回退到 `"/" + path`（确保在 `/mnt` 或 `/fat` 下执行 `ls`/`cat` 等根目录命令正常）；迁出 VFS 后必须保留此回退语义。
  2. `loadseg()` 将 ELF 段直接读入**新建页表**的内核物理地址（`user_dst = 0`），而非当前进程用户空间。
- **规避措施**：
  - 内核 `ipc_copy` 授权描述符同时支持 `GRANT_USER_VA`（普通系统调用）与 `GRANT_KERNEL_BUF`（`kexec` 内核缓冲区读取），使 `exec.c` 彻底剥离 `inode`/`vfs_readi` 依赖。

---

## 五、资源上限、测例约束与测试脚本观察

### [TRAP-12] `usertests` 极限资源压测与系统服务零资源占用要求
- **位置**：
  - [src/kernel/param.h](../src/kernel/param.h)：`NPROC=64`, `NOFILE=16`, `NFILE=100`, `NINODE=50`
  - [src/user/usertests.c](../src/user/usertests.c)：`sbrkmuch`/`sbrkfail`（要求单进程分配 `BIG=100MB` / 128MB）、`mem`（耗尽物理内存）、`forkforkfork`（打满 `NPROC` 后仍需 `open("stopforking")`）、`maxfiles`/`iref`（测试打满 `NFILE`/`NINODE`）。
  - [src/Makefile#L184-L188](../src/Makefile#L184-L188)：`_forktest` 仅链接 `ulib.o` 与 `usys.o`，直接通过原始系统调用运行。
- **约束结论**：
  1. 全部微内核常驻服务（含 BootFS 镜像）总内存占用必须控制在 **< 8MB**（目标 2~4MB），保证 `sbrkmuch` 的 100MB 分配裕度充裕；
  2. IPC 热路径与常驻服务**禁止在请求处理期间调用 `kalloc()` / `malloc()` / `fork()`**，确保 `mem`/`sbrkfail`/`forkforkfork` 极限压测下绝不死锁；
  3. 进程表为 `CAP_SUPERVISOR`（`rs`）预留独立重启槽位（如 `NPROC` 适度设为 72 并为普通用户限制最多 64，或保留 4 槽位专供 `rs`），防止进程槽满时无法重启故障服务；
  4. 系统服务间走 IPC 端点，不消耗 `NFILE`/`NINODE` 配额，使 `maxfiles`/`iref` 行为与宏内核完全一致；
  5. 保持原有 23 个 POSIX 系统调用 ABI 与编号不变（薄代理层转发），确保 `_forktest` 及所有测例二进制零改动通过。

### [TRAP-13] 启动日志、Crash 恢复匹配与测试脚本既有小缺陷
- **位置**：
  - [src/test-xv6.py#L49](../src/test-xv6.py#L49)（失败日志保存写成了 `self.out` 而非 `self.output`）、[src/test-xv6.py#L81](../src/test-xv6.py#L81)（`regexps` 未定义）
  - [src/test-xv6.py#L126](../src/test-xv6.py#L126) 与 [#L157](../src/test-xv6.py#L157)：分别正则匹配 `^recovering` 与 `^ireclaim`。
  - [tests/runner/run_qemu_case.py#L108](../tests/runner/run_qemu_case.py#L108)：`--prompt-wait` 要求终端静默且末尾为 `$`，且各 `run.sh` 严格 `--reject "panic:"` 与 `--reject "kerneltrap"`。
- **约束结论**：
  - `xv6fsd` 启动执行日志重放与孤儿回收时，必须保持原样输出 `recovering ...` 与 `ireclaim: ...` 至控制台；
  - 正常启动及运行期间，服务日志输出不可干扰 `$ ` 提示符或伪造错误关键字，且故障隔离测例中用户态服务崩溃绝对不能触发内核 `panic:` 或 `kerneltrap`。

### [TRAP-14] UART 内核早期输出与用户态 `consdrvd` 并发竞争
- **位置**：[src/kernel/uart.c#L81-L124](../src/kernel/uart.c#L81-L124)、[src/kernel/console.c#L140-L183](../src/kernel/console.c#L140-L183)
- **现象与分析**：
  - 控制台 `^P` 需要触发内核 `procdump()`；同时内核在 `usertrap()` 异常打印与 `panic()` 时需要直接写串口。
  - 若内核与用户态 `consdrvd` 同时操作 `UART0` 的 `IER`/`RHR`，会造成接收字符被吞或 TX 中断丢失。
- **规避措施**：
  - `consdrvd` 启动后独占 `UART0` 的 RX、行规程与 `UART0_IRQ`；内核 `early_putc` 仅在启动早期及异常/Panic 时轮询 `LSR_TX_IDLE` 写 `THR`，绝不读取 `RHR` 或改写 `IER`；`consdrvd` 收到 `^P` 时通过轻量诊断系统调用触发内核 `procdump()`。

## 六、2026-11-24 双架构复核

本次从 `HEAD 04151d8` 导出两份独立源码，避免架构戳、镜像和测试选择文件互相覆盖。与第一节的 `e5fcf65` 相比，仅提示词文档有提交差异。上文的 TRAP 多数是拟议迁移才会触及的条件。本节的“实测”指实际命令或串口观测，“静态”指可在当前提交沿代码路径核对的结论。

| 检查 | RISC-V | LoongArch64 | 判据 |
| --- | --- | --- | --- |
| 环境检查 | 通过 | 通过 | `bash tests/check_env.sh`，两个交叉 GCC 均为 16.2.1 |
| 原始干净构建 | 失败 | 失败 | 根目录 `make semantics` / `make loongarch_semantics`，见 BUG-15 |
| 临时放宽单项告警后，semantics，1 核 | 通过 | 通过 | 每份串口日志均有独立输出行 `EXT2_EXEC_OK`、`FAT32_EXEC_OK`，两次 `unlinkopen: ok` |
| 同一临时条件，mount_storm，3 核 | 通过 | 通过 | 两个后端各跑 256 轮。ext2 / fat32 客体计时分别为 41 / 43 ticks 和 11 / 9 ticks，仅为单次观测 |
| 同一临时条件，完整 usertests，3 核 | 通过 | 通过 | 独立重建 fs.img，用仓库 runner 向客体发送 usertests（不带 -q），两份日志均有独立的 ALL TESTS PASSED 行 |

上述六份串口日志均核对过 `panic:`、`kerneltrap`。重跑时可在从该提交导出的临时副本 `src/Makefile` 末尾加入一条目标专属规则，再从根目录执行目标及 `make run`。不同架构使用不同目录，持久化回合保留同一份组合镜像。

~~~make
user/usertests.o: CFLAGS += -Wno-error=unused-but-set-variable
~~~

### BUG-15 · 宿主 GCC 16 下环境检查通过，两个默认镜像均构建失败（实测）

`src/user/usertests.c:2346,2370` 的 `fsfull()` 定义并递增 `fsblocks`，后续没有读取。两个构建日志都报 `variable ‘fsblocks’ set but not used [-Werror=unused-but-set-variable=]`。内核可链接，失败点在制作 fs.img 所需的用户程序。建议删除无用途的局部计数，或在该测试已有的统计输出中使用它；验收时恢复默认警告策略，重新构建双架构镜像。上表的临时规则仅用于定位和继续探索。

### BUG-16 · 收尾阶段发现拒绝词，运行器仍可返回成功（实测）

`tests/runner/run_qemu_case.py` 在命令循环中判定 reject，最后一次排空输出后只把缺失的 expect 和错误计数写入 result。用临时 make 目标代替客体，空命令表、`--boot-wait 0 --reject 'panic:'`，让目标在 0.15 秒后输出一行合成标记。实际输出为 `[reject:HIT] panic:`、`Result: PASS`，退出码 0。这验证的是判分器，实际双架构语义测试的串口没有该标记。修正时在最终日志快照统一判定 reject、expect 和计数；回归需覆盖最后一条命令后的输出及 final-wait 等待期。

### BUG-17 · 完整测试清理会波及同一主机上的其他 QEMU（静态）

`tests/full_usertests.sh:56` 根据命令行模式执行 `pkill -f "qemu-system"`。两架构并行或有独立演示会话时，清理范围可跨越当前测试。建议启动独立进程组，在成功、失败、超时三条路径都只清理并等待本组。回归时由同一用户启动两个独立会话，结束其一后要求另一会话仍能完成下一条客体命令。

### BUG-18 · FAT32 删除后仍打开的文件保留了已释放的目录项位置（静态）

`src/kernel/fat32.c` 中 `fat32_unlink` 删除目录项并将 `nlink` 清零。`fat32_fill_inode` 保存的 `addrs[1]` 仍指向原目录槽；`fat32_writei → fat32_sync_inode` 可能向已复用的槽写回旧文件的长度和首簇。现有 `src/user/unlinkopen.c` 在 unlink 后只读旧 FD，常规 semantics 通过也覆盖不到这个分支。建议在成功摘除目录项时断开 inode 和该目录槽的关联，保留簇链直到最后引用释放，同时让删除与元数据写回传播 I/O 失败。回归应核对匿名 FD 与独立具名文件的内容、大小，以及再次挂载后的结果。本轮未做磁盘损坏复现。

### BUG-19 · vfsbench 遇到提前 EOF 仍报告预设读入量（静态）

`src/user/vfsbench.c` 的读循环以 EOF 退出，只检查负返回值，随后打印参数 total。用于架构性能对比时，读短的实现可能看起来更快。建议累计每轮实际字节并核对 total，校验内容可复用 `src/user/bigfile.c` 的独立校验逻辑；基准报告同时附读写字节和 ticks。

### 对既有迁移笔记的校准

- TRAP-09/10 的路径重放可能把旧 FD 绑定到同名新文件。架构首版对故障卷的旧句柄使用代次失效。正常运行中，unlink 后持有的 FD 仍须完整支持读写。
- TRAP-11 的 exec 读段采用受限的分片缓冲区，由内核验证并拷入新页表。服务端永远只接触消息或授权缓冲区。
- TRAP-12 中的 8 MiB 是设计预算，实际余量应测空闲页数并跑完整 usertests。`sbrkmuch` 与 `sbrkfail` 的 BIG 都是 100 MiB。当前 `src/user/usertests.c` 注册了 `iref`，未找到名为 `maxfiles` 的测试项。
- TRAP-05 的写穿策略只适合允许提交的块。xv6 根卷须保留 `log_write → write_log → write_head → install_trans` 顺序，home block 提前落盘会破坏恢复语义。

---

## 七、LoongArch64 SMP 与硬件中断/DMA 深度复核（静态核验）

在微内核多核 IPC 与用户态驱动设计复核阶段，对 `src/kernel/arch/loongarch/` 的多核引导、定时器、中断控制器与 PCI 路径进行逐行核验，确认存在 3 项足以导致三核死锁、跨核唤醒卡顿与 DMA 停机失败的底层暗雷：

### BUG-20 · LoongArch64 副核未初始化页表与异常入口 CSR，调度即跳 `0x0` 并死持锁（静态核验）
- **位置**：
  - [src/kernel/main.c#L35-L43](../src/kernel/main.c#L35-L43)
  - [src/kernel/arch/loongarch/entry.S#L14](../src/kernel/arch/loongarch/entry.S#L14)
  - [src/kernel/arch/loongarch/vm.c#L21-L40](../src/kernel/arch/loongarch/vm.c#L21-L40)
  - [src/kernel/arch/loongarch/trap.c#L48-L65](../src/kernel/arch/loongarch/trap.c#L48-L65)
- **现象与根因**：
  - `entry.S` 在启动时将 `LOONGARCH_CSR_TLBRENTRY` 清零；仅 CPU 0 调用的 `kvminit()` 与 `trapinit()` 写入了 `PGDL`、`PWCL`、`PWCH`、`ECFG`、`TCFG`、`EENTRY`、`TLBRENTRY`、`MERRENTRY`。
  - 副核调用的 `kvminithart()` 仅执行 `tlbinit()`，`trapinithart()` 为完全空函数。一旦副核进入 `scheduler()` 持有 `p->lock` 并切换至 `KSTACK` 触发 TLB 重填，硬件将直接跳往地址 `0x0` 并永久死持 `p->lock`，导致全机死锁。
- **修正方案**：保存全局 `kernel_pagetable`，补齐副核 `kvminithart()`（写 `PGDL`/`PWCL`/`PWCH` + `tlbinit()`）与 `trapinithart()`（写 `ECFG`/`TCFG`/`EENTRY`/`TLBRENTRY`/`MERRENTRY`）。

### BUG-21 · LoongArch64 默认时钟周期达 167.8ms，且缺少核间 IPI 唤醒 `idle 0` 副核（静态核验）
- **位置**：
  - [src/kernel/arch/loongarch/trap.c#L53](../src/kernel/arch/loongarch/trap.c#L53)（`tcfg = 0x1000000UL | CSR_TCFG_EN | CSR_TCFG_PER`）
  - [src/kernel/proc.c#L468-L471](../src/kernel/proc.c#L468-L471)（`cpu_idle()` → `idle 0`）
- **现象与根因**：
  - QEMU LoongArch64 稳定计数器频率为 100 MHz，`0x1000000`（16,777,216）对应时钟中断周期长达 **167.8 ms**；且 `wakeup()` 不发送核间中断（IPI），若副核处于 `idle 0`，跨核 IPC 唤醒最坏延迟可达 167.8 ms（若副核 `TCFG` 未启则永久睡死）。
- **修正方案**：缩短龙芯 `TCFG` 周期至 ~10 ms（按比例校准逻辑 `ticks`），在双架构引入轻量核间 IPI 唤醒（龙芯 IOCSR IPI / RISC-V 软中断），并在 IPC 唤醒时优先设置本核 `next_hint` 就地调度。

### BUG-22 · LoongArch64 串口与磁盘双双存在丢中断风险，且用户态 PCI 驱动崩溃时内核不知动态 BAR（静态核验）
- **位置**：
  - [src/kernel/arch/loongarch/apic.c#L14](../src/kernel/arch/loongarch/apic.c#L14)（`UART0_IRQ` 设为边沿触发）
  - [src/kernel/arch/loongarch/hal.c#L37-L39](../src/kernel/arch/loongarch/hal.c#L37-L39)（`VIRTIO0_IRQ` 电平中断）
  - [src/kernel/arch/loongarch/virtio_pci.c#L109-L152](../src/kernel/arch/loongarch/virtio_pci.c#L109-L152)（动态分配 PCI BAR）
- **现象与根因**：
  1. 除 `TRAP-04` 已指出的 VirtIO 电平吞中断外，`UART0` 在 LS7A APIC 中配置为边沿触发，若新字节在排空 `RHR` 与 `extioi_complete` 之间到达同样会丢失中断，导致用户态 `consdrvd` 永久沉睡；
  2. `virtio_pci` 迁出至用户态 `blkdrvd` 后，若驱动在注册 BAR 前崩溃或篡改 BAR，内核无法仅凭动态 BAR 复位 VirtIO 硬件。
- **修正方案**：
  1. `consdrvd` 与 `blkdrvd` 双驱动统一执行“排空 → `IRQ_ACK` → 复检硬件状态位”并共享内核时钟 `NOTIFY_WATCHDOG` 兜底；
  2. `blkdrvd` 退出时，内核第一步直接通过静态 **PCI ECAM（`0:dev:0`）** 清除 `PCI_COMMAND_MASTER`（bit 2）在总线层物理截断 DMA，再视已登记 BAR 有效性写 `device_status = 0` 确认复位后归还 DMA 页。
