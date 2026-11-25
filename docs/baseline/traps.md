# OS_proj7-dev 双架构与微内核重构工程陷阱手册（`traps.md`）

本文档以仓库**原始未改动基线（`e5fcf65` / `4b1dda0`）**为审计对象，重构而成的**工程陷阱（Trap）专卷**。文档与姊妹篇 `bugs.md` 严格解耦：本卷不重复罗列单点代码缺陷，而是聚焦**双架构底层硬件差异、微内核化服务拆分（`rs` / `vfsd` / `xv6fsd` / `ext2d` / `fat32d` / `blkdrvd` / `consdrvd`）、并发与崩溃恢复语义耦合、极限压测资源预算以及构建测试流水线中的隐蔽工程红线**。

---

## 一、重构基线与模块体量快照

* **参考基线**：`e5fcf65` / `4b1dda0`（原始未改动基线）
* **核心工具链与宿主依赖**：`riscv64-unknown-elf-gcc` / `riscv64-linux-gnu-gcc`、`loongarch64-linux-gnu-gcc`、`qemu-system-riscv64`、`qemu-system-loongarch64`、`mkfs.ext2`、`debugfs`、`python3`、`perl`、`bc`、`expect`
* **内核代码体量画像（总计 ~11,471 LOC）**：
  * 文件系统与块缓冲层（拟迁出至 `vfsd`/`xv6fsd`/`ext2d`/`fat32d`）：**~4,193 LOC**（`ext2.c` 1,216 / `fat32.c` 1,183 / `fs.c` 785 / `vfs.c` 507 / `log.c` 237 / `bio.c` 153 / `block.c` 53 / 头文件 ~59）
  * 设备驱动层（拟迁出至 `blkdrvd`/`consdrvd`）：**~1,193 LOC**（`virtio_disk.c` 520 / `arch/loongarch/virtio_pci.c` 315 / `console.c` 196 / `uart.c` 162）
  * 文件描述符 / 管道 / 系统调用适配层：**~845 LOC**（`sysfile.c` 535 / `file.c` 180 / `pipe.c` 130）
  * 微内核常驻核心与双架构 HAL：**~5,240 LOC**

---

## 二、虚拟内存、跨地址空间拷贝与设备映射陷阱

### [TRAP-01] `vmfault()` 隐式耦合 `myproc()` 与双架构 `pa0` 返回地址域分裂

* **位置**：`src/kernel/vm.c:345-477`、`src/kernel/arch/loongarch/vm.c:205-405`
* **严重级别**：`P0` ｜ **核验方式**：静态核验
* **现象与根因**：
  1. `copyin`/`copyout` 虽接收外部 `pagetable`，但缺页调用的 `vmfault()` 硬编码使用 `myproc()->sz` 与 `myproc()->pagetable`。微内核下，服务端（如 `vfsd`/`ext2d`）经 `ipc_copy()` 访问客户端的 `sbrk` 懒分配页（`sbrkarg`/`rwsbrk` 用例）时，`myproc()` 是服务端而非客户端，会导致误拒合法页或把页错映射到服务端页表。
  2. **双架构语义分裂**：LoongArch64 的 `walkaddr()` 返回**纯物理地址**，而 `vmfault()` 返回**带 `DMWIN_MASK` 的直接映射地址**（`arch/loongarch/vm.c:230`），当前靠调用方幂等 `| DMWIN_MASK` 掩盖。微内核 `ipc_copy()` 若直接将返回值用于 DMA 或跨架构抽象极易出错。
  3. **跨进程并发销毁 UAF**：服务端执行 `ipc_copy()` 期间，若客户端并发 `sbrk(-n)` 或 `kexit()` 释放页表，会引发跨核 UAF。
* **规避方案**：双架构统一实现 `vmfault_proc(struct proc *target, uint64 va, int read)` 并**统一返回纯物理地址**（由调用方显式经 `PA2KVA(pa)` 转换）；`ipc_copy()` 期间对目标进程持有地址空间引用/锁，阻止在其活跃拷贝期销毁页表。

### [TRAP-02] 用户态驱动映射 MMIO/DMA 后，进程退出触发 `kfree`/`freewalk` Panic 或静默硬件踩踏

* **位置**：`src/kernel/vm.c:265-290`、`src/kernel/arch/loongarch/vm.c:248-269`、`src/kernel/kalloc.c:51-56`、`src/kernel/proc.c:158-229, 270-316`
* **严重级别**：`P0` ｜ **核验方式**：静态核验
* **现象与根因**：
  1. `proc_freepagetable()` 仅卸载 `[0, sz)` 并逐页 `kfree()`，随后 `freewalk()` 遇到任意残留叶子 PTE 即 `panic("freewalk: leaf")`。
  2. 若 MMIO 落在 `[0, sz)`，RISC-V 会触发 `panic("kfree")`；更致命的是 LoongArch64 因 `kfree()` 下界错用 `end` 而非 `RAMBASE`（见 `baseline_bugs.md` `BUG-HAL-04`），**不会 Panic 而是直接 `memset(mmio_pa, 1, 4096)` 写毁硬件寄存器并挂入空闲链表**。
  3. 若 `kfork()` 继承设备映射或 `kexec()` 未清理设备映射，将造成权限泄漏与重复释放。
* **规避方案**：所有设备映射放置于 `sz` 之上的受控高地址区（以 `TRAPFRAME` 为锚点向下划定 `DEV_MMIO`/`DEV_DMA`），并在 `struct proc` 登记 `devmaps[]`；`kfork()` 禁止继承、`kexec()` 禁止保留；退出时严格按序执行：① 硬件停机/截断 DMA → ② `uvmunmap(MMIO, do_free=0)` → ③ `uvmunmap(DMA, do_free=1)` → ④ `uvmfree(sz)`。

### [TRAP-03] LoongArch64 MMIO 非页对齐、缓存别名（`MAT`）、同页外设暴露与 DMA 物理地址剥离

* **位置**：`src/kernel/memlayout.h:1-29`、`src/kernel/arch/loongarch/loongarch.h:299-315`、`src/kernel/virtio_disk.c:40-48`
* **严重级别**：`P0` ｜ **核验方式**：静态核验
* **现象与根因**：
  1. LoongArch64 `UART0` 物理地址 `0x1fe001e0` 非 4KB 对齐（页内偏移 `0x1e0`），且与同页其他低速外设共享 4KB 物理页。
  2. `PTE_MAT = (1L << 4)` 为一致可缓存属性；用户态 MMIO 映射**必须清零 `PTE_MAT`（`MAT=0` SUC）并置位 `PTE_NX`**（防止投机取指触发读清寄存器副作用，依赖先修 `BUG-HAL-10` 64 位 `perm`）。同时内核 MMIO 必须改走 `DMWIN1`（SUC，见 `BUG-HAL-03`），否则内核 `MAT=1` 与用户态 `MAT=0` 映射同一物理页会产生 Cache Aliasing。
  3. LoongArch64 `kalloc()` 返回带 `DMWIN_MASK (0x9000000000000000)` 的内核虚址，VirtIO DMA 描述符必须剥离该掩码（`pa & ~DMWIN_MASK`）。
  4. **布局利好**：LoongArch64 内核链接于 `0x9000000000200000`，`kalloc()` 内存池始于 `0x9000000090000000`（128 MiB），BootFS 嵌入 `.rodata` 在 LoongArch64 上不消耗 `kalloc()` 可用页。
* **规避方案**：`dev_map()` 按 `PGROUNDDOWN(pa)` 映射并返回 `user_va + (pa & (PGSIZE-1))`，强制 `MAT=0 | PTE_NX`；仅对受信任 `CAP_CONSDRV` 开放 `UART0` 页；`dma_alloc()` 统一返回纯物理地址。

### [TRAP-15] 双架构 `MAXVA` / `TRAPFRAME` 布局不对称，且 LoongArch64 `KSTACK` 占据 `PGDL` 低半区

* **位置**：`src/kernel/memlayout.h:26-28, 75-90`、`src/kernel/arch/loongarch/loongarch.h:321`、`src/kernel/riscv.h:389`
* **严重级别**：`P1` ｜ **核验方式**：静态核验
* **现象与根因**：RISC-V `MAXVA = 1<<38`、`TRAPFRAME = MAXVA - 2*PGSIZE`；LoongArch64 `MAXVA = 1<<47`、`TRAPFRAME = MAXVA - PGSIZE`（且 `TRAMPOLINE == TRAPFRAME`）。更隐蔽的是，LoongArch64 `proc_mapstacks()` 将 `KSTACK(p)` 映射到 `kernel_pagetable` 的低半区 `PGDL`（`TRAPFRAME - (p+1)*2*PGSIZE`），与用户态规划的高地址 `DEV_MMIO`/`DEV_DMA`/`IPC_SHM` 虚址区间直接重合。
* **规避方案**：在 `memlayout.h` 中统一以 `TRAPFRAME` 为锚点向下相对定义设备映射与共享内存区间，禁止硬编码绝对虚址；LoongArch64 若启用 ASID，内核页表必须独占 `ASID=0`（或将 `KSTACK` 迁至 `PGDH` 高半区）。

---

## 三、双架构硬件中断、TLB/ASID 与多核调度陷阱

### [TRAP-04] 用户态中断转发下的电平中断风暴闭环与 Legacy INTx 吞中断

* **位置**：`src/kernel/arch/loongarch/trap.c:186-212`、`src/kernel/arch/loongarch/apic.c:10-33`、`src/kernel/plic.c:20-47`、`src/kernel/virtio_disk.c:493-520`
* **严重级别**：`P0` ｜ **核验方式**：静态核验
* **现象与根因**：
  1. **电平中断风暴**：宏内核在 `devintr()` 内直接读设备 `isr_cfg` 拉低电平线。微内核下内核不再触碰设备私有 MMIO；若内核投递 `NOTIFY_IRQ` 给 `blkdrvd` 后仅 `hal_irq_complete()` 而不屏蔽该 IRQ，`ertn`/`sret` 返回瞬间电平仍高，将触发 100% CPU 无限中断风暴，导致 `blkdrvd` 永不获调度。
  2. **Legacy INTx 吞中断**：QEMU LoongArch64 `virtio-blk-pci` (`vectors=0`) 经 LS7A APIC → EXTIOI 路由，清中断窗口期到达的新完成事件可能被吞。
* **规避方案**：采用标准 **Mask-on-Trap / Unmask-on-Ack** 闭环：内核收 IRQ → 控制器侧屏蔽该 IRQ（RISC-V 清 `PLIC_SENABLE` + `plic_complete`；LoongArch 置 `LS7A_INT_MASK_REG` + `extioi_complete`）→ 投递 `NOTIFY_IRQ` → `blkdrvd` 排空 used ring 并读设备 ISR 后调 `sys_irq_ack()` → 内核解屏蔽 → `blkdrvd` 二次复检 `used_idx != used->idx`，并辅以内核时钟 `NOTIFY_WATCHDOG` 兜底。

### [TRAP-05] LoongArch64 全量刷 TLB 放大多跳 IPC 开销，及引入 ASID 后的 4 类隐蔽雷区

* **位置**：`src/kernel/arch/loongarch/uservec.S:52, 59`、`src/kernel/arch/loongarch/vm.c:12-38, 229`
* **严重级别**：`P1` ｜ **核验方式**：静态核验
* **现象与根因**：基线 LoongArch64 每次 `uservec`/`userret` 均执行 `invtlb 0x0, $zero, $zero` 全刷 TLB，微内核多跳无缓存 IPC 下会导致 `full_usertests.sh` 与 `mount_storm` 严重减速。但若直接移除全刷并启用 `CSR.ASID`，会立刻撞上 4 个暗雷：① `CSR.ASID` 仅 10 位（0..1023），不能直填 `pid`，回绕须全刷 TLB 并递增代次；② `uvmunmap()` 不刷 TLB（`BUG-HAL-12`）将立刻变为必现 UAF；③ `KSTACK` 在 `PGDL` 低半区（`TRAP-15`），必须为内核固定保留 `ASID=0`；④ `kexec()` 换页表必须失效旧 ASID。
* **规避方案**：先在用户态文件系统服务内通过 `libfs` 实现写穿 LRU 块缓存（注意 `TRAP-17` 对 xv6fs WAL 的约束）并以 4 KiB 粒度收发；若需启用 ASID，必须先修 `BUG-HAL-12`、内核独占 `ASID=0` 并实现代次分配器。

### [TRAP-07] `virtio_pci` 重启 BAR 探测破坏活跃译码与崩溃态 DMA 内存踩踏

* **位置**：`src/kernel/arch/loongarch/virtio_pci.c:52, 109-152, 215-216`
* **严重级别**：`P0` ｜ **核验方式**：静态核验
* **现象与根因**：① 重启后的 `blkdrvd` 若在未清除 `PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER` 前向 BAR 写 `0xffffffff` 探测大小，会破坏活跃设备的 MMIO 译码窗口；② `blkdrvd` 崩溃退出时若内核不知动态 BAR（如在注册前崩溃）或先 `kfree()` DMA 页再复位设备，活跃 DMA 会直接踩踏已被重分配给内核/其他进程的物理页。
* **规避方案**：探测 BAR 前先清 `PCI_COMMAND`，重启优先复用已配置合法 BAR 再写 `device_status = 0` 等待读回 0；`blkdrvd` 退出回收时，内核第一步直接通过静态 **PCI ECAM (`0:dev:0`)** 清除 `PCI_COMMAND_MASTER`（bit 2）在总线层物理截断 DMA，再复位设备并确认静止后归还 DMA 页。

### [TRAP-14] UART 内核早期输出 / 异常打印与用户态 `consdrvd` 并发竞争

* **位置**：`src/kernel/uart.c:81-124, 143-162`、`src/kernel/console.c:140-183`
* **严重级别**：`P2` ｜ **核验方式**：静态核验
* **现象与根因**：内核在启动早期、`usertrap()` 异常与 `panic()` 时需直接写串口；若与用户态 `consdrvd` 并发读写 `IER`/`RHR`/`THR`，会造成收字被吞或额外 TX-empty 边沿扰乱 `consdrvd` 发送状态机。
* **规避方案**：`consdrvd` 启动后独占 `UART0` RX、行规程与 `UART0_IRQ`；内核 `early_putc` 仅轮询 `LSR_TX_IDLE` 写 `THR`，绝不读 `RHR` 或改写 `IER`；`consdrvd` 的 TX 中断处理保持完全幂等（无待发数据时忽略）；`^P` 由 `consdrvd` 通过轻量诊断系统调用触发内核 `procdump()`。

### [TRAP-16] `scheduler()` 关中断执行 `cpu_idle()` 与核间 IPI 唤醒使能位缺失

* **位置**：`src/kernel/proc.c:447-471`、`src/kernel/start.c:33`、`src/kernel/arch/loongarch/trap.c:52`
* **严重级别**：`P1` ｜ **核验方式**：静态核验
* **现象与根因**：`scheduler()` 在 `intr_off()` 状态下执行 `wfi` / `idle 0`。双架构硬件虽允许在全局关中断时由“挂起且局部使能”的中断唤醒休眠指令，但当前 `start.c:33` 未使能 `SIE_SSIE`，`arch/loongarch/trap.c:52` 未使能 `IPI_VEC`（bit 12）与 IOCSR IPI Enable。微内核添加 IPI 唤醒时若漏开局部使能位，休眠副核将完全无视 IPI。
* **规避方案**：双架构初始化阶段显式开启 `SIE_SSIE`（RISC-V）与 `IPI_VEC` + IOCSR IPI Enable（LoongArch64），并在收到 IPI 陷入后清除挂起位。

---

## 四、VFS、多文件系统拆分与崩溃恢复语义陷阱

### [TRAP-08] 嵌套挂载点 `unlink` 的“FS 回调 VFS”逆向依赖与挂载表 TOCTOU

* **位置**：`src/kernel/ext2.c:904-908`、`src/kernel/fat32.c:1139-1143`、`src/kernel/vfs.c:194-248, 372-395, 442-463`、`src/kernel/sysfile.c:227-231`
* **严重级别**：`P0` ｜ **核验方式**：静态核验
* **现象与根因**：宏内核中 `ext2_unlink`/`fat32_unlink` 持锁反向回调 `vfs_mountpoint(ip)`。微内核化后 `ext2d`/`fat32d` 绝不能反向 IPC 询问 `vfsd`（会形成 `vfsd -> ext2d -> vfsd` 循环死锁）。此外若 `vfsd` 不对 `MOUNT`/`UMOUNT`/`UNLINK` 串行化，并发操作仍可产生 TOCTOU 误删挂载点；且挂载根的 `..` 跳转拓扑仅 `vfsd` 知晓，不可下发给后端 `fsd`。
* **规避方案**：`vfsd` 独占挂载表与 Vnode 拓扑，`unlink` 前置在 `vfsd` 先 `LOOKUP` 目标并检查 `is_mountpoint(target) == 0`（在 `vfsd` 串行化锁/事件循环内完成）再下发 `FS_OP_UNLINK`；挂载根 `..` 完全由 `vfsd` 拦截返回父挂载目录。

### [TRAP-09] Unlink-While-Open 跨进程引用计数协同与客户端异常退出泄漏

* **位置**：`src/kernel/fs.c:374-424`、`src/kernel/ext2.c:922-925, 1128-1132`、`src/kernel/fat32.c:1147-1150`、`src/kernel/file.c:60-83`
* **严重级别**：`P0` ｜ **核验方式**：静态核验
* **现象与根因**：已删除但仍打开的文件（`unlinkopen` 用例）要求数据块/簇链与 inode 持续存活至最后 FD 关闭；但拆分为多服务后，若客户端被 `kill()` 或崩溃而内核未可靠通知 `vfsd` 回收 FD/`cwd`，将导致孤儿块永久泄漏且 `umount` 永远返回 `EBUSY`；若 `FS_OP_RELEASE` 异步乱序到达，还会在 `fsd` 重演 `inum`/簇号复用竞态（见 `BUG-FS-01`）。
* **规避方案**：定义显式 `FS_OP_OPEN_REF` / `FS_OP_RELEASE(dev, inum, gen)` 协议：`vfsd` 仅在全局引用归零时同步发 `FS_OP_RELEASE` 等待回收完成；内核在 `kexit()` 路径强制同步向 `vfsd` 投递 `PROC_EXIT` 回收该 PID 全部句柄。

### [TRAP-10] FAT32 无持久化 `inum`、服务重启后句柄代次失效与路径重放错绑风险

* **位置**：`src/kernel/fat32.c:406-425, 581-594`
* **严重级别**：`P1` ｜ **核验方式**：静态核验（整合原 `baseline_debug.md` 迁移笔记校准结论）
* **现象与根因**：FAT32 以首簇号作 `inum`，目录项偏移 `entry_off` 仅存内存 `ip->addrs[1]`。`fat32d` 崩溃重启后丢失全部 `entry_off` 与链尾缓存；且**已校准结论明确指出：不能通过“路径重放”恢复旧 FD**，因为原路径可能已被同名新文件替换，路径重放会把旧 FD 静默错绑到新文件造成数据错乱/越权。
* **规避方案**：为每个挂载与服务实例维护单调递增的 **`mount_gen`（卷代次）**；服务崩溃重启后 `vfsd` 递增 `mount_gen`，携带旧代次的打开句柄统一返回 `-1`（`ESTALE`）优雅失效；正常无故障运行中，unlink 后持有的匿名 FD 仍须完整支持读写。

### [TRAP-17] `xv6fsd` WAL 日志落盘顺序约束与跨卷 `begin_op()` 耦合陷阱

* **位置**：`src/kernel/log.c:128-236`、`src/kernel/file.c:79-81, 160-165`、`src/kernel/sysfile.c:157, 203, 343, 405, 426, 446`、`src/kernel/vfs.c:165, 282`
* **严重级别**：`P0` ｜ **核验方式**：静态核验（整合原 `baseline_debug.md` 迁移笔记校准结论）
* **现象与根因**：
  1. `xv6fs` 崩溃恢复严格依赖 **`log_write`（pin 脏块不落 home）→ `write_log`（写日志区）→ `write_head`（提交点）→ `install_trans`（回写 home）** 四步顺序。`TRAP-05` 提到的写穿 LRU 缓存**仅适用于 ext2/FAT32 等允许立即落盘的块**；若对 `xv6fsd` 启用写穿或在 `blkdrvd` 对写请求做重排，home block 提前落盘会直接破坏 `test-xv6.py crash` 的日志恢复语义。
  2. 宏内核在 `sysfile.c` / `vfs.c` 顶层无条件调用 `begin_op()`/`end_op()`，将所有 ext2/FAT32 操作与 xv6 根卷日志强耦合。
* **规避方案**：`begin_op()`/`end_op()` 彻底下沉至 `xv6fsd` 内部；`blkdrvd` 对 `xv6fsd` 提供严格保序写/屏障语义，禁止在 `write_head` 之前落盘 home block。

---

## 五、进程生命周期、ELF 加载与极限资源压测陷阱

### [TRAP-11] `kexec()` 剥离 VFS 后的路径回退、新页表写入与参数拷贝内存尖峰

* **位置**：`src/kernel/exec.c:38-100, 128, 139, 176-197`、`src/kernel/sysfile.c:465-505`
* **严重级别**：`P0` ｜ **核验方式**：静态核验（整合迁移笔记校准结论）
* **现象与根因**：
  1. `kexec()` 对不含 `'/'` 的相对路径在 `cwd` 失败后自动回退 `"/" + path`，剥离 VFS 后若丢失该规则，在 `/mnt` 或 `/fat` 下执行命令将全部失败。
  2. `loadseg()` 将段直接写入未提交的新页表物理页（`user_dst = 0`），而 `exec.c:128, 139` 调用 `copyout(new_pagetable, ...)` 若触发 `vmfault()` 会错误绑定旧进程 `myproc()`。
  3. `sys_exec()` 为每个 `argv[i]` 分配一整页 `kalloc()`（最多 32 页 = 128 KiB），在 `mem`/`sbrkfail` 极限压测后易 OOM。
* **规避方案**：`vfsd` 提供专用 `VFS_OP_EXEC_OPEN`（内置无 `'/'` 路径回退 `"/" + path`）；内核 `kexec()` 使用受限分片内核缓冲区（`GRANT_KERNEL_BUF`）接收数据并自行校验、拷入新页表，服务端绝不接触新页表指针；移除 `exec.c` 中的 `begin_op()`/`end_op()`。

### [TRAP-12] `usertests` 极限压测下的内存预算、进程槽预留、零动态分配与孤儿收养树陷阱

* **位置**：`src/kernel/param.h`、`src/kernel/proc.c:111-152, 321-331, 341-342`、`src/user/usertests.c`、`src/Makefile:184-188`
* **严重级别**：`P0` ｜ **核验方式**：静态核验（整合迁移笔记校准结论）
* **现象与根因**：
  1. **内存预算**：`sbrkmuch` 与 `sbrkfail` 均要求单进程分配 `BIG = 100 MiB`（`usertests` 注册了 `iref`，无 `maxfiles` 测试项）。全部常驻微内核服务 + BootFS 镜像总内存占用设计预算须控制在 **< 8 MiB**（目标 2~4 MiB），且验收须实测空闲页数并跑通完整 `usertests`。
  2. **进程槽耗尽死锁重启**：`forkforkfork` 会打满 64 个普通进程槽；若常驻服务与普通进程共享 64 槽，槽满时故障服务将无法被 `rs` 重启。
  3. **热路径动态分配死锁**：`mem`/`sbrkfail` 会耗尽全部物理页（`kalloc() == 0`），IPC 热路径与常驻服务处理请求时**严禁调用 `kalloc()`/`malloc()`/`fork()`**。
  4. **孤儿收养混淆**：PID 1 为 `rs`、PID 2 为 `/init`，若沿用单一 `initproc` 全局指针，普通用户孤儿会被 `rs` 误收养或服务异常退出被 `/init` 误收割。
  5. **原始 ABI 兼容**：`_forktest` 仅链接 `ulib.o` + `usys.o`，要求原有 23 个 POSIX 系统调用编号与 ABI 零改动兼容。
* **规避方案**：为 `CAP_SUPERVISOR`（`rs`）预留独立系统服务进程槽位（如 `NPROC=72`，普通用户上限 64）；常驻服务启动时静态预分配全部内存池与队列；区分系统服务监护人（`rs`）与用户孤儿收养人（`/init`）；系统服务间走 IPC 端点，不消耗用户态 `NFILE`/`NINODE` 配额。

---

## 六、构建流水线、合盘镜像、QEMU 差异与测试框架陷阱

### [TRAP-13] 测试框架日志正则契约、提示符协议与故障隔离输出约束

* **位置**：`src/test-xv6.py:126, 157`、`tests/runner/run_qemu_case.py:108`、`tests/*/run.sh`
* **严重级别**：`P1` ｜ **核验方式**：静态核验（代码错误已剥离至 `baseline_bugs.md` `BUG-TEST-07`）
* **现象与根因**：
  1. `test-xv6.py` 严格正则匹配行首 `^recovering` 与 `^ireclaim` 验证崩溃恢复；
  2. `run_qemu_case.py --prompt-wait` 依赖终端静默且末尾为 `$`；
  3. 全部 `run.sh` 严格 `--reject "panic:"` 与 `--reject "kerneltrap"`。
* **规避方案**：`xv6fsd` 执行日志重放与孤儿回收时必须原样输出 `recovering ...` 与 `ireclaim: ...`；服务正常/重启日志绝不可包含 `panic:`、`kerneltrap` 字样，也不可在后台异步打印破坏交互 `$ ` 提示符。

### [TRAP-18] 单工作树全局可变状态禁止并行双架构构建与测试

* **位置**：`Makefile:4-5, 52-53, 385-407`、`src/Makefile:73-78`、`tests/{error,ext2,fat32,persist,public_vfs,semantics}/run.sh:16-17`
* **严重级别**：`P1` ｜ **核验方式**：静态核验 + 实测
* **现象与根因**：根目录 `Makefile` 通过全局 `.last-test` / `.last-test-arch` 传递状态，所有目标文件与镜像直接落在 `src/` 下，且 6 套 `run.sh` 的 `result.log` 未按架构分后缀。同一工作树并发执行双架构构建/测试会互相覆盖 `.arch-*`、`fs.img` 与日志。
* **规避方案**：严格串行执行双架构编译，或按架构导出独立副本目录（如 `build-riscv/`、`build-loongarch/`）；各测试日志文件名统一带架构后缀。

### [TRAP-19] 单盘拼接镜像布局硬编码强耦合（`fs.img` = xv6 + ext2 + fat32）

* **位置**：`src/Makefile:233-260`、`src/kernel/param.h:8-11`、`src/kernel/virtio_disk.c:375-380`
* **严重级别**：`P1` ｜ **核验方式**：静态核验
* **现象与根因**：`src/Makefile` 直接将 `ext2.img` 与 `fat32.img` 通过 `cat >> fs.img` 拼接到 xv6 根镜像末尾，偏移量由 `param.h` 中的编译期常量 `FSSIZE`/`EXT2SIZE`/`FAT32SIZE` 硬编码，并在 `virtio_disk_rw()` 中按 `dev` 换算扇区偏移。任意单侧修改分区大小（例如按 `BUG-TEST-03` 扩大 `EXT2_BLOCKS`）若未同步更新 `param.h` 与 `Makefile`，将导致 `FAT32DEV` 分区错位无法挂载。
* **规避方案**：将三分区块数抽取为单一配置源（Makefile 传入 `-DFSSIZE=... -DEXT2SIZE=... -DFAT32SIZE=...` 或共用头文件），在微内核 `blkdrvd` 中通过统一分区表管理逻辑设备偏移并执行严格扇区越界拦截。

### [TRAP-20] LoongArch64 QEMU 版本差异（QEMU 7.2.x vs 8.2+）导致页表/TLB 行为分化

* **位置**：`Dockerfile`、`src/kernel/arch/loongarch/tlbrefill.S`、`src/kernel/arch/loongarch/vm.c`
* **严重级别**：`P1` ｜ **核验方式**：实测（Debian 12 `qemu-system-loongarch64 7.2.22` vs Ubuntu 24.04 `Dockerfile` QEMU 8.2+）
* **现象与根因**：Debian 12 自带的 QEMU 7.2.22 虽能引导 3 核内核并进入 `kexec("/init")`，但其 `LA464` 模型的 `lddir`/`ldpte` 硬件页表遍历仿真与 `Dockerfile` 指定的 Ubuntu 24.04 QEMU 8.2+ 存在行为差异。
* **规避方案**：**绝不可为迁就老版本 QEMU 7.2 而篡改 LoongArch64 规范页表格式**；CI 与验收环境须对齐 `Dockerfile` 的 QEMU 8.2+ 版本，老版本环境下 LoongArch64 以干净编译 + 引导核验为主、运行时全套回归在 QEMU 8.2+ / RISC-V 验证。

### [TRAP-21] 回归测试覆盖盲区（假阳性与缺失负向用例）

* **位置**：`tests/*/run.sh`、`src/user/{unlinkopen,vfsbench,umountstress,test1,test2,stressfs}.c`
* **严重级别**：`P1` ｜ **核验方式**：静态核验
* **现象与根因**：现有测试套件存在四类显著盲区：① 命令回显满足 `--expect`、多实例只检 1 次（`BUG-TEST-09`）；② `unlinkopen.c` 未测 unlink 后继续写 + 槽位复用（未覆盖 `BUG-FS-04`）；③ 缺少畸形 ELF、损坏 ext2/FAT32 镜像、0 字节 FAT32 空文件、孤儿目录 `create`/`link` 等负向安全用例；④ `full_usertests.sh` 与 `test-xv6.py` 存在误判和超时盲区。
* **规避方案**：重构前先加固测试判分器（消除回显假阳性、全量启用 `--prompt-wait` 与早停），并新增针对上述高危路径的最小回归测例，确保微内核每阶段迁移都有可信的自动化绿灯护航。

---

## 七、微内核重构前架构红线 Checklist

* [ ] **地址空间与内存**：`vmfault_proc` 显式绑定目标 `proc` 且双架构统一返回纯物理地址；设备映射登记 `devmaps[]` 并以 `TRAPFRAME` 为锚点相对布局；修复 LoongArch64 `kfree()` 下界与 64 位 `PTE_NX`。
* [ ] **中断与 DMA 安全**：内核 MMIO 改走 `DMWIN1`（SUC）；用户态中断采用 `Mask-on-Trap / Unmask-on-Ack` + 复检 + Watchdog；驱动退出先通过静态 PCI ECAM 清 `PCI_COMMAND_MASTER` 断 DMA 再归还 DMA 页。
* [ ] **VFS 与 FS 拆分**：`vfsd` 独占挂载表、前置 `unlink` 挂载点校验并拦截挂载根 `..`；`mount_gen` 实现重启句柄失效（严禁路径重放）；`xv6fsd` 内部保留完整 WAL 落盘顺序。
* [ ] **资源与并发隔离**：常驻服务总内存 < 8 MiB、热路径零动态分配、`rs` 预留独立进程槽并分离孤儿收养树；双架构开启 IPI 唤醒并消除 `ext2` 锁序反转与 `iput` 快路径竞态。
* [ ] **验证链路可信**：扩大 `ext2.img` 解决 `tests/smp` 确定性失败；消除 `run_qemu_case.py` 与用户态工具的假阳性盲区；LoongArch64 运行时验收对齐 QEMU 8.2+。