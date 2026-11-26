# OS_proj7-dev 深度代码审计报告（`OS_proj7-dev_Audit_Report.md`）

## 一、审计总览

### 各分类与严重级别统计表

| 问题分类                      | Critical | High   | Medium | Low   | 未修复    | 已修复   | 合计                                                                           |
|:------------------------- |:--------:|:------:|:------:|:-----:|:------:|:-----:|:----------------------------------------------------------------------------:|
| **1. 并发、锁序与竞态条件**         | 0        | 4      | 3      | 0     | 7      | 0     | **7** (`AUD-001`~`003`, `026`, `039`, `048`~`049`)                           |
| **2. 内存安全、生命周期与资源泄漏**     | 0        | 2      | 3      | 0     | 5      | 0     | **5** (`AUD-004`~`006`, `045`, `050`)                                        |
| **3. 架构与硬件/HAL 缺陷**       | 0        | 2      | 7      | 1     | 10     | 0     | **10** (`AUD-007`~`010`, `027`~`028`, `033`, `042`, `046`, `057`)            |
| **4. 文件系统与 VFS 语义/持久化缺陷** | 0        | 7      | 6      | 0     | 13     | 0     | **13** (`AUD-011`~`015`, `029`~`030`, `040`~`041`, `043`~`044`, `055`~`056`) |
| **5. 微内核架构、IPC 与服务容错缺陷**  | 0        | 1      | 2      | 0     | 3      | 0     | **3** (`AUD-016`~`017`, `054`)                                               |
| **6. 错误处理与边界条件缺失**        | 0        | 0      | 4      | 2     | 6      | 0     | **6** (`AUD-018`~`020`, `034`~`035`, `052`)                                  |
| **7. 构建系统、测试框架与工程规范问题**   | 0        | 2      | 7      | 4     | 13     | 0     | **13** (`AUD-021`~`025`, `031`~`032`, `036`~`038`, `047`, `051`, `053`)      |
| **总计**                    | **0**    | **18** | **32** | **7** | **57** | **0** | **57**                                                                       |

> 注：`docs/baseline/bugs.md` 中收录的 57 项原始基线缺陷已在 `da20269`..`5b3e153` 中完成前置修复；本报告严格聚焦当前最新代码（`07d59f3`）中**仍然真实存在**的新增缺陷、前置修复引入的残余隐患及深层未覆盖漏洞。

### 工程处置优先级与来源分流表（避免过度工程化）

为避免在教学/竞赛内核中盲目堆砌防御性代码、单纯拉高复杂度，下表将累计核实的 **57 项**问题按**来源归因**（项目扩展/移植引入 vs 原生 xv6 教学简化）与**投入产出比（ROI）**分为三档：

- **第一档 · 必修（11 项）**：真实并发/数据一致性硬伤，或微内核迁移与双架构运行的直接阻塞项；多数仅需修改 1~10 行即可根治，**不增加额外复杂度**。
- **第二档 · 择机修（21 项）**：属于 1~5 行的低成本顺手修补，或在微内核拆分（`vfsd`/`ext2d`/`fat32d`/`rs`）过程中本就会被重写/自然闭环的问题，**无需当前单独折腾**。
- **第三档 · 建议暂不修（25 项）**：MIT 原生 xv6 原本就有的教学简化、“抗人为损坏/畸形磁盘镜像”类深度防御（非 Fuzzing 场景纯增代码量）以及 Web 演示台非核心细节，**建议保持现状以控制复杂度**。

#### 第一档：必修 — 真 Bug 与微内核直接阻塞项（11 项）

| 编号        | 来源归因            | 涉及位置                          | 处置理由与改动评估                                                                |
|:--------- |:--------------- |:----------------------------- |:------------------------------------------------------------------------ |
| `AUD-001` | 项目自研 VFS        | `vfs.c:444-474`               | vfs_lookup("..") 跨挂载 inum 撞号竞态，加 dp->mnt == mnt 校验仅 1 行                  |
| `AUD-002` | 项目改造 proc       | `proc.c:96-148`               | allocpid() 无锁扫描 proc[] 的多核 TOCTOU PID 撞号，加锁或持锁分配即可根治                     |
| `AUD-004` | 双架构 HAL / 原生    | `vm.c / la/vm.c`              | vmfault() 绑定 myproc() 且双架构返回地址域分裂，微内核 ipc_copy 必炸                        |
| `AUD-009` | LoongArch 移植    | `la/vm.c:230`                 | vmfault() 调 tlbinit() 全刷 TLB 并清零 ASID，改 1 行 invtlb_va(va) 更简更快           |
| `AUD-011` | 项目自研 FAT32      | `fat32.c:453-471,1083`        | 空文件首次写入分配首簇后未更新 ip->inum，导致同一文件双 Inode 分裂                                |
| `AUD-026` | 项目自研 VFS/FS     | `fs.c:374-403`                | iput() 释放 ext2/fat32 无链接文件时提前释放磁盘资源，高并发易发缓存劫持                            |
| `AUD-027` | LoongArch 移植    | `la/trap.c:232-245`           | 10ms 子周期即返回 ret=2 致抢占频率 100Hz（设计值 10Hz），改 1~2 行对齐                        |
| `AUD-028` | LoongArch 移植    | `la/extioi.c:14`              | extioi_init() 漏配 VIRTIO0_IRQ (IRQ 17) 路由字节，补 1 行寄存器写                     |
| `AUD-029` | 项目自研 ext2/FAT32 | `ext2.c / fat32.c`            | 目录游标按动态序号映射，边遍历目录边 unlink 会漏删一半文件且 O(N^2)                                |
| `AUD-040` | 项目合盘 / ext2     | `virtio_disk.c:373-388`       | 【仅修 virtio_disk_rw 3 行】补 ROOTDEV/EXT2DEV 分区上界，防越界写穿 FAT32（ext2 深层块校验可忽略） |
| `AUD-054` | 微内核架构文档         | `docs/architecture/README.md` | GRANT_KERNEL_BUF 单页 4 KiB 契约：写码前约定清楚即可，零额外代码复杂度                          |

#### 第二档：择机修 — 低成本顺手修 / 重构期自然闭环（21 项）

| 编号        | 来源归因            | 涉及位置                                    | 处置理由与改动评估                                                              |
|:--------- |:--------------- |:--------------------------------------- |:---------------------------------------------------------------------- |
| `AUD-003` | 项目新增 IPI        | `proc.c:661-672`                        | kkill() 补 1 行 wakeup_idle_cpus()，消除被杀进程在空闲副核上的延迟                       |
| `AUD-005` | 项目自研 ext2       | `ext2.c:798-815`                        | ext2_dir_add 失败回滚目录项与 dp->size，改法简单（微内核迁出时顺手修）                         |
| `AUD-007` | RISC-V HAL      | `entry.S / hal.c`                       | RISC-V _entry 补 2 行 mhartid >= NCPU 拦截，与 LoongArch 对齐                  |
| `AUD-008` | LoongArch 移植    | `la/vm.c:344-406`                       | copyin/copyinstr 补 2 行 va0 >= MAXVA 检查，与 RISC-V 对称                     |
| `AUD-010` | 原生 xv6 遗留       | `vm.c / la/vm.c`                        | uvmclear() 补 1 行单页 TLB 刷新（启用 ASID 前顺手补上）                               |
| `AUD-012` | 项目自研 VFS/FS     | `vfs.c / ext2.c / fat32.c`              | 后端回调 vfs_mountpoint 违反分层：拆出 vfsd/ext2d/fat32d 时必然重写                    |
| `AUD-013` | 项目自研 VFS        | `file.c:148-174`                        | filewrite 对 ext2/fat32 套 3KiB WAL 分块和 begin_op：拆出 vfsd 时自然消除           |
| `AUD-015` | 项目自研 ext2/FAT32 | `ext2.c / fat32.c`                      | create 失败未回滚已扩展的父目录块/簇，迁出 libfs 时顺手修                                   |
| `AUD-016` | 微内核前瞻改造         | `proc.c:13,130-145`                     | NPROC_SVC 与 initproc 混用：引入 PID 1 rs 服务进程时自然闭环                          |
| `AUD-017` | 微内核静态检查         | `.github/scripts/check_repo.py`         | ELF 符号与 ABI 头文件正则盲区：微内核迁移后启用前顺手补齐                                      |
| `AUD-021` | 项目构建系统          | `Makefile`                              | 测试准备目标每次 make clean 全量重编：本地迭代嫌慢时顺手去掉即可                                 |
| `AUD-022` | 项目构建系统          | `src/Makefile`                          | tags/.gdbinit 未适配 build/<arch>：需要 gdb 调试 LoongArch 时顺手修                |
| `AUD-030` | 项目自研 FAT32      | `fat32.c:1203,1143`                     | 空文件保留非零首簇：顺手统一语义或留到 fat32d 整理                                          |
| `AUD-033` | LoongArch 移植    | `la/entry.S / merror.S`                 | entry.S 补 2 行 CPUID < NCPU 拦截；merror.S 机器错误异常兜底                        |
| `AUD-036` | 测试跨架构适配         | `usertests.c`                           | usertests 硬编码 RISC-V Sv39 地址常量，LoongArch 验收时按宏区分即可                     |
| `AUD-037` | 原生 xv6 测试       | `usertests.c:770,864`                   | pipe1/preempt 错误分支 return 改 exit(1)，防止假阳性（改 2 行）                       |
| `AUD-039` | 原生 xv6 遗留       | `printf.c / uart.c`                     | panic() 入口加 1 行 intr_off() 与局部锁变量，防 panic 中途被抢占                        |
| `AUD-042` | 原生 xv6 遗留       | `plic.c:12-47`                          | 6 处 MMIO 指针补 volatile，纯规范修复零复杂度                                        |
| `AUD-043` | 原生 xv6 遗留       | `log.c:216-236`                         | 【仅修 log_write 1 处】把 log.lh.n >= LOGBLOCKS 挪入新增槽位分支（read_head 畸形头校验可忽略） |
| `AUD-047` | 项目测试框架          | `tests/full_usertests.sh / test-xv6.py` | full_usertests.sh QEMU 提前退出卡 13 分钟：CI 遇卡顿时顺手改用 run_qemu_case.py        |
| `AUD-057` | 项目新增 HAL        | `kernelvec.S:69-85`                     | RISC-V ipi_mvec 补 3 行 mcause 校验，避免 M-mode 异常静默死循环                      |

#### 第三档：建议暂不修 — 原生 xv6 教学简化 / 抗畸形镜像防御 / Web 非核心细节（25 项）

| 编号        | 来源归因       | 涉及位置                               | 暂不修理由（控复杂度）                                                      |
|:--------- |:---------- |:---------------------------------- |:---------------------------------------------------------------- |
| `AUD-006` | 原生 xv6 遗留  | `sysfile.c:491`                    | sys_exec 每参数 kalloc 1 页：原生设计，当前 128MiB 内存下不影响测试                  |
| `AUD-014` | 原生 xv6 遗留  | `file.c:173`                       | filewrite 满盘短写返 -1：原生 xv6 契约，乱改可能破坏 usertests 断言                 |
| `AUD-018` | 原生 xv6 遗留  | `user/printf.c`                    | 用户态 printf 64 位截断：原生教学实现，无 64 位打印需求可不动                           |
| `AUD-019` | 原生 xv6 遗留  | `fs.c:470`                         | bmap 在 readi 路径无 alloc=0：仅损坏/空洞文件触发，正常路径不触发                      |
| `AUD-020` | 原生 xv6 遗留  | `user/ulib.c`                      | memmove 参数有符号 int：原生 xv6 写法，无 >2GiB 拷贝场景                         |
| `AUD-023` | Web 控制台    | `web/server/index.js`              | 硬编码 wsl.exe 与 LA CPUS=1：仅在需要 Linux 宿主跑 Web 演示台时再改                |
| `AUD-024` | Web 控制台    | `web/server/ai-trace.js`           | AI 追踪表旧名 sleep 与漏检 la 源码：仅演示台需要时再改                               |
| `AUD-025` | 原生 xv6 遗留  | `mkfs/mkfs.c`                      | 旧 BSD 函数 bcopy/bzero/index：宿主工具正常编译，无须折腾                         |
| `AUD-031` | Web 控制台    | `web/scripts/qa*.mjs`              | QA 脚本选择器失配与硬编码 Edge 路径：非内核核心，按需维护                                |
| `AUD-032` | Web 控制台    | `web/src/traces.js`                | 前端硬编码历史源码行号：非内核核心，答辩演示前再同步                                       |
| `AUD-034` | 原生 xv6 遗留  | `user/ulib.c`                      | start() 丢弃 main() 返回值：xv6 习惯显式调 exit()，改了影响面需重测                  |
| `AUD-035` | 原生 xv6 遗留  | `user/{grep,sh,ln,...}.c`          | 用户态小工具长行/退出码边界：原生教学工具，非核心路径                                      |
| `AUD-038` | Web 控制台    | `web/server/demo-shell.js`         | 快速模式模拟器与内核语义差异：仅 Web 演示用，不影响内核                                   |
| `AUD-041` | 抗畸形镜像防御    | `fat32.c`                          | 损坏 FAT 表/坏簇链 0x0ffffff7 越界防御：非 Fuzzing 场景纯增复杂度                   |
| `AUD-044` | 抗畸形镜像防御    | `fs.c`                             | 损坏 xv6 超级块/块指针/inum 越界防御：原生信任镜像，修了徒增代码量                          |
| `AUD-045` | 原生 xv6 遗留  | `exec.c / src/Makefile`            | ELF machine/T_FILE/W^X/稀疏段校验：除非做安全强化，否则非必须                       |
| `AUD-046` | 双架构 VirtIO | `virtio_disk.c / virtio_pci.c`     | MMIO v2 FEATURES_SEL 与过渡型 PCI BAR：当前 QEMU 参数下工作稳定，迁出 blkdrvd 时再看 |
| `AUD-048` | 原生 xv6 遗留  | `uart.c:167-186`                   | uartintr 锁外读 ISR 理论竞态：迁出 consdrvd 时会整体重构                         |
| `AUD-049` | 原生 xv6 遗留  | `console.c / pipe.c`               | 多读者拆行/满 128B 退格/pipe 64B 分片：原生教学语义，非当前瓶颈                         |
| `AUD-050` | 原生 xv6 遗留  | `user/umalloc.c`                   | K&R malloc 整数溢出与 free(0)：原生教学分配器，无 >128MiB malloc 需求             |
| `AUD-051` | 原生 xv6 遗留  | `user/grind.c`                     | grind.c 孙进程与状态码吞没：仅手工压测用，非 CI 门禁                                 |
| `AUD-052` | Web 控制台    | `web/server/terminal-session.js`   | WebSocket 畸形输入崩溃与孤儿 WSL：仅本地 Web 演示台，按需修                          |
| `AUD-053` | Web 控制台    | `web/server/ai-trace.js / App.jsx` | 带路径命令与 Ctrl-U 前端同步：仅 Web 演示台，按需修                                 |
| `AUD-055` | 抗畸形镜像防御    | `fat32.c:385-412,795`              | 0xe5 首字节文件名与损坏 LFN 字段防御：非 Fuzzing 场景纯增复杂度                        |
| `AUD-056` | 抗畸形镜像防御    | `vfs.c / ext2.c / sysfile.c`       | 16 位 nlink>32767 溢出与 65536 倍数 inum 截断：极端场景，纯增复杂度                 |

---

## 二、分类详细审计记录

### 1. 并发、锁序与竞态条件

#### [AUD-001] `vfs_lookup(dp, "..")` 在挂载点根目录未校验 `dp` 是否仍属于该挂载实例，与 `vfs_umount()` 并发时存在跨挂载 `inum` 撞号竞态

- **严重级别**：High
- **当前状态**：未修复
- **发现 Commit**：`07d59f3`
- **涉及文件与位置**：`src/kernel/vfs.c:444-474`（`vfs_lookup`）、`src/kernel/vfs.c:335-360`（`vfs_umount`）
- **涉及架构 / 模块**：双架构通用 / VFS 挂载点路径解析与并发卸载
- **触发路径与现象**：
  1. 某进程在普通 xv6 根目录（或另一文件系统）下的目录 `dp`（恰巧 `dp->inum == 2`，例如 xv6 根卷下第一个创建的目录或文件分配到的 inode 2）上执行 `vfs_lookup(dp, "..")`；或当挂载实例正处于 `MOUNT_UNMOUNTING` 阶段时执行 `..` 查找。
  2. `vfs_lookup()` 在 `src/kernel/vfs.c:457-460` 仅检查：
     
     ```c
     if((mnt->state == MOUNTED || mnt->state == MOUNT_UNMOUNTING) &&
        mnt->root != 0 && mnt->parent != 0 &&
        mnt->root->dev == dp->dev && mnt->root->inum == dp->inum)
     ```
     
     而没有校验 `dp == mnt->root` 或 `dp->mnt == mnt`。
- **根因分析**：
  1. 在 `vfs_umount()`（`src/kernel/vfs.c:335-359`）中，卸载线程将 `mnt->state = MOUNT_UNMOUNTING` 后释放 `mtable.lock`，随后执行 `iput(root)`（L353）和 `ops->unmount(mnt)`（L354），直到 L356 才重新获取 `mtable.lock` 清零 `mnt`。在 L353 `iput(root)` 释放 `mnt->root` 到 L357 `memset(mnt, 0, ...)` 之间的窗口内（该窗口包含磁盘 I/O 或睡眠锁操作），`mnt->root` 槽位可能已被 `itable` 回收并重分配给其他 inode，或者 `mnt->root` 处于半销毁状态，但 `vfs_lookup()` 对 `MOUNT_UNMOUNTING` 状态的挂载依然解引用 `mnt->root->dev`、`mnt->root->inum` 并对 `mnt->parent` 调用 `idup(parent)`（而 `vfs_umount` 已不再检查 `mnt->readers`）。
  2. 即便不并发卸载，判定条件也应当直接检查 `dp == mnt->root`（或 `dp->mnt == mnt && dp->inum == mnt->root->inum`），在 `MOUNT_UNMOUNTING` 且 `iput(root)` 已开始后绝不能继续读取 `mnt->root` 并递增 `mnt->readers`。
- **潜在影响**：高并发卸载风暴（如扩展版 `umountstress` 叠加 `..` 遍历）下，可能导致正在卸载的 `mnt->parent` 引用计数被并发 `idup` 破坏或读取已回收的 `mnt->root` 字段。
- **修复建议**：
  1. 在 `vfs_umount()` 中，在释放 `mtable.lock` 执行 `iput(root)` 之前，先在 `mtable.lock` 保护下将 `mnt->root = 0; mnt->parent = 0;` 摘出到局部变量（或仅允许 `mnt->state == MOUNTED` 进入 `vfs_lookup` 的 `..` 跨挂载回退，因为一旦 `vfs_mount_busy(mnt) == 0` 通过，说明没有任何进程持有该挂载下的活跃 inode，不可能再有合法的 `dp` 属于该挂载点！）。
  2. 将 `vfs_lookup()` 的匹配条件收紧为 `mnt->state == MOUNTED && mnt->root == dp && mnt->parent != 0`。

---

#### [AUD-002] `allocpid()` 的 `pid_in_use()` 无锁扫描 `proc[]` 且在 `acquire(&p->lock)` 之外赋 `p->pid`，多核并发 `fork` 下存在 TOCTOU PID 撞号窗口

- **严重级别**：High
- **当前状态**：未修复
- **发现 Commit**：`07d59f3`
- **涉及文件与位置**：`src/kernel/proc.c:94-121`（`pid_in_use` / `allocpid`）、`src/kernel/proc.c:183-200`（`detachproc`）
- **涉及架构 / 模块**：双架构通用 / 内核进程管理（`proc.c`）
- **触发路径与现象**：
  `allocpid()` 在 `pid_lock` 保护下调用 `pid_in_use(pid)` 检查 `pid` 是否已被占用，并返回 `pid`；随后在 `allocproc()` 的 `src/kernel/proc.c:148` 执行 `p->pid = allocpid();`。
- **根因分析**：
  1. `allocpid()` 在 `src/kernel/proc.c:118` 释放了 `pid_lock` 之后，调用方 `allocproc()` 才在 L148 将返回值写入 `p->pid`。
  2. 当 `nextpid` 发生回绕（或接近回绕）时，若 CPU A 从 `allocpid()` 拿到空闲 `pid = X` 并释放 `pid_lock`，在 CPU A 执行 `p->pid = X` 之前（或当 `nextpid` 再次回绕扫描时），`proc` 表中尚无任何槽位记录 `q->pid == X`；同时 `detachproc()` 中 `p->pid = 0` 是普通非原子写（L192），与 `pid_in_use()` 中的 `__atomic_load_n(&q->pid, __ATOMIC_RELAXED)` 缺乏 `UNUSED` 状态过滤。
- **潜在影响**：`nextpid` 回绕后，并发 `allocproc()` 或未过滤 `q->state != UNUSED` 的非原子读取存在理论竞态，且违反了“`p->pid` 应具有清晰锁归属与生命周期不变量”的内核同步规范。
- **修复建议**：
  在 `allocproc()` 中（已持有 `p->lock`），要么将 `p->pid` 的赋值直接放在 `pid_lock` 临界区内完成（例如 `allocpid_into(struct proc *p)`），要么在 `detachproc()` 中使用 `__atomic_store_n(&p->pid, 0, __ATOMIC_RELEASE)` 并在 `pid_in_use()` 中使用 `__ATOMIC_ACQUIRE`。

---

#### [AUD-003] `kkill()` 唤醒睡眠进程时未调用 `wakeup_idle_cpus()` 发送跨核 IPI，导致被杀进程在空闲副核上延迟唤醒

- **严重级别**：Medium
- **当前状态**：未修复
- **发现 Commit**：`07d59f3`
- **涉及文件与位置**：`src/kernel/proc.c:700-721`（`kkill`）
- **涉及架构 / 模块**：双架构通用 / 多核调度与信号唤醒
- **触发路径与现象**：
  CPU 0 调用 `kill(pid)` 终止一个正在 `SLEEPING` 的进程（如阻塞在 `sleep(&ticks)`、`consoleread`、`piperead` 或未来微内核的 `ipc_call`），而此时 CPU 0 仍在运行当前进程，CPU 1 / CPU 2 处于 `cpu_idle()`（`wfi` / `idle 0`）。
- **根因分析**：
  `wakeup()` 在 `src/kernel/proc.c:692-693` 将睡眠进程置为 `RUNNABLE` 后会调用 `wakeup_idle_cpus()` 向空闲核发送 IPI；但 `kkill()` 在 `src/kernel/proc.c:711-716` 将 `p->state = RUNNABLE` 后直接 `release(&p->lock); return 0;`，漏掉了 `wakeup_idle_cpus()`。
- **潜在影响**：
  - 在 RISC-V 上，副核时钟中断周期为 100 ms（`1000000` cycles @ 10MHz），空闲副核最坏需等待 100 ms 才会从 `wfi` 醒来调度被杀进程退出；
  - 在未来微内核服务看门狗 `rs` 调用 `kill(stuck_service_pid)` 时，会无故增加高达 100 ms（RISC-V）/ 10 ms（LoongArch）的故障恢复 MTTR 延迟。
- **修复建议**：
  在 `kkill()` 中记录 `int woke = 0;`，当把 `SLEEPING` 进程改为 `RUNNABLE` 时置 `woke = 1;`，在 `release(&p->lock)` 之后若 `woke` 为真则调用 `wakeup_idle_cpus()`。

---

#### [AUD-026] `iput()` 释放 `ext2` / `fat32` 无链接 inode 时在未隔离 `itable` 槽位前提前释放磁盘 inode/簇，导致并发创建重用同一 `inum` 时发生缓存项劫持

- **严重级别**：High
- **当前状态**：未修复
- **发现 Commit**：`07d59f3`
- **涉及文件与位置**：`src/kernel/fs.c:260-264`（`iget`）、`src/kernel/fs.c:374-403`（`iput`）、`src/kernel/ext2.c:1206-1207`（`ext2_itrunc`）、`src/kernel/fat32.c:1128-1138`（`fat32_itrunc`）
- **涉及架构 / 模块**：双架构通用 / VFS Inode 缓存回收与 `ext2`、`fat32` 磁盘资源释放
- **触发路径与现象**：
  1. 线程 A 关闭一个已被 `unlink` 的 `ext2` 或 `fat32` 文件的最后一个引用，进入 `iput(ip)`（`src/kernel/fs.c:374-394`）。此时 `ip->ref == 1 && ip->valid && ip->nlink == 0`。
  2. `iput()` 获取 `ip->lock`，在 **`ip->ref` 仍为 `1` 且 `ip->dev`、`ip->inum` 仍保留在 `itable` 中** 的状态下释放 `itable.lock`（L383），将 `ip->valid` 置为 `0` 后调用 `vfs_itrunc(ip)`（L389）。
  3. `ext2_itrunc()` 在 L1207 调用 `ext2_free_inode(fs, ip->inum)` 将磁盘 inode 位图对应位清零；`fat32_itrunc()` 在 L1133 调用 `fat32_free_chain(fs, ip->addrs[0])` 将首簇（即非空文件的 `ip->inum`）在 FAT 表中清零。
  4. 在线程 A 从 `vfs_itrunc(ip)` 返回并重新获取 `itable.lock`（L393）之前的窗口期内，另一 CPU 上的线程 B 调用 `ext2_create()` 或 `fat32_create()`，从磁盘位图或 FAT 表中立即分配到刚刚释放的同一个 `inum`（或首簇号），随后调用 `ext2_iget()` / `fat32_iget()` -> `iget(mnt->dev, inum)`。
  5. `iget()`（`src/kernel/fs.c:260`）扫描 `itable` 时发现 `ip->ref > 0 && ip->dev == dev && ip->inum == inum`（此时线程 A 的 `ip->ref` 仍为 `1`），直接命中线程 A 正在销毁的 `ip` 槽位，将 `ip->ref` 递增为 `2` 并返回给线程 B。
- **根因分析**：
  原生 `xv6fs` 的 `ialloc()` 与 `iput()` 受日志事务（`begin_op()` / `end_op()`）和磁盘 `dip->type` 保护；而 `ext2` 与 `fat32` 不经过 `log.c`，`ext2_free_inode()` 和 `fat32_free_chain()` 立即将释放后的位图/FAT 表项写回块缓存并释放 `fs->lock`，使磁盘 `inum`/首簇在内存 `itable` 槽位真正退役（`ip->ref` 归零）之前即可被并发分配。若线程 B 在 L391 `releasesleep(&ip->lock)` 与 L393 `acquire(&itable.lock)` 之间填充并操作该 `ip`，会导致新旧生命周期状态互相污染或引用计数异常。
- **潜在影响**：
  多核并发在同一 `ext2` 或 `fat32` 挂载点上高频删除并创建文件时，新创建的 inode 可能复用到尚未完成 `iput()` 退役的 `struct inode` 槽位，引发元数据状态污染或空指针崩溃。
- **具体修复建议**：
  在 `iput()`（`src/kernel/fs.c:375-394`）释放 `itable.lock` 进入 `vfs_itrunc(ip)` 之前，先在持有 `itable.lock` 时将该待销毁槽位从 `iget()` 匹配中隔离（例如设置回收标志或先将 `ip->inum` 保存到局部变量并标记槽位不可匹配），待 `vfs_itrunc(ip)` 完成后再彻底归零 `ip->ref`。

---

#### [AUD-039] `panic()` 未调用 `intr_off()` 关闭本地中断且与并发 `printf()`/`uartputc_sync()` 共享非对称 `panicking` 判断，可致 `panic` 中途被时钟中断抢占或锁计数失衡

- **严重级别**：High
- **当前状态**：未修复
- **发现 Commit**：`07d59f3`
- **涉及文件与位置**：`src/kernel/printf.c:18-19, 64-72, 130-145`（`printf`、`panic`）、`src/kernel/uart.c:503-520`（`uartputc_sync`）
- **涉及架构 / 模块**：双架构通用 / 内核异常停机（`panic`）与串口同步输出
- **触发路径与现象**：
  1. **`panic()` 执行期间本地中断仍处于开启状态**：在 `src/kernel/printf.c:137-145` 中：
     
     ```c
     void
     panic(char *s)
     {
       panicking = 1;
       printf("panic: ");
       printf("%s\n", s);
       panicked = 1; // freeze uart output from other CPUs
       for(;;)
         ;
     }
     ```
     
     `panic()` 全程未调用 `intr_off()`。同时，因为 `panicking` 被置为 `1`，随后调用的 `printf()`（L70 `if(panicking == 0) acquire(&pr.lock);`）和 `uartputc_sync()`（`src/kernel/uart.c:505` `if(panicking == 0) acquire(&tx_lock);`）均跳过了自旋锁获取（从而也未通过 `push_off()` 关中断）。当进程在系统调用上下文（`usertrap()` 已执行 `intr_on()` 且当前未持有自旋锁，如 `ilock`、`iunlock`、`bmap`、`dirlookup`、`kfree`、`uvmunmap` 等触发 `panic`）调用 `panic()` 时，**CPU 中断始终处于开启状态**。
  2. 在 `panic()` 慢速轮询串口输出或停在 `for(;;);` 期间，一旦该核心触发时钟中断（`kerneltrap()` -> `which_dev == 2 && myproc() != 0` -> `yield()`），当前触发 `panic` 的进程会被 `yield()` 切走，CPU 继续调度执行其他进程；若 `panic()` 发生时恰好持有一把自旋锁，时钟中断不会触发，但若发生在 `mycpu()->noff == 0` 的路径，时钟中断会直接打断 `panic()`！
  3. **跨核 `panicking` 改写破坏 `acquire`/`release` 配对**：在 `printf()`（L70, L130）和 `uartputc_sync()`（`uart.c:505, 518`）中，加锁与解锁分别独立读取全局变量 `panicking`。若 CPU A 在 `panicking == 0` 时进入 `printf()` 获取了 `pr.lock`（`noff` 增至 1），在其打印期间 CPU B 触发 `panic()` 将 `panicking` 置为 `1`，则 CPU A 执行到 L130 `if(panicking == 0) release(&pr.lock);` 时会**跳过解锁**，导致 `pr.lock`/`tx_lock` 永远不被释放且 CPU A 的 `mycpu()->noff` 引用计数泄漏。
- **根因分析**：
  `panic()` 缺少入口 `intr_off()`；且 `printf()` / `uartputc_sync()` 未将加锁决策保存在局部变量 `int locked = !panicking;` 中，而是前后两次读取易变的全局 `panicking` 标志。
- **潜在影响**：
  内核发生 `panic` 时可能被时钟中断抢占切走而无法停机，或因多核交错跳过 `release()` 导致死锁与输出截断。
- **具体修复建议**：
  1. 在 `panic()`（`src/kernel/printf.c:138`）第一行立即调用 `intr_off();`；
  2. 在 `printf()` 与 `uartputc_sync()` 中使用局部变量记录加锁状态：`int locking = !__atomic_load_n(&panicking, __ATOMIC_ACQUIRE); if(locking) acquire(...); ... if(locking) release(...);`。

---

#### [AUD-048] `uartintr()` 在未持 `tx_lock` 时读取 `ISR` 清除发送完成中断，且与 `uartputc_sync()` 并发写 `THR` 时可能丢失 `tx_busy` 唤醒信号

- **严重级别**：Medium

- **当前状态**：未修复

- **发现 Commit**：`07d59f3`

- **涉及文件与位置**：`src/kernel/uart.c:81-106`（`uartwrite`）、`src/kernel/uart.c:114-131`（`uartputc_sync`）、`src/kernel/uart.c:167-186`（`uartintr`）

- **涉及架构 / 模块**：双架构通用 / 16550A UART 串口驱动（`uart.c`）

- **触发路径与现象**：
  
  1. 在 `src/kernel/uart.c:167-177` 中：
     
     ```c
     void
     uartintr(void)
     {
       ReadReg(ISR); // acknowledge the interrupt
     
       acquire(&tx_lock);
       if(ReadReg(LSR) & LSR_TX_IDLE){
         // UART finished transmitting; wake up sending thread.
         tx_busy = 0;
         wakeup(&tx_chan);
       }
       release(&tx_lock);
     ```
     
     `ReadReg(ISR)` 在 `acquire(&tx_lock)` **之前**无锁执行。按照 16550A 硬件规范及 QEMU `hw/char/serial.c` 实现，读取 `ISR`（`IIR`）寄存器会立即清除 UART 内部的 `thr_ipending`（发送保持寄存器空中断挂起标志）。
  
  2. 当用户进程在 `uartwrite()` 中向 `THR` 写入 1 字节并将 `tx_busy = 1` 置位后进入 `sleep(&tx_chan, &tx_lock)` 等待发送完成中断时：
     
     - 若另一核心正在执行内核 `printf()` 或输入回显 `consputc()` 调用 `uartputc_sync(c)`（持 `tx_lock` 轮询 `LSR_TX_IDLE` 并写入 `THR`），此时 `uartintr()` 在锁外执行 `ReadReg(ISR)` 清除了发送中断，而紧接着另一个核心的 `uartputc_sync()` 向 `THR` 写入新字节使 `LSR_TX_IDLE` 短暂变为 `0`；
     - 当 `uartintr()` 随后拿到 `tx_lock` 检查 `if(ReadReg(LSR) & LSR_TX_IDLE)` 时，若 `LSR_TX_IDLE` 尚未再次置位，则 `tx_busy` 不会被清零，`wakeup(&tx_chan)` 也被跳过；
     - 同样，若 `uartputc_sync()` 在 `tx_busy == 1` 期间轮询等待 `LSR_TX_IDLE` 并写完字节后没有唤醒或传递状态，而 `ReadReg(ISR)` 已将中断确认，睡眠在 `tx_chan` 上的 `uartwrite()` 进程将丢失唤醒，直至下一次偶发的键盘输入或后续打印触发新的 UART 中断。

- **根因分析**：
  `ReadReg(ISR)` 与 `ReadReg(LSR)` / `tx_busy` 状态更新未在同一个 `tx_lock` 临界区内原子完成，且同步轮询发送函数 `uartputc_sync()` 与中断驱动发送函数 `uartwrite()` 共享硬件 `THR` 却未协调 `tx_busy` 状态机。

- **潜在影响**：
  在多核高频内核 `printf()` 与用户态 `write(1, ...)` 并发输出（或串口输入回显交错）时，可能导致 `uartwrite()` 发生发送唤醒丢失而长时间卡顿。

- **具体修复建议**：
  
  1. 在 `uartintr()` 中先 `acquire(&tx_lock)`，在锁内执行 `ReadReg(ISR)` 与 `LSR_TX_IDLE` 检查；
  2. 在 `uartputc_sync()` 完成 `WriteReg(THR, c)` 后，若检测到 `tx_busy != 0`，等待 `while((ReadReg(LSR) & LSR_TX_IDLE) == 0);` 并置 `tx_busy = 0; wakeup(&tx_chan);`，确保不会吞掉 `uartwrite()` 等待的发送完成事件。

---

#### [AUD-049] `consoleread()` 逐字节释放 `cons.lock` 导致多读者并发读同一行时字符被交错拆分、满 128 字节缓冲提前推进 `cons.w` 锁死退格编辑，且 `pipewrite()` 每 64 字节释放锁破坏小块写入原子性

- **严重级别**：Medium
- **当前状态**：未修复
- **发现 Commit**：`07d59f3`
- **涉及文件与位置**：
  - `src/kernel/console.c:95-152`（`consoleread`）
  - `src/kernel/console.c:168-206`（`consoleintr`）
  - `src/kernel/pipe.c:77-119`（`pipewrite`）
- **涉及架构 / 模块**：双架构通用 / 控制台行缓冲与管道并发同步（`console.c`、`pipe.c`）
- **触发路径与现象**：
  1. **`consoleread()` 缺少读睡眠锁，多进程并发读控制台时同一行字符被交错瓜分（`src/kernel/console.c:95-152`）**：
     `consolewrite()` 已在 `5b3e153` 中引入 `cons.write_lock` 睡眠锁串行化多进程写，但 `consoleread()` 未设置对应的读睡眠锁，且在 `while(n > 0)` 循环内**每读取 1 个字节就执行一次 `release(&cons.lock); either_copyout(...); acquire(&cons.lock);`**（L129-147）。当父子进程（如后台任务与前景 Shell）同时阻塞在 `sleep(&cons.r, &cons.lock)` 上时，一旦用户输入回车触发 `wakeup(&cons.r)`，所有睡眠读者被同时唤醒并在逐字节释放 `cons.lock` 的间隙交替抢夺 `cons.r++`！这不仅导致每个进程只拿到残缺交错的字符片段，而且未抢到唯一那个 `'
     '` 字符的进程会继续卡在 `while(cons.r == cons.w)` 睡眠等待下一行。
  2. **`consoleintr()` 在缓冲区满 128 字节时提前令 `cons.w = cons.e`，在无活跃读者时直接锁死退格与换行（`src/kernel/console.c:171-201`）**：
     
     ```c
     if(c == '
     ' || c == C('D') || cons.e-cons.r == INPUT_BUF_SIZE){
       cons.w = cons.e;
       wakeup(&cons.r);
     }
     ```
     
     当用户在当前尚无进程调用 `consoleread()` 时连续输入达到 128 字节（`cons.e - cons.r == INPUT_BUF_SIZE`），L198 立即将 `cons.w = cons.e`。此后用户若按退格键（`C('H')`/`''`，L180 检查 `if(cons.e != cons.w)`）或清除行（`C('U')`，L172 检查 `while(cons.e != cons.w ...)`），均因 `cons.e == cons.w` 而无法删除任何字符；同时由于 `cons.e - cons.r < INPUT_BUF_SIZE` 为假，后续输入的回车 `
     ` 也被全部丢弃。此外，L190 在检查 `c == C('D')` 之前无条件执行 `consputc(c)`，导致用户按下 `Ctrl-D` 时向终端回显原始控制字符 ``。
  3. **`pipewrite()` 每 64 字节（或跨页边界）释放 `pi->lock`，导致 `<= PIPESIZE (512B)` 的并发管道写发生 64 字节粒度交错（`src/kernel/pipe.c:83-116`）**：
     `pipewrite()` 将写入按 `sizeof(kbuf)`（64 字节）分批 `copyin`，每写完 64 字节就执行 `wakeup(&pi->nread); release(&pi->lock);`。当两个写进程并发向同一管道写入 65~512 字节的数据块时，会在 64 字节边界处被彼此打断并交错混写。
- **根因分析**：
  为避免持自旋锁执行 `copyin`/`copyout` 引发缺页死锁，代码将自旋锁粒度拆小到了单字节（`consoleread`）或 64 字节（`pipewrite`），但未在 `consoleread` 外层像 `consolewrite` 一样加 `sleeplock` 保护单次系统调用的整体原子性；`consoleintr` 则对 `C('D')` 回显与满缓冲边界处理不够严密。
- **潜在影响**：
  多进程并发读标准输入会拆碎输入行并挂起未读到 `
  ` 的进程；预输入满 128 字节后无法退格修正；并发写管道在 64 字节边界产生数据交错。
- **具体修复建议**：
  1. 在 `cons` 结构中增加 `struct sleeplock read_lock;`，在 `consoleread()` 入口与退出处通过 `acquiresleep(&cons.read_lock)` / `releasesleep(&cons.read_lock)` 串行化并发读；
  2. 在 `consoleintr()` 中当 `c == C('D')` 时不调用 `consputc(c)` 回显原始 ``；
  3. 若需保证管道 `<= PIPESIZE` 写入原子性，可在 `struct pipe` 中增加写睡眠锁或在内核栈/页缓冲中一次性 `copyin` 不超过 `PIPESIZE` 的块后再持 `pi->lock` 写入。

### 2. 内存安全、生命周期与资源泄漏

#### [AUD-004] LoongArch64 的 `vmfault()` 返回带 `DMWIN_MASK` 的虚拟地址而 RISC-V 返回物理地址，且 `vmfault()` 隐式绑定 `myproc()`

- **严重级别**：High
- **当前状态**：未修复
- **发现 Commit**：`07d59f3`
- **涉及文件与位置**：`src/kernel/arch/loongarch/vm.c:206-232`（LoongArch `vmfault`） vs `src/kernel/arch/loongarch/vm.c:70-87`（LoongArch `walkaddr`） vs `src/kernel/vm.c:464-484`（RISC-V `vmfault`）
- **涉及架构 / 模块**：LoongArch64 & RISC-V / 虚拟内存管理（`vm.c`）
- **触发路径与现象**：
  1. 在 LoongArch64 上，`walkaddr(pagetable, va)` 返回 `PTE2PA(*pte)`，即**不带 `DMWIN_MASK` 的纯物理地址**（`0x90000000...`）；而 `vmfault(pagetable, va, read)` 返回 `(uint64)mem`（即 `kalloc()` 返回的**带 `DMWIN_MASK` `0x9000000090000000` 的直接映射内核虚拟地址**）。
  2. 在 `copyout`/`copyin`/`copyinstr`（`src/kernel/arch/loongarch/vm.c:323-384`）中，当 `walkaddr` 返回 0 时调用 `pa0 = vmfault(...)`，随后统一执行 `(pa0 + (dstva - va0)) | DMWIN_MASK`。当前仅因 `DMWIN_MASK` 按位或具有幂等性而未崩溃。
  3. 更严重的是，在 RISC-V `src/kernel/vm.c:479` 中，`vmfault(pagetable, va, read)` 接收了参数 `pagetable`，却在调用 `mappages` 时写成了：
     
     ```c
     if (mappages(p->pagetable, va, PGSIZE, mem, PTE_W|PTE_U|PTE_R) != 0)
     ```
     
     即使用 `ismapped(pagetable, va)` 检查传入的 `pagetable`，却把新分配的物理页映射到了 `myproc()->pagetable`！
- **根因分析**：
  - RISC-V `vmfault()` 第 472 行检查 `ismapped(pagetable, va)`，第 479 行却映射 `p->pagetable`。若传入的 `pagetable != myproc()->pagetable`（例如 `kexec()` 构建新页表期间或微内核跨进程 `copyin`/`copyout`），`vmfault()` 会将物理页错误映射进旧页表/服务端页表，随后 `copyout()` 第 365 行在传入的 `pagetable` 上调用 `pte = walk(pagetable, va0, 0)`，将因 `pte == 0` 直接触发**内核空指针解引用崩溃**（`*pte & PTE_W`）！
  - 同时，RISC-V `vmfault()` 在映射懒分配页时使用的权限为 `PTE_W|PTE_U|PTE_R`（无 `PTE_NX`，在 RISC-V 上虽为 0，但在代码一致性上未显式表达，且未检查 `va >= MAXVA`）。
- **潜在影响**：
  - 任何对非 `myproc()->pagetable` 的 `copyout`/`copyin` 一旦落入未映射但 `< p->sz` 的区域，RISC-V 内核会在 `src/kernel/vm.c:367` 解引用 `pte == NULL` 崩溃；
  - LoongArch64 `vmfault()` 返回值带 `DMWIN_MASK` 与 `walkaddr()` 返回纯物理地址语义矛盾，若后续 DMA 或物理页计算直接使用 `vmfault()` 返回值将产生高位地址污染。
- **修复建议**：
  1. 在 RISC-V `src/kernel/vm.c:469, 479` 中增加 `va >= MAXVA` 检查，并将 `mappages(p->pagetable, ...)` 修正为 `mappages(pagetable, ...)`；同时在 `src/kernel/vm.c:366` 增加 `if(pte == 0 || (*pte & PTE_W) == 0) return -1;` 防御空指针。
  2. 在 LoongArch64 `src/kernel/arch/loongarch/vm.c:231` 中统一返回 `((uint64)mem) & ~DMWIN_MASK` 纯物理地址，使 `vmfault()` 与 `walkaddr()` 返回地址域严格一致。

---

#### [AUD-005] `ext2_dir_add()` 在目录扩展块写入后若 `ext2_sync_inode(dp)` 失败，未回退 `dp->size` 亦未释放新分配的目录块

- **严重级别**：High
- **当前状态**：未修复
- **发现 Commit**：`07d59f3`
- **涉及文件与位置**：`src/kernel/ext2.c:798-815`（`ext2_dir_add`）、`src/kernel/ext2.c:893-900`（`ext2_create`）
- **涉及架构 / 模块**：双架构通用 / ext2 文件系统目录管理
- **触发路径与现象**：
  当 ext2 目录现有块已满，需要分配新块扩展目录时，`ext2_dir_add()` 执行：
  
  ```c
  bno = ext2_bmap(dp, dp->size / fs->block_size, 1);
  if(bno == 0)
    return -1;
  bp = bread(fs->dev, bno);
  ...
  bwrite(bp);
  brelse(bp);
  dp->size += fs->block_size;
  if(ext2_sync_inode(dp) < 0)
    return -1;
  ```
- **根因分析**：
  1. `ext2_bmap(dp, ..., 1)` 已分配新数据块（及可能的间接块）并写入 `dp->addrs[]`、递增 `dp->nblocks`；
  2. 随后 `bp` 中写入了指向 `inum` 的新目录项并 `bwrite(bp)` 落盘，且 `dp->size += fs->block_size`；
  3. 若此时 `ext2_sync_inode(dp)` 失败返回 `-1`，调用方 `ext2_create()`（L893-898）会立即执行 `ext2_free_inode(fs, inum)` 释放该子 inode！但此时内存中缓存的父目录 `dp` 依然保留着增加后的 `dp->size` 和指向 `bno` 的块指针，而磁盘块 `bno` 中还留着 `de->inode = inum` 的目录项！
  4. 后续任何访问父目录 `dp` 的进程都会在 `dp` 的新块中读到指向已被释放（甚至被重分配给其他文件）的 `inum` 的悬空目录项（Dangling Directory Entry），导致跨文件数据损坏或目录结构混乱。
- **修复建议**：
  在 `ext2_dir_add()` 的新块扩展分支中，若 `ext2_sync_inode(dp) < 0`，应将 `de->inode = 0; bwrite(bp);` 抹除该目录项并回退 `dp->size -= fs->block_size`。

---

#### [AUD-006] `sys_exec()` 在内核堆上贪婪分配最多 `MAXARG`（32）个 4 KiB 物理页暂存参数，在内存紧缺时极易触发假性 OOM

- **严重级别**：Medium
- **当前状态**：未修复
- **发现 Commit**：`07d59f3`
- **涉及文件与位置**：`src/kernel/sysfile.c:469-509`（`sys_exec`）、`src/kernel/exec.c:119-145`（`kexec`）
- **涉及架构 / 模块**：双架构通用 / 系统调用与内存管理
- **触发路径与现象**：
  用户进程调用 `exec(path, argv)` 传入多个短参数（例如 `sh` 或测试程序传入 10~31 个几字节的参数）时，`sys_exec()` 在 `src/kernel/sysfile.c:491` 为**每一个**非空 `argv[i]` 调用 `kalloc()` 分配一整页（4096 字节），最多消耗 `32 * 4 KiB = 128 KiB` 物理内存，而实际上 `kexec()` 中用户栈总共才 1 页（`USERSTACK = 1`，即 4096 字节，`sp < stackbase` 即报失败）。
- **根因分析**：
  既然 `kexec()` 中所有 `argv[i]` 字符串加 `ustack[]` 指针数组的总大小受限于单个用户栈页（`< PGSIZE = 4096` 字节），在 `sys_exec()` 中为每个参数分配一个独立的 4 KiB 页属于高达 32 倍的冗余物理页开销。在 `usertests` 的 `execout` / `sbrkfail` 或未来微内核常驻服务占用内存后，空闲页接近阈值时会导致合法的短参数 `exec` 不必要地因 `kalloc() == 0` 失败。
- **修复建议**：
  在 `sys_exec()` 中仅分配 **1 个** 4 KiB 物理页作为紧凑字符串池（`char *argbuf = kalloc()`），按实际 `fetchstr` 长度顺序紧凑存放所有 `argv[i]`，若总长度超过 `PGSIZE` 直接返回 `-1`，结束后仅需一次 `kfree(argbuf)`。

---

---

#### [AUD-045] `kexec()` 缺失可执行文件 `T_FILE` 类型与 ELF `machine`/`type` 架构校验、放行 `RWX` 段（且 `_forktest` 以 `-N` 链接为 `RWX`），并在稀疏 `PT_LOAD` 段间隙错误分配映射物理页

- **严重级别**：Medium
- **当前状态**：未修复
- **发现 Commit**：`07d59f3`
- **涉及文件与位置**：
  - `src/kernel/exec.c:14-24`（`flags2perm`）
  - `src/kernel/exec.c:66-108`（`kexec`）
  - `src/Makefile:195-201`（`$(BUILD)/$U/_forktest`）
- **涉及架构 / 模块**：双架构通用 / ELF 加载器（`exec.c`）与用户态构建规则（`src/Makefile`）
- **触发路径与现象**：
  1. **未校验 Inode 类型为普通文件（`T_FILE`）**：`kexec()` 在 `src/kernel/exec.c:66-70` 对 `ip = namei(path)` 加锁后，未检查 `ip->type == T_FILE`，直接调用 `vfs_readi(ip, 0, (uint64)&elf, 0, sizeof(elf))` 读取目录（`T_DIR`）或设备节点（`T_DEVICE`）的内容作为 ELF 头。
  2. **未校验 `elf.type == ET_EXEC` 与架构 `elf.machine` 及入口对齐**：`src/kernel/exec.c:73` 仅检查 `elf.magic != ELF_MAGIC || elf.phentsize != sizeof(ph) || elf.phnum == 0`，未检查 `elf.type == 2`（`ET_EXEC`），也未检查 `elf.machine` 是否匹配当前内核架构（RISC-V `EM_RISCV = 243` vs LoongArch64 `EM_LOONGARCH = 258`）及 `elf.entry` 指令对齐（LoongArch64 要求 4 字节对齐、RISC-V 要求 2/4 字节对齐）。由于本项目支持在双架构间挂载访问共享的 `ext2`/`fat32` 镜像，一旦在 LoongArch64 内核上误执行 RISC-V ELF 二进制（或反之），`kexec()` 会在两种架构 64 位 `sizeof(struct proghdr) == 56` 均成立的情况下销毁当前进程地址空间并跳入异构机器码，触发非法指令崩溃而非安全返回 `-1`。
  3. **`flags2perm()` 允许 `RWX`（写+执行）共存，且 `_forktest` 实际被链接为 `RWX` 段**：
     - `flags2perm()`（L14-24）对同时置位 `ELF_PROG_FLAG_EXEC (0x1)` 与 `ELF_PROG_FLAG_WRITE (0x2)` 的段直接返回 `PTE_X | PTE_W`，违反 `W^X`（写异或执行）安全原则；
     - `src/Makefile:199` 使用 `$(LD) $(LDFLAGS) -N -e main -Ttext 0` 链接 `_forktest`（未使用 `$(USER_LD)`，且漏生成 `forktest.sym`），`-N`（`--omagic`）将 `.text` 与 `.data`/`.bss` 合并为单个 `RWE`（`flags = 0x7`）的 `PT_LOAD` 段，导致 `_forktest` 运行时代码页具有可写权限。
  4. **稀疏 `PT_LOAD` 段间隙被错误分配物理页并赋予后一段权限（L94-99）**：
     
     ```c
     if(ph.vaddr % PGSIZE != 0 || ph.vaddr < sz)
       goto bad;
     if((sz1 = uvmalloc(pagetable, sz, ph.vaddr + ph.memsz, flags2perm(ph.flags))) == 0)
       goto bad;
     ```
     
     当首段 `ph.vaddr > 0` 或相邻两个 `PT_LOAD` 段之间存在虚拟地址空洞（`ph.vaddr > PGROUNDUP(sz)`）时，`uvmalloc()` 会从 `PGROUNDUP(sz)` 开始一直分配到 `ph.vaddr + ph.memsz`，把原本未映射的段间空洞 `[PGROUNDUP(sz), ph.vaddr)` 全部填充物理页并映射为后一个段的读写/执行权限。
- **根因分析**：
  `kexec()` 沿用了最简教学假设（假定所有待执行文件都是本架构、从虚拟地址 `0` 开始连续紧凑排列的普通文件），未针对双架构共存、W^X 强化与非连续段做边界约束。
- **潜在影响**：
  跨架构误执行二进制会直接杀死调用进程（如 shell 若直接 `exec`），`_forktest` 破坏用户态 W^X 隔离，且非零基址或含段间隙的 ELF 会产生意外的物理内存消耗与可写/可执行空洞映射。
- **具体修复建议**：
  1. 在 `ilock(ip)` 后立即校验 `if(ip->type != T_FILE) goto bad;`；
  2. 在 ELF 头检查中补充 `elf.type == 2`、当前架构 `elf.machine`（`#ifdef ARCH_LOONGARCH` 为 `258`，否则为 `243`）以及 `elf.entry` 对齐校验；
  3. 在 `kexec()` 中拒绝同时包含 `PF_W` 与 `PF_X` 的段（`if((ph.flags & 0x3) == 0x3) goto bad;`），并将 `src/Makefile:199` 的 `_forktest` 改为使用 `-T $(USER_LD)` 移除 `-N`；
  4. 要求 `ph.vaddr == PGROUNDUP(sz)`（或仅对 `[ph.vaddr, ph.vaddr + ph.memsz)` 调用映射）。

---

#### [AUD-050] 用户态堆分配器 `src/user/umalloc.c` 存在 `nbytes` 无符号回绕、`morecore()` 向 `sbrk(int)` 传大数触发负值缩堆崩溃、非对齐 `sbrk` 破坏 `Header` 对齐以及 `free(0)` 空指针崩溃

- **严重级别**：Medium
- **当前状态**：未修复
- **发现 Commit**：`07d59f3`
- **涉及文件与位置**：`src/user/umalloc.c:25-90`（`free`、`morecore`、`malloc`）
- **涉及架构 / 模块**：双架构通用 / 用户态标准库内存分配器（`umalloc.c`）
- **触发路径与现象**：
  1. **`malloc(uint nbytes)` 的 `nunits` 计算存在 32 位无符号整数溢出回绕（`src/user/umalloc.c:69`）**：
     
     ```c
     nunits = (nbytes + sizeof(Header) - 1)/sizeof(Header) + 1;
     ```
     
     由于 `nbytes` 与 `nunits` 均为 32 位 `uint`，而 `sizeof(Header)` 为 16 字节，当传入 `nbytes >= 0xfffffff1U`（例如 `malloc(-1)`）时，`nbytes + 15` 发生 32 位无符号回绕变为 `0..14`，计算出 `nunits = 1`。`malloc()` 不会返回 `0`（失败），而是**成功返回一个 0 字节可用空间的指针**，后续写入立即越界踩坏相邻堆头 `Header`！
  2. **`morecore(uint nu)` 中 `nu * sizeof(Header)` 乘法溢出或转有符号 `int` 变负数，导致 `sbrk()` 反向缩堆并解引用未映射内存（`src/user/umalloc.c:47-61`）**：
     
     ```c
     static Header*
     morecore(uint nu)
     {
       ...
       if(nu < 4096)
         nu = 4096;
       p = sbrk(nu * sizeof(Header));
       if(p == SBRK_ERROR)
         return 0;
       hp = (Header*)p;
       hp->s.size = nu;
       free((void*)(hp + 1));
       return freep;
     }
     ```
     - `sbrk()` 的声明为 `char* sbrk(int)`（内核 `sys_sbrk` 通过 `argint(0, &n)` 读取有符号 32 位 `int`）。
     - 当 `nbytes` 介于 `[128 MiB, 256 MiB)`（即 `nu` 介于 `[0x00800000, 0x01000000)`）时，`nu * 16` 的结果位于 `[0x80000000U, 0xfffffff0U]`，作为 `int` 传给 `sbrk()` 时变成**负数**！内核 `sys_sbrk()` 将其当作**缩减进程内存（`growproc(n < 0)`）**执行并返回缩堆前的旧顶地址 `p`，随后 `hp->s.size = nu` 直接写入已被解除映射（或位于堆顶之外）的地址触发缺页崩溃！
     - 当 `nbytes >= 256 MiB`（`nu >= 0x01000000`）时，`nu * sizeof(Header)` 发生 32 位截断回绕为小整数，`sbrk()` 仅申请了极小内存，而 `hp->s.size = nu` 却记录了庞大的 `nu`，导致后续 `malloc()` 分配到未映射空间。
  3. **字节粒度 `sbrk()` 后调用 `malloc()` 会破坏 `Header` 的 8/16 字节对齐**：
     若用户程序先调用了非 16 字节倍数的 `sbrk(k)`（如 `grind.c:128` 的 `sbrk(6011)`），进程 `p->sz` 处于非对齐地址。随后 `morecore()` 调用 `sbrk(nu * sizeof(Header))` 返回的 `p` 同样未对齐，导致 `hp = (Header*)p` 产生非对齐的 64 位指针读写与非对齐的 `malloc` 返回值。
  4. **`free(0)` 未做空指针拦截（`src/user/umalloc.c:25-44`）**：
     C 标准要求 `free(NULL)` 为安全空操作，但 `free(0)` 直接计算 `bp = (Header*)0 - 1` 并访问 `p->s.ptr` / `bp->s.size`，立即引发用户进程崩溃。
- **根因分析**：
  `umalloc.c` 直接照搬了 K&R 32 位时代无溢出防护的示例实现，未校验 `nbytes` 与 `nu * sizeof(Header)` 相对有符号 32 位 `int` 上限（`0x7fffffff`）的溢出，亦未处理 `free(0)`。
- **潜在影响**：
  大内存申请（`>= 128 MiB` 或负数转型的大 `uint`）会触发隐式缩堆、堆元数据破坏或返回 0 字节块导致堆溢出。
- **具体修复建议**：
  1. 在 `free(void *ap)` 开头增加 `if(ap == 0) return;`；
  2. 在 `malloc(uint nbytes)` 开头增加上限校验：`if(nbytes > 0x7fffffffU - sizeof(Header)) return 0;`；
  3. 在 `morecore(uint nu)` 中校验 `if(nu > 0x7fffffffU / sizeof(Header)) return 0;`，确保传入 `sbrk()` 的字节数始终在正有符号 `int` 范围内。

---

### 3. 架构与硬件/HAL 缺陷

#### [AUD-007] RISC-V `entry.S`、`start.c` 与 `hal_send_ipi()` 缺少对 `mhartid >= NCPU` 及实际在线核数的越界防护

- **严重级别**：High
- **当前状态**：未修复
- **发现 Commit**：`07d59f3`
- **涉及文件与位置**：
  - `src/kernel/entry.S:12-19`
  - `src/kernel/start.c:43-49`
  - `src/kernel/hal.c:37-43`
  - `src/kernel/proc.c:661-672`
- **涉及架构 / 模块**：RISC-V 64 / 启动、中断与 SMP HAL
- **触发路径与现象**：
  1. 在 `src/kernel/entry.S:14-17` 中，所有进入 `_entry` 的 hart 直接按 `mhartid` 计算栈指针 `sp = stack0 + ((mhartid + 1) * 4096)`，未检查 `mhartid >= NCPU`。若 QEMU `-smp` 配置超过 `NCPU (8)` 或硬件存在稀疏 `mhartid >= 8`，将直接发生 `.bss` 栈越界写，踩坏 `stack0` 之后的内核全局变量（如 `mscratch_buf` 等）。
  2. 在 `src/kernel/hal.c:37-43` 中，`hal_send_ipi(int cpu)` 仅检查 `if(cpu < 0 || cpu >= NCPU)`，而 `wakeup_idle_cpus()` 遍历 `0..NCPU-1`。虽然未启动的 CPU `cpus[i].idle` 为 0，但对比 LoongArch64 已在 `07d59f3` 中引入 `la_ncpus` 上界保护，RISC-V 仍缺少 `mhartid >= NCPU` 的停机拦截（`bge a1, t0, spin`）。
- **根因分析**：
  `stack0` 和 `cpus[]`、`mscratch_buf[]` 的大小均固定为 `NCPU`（8），但 RISC-V `_entry` 对 `mhartid` 无上界检查直接做乘法偏移寻址并写入 `mscratch_buf[id]`。
- **潜在影响**：当启动核数或物理 hart ID `>= NCPU` 时，越界 hart 会静默覆写内核 `.bss`/`.data` 段引发难以排查的内存损坏。
- **修复建议**：
  在 `src/kernel/entry.S` 读取 `csrr a1, mhartid` 后立即增加上界检查：`li t0, NCPU; bgeu a1, t0, spin`（需包含 `param.h`）。

---

#### [AUD-008] LoongArch64 `copyin()` 与 `copyinstr()` 缺失 `va0 >= MAXVA` 显式上界拦截，行为依赖下层函数隐式防御且与 RISC-V 不对称

- **严重级别**：Medium
- **当前状态**：未修复
- **发现 Commit**：`07d59f3`
- **涉及文件与位置**：`src/kernel/arch/loongarch/vm.c:344-406`（`copyin`、`copyinstr`）
- **涉及架构 / 模块**：LoongArch64 / 虚拟内存拷贝（`arch/loongarch/vm.c`）
- **触发路径与现象**：
  对比 `src/kernel/vm.c:392, 424`（RISC-V 在 `copyout`、`copyin`、`copyinstr` 中均显式检查 `if(va0 >= MAXVA) return -1;`），LoongArch64 的 `src/kernel/arch/loongarch/vm.c` 仅在 `copyout()`（L321）添加了 `if(va0 >= MAXVA) return -1;`，而在 `copyin()`（L349）与 `copyinstr()`（L374）中**漏掉了 `if(va0 >= MAXVA) return -1;`**。
- **根因分析**：
  在 `BUG-HAL-11` 修复时，仅给 LoongArch64 的 `copyout()` 补上了 `va0 >= MAXVA`，未同步补齐 `copyin()` 与 `copyinstr()`。虽然当前 `walkaddr()` 和 `vmfault()` 内部碰巧有 `if(va >= MAXVA) return 0;`，但若未来在微内核重构中修改 `vmfault_proc` 或调整内联逻辑，缺少入口 `va0 >= MAXVA` 检查将直接把 `va0 >= MAXVA` 传给 `walk()` 触发内核 `panic("walk")`。
- **潜在影响**：双架构 `vm.c` 边界防御不对称，存在潜在的 `panic("walk")` DoS 风险。
- **修复建议**：
  在 `src/kernel/arch/loongarch/vm.c` 的 `copyin()`（L349 后）和 `copyinstr()`（L374 后）统一补齐 `if(va0 >= MAXVA) return -1;`。

---

#### [AUD-009] LoongArch64 `vmfault()` 每次懒分配单页均调用 `tlbinit()` 执行全量 TLB 刷新并重写 `stlbps`/`asid`/`tlbrehi` CSR

- **严重级别**：Medium
- **当前状态**：未修复
- **发现 Commit**：`07d59f3`
- **涉及文件与位置**：`src/kernel/arch/loongarch/vm.c:230`（`vmfault`）
- **涉及架构 / 模块**：LoongArch64 / 页表与 TLB 管理
- **触发路径与现象**：
  在 LoongArch64 `vmfault()`（`src/kernel/arch/loongarch/vm.c:225-231`）中，每次为懒分配页面建立映射后，都会调用 `tlbinit()`：
  
  ```c
  void tlbinit(void) {
    asm volatile("invtlb 0x0,$zero,$zero");
    w_csr_stlbps(0xcU);
    w_csr_asid(0x0U);
    w_csr_tlbrehi(0xcU);
  }
  ```
- **根因分析**：
  1. `tlbinit()` 会无条件将 `CSR_ASID` 重置为 `0x0U` 并执行 `invtlb 0x0` 全刷 TLB；
  2. 该文件第 303 行已经实现了单页精确刷新函数 `invtlb_va(uint64 va)`（`invtlb 0x5, $zero, %0`）。在 `vmfault()` 中调用 `tlbinit()` 不仅在大规模 `sbrk` 懒分配（如 `usertests` 的 `sbrkmuch` 25,600 页缺页）时造成 25,600 次全量 TLB 清空和多余的 CSR 写操作，而且一旦后续按架构设计启用非零进程 `ASID`，`vmfault()` 调用 `tlbinit()` 会**把当前运行进程的 `CSR_ASID` 静默篡改为 0**，立刻引发严重的地址空间错乱与权限泄漏！
- **潜在影响**：性能退化（`sbrkmuch` 25,600 次全刷 TLB），且埋下启用 ASID 后的致命地址空间篡改隐患。
- **修复建议**：
  将 `src/kernel/arch/loongarch/vm.c:230` 的 `tlbinit();` 替换为 `invtlb_va(va);`。

---

#### [AUD-010] 双架构 `uvmclear()` 清除用户栈保护页 `PTE_U` / `PTE_PLV` 后未执行单页 TLB 刷新

- **严重级别**：Medium
- **当前状态**：未修复
- **发现 Commit**：`07d59f3`
- **涉及文件与位置**：`src/kernel/vm.c:334-342`、`src/kernel/arch/loongarch/vm.c:303-311`
- **涉及架构 / 模块**：双架构通用 / 页表权限变更与 TLB 一致性
- **触发路径与现象**：
  `uvmclear(pagetable_t pagetable, uint64 va)` 将指定虚拟页的 `*pte &= ~PTE_U`（RISC-V）或 `*pte &= ~PTE_PLV`（LoongArch64），用于 `kexec()` 设置用户栈下方的 Guard Page。
- **根因分析**：
  修改有效 PTE 的权限位（从用户可访问降权为仅内核可访问）后，`uvmclear()` 未调用 `sfence_vma_va(va)` 或 `invtlb_va(va)`。虽然在当前 `kexec()` 中，`pagetable` 是新建的且返回用户态时会切换 `satp`/`pgdl` 刷 TLB，但作为通用 VM 原语，若在当前活跃页表上调用 `uvmclear()`，硬件或 QEMU TLB 可能继续缓存带有 `PTE_U`/`PTE_PLV` 的旧条目。
- **潜在影响**：在活跃页表上调用 `uvmclear()` 会导致栈保护页失效。
- **修复建议**：
  在 `src/kernel/vm.c:341` 后添加 `sfence_vma_va(va);`，在 `src/kernel/arch/loongarch/vm.c:310` 后添加 `invtlb_va(va);`。

---

#### [AUD-027] LoongArch64 `devintr()` 在每个 10ms 看门狗子周期均返回 `ret = 2`，导致进程抢占频率比设计值高 10 倍（100Hz vs 10Hz）且对只写寄存器 `CSR.TICLR` 执行冗余读取

- **严重级别**：Medium
- **当前状态**：未修复
- **发现 Commit**：`07d59f3`
- **涉及文件与位置**：`src/kernel/arch/loongarch/trap.c:29-32, 110-111, 161-162, 232-245`（`devintr`、`usertrap`、`kerneltrap`）、`src/kernel/arch/loongarch/loongarch.h:302-316`（`r_csr_ticlr`、`w_csr_ticlr`）
- **涉及架构 / 模块**：LoongArch64（`ARCH=loongarch`）/ 时钟中断与调度抢占
- **触发路径与现象**：
  1. 为给 `virtio_disk_watchdog()` 提供 10ms 级别的丢中断轮询兜底，LoongArch64 将硬件定时器周期设为 `LA_TIMER_PERIOD_10MS = 1000000UL`（约 10ms），并在 `devintr()`（`src/kernel/arch/loongarch/trap.c:233-237`）中通过 `if(++timer_subticks >= LA_TIMER_SUBTICKS)` 每 10 个子周期（100ms）才调用一次 `clockintr()` 更新系统 `ticks`。
  2. 然而在 L244，只要触发了 `TI_VEC`（每 10ms 一次），`devintr()` 就无条件设置 `ret = 2;`。
  3. `usertrap()`（L110）和 `kerneltrap()`（L161）只要看到 `which_dev == 2` 就立即调用 `yield()`。这导致 LoongArch64 下所有 CPU 核心每 **10ms** 就会强制抢占当前运行进程（100Hz 抢占），时间片长度被缩短为 RISC-V（100ms / 10Hz）的 1/10。
  4. 此外，L243 执行 `w_csr_ticlr(r_csr_ticlr() | CSR_TICLR_CLR);`；根据《龙芯架构参考手册 卷一》第 7.5.8 节，`CSR.TICLR`（`0x44`）的第 0 位 `CLR` 为 **W1（写 1 清零、读出恒为 0）** 位，先调用 `r_csr_ticlr()` 执行 `csrrd` 纯属冗余。
- **根因分析**：
  将 10ms 的 VirtIO 看门狗轮询与 100ms 的系统调度时钟节拍耦合在同一个 `TI_VEC` 分支中，但未区分“仅执行看门狗的子周期（应返回 `1`）”与“满 100ms 的调度节拍（应返回 `2` 以触发 `yield()`）”。
- **潜在影响**：
  在 LoongArch64 SMP 或高负载测试（如 `usertests`）下，10 倍频率的 `yield()` 抢占显著放大了 `proc[].lock` 自旋锁竞争与上下文切换开销。
- **具体修复建议**：
  1. 使用 per-CPU 子计数器 `timer_subticks[NCPU]`，仅在满 10 个子周期（100ms）时令 `devintr()` 返回 `2`，其余 9 个 10ms 子周期仅在主核执行 `virtio_disk_watchdog()` 并返回 `1`。
  2. 将 L243 简化为 `w_csr_ticlr(CSR_TICLR_CLR);`。

---

#### [AUD-028] LoongArch64 `extioi_init()` 仅写入 `EXTIOI_ROUTE_BASE` 首 64 位寄存器（IRQ 0..7），漏配 `VIRTIO0_IRQ`（IRQ 17）的路由字节

- **严重级别**：Medium
- **当前状态**：未修复
- **发现 Commit**：`07d59f3`
- **涉及文件与位置**：`src/kernel/arch/loongarch/extioi.c:129-139`（`extioi_init`）、`src/kernel/memlayout.h:7-8`（`UART0_IRQ = 2`, `VIRTIO0_IRQ = 17`）
- **涉及架构 / 模块**：LoongArch64（`ARCH=loongarch`）/ EXTIOI 扩展 I/O 中断控制器驱动
- **触发路径与现象**：
  在 `extioi_init()`（`src/kernel/arch/loongarch/extioi.c:129-139`）中：
  
  ```c
  iocsr_writeq((0x1UL << UART0_IRQ) | (0x1UL << VIRTIO0_IRQ),
               LOONGARCH_IOCSR_EXTIOI_EN_BASE);
  iocsr_writeq(0x01UL, LOONGARCH_IOCSR_EXTIOI_MAP_BASE);
  iocsr_writeq(0x10000UL, LOONGARCH_IOCSR_EXTIOI_ROUTE_BASE);
  iocsr_writeq(0x1, LOONGARCH_IOCSR_EXRIOI_NODETYPE_BASE);
  ```
  
  在 LoongArch EXTIOI 控制器中，`LOONGARCH_IOCSR_EXTIOI_ROUTE_BASE`（`0x1c00..0x1cff`）按 **每个中断号 1 字节（8 bit）** 编址路由寄存器。向 `0x1c00` 写入 64 位值 `0x10000UL` 仅设置了 `0x1c00..0x1c07`（对应 **IRQ 0..7**，其中第 2 字节 `0x1c02` 对应 `UART0_IRQ = 2`），而 **`VIRTIO0_IRQ = 17` 位于 `0x1c11`（即 `LOONGARCH_IOCSR_EXTIOI_ROUTE_BASE + 0x10` 的第 1 字节）**，在初始化过程中完全未被写入，保持复位值 `0x00`。
- **根因分析**：
  代码从早期低中断号设备迁移到 PCI INTx（`VIRTIO0_IRQ = 17`）时，仅更新了 `EXTIOI_EN_BASE` 的 64 位位图掩码，却忽略了 `EXTIOI_ROUTE_BASE` 是每 IRQ 占用 1 字节（每个 64 位 IOCSR 写操作仅覆盖 8 个 IRQ）。
- **潜在影响**：
  QEMU 的 `loongarch_extioi` 在路由字节为 `0x00` 时恰好回退到 Core 0；但在遵循硬件规范的模拟器或真实龙芯硬件上，核心掩码为 `0` 会导致 `VIRTIO0_IRQ` 无法路由至任何核心。此外宏名 `LOONGARCH_IOCSR_EXRIOI_NODETYPE_BASE` 存在拼写笔误（`EXRIOI` -> `EXTIOI`）。
- **具体修复建议**：
  在 `extioi_init()` 中显式为 `VIRTIO0_IRQ = 17` 写入对应的 64 位路由寄存器：
  
  ```c
  iocsr_writeq(0x1UL << ((UART0_IRQ % 8) * 8),
               LOONGARCH_IOCSR_EXTIOI_ROUTE_BASE + (UART0_IRQ & ~7UL));
  iocsr_writeq(0x1UL << ((VIRTIO0_IRQ % 8) * 8),
               LOONGARCH_IOCSR_EXTIOI_ROUTE_BASE + (VIRTIO0_IRQ & ~7UL));
  ```

---

#### [AUD-033] LoongArch64 `_entry` 缺少 `CPUID < NCPU` 边界检查，且机器错误异常入口 `handle_merr` 未切换内核栈与内核页表即直接调用 C 函数 `machine_trap()`

- **严重级别**：High
- **当前状态**：未修复
- **发现 Commit**：`07d59f3`
- **涉及文件与位置**：`src/kernel/arch/loongarch/entry.S:22-30`（`_entry`）、`src/kernel/arch/loongarch/merror.S:1-7`（`handle_merr`）、`src/kernel/arch/loongarch/trap.c:168-172`（`machine_trap`）
- **涉及架构 / 模块**：LoongArch64（`ARCH=loongarch`）/ 启动汇编与机器错误（Machine Error）异常处理
- **触发路径与现象**：
  1. 在 `src/kernel/arch/loongarch/entry.S:22-30` 中：
     
     ```assembly
     la          $sp, stack0
     li.d        $a0, 1024*4
     csrrd       $a1, LOONGARCH_CSR_CPUID
     andi        $a1, $a1, 0x1ff
     addi.d      $tp, $a1, 0
     addi.d      $a1, $a1, 1
     mul.d       $a0, $a0, $a1
     add.d       $sp, $sp, $a0
     bl          main
     ```
     
     `_entry` 提取 9 位 `CPUID`（`0..511`）存入 `$tp` 并计算 `$sp = stack0 + (cpuid + 1) * 4096`，但未检查 `$a1 < NCPU`（`8`）。若任意被唤醒核心的 `CPUID >= 8`，其启动栈指针 `$sp` 将直接越过 `stack0[4096 * NCPU]` 踩踏 `.bss` 段后续全局变量，并在后续 `mycpu()`（`&cpus[tp]`）中触发越界写。
  2. 在 `src/kernel/arch/loongarch/merror.S:1-7` 中：
     
     ```assembly
     handle_merr:
         bl machine_trap
         ertn
     ```
     
     当 CPU 在 **用户态（`PLV=3`）** 执行期间触发机器错误异常（进入 `CSR.MERRENTRY`）时，硬件仅将特权级切换为 `PLV=0`，但 **`$sp` 仍为用户态栈指针，`CSR.PGDL` 仍为用户页表**。`handle_merr` 未切换内核栈或内核页表便直接执行 `bl machine_trap` -> `panic("machine error")` -> `printf()`，导致内核在用户态 `$sp` 上压栈运行（若用户 `$sp` 非法或未映射则直接引发二次 TLB/页异常死循环）。
- **根因分析**：
  1. LoongArch64 启动汇编与 RISC-V `entry.S`（见 `AUD-007`）存在同源的 `CPUID >= NCPU` 越界缺陷；
  2. `merror.S` 仅编写了最小桩代码，未考虑从 `PLV=3` 触发 Machine Error 时的 `$sp` / `$tp` / `PGDL` 上下文切换。
- **潜在影响**：
  当核号超出 `NCPU` 时破坏内核 `.bss` 数据段；当用户态下发生机器错误异常时，内核无法安全输出 `panic("machine error")` 诊断信息即发生双重异常崩溃。
- **具体修复建议**：
  1. 在 `src/kernel/arch/loongarch/entry.S:26` 后增加 `li.d $t0, NCPU; bgeu $a1, $t0, spin` 边界拦截；
  2. 在 `handle_merr` 中检查 `CSR.MERRCTL` 的 `PPLV`（或通过 `stack0` 设置紧急内核异常栈并恢复 `kernel_pagetable`）后再调用 `machine_trap`。

---

#### [AUD-042] RISC-V PLIC 中断控制器驱动 `src/kernel/plic.c` 访问 MMIO 寄存器未添加 `volatile` 限定符

- **严重级别**：Low
- **当前状态**：未修复
- **发现 Commit**：`07d59f3`
- **涉及文件与位置**：`src/kernel/plic.c:12-47`（`plicinit`、`plicinithart`、`plic_claim`、`plic_complete`）
- **涉及架构 / 模块**：RISC-V 64（`ARCH=riscv`）/ PLIC 平台级中断控制器驱动
- **触发路径与现象**：
  在 `src/kernel/plic.c` 的全部 6 处 MMIO 寄存器读写中：
  
  ```c
  *(uint32*)(PLIC + UART0_IRQ*4) = 1;
  *(uint32*)(PLIC + VIRTIO0_IRQ*4) = 1;
  *(uint32*)PLIC_SENABLE(hart) = (1 << UART0_IRQ) | (1 << VIRTIO0_IRQ);
  *(uint32*)PLIC_SPRIORITY(hart) = 0;
  int irq = *(uint32*)PLIC_SCLAIM(hart);
  *(uint32*)PLIC_SCLAIM(hart) = irq;
  ```
  
  所有指针强转均使用了普通 `*(uint32*)` 而非 `*(volatile uint32*)`。
- **根因分析**：
  对比内核中 `uart.c`（`volatile unsigned char *`）、`virtio_disk.c`（`volatile uint32 *`）、`hal.c`（`volatile uint32 *`）及 LoongArch64 的 `apic.c`（`volatile uint64 *`），唯独 `plic.c` 遗漏了 `volatile` 关键字。在启用更高编译优化等级（如 `-O2`/`-O3` 或跨文件 LTO 内联 `plic_claim`/`plic_complete`）时，编译器有权认为对同一地址 `PLIC_SCLAIM(hart)` 的读后写或重复访问是普通内存操作并予以重排或消除。
- **潜在影响**：
  在 LTO 或高优化编译下可能导致 PLIC 中断认领（claim）与完成（complete）MMIO 读写被优化重排，引发外设中断挂起。
- **具体修复建议**：
  将 `src/kernel/plic.c` 中的 `*(uint32*)` 统一改为 `*(volatile uint32*)`。

---

#### [AUD-046] RISC-V VirtIO MMIO v2 驱动未通过 `FEATURES_SEL` 协商高 32 位 `VIRTIO_F_VERSION_1` 且未白名单过滤未知设备特性，LoongArch64 `virtio_pci` 解析 Capability 未跳过 I/O BAR 且未对齐掩码

- **严重级别**：Medium
- **当前状态**：未修复
- **发现 Commit**：`07d59f3`
- **涉及文件与位置**：
  - `src/kernel/virtio_disk.c:70-88, 220-264, 474, 514-520`
  - `src/kernel/virtio.h:16-17, 79-83`
  - `src/kernel/arch/loongarch/virtio_pci.c:112-209`
- **涉及架构 / 模块**：双架构（RISC-V VirtIO MMIO v2 与 LoongArch64 VirtIO PCI Modern）/ 块设备驱动
- **触发路径与现象**：
  1. **RISC-V VirtIO MMIO v2 未通过 `*_FEATURES_SEL` 读写高 32 位特性且采用黑名单放行未知特性（`src/kernel/virtio_disk.c:70-88, 244-255`）**：
     - `virtio_disk_init()` 在 L221 强制要求 Modern VirtIO MMIO v2（`*R(VIRTIO_MMIO_VERSION) == 2`，且 `src/Makefile:315` 显式传入 `-global virtio-mmio.force-legacy=false`）。
     - 按照 VirtIO v1.1 规范第 4.2.2 节，MMIO v2 的 `DEVICE_FEATURES (0x010)` 与 `DRIVER_FEATURES (0x020)` 为 32 位窗口寄存器，必须先写 `DEVICE_FEATURES_SEL (0x014)` / `DRIVER_FEATURES_SEL (0x024)` 选择低 32 位（`SEL=0`）或高 32 位（`SEL=1`，含 bit 32 `VIRTIO_F_VERSION_1`）。
     - 然而 RISC-V 分支的 `virtio_get_features()` 与 `virtio_set_features()` 完全未访问 `*_FEATURES_SEL`，仅读写了低 32 位寄存器（从未在 MMIO v2 上确认 bit 32 `VIRTIO_F_VERSION_1`）；且 L245-251 在 RISC-V 上采用黑名单剔除 7 个已知位（`features &= ~(1 << ...)`），保留并确认了设备通告的所有其他未知低 32 位特性（对比 LoongArch64 在 L253 执行了白名单过滤 `features &= (1ULL << VIRTIO_F_VERSION_1)`）。
  2. **LoongArch64 `virtio_pci.c` 的 BAR 探测与 Capability 链表解析缺陷（`src/kernel/arch/loongarch/virtio_pci.c:112-209`）**：
     - `alloc_bars()`（L119, L137）在向 BAR 寄存器写入 `0xffffffff` 探测大小前，未先清除 `PCI_COMMAND` 中的 `PCI_COMMAND_IO | PCI_COMMAND_MEMORY` 解码位；
     - `parse_caps()`（L184-205）未按 PCI 规范屏蔽 Capability 指针低 2 位（`cap &= ~0x3`），亦无循环上限防止损坏配置空间死循环；且遇到 `VIRTIO_PCI_CAP_COMMON_CFG` / `NOTIFY_CFG` / `ISR_CFG` 时，未先检查该 Capability 指向的 `bar` 是否为已映射的 MMIO BAR（`bar < 6 && bar_addr[bar] != 0`），而是直接调用 `cap_addr(bar, offset)`。若过渡型（Transitional）VirtIO PCI 设备在 Capability 链表中先列出基于 I/O BAR（在 `alloc_bars` L125 被跳过，`bar_addr[bar] == 0`）的 Capability，内核将在 L177 直接触发 `panic("virtio pci cap bar")`。
  3. **`virtio_disk_pending()` 无锁读取 `disk.used_idx` 且 `struct virtq_used` 缺少 `volatile` 修饰（`src/kernel/virtio_disk.c:514-520`、`src/kernel/virtio.h:79-83`）**：
     `virtio_disk_pending()` 在未持有 `disk.vdisk_lock` 的情况下读取 `disk.used_idx` 并与 DMA 更新的 `disk.used->idx` 比较；同时 `struct virtq_used` 的 `idx` 与 `ring[]` 未声明 `volatile`。
- **根因分析**：
  RISC-V 端沿用了早期 xv6 针对单窗口特性的简化 MMIO 写法，未随 `virtio-mmio.force-legacy=false` 补齐 `FEATURES_SEL` 双字协商与特性白名单；LoongArch64 PCI 驱动仅按当前 QEMU 默认参数（`disable-legacy=on`）的路径编写了 `parse_caps()`。
- **潜在影响**：
  当升级 QEMU 版本新增默认 VirtIO 块设备特性位，或在未开启 `disable-legacy=on` 的 VirtIO PCI 配置下运行时，会导致特性协商失配或启动期 `panic("virtio pci cap bar")`。
- **具体修复建议**：
  1. 在 `src/kernel/virtio.h` 中补充 `VIRTIO_MMIO_DEVICE_FEATURES_SEL (0x014)` 与 `VIRTIO_MMIO_DRIVER_FEATURES_SEL (0x024)`，使 RISC-V `virtio_get_features()` / `virtio_set_features()` 分 `SEL=0/1` 读写完整 64 位特性，并在双架构统一使用白名单 `features &= (1ULL << VIRTIO_F_VERSION_1);`；
  2. 在 `virtio_pci.c:alloc_bars()` 入口先关闭 `PCI_COMMAND` 内存/IO 解码，在 `parse_caps()` 中对 `cap` 指针做 `cap &= ~0x3`、添加最大迭代步数（如 48 次），并在 `bar >= 6 || bar_addr[bar] == 0` 时 `continue` 跳过非 MMIO Capability；
  3. 为 `disk.used` 指向的 `idx` 访问增加 `volatile` 限定或原子加载。

---

#### [AUD-057] RISC-V 机器态陷阱入口 `ipi_mvec`（`mtvec`）未校验 `mcause` 是否为机器软件中断，一旦发生不可委托的 M-mode 同步异常将原地死循环执行 `mret`

- **严重级别**：Medium
- **当前状态**：未修复
- **发现 Commit**：`07d59f3`
- **涉及文件与位置**：`src/kernel/kernelvec.S:69-85`（`ipi_mvec`）、`src/kernel/start.c:33-35, 46-50`（`start`）
- **涉及架构 / 模块**：RISC-V 64（`ARCH=riscv`）/ M-mode 陷阱向量与核间 IPI 转发
- **触发路径与现象**：
  在 `src/kernel/start.c:48` 中，每个 RISC-V 核心在进入 S-mode 前将机器态陷阱基址寄存器 `mtvec` 设置为 `ipi_mvec`（Direct 模式）：
  
  ```assembly
  ipi_mvec:
          csrrw a0, mscratch, a0
          sd a1, 0(a0)
          sd a2, 8(a0)
          csrr a1, mhartid
          slli a1, a1, 2
          li a2, 0x02000000
          add a2, a2, a1
          sw zero, 0(a2)
          li a1, 2
          csrs mip, a1
          ld a2, 8(a0)
          ld a1, 0(a0)
          csrrw a0, mscratch, a0
          mret
  ```
  
  `ipi_mvec` 假定任何进入 `mtvec` 的陷阱都必然是 M-mode 软件中断（`mcause == 0x8000000000000003`），全程未读取 `mcause` 进行校验，也不推进 `mepc`。
- **根因分析**：
  在 RISC-V 特权架构下，`w_medeleg(0xffff)` 仅能将编号 `0..15` 中允许委托的异常委托给 S-mode；而对于硬件不允许委托至 S-mode 的异常（如 S-mode 执行 `ecall` 触发的 `mcause = 9` 若硬件未委托、M-mode 自身异常、PMP/物理总线访问故障或编号 `>= 16` 的平台异常），CPU 会统一跳转到 `mtvec`（即 `ipi_mvec`）。由于 `ipi_mvec` 不检查 `mcause` 直接执行 `mret` 返回故障指令 `mepc`，CPU 会陷入无任何日志输出的 M-mode 无限异常风暴，同时不断向 `mip.SSIP` 注入虚假的 S-mode 软件中断。
- **潜在影响**：
  一旦发生任何陷入 M-mode 的同步异常或硬件访问错误，系统无法报错停机，而是静默卡死在 M-mode `mret` 死循环中，给调试带来极大困难。
- **具体修复建议**：
  在 `ipi_mvec` 中读取 `csrr a1, mcause`，检查最高位（中断标志）及低位原因码是否为 `3`（Machine Software Interrupt）；若非 M-mode 软件中断，应跳转到错误停机分支（或将 `mtval`/`mcause`/`mepc` 输出至 UART 后死循环停机）。

### 4. 文件系统与 VFS 语义/持久化缺陷

#### [AUD-011] `fat32_iget()` 对空文件合成的 `inum`（`FAT32_EMPTY_INO_BIT | (entry_off >> 5)`）在首次写入分配首簇后未同步更新 `ip->inum`，导致同一文件在 `itable` 中产生双 Inode 分裂（Cache Aliasing）

- **严重级别**：High
- **当前状态**：未修复
- **发现 Commit**：`07d59f3`
- **涉及文件与位置**：
  - `src/kernel/fat32.c:453-471`（`fat32_iget`）
  - `src/kernel/fat32.c:1083-1091, 1104-1107`（`fat32_writei`）
  - `src/kernel/fat32.c:1238-1244`（`fat32_create`）
- **涉及架构 / 模块**：双架构通用 / FAT32 文件系统与 `itable` 缓存一致性
- **触发路径与现象**：
  1. 当打开一个磁盘上已存在的 0 字节 FAT32 空文件（`first_cluster == 0`），或者对一个文件执行导致 `clus == 0` 的路径时，`fat32_iget()`（`src/kernel/fat32.c:460-463`）为其分配合成 inode 号：
     
     ```c
     inum = FAT32_EMPTY_INO_BIT | (entry_off >> 5);
     ```
     
     并在 `itable` 中登记 `ip1->inum = 0x80000000 | (entry_off >> 5)`，`ip1->addrs[0] = 0`。
  2. 随后进程 A 保持该文件 FD 打开，并向该 FD 写入数据。`fat32_writei()`（`src/kernel/fat32.c:1083-1090`）为其分配首簇 `clus`（例如 `clus = 5`），设置 `ip1->addrs[0] = 5`，并调用 `fat32_sync_inode(ip1)` 将目录项中的首簇号写回磁盘（此时磁盘目录项的 `first_cluster` 变为 `5`）。
  3. **但是 `ip1->inum` 依然停留在 `0x80000000 | (entry_off >> 5)`！**
  4. 此时进程 B（或同一进程再次调用 `open` / `stat` / `unlink`）通过路径查找该文件：`fat32_lookup()` 从磁盘读到目录项的 `first_cluster == 5`，调用 `fat32_iget()`。此时 `clus == 5 != 0`，`fat32_iget()` 使用 `inum = 5` 调用 `iget(mnt->dev, 5)`！
  5. 因为 `itable` 中已打开的 `ip1->inum` 是 `0x80000000 | ...` 而非 `5`，`iget()` **不会命中 `ip1`，而是分配第二个独立的内存 inode `ip2`（`ip2->inum = 5`）**！
- **根因分析**：
  FAT32 的 `fat32_iget()` 用两种不同的命名空间生成 `ip->inum`：`clus == 0` 时用目录项偏移，而 `clus >= 2` 时用首簇号 `clus`。当一个 `clus == 0` 的打开文件在内存中获得首簇分配并同步到磁盘目录项后，磁盘上的标识变成了 `clus`，但内存中存活的 `ip1->inum` 仍是 `FAT32_EMPTY_INO_BIT | (entry_off >> 5)`。
- **潜在影响**：
  - 同一物理文件在内存中同时存在两个 `struct inode`（`ip1` 和 `ip2`），各自有独立的 `ip->lock`、`ip->size` 和簇链缓存 `ip->addrs[2..3]`；
  - 并发读写或先写 `ip1` 再通过路径 `unlink`（操作 `ip2`）时，`ip2->ref == 1 && ip2->nlink == 0` 会在 `iput(ip2)` 中**立即把刚刚分配给 `ip1` 的簇链全部释放（`fat32_free_chain`）**，而 `ip1` 仍被打开并继续向已释放的簇写入，造成严重的 FAT32 文件系统损坏与跨文件数据覆盖（UAF on disk clusters）！
- **修复建议**：
  有两套根治方案：
  - **方案 A（推荐）**：在 `fat32_writei()` 为 `ip->addrs[0] == 0` 的空文件成功分配首簇 `clus` 后（或统一在 `fat32_create` / `fat32_itrunc` 中），获取 `itable.lock` 将 `ip->inum = clus` 原子更新为新分配的首簇号；
  - **方案 B**：对于非根目录的所有 FAT32 普通文件和子目录，既然目录项偏移 `entry_off` 在文件生命周期内唯一且固定，或者统一禁止 `clus == 0` 状态落盘（例如 `fat32_itrunc` 保留首簇、`mkfat32` 预分配首簇，若遇到外部 0 字节文件在 `fat32_writei` 分配首簇时同步持有 `itable.lock` 更新 `ip->inum = clus`）。

---

#### [AUD-012] `vfs_mount()` 对挂载点 `mp` 的已挂载判定存在非根同号 Inode（如跨卷 `inum == 2`）误拒绝缺陷，且 `ext2_unlink`/`fat32_unlink` 先判空目录后判挂载点

- **严重级别**：High
- **当前状态**：未修复
- **发现 Commit**：`07d59f3`
- **涉及文件与位置**：`src/kernel/vfs.c:214-232`（`vfs_mount`）、`src/kernel/ext2.c:946-955`（`ext2_unlink`）、`src/kernel/fat32.c:1293-1302`（`fat32_unlink`）
- **涉及架构 / 模块**：双架构通用 / VFS 挂载拓扑检查与删除解耦
- **触发路径与现象**：
  在 `ext2_unlink()`（`src/kernel/ext2.c:946`）与 `fat32_unlink()`（`src/kernel/fat32.c:1293`）中，先调用了磁盘扫描函数 `ext2_is_dir_empty(ip)` / `fat32_is_dir_empty(ip)`，然后才在 L951 / L1298 检查 `if(vfs_mountpoint(ip) != 0)`，且文件系统后端直接反向依赖 VFS 层的 `vfs_mountpoint(ip)`。
- **根因分析**：
  对比 `sys_unlink()` 对 `FS_XV6` 的处理（`src/kernel/sysfile.c:227-238` 先检查 `vfs_mountpoint(ip) != 0`，再检查 `!isdirempty(ip)`），`ext2_unlink` 与 `fat32_unlink` 顺序颠倒且跨层回调 VFS。
- **潜在影响**：`ext2_unlink` / `fat32_unlink` 直接回调 `vfs_mountpoint(ip)` 违反 VFS 与具体文件系统后端的分层解耦，且在挂载点底层目录非空时先触发多余的底层磁盘 I/O。
- **修复建议**：
  将 `vfs_mountpoint()` 拦截统一上提至 `vfs_unlink()` 层（或至少在 `ext2_unlink` / `fat32_unlink` 中置于 `is_dir_empty` 磁盘遍历之前）。

---

#### [AUD-013] `filewrite()` 对非 `FS_XV6` 文件系统（`ext2` / `fat32`）仍按 xv6 日志事务上限分块（每次仅 3 KiB）并在每次循环中无条件调用 `begin_op()` / `end_op()` 和 `sync_inode()`

- **严重级别**：Medium

- **当前状态**：未修复

- **发现 Commit**：`07d59f3`

- **涉及文件与位置**：`src/kernel/file.c:148-174`（`filewrite`）、`src/kernel/ext2.c:1111-1114`（`ext2_writei`）、`src/kernel/fat32.c:1104-1107`（`fat32_writei`）

- **涉及架构 / 模块**：双架构通用 / VFS 与块文件写入性能及日志耦合

- **触发路径与现象**：
  在 `filewrite()`（`src/kernel/file.c:153-172`）中：
  
  ```c
  int max = ((MAXOPBLOCKS-1-1-2) / 2) * BSIZE; // 3 * 1024 = 3072 bytes
  int i = 0;
  while(i < n){
    int n1 = n - i;
    if(n1 > max)
      n1 = max;
  
    begin_op();
    ilock(f->ip);
    if ((r = vfs_writei(f->ip, 1, addr + i, f->off, n1)) > 0)
      f->off += r;
    iunlock(f->ip);
    end_op();
    ...
  }
  ```

- **根因分析**：
  
  1. `ext2` 和 `fat32` 根本不使用 xv6 的 `log.c` WAL 日志（它们直接通过 `bwrite()` 写 `EXT2DEV` / `FAT32DEV`），但 `filewrite()`（以及 `sys_open`、`sys_mkdir`、`sys_unlink`、`sys_link`、`sys_chdir`、`fileclose`）对所有文件系统无条件调用 `begin_op()` / `end_op()`。
  2. 当一个进程在根卷 `xv6fs` 上执行大量写操作触发 `log.c` `commit()` 时，另一个在 `/mnt`（ext2）或 `/fat`（FAT32）上写文件的进程会在 `begin_op()` 上被无辜阻塞！
  3. 此外，每次 `vfs_writei` 结束时，`ext2_writei` 和 `fat32_writei` 都会调用 `ext2_sync_inode` / `fat32_sync_inode` 读写磁盘 inode/目录项；3 KiB 的小分片导致写大文件时产生高频重复的元数据落盘与 `log.lock` 争用。

- **潜在影响**：跨文件系统操作被根卷 WAL 日志全局串行化阻塞，且放大了大文件写入的元数据 I/O 次数。

- **修复建议**：
  在重构或优化 `filewrite()` 时，仅当 `f->ip->fstype == FS_XV6` 时才限制 `max = ((MAXOPBLOCKS-1-1-2) / 2) * BSIZE` 并包裹 `begin_op()` / `end_op()`（或将 `begin_op`/`end_op` 下沉至 `xv6` 后端操作中）。

---

#### [AUD-014] `filewrite()` 在循环分片写入中途失败（如磁盘满 `r != n1`）时，已推进 `f->off` 并落盘部分数据，但丢弃已写入字节数一律返回 `-1`

- **严重级别**：Medium
- **当前状态**：未修复
- **发现 Commit**：`07d59f3`
- **涉及文件与位置**：`src/kernel/file.c:155-174`（`filewrite`）
- **涉及架构 / 模块**：双架构通用 / 文件系统写入语义
- **触发路径与现象**：
  当用户调用 `write(fd, buf, 8192)` 写入 8 KiB 数据，前两轮（6144 字节）成功写入并使 `f->off += 6144`，第三轮因磁盘满返回 `r = 0 < n1`。此时 `filewrite()` 跳出循环并在 L173 执行：
  
  ```c
  ret = (i == n ? n : -1);
  ```
- **根因分析**：
  `f->off` 和磁盘文件大小已经前进了 `i`（6144）字节，但 `filewrite()` 返回 `-1` 而非已成功写入的字节数 `i > 0 ? i : -1`。虽然 xv6 的 `usertests.c`（如 `fsfull` / `diskfull`）检查 `write() != expected_n` 即可通过，但返回 `-1` 会导致调用方无法获知 `f->off` 实际已前进的偏移量。
- **潜在影响**：短写（Short Write）后文件偏移量与返回值语义不一致。
- **修复建议**：
  核对 `usertests.c` 对满盘 `write()` 的断言；若 `usertests` 要求满盘返回 `-1` 则保持并在注释中注明 xv6 契约，而在 `ext2_writei`/`fat32_writei` 单次调用内确保错误传播一致。

---

#### [AUD-015] `ext2_create()` 与 `fat32_create()` 在目录项添加失败回滚时，未回滚父目录 `dp` 在 `ext2_dir_add` / `fat32_find_free_entry` 中可能已扩展的目录块或目录簇

- **严重级别**：Medium
- **当前状态**：未修复
- **发现 Commit**：`07d59f3`
- **涉及文件与位置**：`src/kernel/fat32.c:564-595, 611-626`（`fat32_find_free_entry` / `fat32_add_entry`）、`src/kernel/ext2.c:798-814`（`ext2_dir_add`）
- **涉及架构 / 模块**：双架构通用 / ext2 与 FAT32 目录扩展
- **触发路径与现象**：
  1. 在 `fat32_add_entry()` 中，当目录簇已满时，`fat32_find_free_entry()` 调用 `newclus = fat32_append_cluster(fs, clus)` 为父目录 `dp` 追加一个新簇；随后在 L625 调用 `fat32_write_entry(fs, *entry_off, &de)`。
  2. 在 `fat32_create()` 创建子目录（`type == T_DIR`）时，先在父目录 `dp` 中添加子目录条目（可能扩展了 `dp` 的簇链），随后在 L1224-1235 向新建的子目录簇写入 `.` 和 `..`；若此时写入失败，`fat32_create()` 调用 `fat32_remove_entry(fs, entry_off)` 将该槽位标为 `0xe5`。
- **根因分析**：
  `fat32_remove_entry()` 将新扩展簇的第一个槽位写成 `0xe5`（已删除标记）而非 `0x00`（目录结束标记），导致后续遍历该目录时不会在该槽位早停（`de.name[0] == 0x00`），而是每次都必须扫完整个新扩展的空簇。同理，`ext2_create()` 在 L903 `ext2_sync_inode(dp)` 失败时回滚删除了目录项，但 `dp->size` 在 `ext2_dir_add()` 中被增加的 `fs->block_size` 未回退。
- **潜在影响**：异常失败路径下父目录残留空扩展块或退化扫描性能。
- **修复建议**：
  在 `ext2_create()` L904 回滚分支中若目录刚刚扩容则同步恢复，在 `fat32` 新分配目录簇首槽回滚时若后续全为 `0x00` 可直接写回 `0x00`。

---

#### [AUD-029] `ext2_readi()` 与 `fat32_readi()` 读取目录时将文件偏移 `f->off` 映射为“当前未删除项的动态序号”，导致边遍历目录边 `unlink()` 时漏删一半文件且复杂度退化为 $O(N^2)$

- **严重级别**：High
- **当前状态**：未修复
- **发现 Commit**：`07d59f3`
- **涉及文件与位置**：`src/kernel/ext2.c:1025-1057`（`ext2_readi`）、`src/kernel/fat32.c:990-1038`（`fat32_readi`）、`src/kernel/file.c:118-127`（`fileread`）
- **涉及架构 / 模块**：双架构通用 / `ext2` 与 `fat32` 目录读取（`T_DIR`）与游标推进语义
- **触发路径与现象**：
  1. 在 `ext2_readi()`（`src/kernel/ext2.c:1028-1051`）和 `fat32_readi()`（`src/kernel/fat32.c:993-1031`）中，当 `ip->type == T_DIR` 时，函数计算 `want = off / sizeof(xde)`，每次调用都从目录第 0 块/首簇从头扫描，仅对有效（未删除）目录项递增 `seen++`，当 `seen++ == want` 时拷贝 **1 个** `struct dirent`（16 字节）并 `return sizeof(xde);`，随后 `fileread()` 将 `f->off` 增加 `sizeof(xde)`（即下次 `want` 变为 `k + 1`）。
  2. **边 `read` 目录边 `unlink` 跳过半数文件**：当用户程序打开目录并通过循环 `while(read(fd, &de, sizeof(de)) == sizeof(de)) { unlink(de.name); }` 清理目录时，删除第 `k` 个有效目录项后，后续所有有效目录项在磁盘上的动态有效序号 `seen` 全部前移 1 位（原本的第 `k + 1` 项变成了新的第 `k` 项），但打开目录的 `f->off` 已推进到 `(k + 1) * sizeof(de)`（下次寻找 `want = k + 1`）。结果每一次 `unlink()` 都会直接跳过紧随其后的那一个有效文件，导致目录清理循环永远漏掉一半文件。
  3. **忽略 `n > sizeof(struct dirent)` 的批量读取请求**：若用户态一次传入 `read(dir_fd, buf, 512)` 请求读取多个 `struct dirent`，两个函数在拷贝第 1 个目录项后立即 `return sizeof(xde)`，无法按 `n` 批量填充。
  4. **16 位 `ushort inum` 截断风险**：`struct dirent` 的 `inum` 字段为 16 位 `ushort`，`ext2_readi()`（L1044 `xde.inum = de->inode;`）直接将 32 位 `de->inode` 截断为 `ushort` 且未对截断后等于 `0` 做非零兜底。
- **根因分析**：
  `ext2`（变长 `rec_len` 目录项）和 `fat32`（32 字节短目录项 + 变长 LFN 槽位）的磁盘物理目录偏移与 xv6 用户态定长 16 字节 `struct dirent` 不一致，实现者采用了无状态的“按有效项序号 `want = off / 16` 从头重扫”策略，破坏了目录流游标在交错 `unlink()` 下的单调性，同时使列举 $N$ 个文件的目录退化为 $\sum_{i=1}^N i = O(N^2)$ 次块读取。
- **潜在影响**：
  任何在 `ext2` 或 `fat32` 挂载目录下执行“遍历目录描述符并删除文件”的用户态程序都会遗漏一半文件；大目录下的 `ls` 会产生平方级磁盘 I/O 开销。
- **具体修复建议**：
  将目录流游标与真实的底层物理目录项字节偏移关联（或在 `ext2_readi()` / `fat32_readi()` 中支持基于物理偏移推进并循环填充直至满足 `n` 字节）。

---

#### [AUD-030] `fat32_create()` 与 `fat32_itrunc()` 为 0 字节空文件分配并保留非零首簇，违反 Microsoft FAT32 规范并导致 `fat32_writei()` 空文件分支成为死代码

- **严重级别**：Medium
- **当前状态**：未修复
- **发现 Commit**：`07d59f3`
- **涉及文件与位置**：`src/kernel/fat32.c:1141-1153`（`fat32_itrunc`）、`src/kernel/fat32.c:1203-1211, 1238-1243`（`fat32_create`）、`src/kernel/fat32.c:1083-1090`（`fat32_writei`）
- **涉及架构 / 模块**：双架构通用 / FAT32 文件系统磁盘格式兼容性与空间管理
- **触发路径与现象**：
  1. 在 `fat32_create()`（`src/kernel/fat32.c:1203-1208`）中，创建普通文件（`type == T_FILE`）时无条件调用 `clus = fat32_alloc_cluster(fs);`，并将 `clus >= 2` 与 `size = 0` 一同写入磁盘 8.3 目录项。
  2. 在 `fat32_itrunc()`（`src/kernel/fat32.c:1143-1152`）中，当对未删除文件执行 `O_TRUNC` 截断（`ip->nlink > 0`）时，仅调用 `fat32_free_after(fs, ip->addrs[0])` 释放后续簇并清零首簇内容，仍将 `ip->addrs[0] >= 2` 与 `de.size = 0` 写回磁盘目录项。
  3. 根据 Microsoft FAT32 文件系统规范（以及 Linux `fsck.vfat` / `dosfsck` 校验规则），**任何 `DIR_FileSize == 0` 的普通文件，其目录项中的首簇号（`DIR_FstClusHI:DIR_FstClusLO`）必须为 `0`**；若 `size == 0` 但首簇号非 0，`fsck.vfat` 会报告簇链长度与文件大小不一致的损坏错误。
  4. 同时，这导致每创建一个 0 字节空文件都白白消耗 1 个数据簇，且令 `fat32_writei()` L1083 专门编写的 `if(ip->addrs[0] == 0)` 空文件首簇延迟分配逻辑对本系统创建或截断的文件永远不可达（仅对外部镜像预置的 0 字节文件触发，而触发时又会命中 `AUD-011` 的 `inum` 分裂缺陷）。
- **根因分析**：
  为规避 `AUD-011`（0 字节文件无首簇号时使用合成 `inum` 在首次写入分配簇后发生 `inum` 变化），`fat32_create()` 和 `fat32_itrunc()` 采取了“即使文件大小为 0 也强行挂一个数据簇”的权宜做法，破坏了 FAT32 标准磁盘布局。
- **潜在影响**：
  由内核修改后的 FAT32 镜像在宿主机运行 `fsck.vfat` 时只要包含新建或 `O_TRUNC` 过的 0 字节文件就会报错；磁盘剩余空间不足 1 个簇时无法创建 0 字节空文件。
- **具体修复建议**：
  统一 FAT32 普通文件的 `inum` 编码规则（例如始终以目录项物理偏移 `entry_off >> 5` 作为普通文件的 `ip->inum`，仅目录使用首簇号），从而允许 0 字节文件按 FAT32 规范保持 `first_cluster == 0`，并在 `fat32_itrunc()` 中当 `ip->nlink > 0` 时调用 `fat32_free_chain(fs, ip->addrs[0])` 将 `ip->addrs[0]` 彻底置 0。

---

#### [AUD-040] `virtio_disk_rw()` 缺失逻辑分区上界检查且 `ext2_bmap()` / `ext2_free_indirect()` 未校验磁盘块号合法范围，损坏或恶意镜像块指针可跨分区覆写 `FAT32DEV` 或 `ext2` 元数据

- **严重级别**：High
- **当前状态**：未修复
- **发现 Commit**：`07d59f3`
- **涉及文件与位置**：`src/kernel/virtio_disk.c:373-388`（`virtio_disk_rw`）、`src/kernel/ext2.c:125-145, 237-256, 515-623, 1064-1075, 1100-1107, 1131-1174`（`ext2_bmap`、`ext2_bmap_double`、`ext2_free_indirect`、`ext2_free_double_indirect`）
- **涉及架构 / 模块**：双架构通用 / VirtIO 块设备多分区隔离与 `ext2` 块映射校验
- **触发路径与现象**：
  1. **`virtio_disk_rw()` 缺少分区容量上界拦截（`src/kernel/virtio_disk.c:375-387`）**：
     系统将三个文件系统打包在同一块 VirtIO 磁盘上：`ROOTDEV`（`[0, FSSIZE)`，共 2000 块）、`EXT2DEV`（`[FSSIZE, FSSIZE + EXT2SIZE)`，共 8192 块）、`FAT32DEV`（`[FAT32START, ...)`，其中 `FAT32START = FSSIZE + EXT2SIZE = 10192`）。然而 `virtio_disk_rw()` 仅做起始偏移累加：
     
     ```c
     if(b->dev == ROOTDEV){
       // Root xv6 volume starts at block 0.
     } else if(b->dev == EXT2DEV){
       blockno += FSSIZE;
     } else if(b->dev == FAT32DEV){
       blockno += FAT32START;
     }
     ```
     
     完全没有检查 `ROOTDEV` 的 `b->blockno < FSSIZE` 或 `EXT2DEV` 的 `b->blockno < EXT2SIZE`！一旦 `ROOTDEV` 访问 `blockno >= 2000` 就会直接读写 `EXT2DEV` 分区；一旦 `EXT2DEV` 访问 `blockno >= 8192` 就会直接读写 `FAT32DEV` 分区！
  2. **`ext2_bmap()` / `ext2_bmap_double()` / `ext2_free_indirect()` 未校验磁盘块指针范围**：
     虽然 `ext2_free_block()`（`src/kernel/ext2.c:413`）检查了 `if(blockno < fs->sb.s_first_data_block || blockno >= fs->sb.s_blocks_count) return;`，但在 `ext2_bmap()`（L584-622）、`ext2_bmap_double()`（L537-574）、`ext2_free_indirect()`（L1139 `bp = bread(fs->dev, ind_bno);`）和 `ext2_free_double_indirect()`（L1162 `bp = bread(fs->dev, outer_bno);`）中，从磁盘 `di.i_block[]` 或一/二级间接块读出的 32 位块号 `bno` / `inner` **从未校验是否落在合法数据块区间 `[first_data_block, s_blocks_count)` 内**！
  3. 若 `ext2` 镜像中某个文件的直接或间接块指针包含越界值（如 `bno >= 8192`），`ext2_readi()` / `ext2_writei()` / `ext2_itrunc()` 调用 `bread(fs->dev, bno)` 时将直接穿透到 `FAT32DEV` 分区读写（或越过磁盘末尾触发 `panic("virtio_disk_intr status")`）；若 `bno` 指向 `1`（超级块）、`2`（块组描述符）或位图/inode 表块，`ext2_writei()` 将直接覆写破坏 `ext2` 核心元数据！
- **根因分析**：
  `ext2_mount()` 虽校验了超级块和块组描述符本身，但在文件读写与间接块遍历时无条件信任了磁盘上的块指针，且底层 `virtio_disk_rw()` 未强制实施逻辑分区边界隔离。
- **潜在影响**：
  损坏或特制的 `ext2` 镜像可通过越界块指针静默篡改相邻的 `FAT32DEV` 文件系统分区，或触发 VirtIO 越界 I/O 内核崩溃（DoS）。
- **具体修复建议**：
  1. 在 `virtio_disk_rw()` 中对 `ROOTDEV`（`b->blockno >= FSSIZE`）、`EXT2DEV`（`b->blockno >= EXT2SIZE`）增加分区上界检查；
  2. 在 `ext2.c` 中封装 `ext2_valid_datablock(fs, bno)`（校验 `bno >= first_data_block && bno < fs->sb.s_blocks_count` 且不属于超级块、块组描述符、位图与 inode 表块），在 `ext2_bmap()`、`ext2_bmap_double()`、`ext2_free_indirect()`、`ext2_free_double_indirect()` 中对任何非零块号执行校验。

---

#### [AUD-041] `fat32_read_fat()`、簇链定位 `fat32_cluster_at()` 与目录遍历循环未拦截损坏/坏簇表项（`0`、`1`、`0x0ffffff7`、`> nclus + 1`），可触发偏移下溢覆写保留扇区或磁盘越界 Panic

- **严重级别**：High
- **当前状态**：未修复
- **发现 Commit**：`07d59f3`
- **涉及文件与位置**：`src/kernel/fat32.c:135-139, 167-178, 282-302, 318-354, 538-560, 570-594, 918-966, 996-1036, 1253-1273`（`fat32_read_fat`、`fat32_cluster_at`、`fat32_locate_entry`、`fat32_readi`、`fat32_find_entry`、`fat32_find_free_entry`、`fat32_is_dir_empty`）
- **涉及架构 / 模块**：双架构通用 / FAT32 簇链解析与磁盘偏移计算
- **触发路径与现象**：
  1. `fat32_read_fat(struct fat32_fs *fs, uint clus, uint *val)`（`src/kernel/fat32.c:167-178`）仅校验输入参数 `clus >= 2 && clus <= fs->nclus + 1`，对从 FAT 表中读出的目标值 `*val = fat32_get32(buf) & FAT32_MASK` **不做任何合法性校验**。所有调用方仅通过 `fat32_is_eoc(next)` 判断是否 `>= 0x0ffffff8`。
  2. **`fat32_cluster_at()` 末次迭代返回非法簇号导致整数下溢或越界读写（L335-353）**：
     
     ```c
     while(cur < index){
       if(++hops > fs->nclus)
         return 0;
       if(fat32_read_fat(fs, clus, &next) < 0)
         return 0;
       if(fat32_is_eoc(next)){
         ...
       }
       clus = next;
       cur++;
     }
     ip->addrs[2] = cur;
     ip->addrs[3] = clus;
     return clus;
     ```
     
     当循环处于最后一步（`cur == index - 1`）时，若 FAT 表项损坏或包含非法值：
     - 若 `next == 1`（保留簇号）：`!fat32_is_eoc(1)` 为真，`clus = 1; cur++;` 后循环结束并**直接返回 `clus = 1`**！随后 `fat32_readi()`（L1052）或 `fat32_writei()`（L1095）调用 `fat32_clus_offset(fs, 1)`，其中 `(clus - 2)` 发生 32 位无符号下溢变为 `0xffffffffU`，计算出的磁盘偏移回绕到镜像前端，导致 `fat32_writei()` **直接覆写 FAT32 引导扇区（BPB）或 FAT 表**！
     - 若 `next > fs->nclus + 1 && next < 0x0ffffff8`（例如标准 FAT32 坏簇标记 `0x0ffffff7` 或越界簇号）：`fat32_cluster_at()` 直接返回该越界簇号，`fat32_clus_offset(fs, clus)` 访问超出 VirtIO 磁盘容量的扇区，直接触发 `panic("virtio_disk_intr status")`！
     - 若 `next == 0`（断裂的簇链指向空闲簇 0）：在 `fat32_writei()`（L1091-1093）中会因 `clus == 0` 中止；但在 `fat32_readi()`（L1049）中会被静默当成稀疏零簇返回全 0 数据而不报错。
  3. **全部 5 个目录遍历循环在校验前直接对越界 `next` 发起磁盘读（L539, L571, L919, L997, L1254）**：
     在 `fat32_find_entry()`、`fat32_find_free_entry()`、`fat32_locate_entry()`、`fat32_readi()`（目录分支）和 `fat32_is_dir_empty()` 中，外层循环条件均为 `while(clus >= 2)`（未检查 `clus <= fs->nclus + 1`）。当上一轮末尾读出 `next` 属于 `[fs->nclus + 2, 0x0ffffff7]`（如坏簇 `0x0ffffff7`）并赋给 `clus` 后，下一轮循环立即进入内层 `for(pos = 0; ...)` 调用 `fat32_clus_offset(fs, clus)` 和 `fat32_read_entry()` 读取越界扇区，触发 `panic("virtio_disk_intr status")`。
- **根因分析**：
  FAT32 簇链步进时只区分了 `EOC (>= 0x0ffffff8)` 与“其他所有值”，漏掉了对非 EOC 状态下 `next < 2 || next > fs->nclus + 1`（含空闲簇 `0`、保留簇 `1`、越界簇及坏簇 `0x0ffffff7`）的拦截。
- **潜在影响**：
  遇到含坏簇标记（`0x0ffffff7`）或断裂/越界 FAT 表项的 FAT32 镜像时，内核在读写文件或遍历目录时会触发 VirtIO 越界 Panic 或下溢覆写 FAT 表/BPB。
- **具体修复建议**：
  1. 在 `fat32_cluster_at()` 的 L347 之前，当 `!fat32_is_eoc(next)` 时，强制校验 `if(next < 2 || next > fs->nclus + 1) return 0;`；
  2. 在全部 5 个目录遍历循环中，将 `while(clus >= 2)` 收紧为 `while(clus >= 2 && clus <= fs->nclus + 1)`，并在 `fat32_find_free_entry()` 中若因非法 `next`（非 EOC）退出循环时直接返回 `-1` 而非调用 `fat32_append_cluster()`。

---

#### [AUD-043] `src/kernel/log.c` 的 `read_head()` 与 `install_trans()` 缺失磁盘日志头边界校验（可致内核 `.bss` 越界写与跨分区覆写），且 `log_write()` 在日志吸收（Log Absorption）前过早检查 `log.lh.n >= LOGBLOCKS` 导致满槽位重复写被误杀 Panic

- **严重级别**：High

- **当前状态**：未修复

- **发现 Commit**：`07d59f3`

- **涉及文件与位置**：
  
  - `src/kernel/log.c:67-98`（`install_trans`、`read_head`）
  - `src/kernel/log.c:216-236`（`log_write`）

- **涉及架构 / 模块**：双架构通用 / xv6 预写式日志（WAL `log.c`）

- **触发路径与现象**：
  
  1. **启动恢复期 `read_head()` 未校验 `lh->n` 范围导致内核 `.bss` 数组越界写（`src/kernel/log.c:88-98`）**：
     
     ```c
     static void
     read_head(void)
     {
       struct buf *buf = bread(log.dev, log.start);
       struct logheader *lh = (struct logheader *) (buf->data);
       int i;
       log.lh.n = lh->n;
       for (i = 0; i < log.lh.n; i++) {
         log.lh.block[i] = lh->block[i];
       }
       brelse(buf);
     }
     ```
     
     内核启动执行 `fsinit() -> initlog() -> recover_from_log() -> read_head()` 时，直接信任磁盘日志头中的有符号 32 位整数 `lh->n`，未检查 `lh->n < 0 || lh->n > LOGBLOCKS`（`LOGBLOCKS = 30`）。若磁盘镜像日志头损坏使 `lh->n > 30`，`for` 循环将越界写入内核 `.bss` 段中 `log.lh.block[30]` 之后的全局变量内存！
  
  2. **`install_trans()` 未校验目标块号 `log.lh.block[tail]` 合法范围（`src/kernel/log.c:67-84`）**：
     在重放日志时，`install_trans()` 直接执行 `struct buf *dbuf = bread(log.dev, log.lh.block[tail]); ... bwrite(dbuf);`，既未校验 `tail + 1 < sb.nlog`，也未校验目标块号 `log.lh.block[tail]` 必须位于根文件系统可写元数据/数据区 `[sb.inodestart, sb.size)`。损坏的 `lh->block[tail]` 可在启动恢复时直接覆写超级块（块 1）、日志区自身，或因 `virtio_disk_rw()` 缺少 `ROOTDEV` 上界检查（见 `AUD-040`）跨分区覆写 `EXT2DEV`/`FAT32DEV`。
  
  3. **`log_write()` 在日志吸收（Log Absorption）循环前过早判定 `log.lh.n >= LOGBLOCKS` 触发误杀 Panic（`src/kernel/log.c:216-236`）**：
     
     ```c
     void
     log_write(struct buf *b)
     {
       int i;
     
       acquire(&log.lock);
       if (log.lh.n >= LOGBLOCKS)
         panic("too big a transaction");
       if (log.outstanding < 1)
         panic("log_write outside of trans");
     
       for (i = 0; i < log.lh.n; i++) {
         if (log.lh.block[i] == b->blockno)   // log absorption
           break;
       }
       log.lh.block[i] = b->blockno;
       if (i == log.lh.n) {  // Add new block to log?
         bpin(b);
         log.lh.n++;
       }
       release(&log.lock);
     }
     ```
     
     当并发文件系统操作已在当前事务中填满全部 `LOGBLOCKS`（30）个不同日志槽位（即 `log.lh.n == 30`）后，若同一事务继续对**已经在 `log.lh.block[0..29]` 中的某个块**（如位图块、Inode 块或间接块）再次调用 `log_write(b)`，该操作属于日志吸收（`i < log.lh.n`），**根本不需要分配新的日志槽位**。但由于 `if (log.lh.n >= LOGBLOCKS) panic("too big a transaction");` 被放在了日志吸收循环（L226）之前而非 `if (i == log.lh.n)` 分支内部，内核会在第 30 个已入志块的合法重复更新上直接触发 `panic("too big a transaction")`！（此外 `log_write` 亦未校验 `b->dev == log.dev`）。

- **根因分析**：
  `read_head()` 与 `install_trans()` 假定磁盘日志头永不损坏；`log_write()` 将“新增日志槽位容量上限检查”错误前置到了“已入志块吸收查找”之前。

- **潜在影响**：
  损坏镜像可在启动期通过 `read_head()` 触发内核 `.bss` 缓冲区溢出；高并发满事务下对已入志块的二次修改会被误判为事务超限而引发内核 Panic。

- **具体修复建议**：
  
  1. 在 `read_head()` 中校验 `if(lh->n < 0 || lh->n > LOGBLOCKS) { log.lh.n = 0; brelse(buf); return; }`，并在 `install_trans()` 中校验 `log.lh.block[tail]` 位于 `[sb.inodestart, sb.size)` 范围内；
  2. 在 `log_write()` 中校验 `if(b->dev != log.dev) panic("log_write dev");`，并将 `if(log.lh.n >= LOGBLOCKS) panic("too big a transaction");` 移入 `if(i == log.lh.n)` 分支内部。

---

#### [AUD-044] `src/kernel/fs.c` 缺失 xv6 根文件系统超级块几何参数、磁盘块指针范围与目录项 `inum` 上界校验，损坏元数据可触发 `bfree()` 释放超级块/位图自毁或越界访问 Inode 表外块

- **严重级别**：High
- **当前状态**：未修复
- **发现 Commit**：`07d59f3`
- **涉及文件与位置**：
  - `src/kernel/fs.c:41-48`（`fsinit`）
  - `src/kernel/fs.c:93-106`（`bfree`）
  - `src/kernel/fs.c:470-540`（`bmap`、`itrunc`）
  - `src/kernel/fs.c:639-662`（`dirlookup`）
- **涉及架构 / 模块**：双架构通用 / xv6 原生根文件系统（`fs.c`）
- **触发路径与现象**：
  1. **`fsinit()` 未校验超级块几何布局（`src/kernel/fs.c:41-48`）**：
     `fsinit()` 仅检查 `if(sb.magic != FSMAGIC) panic("invalid file system");`，未验证 `sb.size <= FSSIZE`、`sb.nlog >= LOGBLOCKS + 1`、`sb.logstart >= 2`、`sb.inodestart >= sb.logstart + sb.nlog`、`sb.bmapstart >= sb.inodestart + (sb.ninodes / IPB + 1)` 等几何不变量。
  2. **`bmap()`、`itrunc()` 与 `bfree()` 未校验块号必须位于数据区 `[data_start, sb.size)`，损坏块指针可清除超级块/Inode 表/位图的已分配标志导致文件系统自毁（`src/kernel/fs.c:93-106, 470-540`）**：
     - 在 `src/mkfs/mkfs.c:198, 263-276` 中，`balloc(freeblock)` 将所有元数据块 `[0, nmeta)`（引导块 0、超级块 1、日志块 `2..32`、Inode 表块 `33..45`、位图块 `46`）在位图中的比特全部置为 `1`，以防止 `balloc()` 分配它们。
     - 然而在 `src/kernel/fs.c` 中，`bmap()`（L476, 488, 496）、`itrunc()`（L520, 526, 530）与 `bfree()`（L93-106）从未校验 `ip->addrs[]` 或间接块 `a[]` 中的块号 `b` 是否满足 `b >= data_start && b < sb.size`。
     - 一旦磁盘 Inode 或间接块中含有指向元数据区 `[1, nmeta)` 的损坏块号 `b`：
       - 调用 `writei()` 时，`bmap()` 直接返回该元数据块号并覆写超级块、Inode 表或位图块；
       - 调用 `itrunc()` / `unlink()` 时，`bfree(ip->dev, b)` 读取位图发现该元数据块的比特恰好为 `1`（不会触发 `panic("freeing free block")`），于是**直接将超级块、日志头、Inode 表或位图块在位图中的占用比特清零（置为 `0` 空闲）**！紧接着下一次 `balloc()` 就会将该元数据块当作普通文件数据块重新分配并调用 `bzero()` 清零，造成整个根文件系统彻底自毁！
       - 若块号 `b >= sb.size`（`FSSIZE = 2000`），`bmap()` / `itrunc()` / `bfree()` 会直接越界读写 `EXT2DEV` 或 `FAT32DEV` 分区。
  3. **`dirlookup()` 未校验目录项 `de.inum < sb.ninodes`（`src/kernel/fs.c:639-662`）**：
     `dirlookup()` 在 L650-658 仅跳过 `de.inum == 0`，未检查 `de.inum >= sb.ninodes`。当目录中存在 `de.inum >= sb.ninodes` 的损坏目录项时，`iget(dp->dev, inum)` 会为其分配内存 Inode 槽位，随后 `ilock(ip)` 和 `iupdate(ip)` 计算 `IBLOCK(ip->inum, sb)` 时越过 Inode 表落入位图区或数据块区，将普通文件数据块当作 `struct dinode` 解析或覆写。
- **根因分析**：
  `fs.c` 对从磁盘读出的块指针和目录项 `inum` 完全缺乏上下界防御，且 `bfree()` 仅靠位图比特是否为 `1` 判断块合法性，而元数据块在位图中同样为 `1`。
- **潜在影响**：
  根文件系统任何单点块指针或目录项 `inum` 损坏都会被放大为超级块/Inode 表/位图被释放清零或跨分区数据污染。
- **具体修复建议**：
  1. 在 `fsinit()` 中计算并保存数据区起始块号 `data_start = sb.bmapstart + (sb.size / BPB + 1)`，校验 `sb.size <= FSSIZE && data_start < sb.size`；
  2. 在 `bmap()`、`itrunc()` 与 `bfree()` 中统一校验磁盘块号 `b >= data_start && b < sb.size`；
  3. 在 `dirlookup()`（及 `iget()` 针对 `FS_XV6`）中校验 `inum > 0 && inum < sb.ninodes`。

---

#### [AUD-055] `fat32.c` 的 `fat32_name83()` 未拦截首字节 `0xe5`（删除标记）及 FAT32 非法短名字符与控制字符，且 `fat32_lfn_feed()` 未校验 LFN 条目规范字段与空长名（`endpos == 0`）

- **严重级别**：Medium
- **当前状态**：未修复
- **发现 Commit**：`07d59f3`
- **涉及文件与位置**：
  - `src/kernel/fat32.c:385-412`（`fat32_name83`）
  - `src/kernel/fat32.c:795-866`（`fat32_lfn_feed`、`fat32_lfn_take`）
  - `src/kernel/fat32.c:1015-1027`（`fat32_readi`）
- **涉及架构 / 模块**：双架构通用 / FAT32 文件系统短文件名编码与 LFN 解析（`fat32.c`）
- **触发路径与现象**：
  1. **`fat32_name83()` 未拦截首字节 `0xe5` 及非法 8.3 短名字符，创建首字节为 `0xe5` 的文件会导致目录项落盘即变成“已删除”状态并永久泄漏数据簇（`src/kernel/fat32.c:385-412`）**：
     
     ```c
     for(i = 0; name[i] && name[i] != '.'; i++){
       if(i >= 8 || name[i] == '/' || name[i] == ' ')
         return -1;
       out[i] = fat32_toupper(name[i]);
     }
     ```
     
     `fat32_name83()` 仅过滤了 `'/'`、`' '` 与 `'.'`，未拒绝控制字符（`(uchar)c < 0x20`）、FAT32 规范禁止的短名特殊字符（`" * + , : ; < = > ? [ \ ] |`）以及首字节 `0xe5`。在 FAT32 中，目录项首字节 `de.name[0] == 0xe5` 表示**该目录槽位已被删除空闲**（见 `fat32_is_valid_entry()` L367 及 `fat32_find_free_entry()` L578）。若用户调用 `open("/fat/åabc", O_CREATE | O_WRONLY)`，`fat32_create()` 会成功分配数据簇 `clus` 并写入 `de.name[0] = 0xe5` 的目录项，使得该文件一落盘就在磁盘目录上“隐身”变成空闲槽，而其分配的簇链在卸载后永久泄漏！
  2. **`fat32_lfn_feed()` 与 `fat32_lfn_take()` 未校验 `lde->type == 0`、`lde->first_clus_lo == 0`、`!(lde->ord & 0x80)` 及非空长名 `st->endpos > 0`（`src/kernel/fat32.c:795-866, 1015-1027`）**：
     - 按照 Microsoft FAT32 LFN 规范，合法 LFN 目录项必须满足 `LDIR_Type == 0`（`lde->type == 0`）、`LDIR_FstClusLO == 0`（`lde->first_clus_lo == 0`）且 `LDIR_Ord` 的 bit 7 为 0（`(lde->ord & 0x80) == 0`），而 `fat32_lfn_feed()` 未检查这三项；
     - 若损坏的 LFN 条目在第一个字符位置（`seq == 1, i == 0`）即出现终止符 `0x0000` 或 `0xFFFF`，`st->endpos` 会被设为 `0`，而 `fat32_lfn_take()` 仍返回 `ok = 1, *len = 0`。随后 `fat32_readi()`（L1022-1023）执行 `memmove(xde.name, lname, 0)`，向用户态 `ls` 返回一个 `xde.inum != 0` 但文件名为空字符串 `""` 的幽灵目录项，而非回退使用 8.3 短文件名。
- **根因分析**：
  `fat32_name83()` 缺少对 FAT32 8.3 字符集白名单/黑名单及首字节 `0xe5` 的检查；LFN 状态机缺少对 `endpos > 0` 及保留字段的校验。
- **潜在影响**：
  异常文件名可直接在 FAT32 分区制造无法通过 `unlink` 回收的孤儿簇，或导致目录遍历输出空文件名项。
- **具体修复建议**：
  1. 在 `fat32_name83()` 中拒绝 `(uchar)c <= 0x20`、`(uchar)name[0] == 0xe5` 及 `"*+,:;<=>?[\]|` 等非法 FAT 短名字符；
  2. 在 `fat32_lfn_feed()` 入口校验 `if((lde->ord & 0x80) || lde->type != 0 || lde->first_clus_lo != 0) { fat32_lfn_reset(st); return; }`，并在 `fat32_lfn_take()` 中要求 `st->endpos > 0`。

---

#### [AUD-056] `fs.c`、`vfs.c` 与 `ext2.c` 在 `create`/`link` 中对 16 位有符号 `ip->nlink`（`short`）递增缺失上限溢出检查（溢出后触发 `panic("unlink: nlink < 1")` 或泄漏 Inode），且 `ext2_readi()` 将 32 位 `de->inode` 截断为 16 位 `ushort`

- **严重级别**：Medium
- **当前状态**：未修复
- **发现 Commit**：`07d59f3`
- **涉及文件与位置**：
  - `src/kernel/file.h:33`、`src/kernel/fs.h:36, 57`
  - `src/kernel/vfs.c:27-52`（`xv6_link`）
  - `src/kernel/sysfile.c:233-234, 317`（`sys_unlink`、`create`）
  - `src/kernel/ext2.c:248, 868, 902, 962-972, 985-1014, 1044`（`ext2_fill_inode`、`ext2_create`、`ext2_unlink`、`ext2_link`、`ext2_readi`）
- **涉及架构 / 模块**：双架构通用 / VFS、`xv6fs` 与 `ext2` 链接计数及目录项 Inode 号映射
- **触发路径与现象**：
  1. **16 位有符号 `short nlink` 递增无上限检查导致符号翻转（`src/kernel/vfs.c:35`、`src/kernel/sysfile.c:317`、`src/kernel/ext2.c:248, 902, 992`）**：
     - 内存 `struct inode`（`src/kernel/file.h:33`）与磁盘 `struct dinode`（`src/kernel/fs.h:36`）中的 `nlink` 均为 **16 位有符号 `short`**（正数上限 `32767`），而 `ext2` 磁盘 `struct ext2_dinode` 的 `i_links_count` 为 16 位无符号 `ushort`，在 `ext2_fill_inode()`（L248）中直接赋给有符号 `ip->nlink`。
     - `xv6_link()`、`create()`、`ext2_create()` 与 `ext2_link()` 在执行 `ip->nlink++` 或 `dp->nlink++` 前均未检查 `nlink >= 32767`。一旦链接数从 `32767` 递增溢出为 `-32768`（或挂载的 `ext2` 镜像中某 Inode 的 `i_links_count >= 32768`）：
       - 在 `FS_XV6` 上，对该文件执行 `unlink()` 会在 `src/kernel/sysfile.c:233-234` 立即触发内核崩溃 **`panic("unlink: nlink < 1")`**；
       - 在 `ext2` 上，`ext2_unlink()` 第 970 行 `else if(ip->nlink > 0) ip->nlink--;` 因 `ip->nlink < 0` 跳过递减，导致该文件永远无法被释放。
  2. **`ext2_readi()` 读取目录项时将 32 位 `de->inode` 直接窄化截断为 16 位 `ushort xde.inum`（`src/kernel/ext2.c:1044`）**：
     
     ```c
     memset(&xde, 0, sizeof(xde));
     xde.inum = de->inode;
     ```
     
     `struct dirent` 的 `inum` 字段为 16 位 `ushort`（`src/kernel/fs.h:57`），而 `ext2` 的 `de->inode` 为 32 位 `uint`。对比 `fat32_readi()`（`src/kernel/fat32.c:1019-1021`）专门做了 `if(xde.inum == 0) xde.inum = 1;` 防止截断后低 16 位为 0，`ext2_readi()` 未做非零保护——若外部 `ext2` 镜像中存在编号为 `65536` 倍数（低 16 位为 0）的 Inode，`xde.inum` 会变为 `0`，被用户态 `ls`（`src/user/ls.c:61` `if(de.inum == 0) continue;`）静默当成空目录项跳过。
- **根因分析**：
  VFS 复用了早期 xv6 面向小型文件系统的 16 位有符号 `short nlink` 与 16 位 `ushort dirent.inum`，在接入 `ext2` 32 位 `inode` / 无符号 `i_links_count` 时未补齐溢出边界检查。
- **潜在影响**：
  畸形或高链接数镜像可触发 `FS_XV6` 的 `panic("unlink: nlink < 1")` 或 `ext2` 资源泄漏。
- **具体修复建议**：
  1. 在 `xv6_link()`、`create()`、`ext2_create()` 与 `ext2_link()` 中递增 `nlink` 前统一检查 `if(ip->nlink >= 32767) return -1;`，并在 `ext2_fill_inode()` 中若 `di->i_links_count > 32767` 则饱和截断或拒绝加载；
  2. 在 `ext2_readi()` 的 L1044 后补充 `if(xde.inum == 0) xde.inum = 1;`。

### 5. 微内核架构、IPC 与服务容错缺陷

#### [AUD-016] `proc.c` 进程槽位分区预留（`NPROC_SVC = 8`）未区分系统服务监护人（`rs`）与普通用户孤儿收养人（`initproc`），且 `allocproc(PFLAG_SERVICE)` 尚无外部可达入口

- **严重级别**：High
- **当前状态**：未修复
- **发现 Commit**：`07d59f3`
- **涉及文件与位置**：`src/kernel/proc.c:13, 130-145, 282-294, 375-385`
- **涉及架构 / 模块**：双架构通用 / 微内核进程管理与服务监管架构
- **触发路径与现象**：
  在提交 `d8ba077` 中，代码引入了 `NPROC_SVC = 8`（`proc[0..7]`）与 `NPROC_USER = 64`（`proc[8..71]`）的物理槽位硬分区，并在 `allocproc(int flags)` 中根据 `flags & PFLAG_SERVICE` 选择分区。
  但是：
  1. `userinit()`（`src/kernel/proc.c:286`）调用的是 `p = allocproc(0); initproc = p;`，使得 PID 1 被分配在普通用户槽 `proc[8]`，而 `proc[0..7]` 全部闲置，且 `allocproc` 是 `static` 函数，目前没有任何路径传入 `PFLAG_SERVICE`；
  2. 更重要的是，查看 `reparent(struct proc *p)`（`src/kernel/proc.c:375-385`）：
     
     ```c
     void reparent(struct proc *p) {
       struct proc *pp;
       for(pp = proc; pp < &proc[NPROC]; pp++){
         if(pp->parent == p){
           pp->parent = initproc;
           wakeup(initproc);
         }
       }
     }
     ```
- **根因分析**：
  对照 `docs/architecture/README.md` 第 101 行与 `docs/baseline/traps.md` `TRAP-12`：
  - 微内核架构下 PID 1 是 `rs`（`rsproc`，占用 `proc[0]` 服务槽），PID 2 是 `/init`（`initproc`，占用 `proc[8]` 普通用户槽）；
  - 当 `rs` 与 `/init` 并存时，若 `reparent()` 不区分 `pp->flags & PFLAG_SERVICE`，或者若 `initproc` 指向了 `rs`（PID 1），普通用户进程的孤儿会被过继给 `rs`（导致 `rs` 的 `wait()` 收到普通用户孤儿，或者 `/init` 的 `wait()` 收到崩溃的微内核服务进程），直接破坏服务崩溃监控与 Shell 子进程回收边界！
  - 此外，在 `kexit()`（`src/kernel/proc.c:395`）中仅检查 `if(p == initproc) panic("init exiting");`，微内核化后若 `rsproc` 退出或 `initproc` 未分离，缺乏对 `rsproc` 的独立保护。
- **潜在影响**：在微内核服务层合入时，若直接复用当前单一 `initproc` 和无分流 `reparent()`，会导致服务重启监控与用户孤儿回收互相串扰。
- **修复建议**：
  在合入微内核服务引导时，在 `proc.c` 中显式维护 `struct proc *rsproc`（`PFLAG_SERVICE` 监护人）与 `struct proc *initproc`（普通用户孤儿监护人）：`reparent()` 对 `pp->flags & PFLAG_SERVICE` 过继给 `rsproc`，对普通进程过继给 `initproc`。

---

#### [AUD-017] `.github/scripts/check_repo.py` 的 ELF 符号纯净度检查与 `SHARED_ABI_HEADER_RE` 存在漏检盲区

- **严重级别**：Medium
- **当前状态**：未修复
- **发现 Commit**：`07d59f3`
- **涉及文件与位置**：`.github/scripts/check_repo.py:38-43, 76-104, 304-350, 438-441`
- **涉及架构 / 模块**：构建与 CI 静态检查 / 微内核边界门禁
- **触发路径与现象**：
  在提交 `de5ce89` 新增的 `.github/scripts/check_repo.py` 微内核静态检查中，存在三处核验盲区：
  1. **自动激活条件过于脆弱（L438-441）**：
     
     ```python
     post_migration = (
         os.path.exists(os.path.join(ROOT, "src", "kernel", "ipc.c"))
         and not os.path.exists(os.path.join(ROOT, "src", "kernel", "fs.c"))
     )
     ```
     
     如果迁移过程中保留了一个空的或残留部分辅助函数的 `src/kernel/fs.c`（或仅迁出了 `ext2.c`/`fat32.c`/`virtio_disk.c` 而未删 `fs.c`），`post_migration` 将永远为 `False`，导致内核 ELF 符号检查在 CI 中完全不激活！此外，在 `.github/workflows/ci.yml:25` 中，`repo-checks` 任务在**未编译内核**的情况下运行 `python3 .github/scripts/check_repo.py`，此时 `build/riscv/kernel/kernel` 和 `build/loongarch/kernel/kernel` 根本不存在（`candidates` 为空列表，L361），因此即便 `post_migration == True`，CI 中的 `repo-checks` job 也**永远不会检查任何 ELF 文件**（直接打印 `kernel ELF symbol check reserved` 并返回成功）！
  2. **`readelf -Ws` 符号列解析在含版本或额外标记时可能错位（L321-331）**：
     `parts = line.split()` 假设 `parts[7]` 固定为符号名。当 `readelf -Ws` 输出中 `Vis` 列之后出现额外标记（如 `[<other>: 0x80]`，这在 **LoongArch64** 和 **RISC-V** STO_RISCV_VARIANT_CC 等符号中经常出现！）时，`parts[7]` 实际上是 `"[<other>:"` 而真正的符号名在 `parts[-1]`，导致大量带架构特殊 `st_other` 属性的函数符号被漏检！
  3. **禁止符号表遗漏了关键驱动与文件系统内部符号（L76-104）**：
     `ELF_FORBIDDEN_SYMBOL_RULES` 仅匹配了 `^uartinit|uartintr|...`，却漏掉了 `uartwrite`、`plicinit`、`plicinithart`、`plic_claim`、`plic_complete`、`apic_init`、`extioi_init`、`namex`、`skipelem`、`bget`、`write_log`、`write_head`、`read_head`、`readsb` 等核心文件系统/驱动符号。
- **根因分析**：
  - CI 工作流中 `check_repo.py` 仅在 `repo-checks` job（编译前）执行，`qemu-suites` job 编译完内核后未调用 `check_repo.py --check-elf build/${{ matrix.arch }}/kernel/kernel`；
  - `readelf -Ws` 解析未取行末符号名 `parts[-1]`。
- **潜在影响**：微内核迁移后，CI 的内核 ELF 符号纯净度检查会被静默跳过或漏检 LoongArch/RISC-V 带 `st_other` 标记的残留符号。
- **修复建议**：
  1. 在 `extract_elf_defined_symbols()` 的 `readelf` 分支中使用 `name = parts[-1]` 提取符号名；
  2. 将 `uartwrite`、`bget`、`namex`、`skipelem`、`write_log`、`write_head`、`read_head` 加入 `ELF_FORBIDDEN_SYMBOL_RULES`；
  3. 在 `.github/workflows/ci.yml` 的 `qemu-suites` 构建步骤之后追加 `python3 .github/scripts/check_repo.py --check-elf "build/${{ matrix.arch }}/kernel/kernel"`。

---

---

#### [AUD-054] 微内核架构设计文档中 `kexec()` 的 `GRANT_KERNEL_BUF` 跨空间加载契约未约束 `kalloc()` 单页（4 KiB）物理连续上限，且内核头文件缺失 Include Guard 与 ANSI `(void)` 原型约束

- **严重级别**：Medium
- **当前状态**：未修复
- **发现 Commit**：`07d59f3`
- **涉及文件与位置**：
  - `docs/architecture/README.md:147, 166`
  - `src/kernel/defs.h:66, 138, 184`
  - `src/kernel/file.h:14-16`
- **涉及架构 / 模块**：双架构通用 / 微内核 IPC 能力授权契约与内核公共头文件规范
- **触发路径与现象**：
  1. **`kexec()` 的 `GRANT_KERNEL_BUF` 装载契约与单页 `kalloc()` 物理内存布局冲突（`docs/architecture/README.md:147, 166`）**：
     - `docs/architecture/README.md:147` 与 `L166` 规定：`kexec()` 通过 `VFS_OP_EXEC_READ` 携带 `GRANT_KERNEL_BUF` 直接填充新页表物理页。
     - 然而内核物理内存分配器 `kalloc()` 仅能分配**单页（`PGSIZE = 4096` 字节）**，新创建的 `pagetable` 中相邻虚拟页对应的物理页帧在物理内存中**并非连续（甚至逆序）**；同时在 `kexec()` 完成提交前，`myproc()->pagetable` 仍指向旧进程页表而非新 `pagetable`。
     - 若按文档字面将 `VFS_OP_EXEC_READ` 对整个 ELF `PT_LOAD` 段（如 `usertests` 代码段 > 100 KiB）绑定单个 `GRANT_KERNEL_BUF` 内核物理页起始地址交由 `ipc_copy()` 连续写入，超过 4096 字节的部分将直接越界覆写相邻物理页帧！
  2. **内核头文件普遍缺失 Include Guard、残留 K&R 空参数原型 `()` 且 `mkdev(m,n)` 存在负数符号扩展污染（`src/kernel/defs.h`、`src/kernel/file.h`）**：
     - `src/kernel/` 下 16 个头文件中除 `vfs.h` 和 `block.h` 外，其余 14 个头文件（`defs.h`、`param.h`、`proc.h`、`fs.h`、`file.h`、`buf.h`、`memlayout.h`、`riscv.h`、`arch/loongarch/loongarch.h` 等）均无 `#ifndef` Include Guard；
     - `src/kernel/defs.h` 中的 `void iinit();`（L66）、`struct proc* myproc();`（L138）、`void syscall();`（L184）及多处 `.c` 定义仍使用 K&R 空参数列表 `()` 而非 `(void)`，在 C17 下关闭了编译期参数检查，在 C23（GCC 15 默认标准）或开启 `-Wstrict-prototypes` 时会报编译错误；
     - `src/kernel/file.h:16` 的 `#define mkdev(m,n) ((uint)((m)<<16| (n)))` 未对 `m` 和 `n` 做 `& 0xFFFF` 掩码，当 `short n < 0` 时符号扩展会直接污染高 16 位的主设备号 `m`。
- **根因分析**：
  架构文档在设计 `GRANT_KERNEL_BUF` 时未显式限定“每次 `VFS_OP_EXEC_READ` 仅传输单页 `<= PGSIZE` 或在 `ipc_grant` 中显式绑定目标 `pagetable_t`”；头文件则沿用了早期 xv6 K&R 风格。
- **潜在影响**：
  微内核迁移实现 `VFS_OP_EXEC_READ` 时若直接按段传入 `GRANT_KERNEL_BUF` 会引发严重物理内存越界踩踏；升级 GCC 15（默认 `-std=gnu23`）或组合包含头文件时易触发重定义或原型告警。
- **具体修复建议**：
  1. 在 `docs/architecture/README.md` 与即将实现的 `ipc.h` 中明确：`GRANT_KERNEL_BUF` 的 `grant->len` 硬上限为 `PGSIZE (4096)`，`kexec()` 的 `loadseg()` 必须按页循环（每次传入单页内核直接映射地址 `PA2KVA(walkaddr(new_pagetable, va + i))` 和 `n <= PGSIZE`），或在 `struct ipc_grant` 中显式保存 `pagetable_t target_pt`；
  2. 为所有内核头文件补齐 `#ifndef` Include Guard，将空参数函数声明统一改为 `(void)`，并将 `mkdev(m,n)` 修正为 `((uint)((((uint)(m) & 0xFFFF) << 16) | ((uint)(n) & 0xFFFF)))`。

### 6. 错误处理与边界条件缺失

#### [AUD-018] `src/user/printf.c` 的 `printint()` 将有符号 64 位整数强转为 32 位 `uint x`，导致用户态 `printf("%ld" / "%lu" / "%lx")` 高 32 位被静默截断

- **严重级别**：Medium

- **当前状态**：未修复

- **发现 Commit**：`07d59f3`

- **涉及文件与位置**：`src/user/printf.c:33-56`（`printint`）

- **涉及架构 / 模块**：双架构通用 / 用户态 C 库（`ulib` / `printf.c`）

- **触发路径与现象**：
  在 `src/user/printf.c:88-114` 中，`vprintf()` 专门解析了 `%ld`、`%lld`、`%lu`、`%llu`、`%lx`、`%llx` 并使用 `va_arg(ap, uint64)` 取出 64 位整数传给 `printint(&pb, ..., base, sgn)`。
  然而在 `printint()`（`src/user/printf.c:33-45`）中：
  
  ```c
  static void
  printint(struct printbuf *pb, long long xx, int base, int sgn)
  {
    char buf[20];
    int i, neg;
    uint x;
  
    neg = 0;
    if(sgn && xx < 0){
      neg = 1;
      x = -xx;
    } else {
      x = xx;
    }
  ```

- **根因分析**：
  局部变量 `x` 被声明为 32 位 `uint`（`unsigned int`）而非 `uint64`（`unsigned long long`）！对比内核态 `src/kernel/printf.c:33`（声明为 `unsigned long long x;`），用户态 `src/user/printf.c:37` 的 `uint x;` 会将所有传入 `printint` 的 64 位整数直接截断低 32 位！
  更危险的是：当 `sgn == 1` 且传入一个高 32 位非零的正 64 位整数（或 `%lu` 传入 `>= 2^32` 的数）时，不仅高 32 位全部丢失，若某 64 位无符号数的 bit 63 为 1，还会因 `long long xx` 符号位处理出错。

- **潜在影响**：用户态程序（以及即将迁往用户态的 6 个微内核服务 `rs`/`vfsd`/`ext2d`/`fat32d`/`blkdrvd`/`consdrvd`！）一旦使用 `printf("%lx", addr)` 或 `printf("%lu", size64)` 打印 64 位地址、偏移量、块号或令牌，高 32 位将被全部静默截断，严重误导调试并可能破坏基于日志的断言。

- **修复建议**：
  将 `src/user/printf.c:37` 的 `uint x;` 改为 `unsigned long long x;`（与 `src/kernel/printf.c:33` 保持一致）。

---

#### [AUD-019] `src/kernel/fs.c` 的 `bmap()` 在 `readi()` 无日志事务上下文中被调用时无 `alloc=0` 区分，读取含空洞或满盘短写文件会触发 `panic("log_write outside of trans")`

- **严重级别**：Medium
- **当前状态**：未修复
- **发现 Commit**：`07d59f3`
- **涉及文件与位置**：`src/kernel/fs.c:486-505`（`bmap`）、`src/kernel/fs.c:559-583`（`readi`）
- **涉及架构 / 模块**：双架构通用 / xv6fs 根文件系统
- **触发路径与现象**：
  1. 在 `readi()`（`src/kernel/fs.c:570`）中，读取文件偏移 `off` 时调用了 `bmap(ip, off/BSIZE)`。由于 xv6 的 `bmap(ip, bn)` **没有 `alloc` 参数**（不同于 `ext2_bmap(ip, bn, alloc)`），如果文件存在空洞或读取触发了未分配块，`readi()` 也会调用 `balloc()` 分配磁盘块并修改 `ip->addrs[]`！而 `readi()` 并不会调用 `iupdate(ip)`，且 `fileread()` 甚至**没有包裹 `begin_op()` / `end_op()`**！
  2. 如果在 `readi()` 中触发了 `bmap()` -> `balloc()` -> `log_write()`，由于 `fileread()` 没有调用 `begin_op()`，`log_write()`（`src/kernel/log.c:224`）将直接触发 **`panic("log_write outside of trans")`**！
- **根因分析**：
  `bmap()` 既被 `writei()`（在事务内）调用，也被 `readi()`（在 `fileread()` 中无事务）调用。一旦磁盘镜像中某个 inode 的 `size` 大于实际已分配的块数（例如崩溃恢复或 `mkfs.c` 对根目录虚增的块），随后对该文件执行 `read()` 就会在 `readi()` -> `bmap()` 中尝试 `balloc()` -> `log_write()`，从而在无事务上下文中直接导致内核 `panic("log_write outside of trans")`！
- **潜在影响**：对曾经历满盘短写或含空洞的 xv6fs 文件执行普通 `read()` 系统调用可触发内核 Panic。
- **修复建议**：
  为 `fs.c` 的 `bmap(struct inode *ip, uint bn, int alloc)` 增加 `int alloc` 参数（或拆分只读查询），在 `readi()` 中传入 `alloc = 0`（遇到未分配块直接填充 0 或停止，绝不在 `readi` 中调用 `balloc`）。

---

#### [AUD-020] `src/user/ulib.c` 的 `memmove()` 参数 `n` 为有符号 `int` 而 `memcpy()` / `user.h` 声明为 `uint`，超大长度时符号不一致

- **严重级别**：Low
- **当前状态**：未修复
- **发现 Commit**：`07d59f3`
- **涉及文件与位置**：`src/user/ulib.c:112, 146`、`src/user/user.h:33, 41`
- **涉及架构 / 模块**：双架构通用 / 用户态基础库（`ulib.c`）
- **触发路径与现象**：
  `src/user/user.h:41` 声明 `void *memcpy(void *, const void *, uint);`，内部直接调用 `memmove(dst, src, n)`，而 `memmove` 的第三个参数是 `int n`（且当 `src <= dst` 且重叠检查采用 `src > dst` 时，若 `src < dst` 但两缓冲区不重叠，`ulib.c:123` 依然走反向拷贝并计算 `dst += n; src += n;`）。
- **根因分析**：
  内核态 `src/kernel/string.c:31` 已使用 `uint n` 和精确重叠判断 `if(s < d && s + n > d)`，而用户态 `src/user/ulib.c:112` 仍保留了 `int n`（若 `n < 0` 则不拷贝但首先若走 `else` 分支 `dst += n` 会指针下溢）。
- **潜在影响**：用户态 `memmove` 传入负值或超大 `uint` 时存在指针下溢风险。
- **修复建议**：
  将 `src/user/ulib.c` 与 `src/user/user.h` 中 `memmove` 的长度参数统一改为 `uint n`，并与 `src/kernel/string.c:31` 的实现对齐。

---

#### [AUD-034] 用户态运行时入口 `ulib.c:start()` 丢弃 `main()` 返回值恒定调用 `exit(0)` 且未声明 `(argc, argv)` 参数转发，导致 `test1` / `test2` 失败时仍返回退出码 `0`

- **严重级别**：Medium
- **当前状态**：未修复
- **发现 Commit**：`07d59f3`
- **涉及文件与位置**：`src/user/ulib.c:11-17`（`start`）、`src/user/test1.c:62-71`（`main`）、`src/user/test2.c:90-96`（`main`）、`src/user/user.ld:1-5`、`src/kernel/arch/loongarch/user.ld:1-5`
- **涉及架构 / 模块**：双架构通用 / 用户态 C 运行时启动包装器（CRT0）与测试退出码
- **触发路径与现象**：
  1. 在 `src/user/ulib.c:11-17` 中，所有用户态程序（除 `_forktest` 外）的 ELF 默认入口函数 `start()` 实现如下：
     
     ```c
     void
     start()
     {
       extern int main();
       main();
       exit(0);
     }
     ```
  2. **丢弃 `main()` 返回值掩盖错误退出码**：`src/user/test1.c:62-71` 在拷贝校验失败时将 `fail` 置为 `1` 并执行 `return fail;`；`src/user/test2.c:90-96` 在跨文件系统拷贝失败时执行 `return rc;`（`rc = 1..6`）。然而 `main()` 返回到 `start()` 后，`start()` 完全忽略返回值并无条件调用 `exit(0)`，导致父进程通过 `wait(&status)` 获取到的退出状态恒为 `0`（成功）。
  3. **未显式声明并转发 `(int argc, char **argv)`**：`start()` 定义为无参函数并以无参形式调用 `main()`。`kexec()` 将 `argc` 放入 `$a0`、`argv` 放入 `$a1` 后跳转到 `start()`；当前仅靠编译器在 `-O` 下恰好未在 `start()` 序言中改写 `$a0`/`$a1` 才使 `main(argc, argv)` 侥幸收到参数，属于脆弱的未定义 ABI 依赖。
  4. **链接脚本缺失显式 `ENTRY(start)`**：`src/user/user.ld` 与 `src/kernel/arch/loongarch/user.ld` 均未声明 `ENTRY(start)`，`src/Makefile:183` 也未传 `-e start`，仅依赖 GNU `ld` 在缺失 `_start` 时对符号 `start` 的隐式回退匹配。
- **根因分析**：
  `ulib.c:start()` 仅作为防止 `main()` 漏写 `exit()` 时的兜底桩函数，未按标准 C 运行时语义执行 `exit(main(argc, argv))`，且未在链接脚本中显式固化入口符号。
- **潜在影响**：
  任何通过 `return non_zero;` 报告错误的测试程序或用户工具（如 `test1`、`test2`）在进程退出码层面全部变成假阳性 `0`。
- **具体修复建议**：
  1. 将 `src/user/ulib.c:11-17` 修改为：
     
     ```c
     void
     start(int argc, char **argv)
     {
       extern int main(int, char **);
       exit(main(argc, argv));
     }
     ```
  2. 在 `src/user/user.ld` 与 `src/kernel/arch/loongarch/user.ld` 顶部显式声明 `ENTRY(start)`。

---

#### [AUD-035] 用户态工具集边界缺陷：`grep` 遇到超 1023 字节无换行长行时静默提前终止，`sh` 内置 `cd` 路径截断脆弱，且 `ln`/`mkdir`/`rm`/`kill` 失败时仍返回 `exit(0)`

- **严重级别**：Low
- **当前状态**：未修复
- **发现 Commit**：`07d59f3`
- **涉及文件与位置**：`src/user/grep.c:190-214`（`grep`）、`src/user/sh.c:214-225`（`main`）、`src/user/ln.c:315-317`、`src/user/mkdir.c:333-340`、`src/user/rm.c:356-363`、`src/user/kill.c:300-302`
- **涉及架构 / 模块**：双架构通用 / 用户态基础工具链（`src/user/`）
- **触发路径与现象**：
  1. **`grep` 长行静默停止读取（`src/user/grep.c:197-213`）**：`buf` 大小为 1024 字节。当输入文件某一行超过 1023 字节不含换行符时，首轮读取填满 `m = 1023` 字节，`strchr(p, '\n')` 返回 `0`，`p` 仍指向 `buf`，导致 `m -= p - buf` 后 `m` 仍为 `1023`。下一轮循环调用 `read(fd, buf + 1023, sizeof(buf) - m - 1)` 时请求长度 `1024 - 1023 - 1 == 0`，`read()` 返回 `0`，`grep` 立即结束循环并静默丢弃文件剩余全部内容。
  2. **`sh` 内置 `cd` 参数解析脆弱（`src/user/sh.c:220-224`）**：`if(cmd[0] == 'c' && cmd[1] == 'd' && cmd[2] == ' ')` 仅跳过 `'cd '` 的单空格且无条件执行 `cmd[strlen(cmd)-1] = 0;`。若输入为 `cd  /mnt`（多个空格）则传入 `" /mnt"` 导致 `chdir` 失败；若通过管道/EOF 输入 `cd /mnt` 且末尾无换行符，则 `cmd[strlen(cmd)-1] = 0` 会截断目录名最后一个字符（变成 `"/mn"`）。
  3. **命令失败退出码恒为 `0`**：`ln.c`（L315 `link(...) < 0`）、`mkdir.c`（L334 `mkdir(...) < 0`）、`rm.c`（L357 `unlink(...) < 0`）和 `kill.c`（L301 未检查 `kill()` 返回值）在系统调用失败打印错误后均执行 `exit(0)` 而非 `exit(1)`。
- **根因分析**：
  xv6 原始教学工具集未处理满缓冲区无换行边界、空白符归一化及 POSIX 非零错误退出码。
- **潜在影响**：
  对包含长行的数据文件运行 `grep` 会漏检后续内容；测试脚本若依赖 `ln`/`mkdir`/`rm`/`kill` 的进程退出状态将无法感知执行失败。
- **具体修复建议**：
  1. 在 `grep.c` 中当 `m == sizeof(buf) - 1` 且未找到 `\n` 时，直接对当前缓冲区分片匹配或丢弃超长行前缀并重置 `m = 0`；
  2. 在 `sh.c` 处理 `cd` 时仅在末尾字符确为 `\n` 或 `\r` 时截断，并跳过 `cmd + 3` 之后的连续空白字符；
  3. 在 `ln.c`、`mkdir.c`、`rm.c`、`kill.c` 遇到系统调用返回 `< 0` 时调用 `exit(1)`。

---

---

#### [AUD-052] Web 服务端 `TerminalSession.input()` 在 `real` 模式下收到非字符串 `data` 时抛出未捕获异常导致整个 Node.js 服务崩溃，`stop()` 仅终止宿主 `wsl.exe` 残留后台 QEMU 进程，且 `/api/source` 对非数字 `line` 产生 `NaN` 空切片

- **严重级别**：Medium
- **当前状态**：未修复
- **发现 Commit**：`07d59f3`
- **涉及文件与位置**：
  - `web/server/terminal-session.js:26-47, 76-81, 119-131`
  - `web/server/index.js:92-111, 121-130`
- **涉及架构 / 模块**：Web 调试控制台后端服务（`web/server/`）
- **触发路径与现象**：
  1. **WebSocket `input` 消息传入非字符串 `data` 时导致整个 Node.js 进程未捕获异常崩溃（`web/server/terminal-session.js:119-131`、`web/server/index.js:124-128`）**：
     
     ```javascript
     input(data) {
       if (this.mode === 'real' && this.child?.stdin?.writable) this.child.stdin.write(data);
       else if (this.mode === 'demo' && this.demo) this.demo.input(data);
     }
     ```
     
     在 `web/server/index.js:124-128` 中，服务端解析 WebSocket JSON 后直接调用 `session.handle(message)` 且外层无 `try / catch`。当会话处于 `real` 模式时，若客户端发送的 `{"type": "input", "data": ...}` 中 `data` 缺失（`undefined`）、为 `null`、数字或对象，Node.js 的 `child.stdin.write(data)` 会同步抛出未捕获异常 `TypeError [ERR_INVALID_ARG_TYPE]: The "chunk" argument must be of type string or an instance of Buffer or Uint8Array`，**直接导致整个 Web 教学服务器进程崩溃退出**！（而在 `demo` 模式下，`DemoShell.input(undefined)` 会通过 `String(data)` 将其转为字面量 `"undefined"` 注入终端命令行）。
  2. **`TerminalSession.stop()` 仅向宿主 `wsl.exe` 发送信号，在编译期或内核卡死时残留后台 Linux `make` / `qemu-system-*` 孤儿进程（`web/server/terminal-session.js:26-47`）**：
     `stop()` 先写 `x`（Ctrl-A X），500ms 后调用 `previousChild.kill()`。若此时 `make` 仍在编译构建镜像（QEMU 尚未启动故不响应 `x`），或 QEMU 因死锁未响应串口，终止 Windows 侧 `wsl.exe` 包装进程并不会自动清理 WSL 内部已派生的进程树，残留的后台 `make`/`qemu-system-*` 会继续读写共享的 `build/<arch>/fs.img`，与新启动的会话发生磁盘镜像写冲突。
  3. **`/api/source` 接口对非数字 `line` 参数计算得 `NaN` 返回空数组，且路径白名单未像 `ai-trace.js` 一样限制在 `src/` 下（`web/server/index.js:92-111`）**：
     
     ```javascript
     const line = Math.max(1, Number(request.query.line || 1));
     ```
     
     当 `request.query.line` 为非数字字符串（如 `?file=src/kernel/vfs.c&line=abc`）时，`Number("abc")` 为 `NaN`，`Math.max(1, NaN)` 仍为 `NaN`，导致 `start` 与 `end` 全部变成 `NaN`，`lines.slice(NaN, NaN)` 返回空数组 `lines: []`。此外，对比 `ai-trace.js:25-28` 严格限制只读 `src/kernel/` 与 `src/user/`，`/api/source` 允许读取仓库根目录下任意 `.md`/`.c`/`.h`/`.S`/`.pl` 文件。
- **根因分析**：
  WebSocket 消息处理缺少对 `message.data` 类型的校验与异常隔离；`Number(...) || 1` 写法未写在 `Math.max` 内部处理 `NaN`；跨 WSL 进程管理缺少进程组清理。
- **潜在影响**：
  单条畸形 WebSocket 消息即可打挂整个 Web 控制台后端；频繁点击“重启内核”易造成多个 QEMU 实例并发踩踏同一 `fs.img`。
- **具体修复建议**：
  1. 在 `TerminalSession.input(data)` 入口增加 `if (typeof data !== 'string' || data.length === 0) return;`，并在 `index.js` 的 `socket.on('message')` 中用 `try / catch` 包裹 `session.handle(message)`；
  2. 在 `index.js:94` 将行号解析改为 `const parsed = Number.parseInt(String(request.query.line ?? '1'), 10); const line = Number.isFinite(parsed) && parsed >= 1 ? parsed : 1;`；
  3. 在 Linux/WSL 下通过 `setsid` 启动会话并在 `stop()` 中终止整个进程组。

### 7. 构建系统、测试框架与工程规范问题

#### [AUD-021] 顶层 `Makefile` 的 `prepare_case` / `smp` / `prepare_extended_case` 每次准备单套测试都会调用 `make ARCH=$(TEST_ARCH) clean`，无条件删除已编译产物并强制全量重编

- **严重级别**：Medium
- **当前状态**：未修复
- **发现 Commit**：`07d59f3`
- **涉及文件与位置**：`Makefile:54-58, 116-120, 145-149`、`src/Makefile:286-295`
- **涉及架构 / 模块**：构建与测试流水线（`Makefile` / `src/Makefile`）
- **触发路径与现象**：
  1. 在 `.github/workflows/ci.yml:75-105` 以及 `tests/run_all.sh:20` 中，测试流水线循环执行 10~11 个测试套件：`for s in "${SUITES[@]}"; do make "$target" && make run; done`。
  2. 在 `Makefile` 的 `prepare_case`（L54）、`smp`（L116）、`prepare_extended_case`（L145）中，**每一个测试套件**都会执行：
     
     ```makefile
     cd "$(XV6_DIR)" && \
     make ARCH="$(TEST_ARCH)" clean && \
     make ARCH="$(TEST_ARCH)" kernel/kernel && \
     rm -f "../build/$(TEST_ARCH)/fs.img" "../build/$(TEST_ARCH)/ext2.img" "../build/$(TEST_ARCH)/fat32.img" && \
     make ARCH="$(TEST_ARCH)" fs.img
     ```
  3. 而在 `src/Makefile:286-295` 中，`clean` 目标会删除 `$(BUILD)`（即 `../build/$(ARCH)`）下**所有内核 `.o` 和全部 28 个用户程序 `_cat`..`_usertests` 的 `.o` 与 ELF**！
- **根因分析**：
  提交 `8dc9d7c` 已经将双架构编译产物完全隔离到了 `build/riscv/` 和 `build/loongarch/`，原本每次测试只需要**重新生成干净的磁盘镜像 `fs.img`、`ext2.img`、`fat32.img`**（避免前一个测试修改了磁盘镜像），根本不需要在 11 个测试套件之间反复 `make clean` 删除并重编完全未改动的内核和 28 个用户态二进制程序（尤其是庞大的 `_usertests`）！
  在 CI 和 `make verify-all` 中，这导致同一架构的内核和 `_usertests` 被毫无必要地重复全量编译了 **12 次**（双架构合计 24 次全量编译），大幅拖慢了 Tier 1 / Tier 2 回归速度。
- **潜在影响**：`run_all.sh` 与 CI `qemu-suites` 编译耗时被放大 10 倍以上；一旦进入微内核阶段（需额外编译 6 个常驻服务 + 打包 BootFS），每个套件重编一次将导致 CI 超时。
- **修复建议**：
  在顶层 `Makefile` 的 `prepare_case`、`smp`、`prepare_extended_case` 中移除 `make ARCH="$(TEST_ARCH)" clean`，仅保留 `rm -f "../build/$(TEST_ARCH)/fs.img" "../build/$(TEST_ARCH)/ext2.img" "../build/$(TEST_ARCH)/fat32.img"` 后执行 `make ARCH="$(TEST_ARCH)" kernel/kernel fs.img`。这样既保证每次测试都使用全新的干净磁盘镜像，又能 100% 复用已编译的内核与用户程序对象文件。

---

#### [AUD-022] `src/Makefile` 的 `tags` 与 `.gdbinit` 目标未适配 `build/<arch>` 隔离目录与 LoongArch64 架构

- **严重级别**：Low
- **当前状态**：未修复
- **发现 Commit**：`07d59f3`
- **涉及文件与位置**：`src/Makefile:176-177, 323-328`
- **涉及架构 / 模块**：构建与调试支持（`src/Makefile`）
- **触发路径与现象**：
  1. `src/Makefile:323-324` 中 `.gdbinit: .gdbinit.tmpl-riscv` 硬编码依赖 RISC-V 模板（其中硬编码了 `set architecture riscv:rv64` 和 `file kernel/kernel` 旧路径，而现在内核输出在 `../build/$(ARCH)/kernel/kernel`）。执行 `make -C src ARCH=riscv qemu-gdb` 或 `ARCH=loongarch qemu-gdb` 时，GDB 加载 `kernel/kernel` 会因找不到文件失败。
  2. `src/Makefile:176-177` 中 `tags: $(OBJS)` 仅扫描 `kernel/*.S kernel/*.c`，漏掉了 `kernel/arch/loongarch/*`。
- **根因分析**：
  提交 `8dc9d7c` 将 `KERNEL_OUT` 迁至 `../build/$(ARCH)/kernel/kernel` 后，未同步更新 `.gdbinit` 生成规则中的内核 ELF 路径。
- **潜在影响**：`make qemu-gdb` 生成的 `.gdbinit` 指向不存在的 `kernel/kernel` 路径，导致 GDB 调试无法直接加载符号。
- **修复建议**：
  在 `.gdbinit` 生成规则中将 `file kernel/kernel` 替换为 `file $(KERNEL_OUT)`，并在 `ARCH=loongarch` 时适配架构设置。

---

#### [AUD-023] `web/server/index.js` 与 `web/server/terminal-session.js` 硬编码 Windows `wsl.exe` 调用且将 LoongArch64 强制限制为 `CPUS=1`

- **严重级别**：High
- **当前状态**：未修复
- **发现 Commit**：`07d59f3`
- **涉及文件与位置**：
  - `web/server/index.js:39-44`（`probeRuntime`）
  - `web/server/terminal-session.js:76-81`（`startReal`）
- **涉及架构 / 模块**：Web 观测控制台后端（`web/server/`）
- **触发路径与现象**：
  1. 在 Linux 原生宿主环境（包括用户的 Fedora KDE 开发机、Ubuntu 24.04 Docker 容器及 GitHub Actions CI）下启动 `web` 服务时，`probeRuntime()`（`web/server/index.js:41`）无条件调用 `spawn('wsl.exe', ...)`，在 Linux 上必然触发 `ENOENT` 错误并将 `runtimeInfo.realAvailable` 置为 `false`，导致 Linux 下完全无法启动真实 QEMU 会话！
  2. 在 `web/server/terminal-session.js:76` 中：
     
     ```javascript
     const makeArgs = arch === 'loongarch' ? 'ARCH=loongarch CPUS=1 qemu' : 'ARCH=riscv CPUS=3 qemu';
     ```
     
     尽管内核已在 `da20269`、`d8ba077`、`07d59f3` 中彻底修复了 LoongArch64 3 核 SMP 启动与 IPI 唤醒，Web 后端仍将 LoongArch64 硬编码降级为 `CPUS=1`（且同样通过 `wsl.exe` 启动）。
- **根因分析**：
  Web 服务端最初在 Windows + WSL 环境下编写，未检查 `process.platform !== 'win32'` 的原生 Linux 执行路径，也未随 LoongArch64 SMP 修复同步更新 `CPUS=3`。`docs/architecture/README.md:247` 也明确指出需要适配该文件以支持 Linux 原生双架构 `CPUS=3`。
- **潜在影响**：在 Linux 宿主机或 Docker 容器中，Web 控制台的“真实 QEMU 内核模式”100% 不可用，且在 Windows 下 LoongArch64 真实会话被锁死为单核。
- **修复建议**：
  1. 在 `web/server/index.js` 和 `web/server/terminal-session.js` 中根据 `process.platform === 'win32'` 自动分流：Linux/macOS 原生环境直接调用 `bash -lc`，Windows 环境通过 `wsl.exe` 调用；
  2. 将 `terminal-session.js:76` 统一改为 `ARCH=${arch} CPUS=3 qemu`。

---

#### [AUD-024] `web/server/ai-trace.js` 中系统调用表仍使用旧名 `'sleep'` 而非当前内核的 `'pause'`，且未检索 `src/kernel/arch/loongarch/` 下的源码证据

- **严重级别**：Medium
- **当前状态**：未修复
- **发现 Commit**：`07d59f3`
- **涉及文件与位置**：`web/server/ai-trace.js:8-12, 122-130`
- **涉及架构 / 模块**：Web 控制台 AI 调用链分析器（`web/server/ai-trace.js`）
- **触发路径与现象**：
  1. `web/server/ai-trace.js:8-12` 定义的 `SYSCALL_NAMES` 包含 `'sleep'` 而非 `'pause'`，但仓库中系统调用名为 `pause`（`SYS_pause = 13`，`sys_pause`，`src/user/usys.pl` 中也是 `entry("pause")`）。
  2. `collectEvidence()`（L122-130）无条件固定收集 RISC-V 的 `['src/kernel/trampoline.S', 'uservec']` 和 `['src/kernel/trap.c', 'usertrap']`，当 `payload.arch === 'loongarch'` 时完全不会收集 `src/kernel/arch/loongarch/uservec.S` 与 `src/kernel/arch/loongarch/trap.c`，导致 LoongArch64 架构下的 AI 调用链证据仍指向 RISC-V 的 `trampoline.S` 和 `trap.c`。
- **根因分析**：
  `ai-trace.js` 未针对 `payload.arch === 'loongarch'` 切换架构特定陷阱入口文件路径，且系统调用列表未与 xv6-riscv rev5 的 `SYS_pause` 对齐。
- **潜在影响**：Web 控制台在切换到 LoongArch64 模式时，AI 源码证据收集器展示的陷阱层代码仍然是 RISC-V 源码。
- **修复建议**：
  将 `SYSCALL_NAMES` 中的 `'sleep'` 改为 `'pause'`（或同时保留两者），并在 `collectEvidence()` 中根据 `payload.arch === 'loongarch'` 收集 `src/kernel/arch/loongarch/{uservec.S,trap.c,vm.c,hal.c}`。

---

#### [AUD-025] `src/mkfs/mkfs.c` 使用未按 `-Wall -Werror` 编译且包含已弃用的 BSD 函数 `bcopy`/`bzero`/`index` 及根目录大小无条件向上加 1 块的冗余计算

- **严重级别**：Low
- **当前状态**：未修复
- **发现 Commit**：`07d59f3`
- **涉及文件与位置**：`src/Makefile:202-205`、`src/mkfs/mkfs.c:127, 154, 192-196, 350`
- **涉及架构 / 模块**：构建工具（`src/mkfs/mkfs.c`）
- **触发路径与现象**：
  1. 对比 `src/Makefile:208` 编译 `mkfat32` 时启用了 `gcc -Wall -Werror -O`，第 204 行编译 `mkfs` 时仅使用 `gcc -I. -o $@ mkfs/mkfs.c`，未开启 `-Wall -Werror`。
  2. `src/mkfs/mkfs.c:192-196` 在修正根目录 `rootino` 的 `din.size` 时：
     
     ```c
     off = xint(din.size);
     off = ((off/BSIZE) + 1) * BSIZE;
     din.size = xint(off);
     ```
     
     当 `off` 恰好是 `BSIZE`（1024）的整数倍时，仍然会多加一个未分配的空块大小（使 `rootino` 的 `size` 虚增 1024 字节，尽管对应的 `addrs[]` 槽位为 0）。
- **根因分析**：
  历史遗留的 `mkfs.c` 逻辑未做整数倍对齐判断，且未启用 `-Wall -Werror`。
- **潜在影响**：当根目录条目总字节数恰好为 1024 字节时，根目录 `size` 被设为 2048 字节，导致首次在根目录创建新文件或 `dirlookup` 时读取未分配块（触发 `AUD-019` 中 `readi` -> `bmap` 的无事务分配块隐患）。
- **修复建议**：
  将 `mkfs.c:194` 改为对齐公式或直接保留 `iappend` 写入的精确 `din.size`，并在 `src/Makefile` 中为 `mkfs` 开启 `-Wall -Werror`。

---

#### [AUD-031] Web 调试控制台自动化 QA 脚本按钮选择器（`'演示'` vs `'快速'`）与前端实际文案失配，且硬编码 Windows Edge 绝对路径

- **严重级别**：Medium
- **当前状态**：未修复
- **发现 Commit**：`07d59f3`
- **涉及文件与位置**：`web/scripts/qa-real-switch.mjs:7, 27, 40`、`web/scripts/qa.mjs:7`、`web/scripts/qa-ai-live.mjs:7`、`web/src/App.jsx:234-239`（`Header`）
- **涉及架构 / 模块**：Web 调试控制台 Playwright 自动化 QA 脚本（`web/scripts/`）
- **触发路径与现象**：
  1. 在 `web/src/App.jsx:234-236` 中，顶部运行模式切换按钮的渲染文案为 **`快速`** 与 **`真实 QEMU`**：
     
     ```jsx
     <button className={mode === 'demo' ? 'active' : ''} onClick={() => onModeChange('demo')}>快速</button>
     <button className={mode === 'real' ? 'active' : ''} onClick={() => onModeChange('real')}>真实 QEMU</button>
     ```
  2. 然而在 `web/scripts/qa-real-switch.mjs:27` 中，测试脚本查找的是 `const demoButton = page.getByRole('button', { name: '演示' });`，并在 L40 调用 `await demoButton.click();`。由于页面上不存在名为 `'演示'` 的按钮，该脚本在执行到模式切回步骤时必然因 Playwright Locator 超时而失败。
  3. 此外，`web/scripts/qa.mjs:7`、`web/scripts/qa-ai-live.mjs:7` 和 `web/scripts/qa-real-switch.mjs:7` 均硬编码了 Windows 下的浏览器路径 `const edgePath = 'C:\\Program Files (x86)\\Microsoft\\Edge\\Application\\msedge.exe';` 并直接传给 `chromium.launch({ executablePath: edgePath })`，导致所有 QA 脚本在 Linux、macOS 或 CI 容器中无法运行。
- **根因分析**：
  前端 `App.jsx` 将 `demo` 模式的按钮文案从“演示”改为“快速”后，未同步更新 `qa-real-switch.mjs` 中的 ARIA role name 选择器；且浏览器启动配置未兼容非 Windows 环境。
- **潜在影响**：
  `qa-real-switch.mjs` 回归测试 100% 失败，且 QA 脚本无法在跨平台或 Linux CI 环境中复用。
- **具体修复建议**：
  1. 将 `web/scripts/qa-real-switch.mjs:27` 中的 `{ name: '演示' }` 更新为 `{ name: '快速' }`。
  2. 在三个 QA 脚本中检查 `fs.existsSync(edgePath)` 或环境变量 `PLAYWRIGHT_EXECUTABLE_PATH`，若不存在则回退到 Playwright 默认 Chromium。

---

#### [AUD-032] Web 前端确定性追踪器 `web/src/traces.js` 硬编码历史源码行号且未传入 `arch`，导致“源码上下文”弹窗定位错位及 LoongArch 模式显示 RISC-V 源码

- **严重级别**：Medium
- **当前状态**：未修复
- **发现 Commit**：`07d59f3`
- **涉及文件与位置**：`web/src/traces.js:23, 38, 40, 79-80, 107, 123, 125, 165-166, 201, 235, 352`（`traceCommand` 等）、`web/src/App.jsx:138-167, 463`（`SourceView`、`runCommand`）、`web/server/index.js:241-256`（`/api/source`）
- **涉及架构 / 模块**：Web 调试控制台调用链可视化与源码映射（`web/src/`）
- **触发路径与现象**：
  1. 当用户在 Web 控制台中点击任意内核调用链节点的“源码上下文”标签时，`SourceView`（`web/src/App.jsx:138`）请求 `/api/source?file=${node.file}&line=${node.line}`，后端（`web/server/index.js:250-251`）仅返回以 `node.line` 为中心、前后各 16 行的代码切片。
  2. 由于近期 `ext2.c`、`fat32.c`（新增 LFN 支持等）、`vfs.c` 和 `hal.c` 经历了大幅增删，而 `web/src/traces.js` 中硬编码的行号从未同步更新，导致大量节点指向错误的代码位置：
     - `ext2_lookup`：`traces.js:38` 硬编码 `line: 592`（实际位于 `src/kernel/ext2.c:689`，偏差 97 行，第 592 行实际是 `ext2_bmap`）；
     - `fat32_lookup`：`traces.js:40` 硬编码 `line: 1001`（实际位于 `src/kernel/fat32.c:1156`，偏差 155 行，第 1001 行实际是 `fat32_readi`）；
     - `ext2_readi` / `fat32_readi`：`traces.js:79-80` 硬编码 `971` / `850`（实际位于 `1017` / `978`）；
     - `ext2_writei` / `fat32_writei`：`traces.js:165-166` 硬编码 `1035` / `931`（实际位于 `1081` / `1064`）；
     - `ext2_mount` / `fat32_mount`：`traces.js:235` 硬编码 `1136` / `597`（实际位于 `1272` / `702`）；
     - `vfs_readi`：`traces.js:107` 硬编码 `478`（实际位于 `503`）；`hal_disk_rw`：`traces.js:123` 硬编码 `43`（实际位于 `58`）。
  3. 此外，`traceCommand(raw, state, result)`（`traces.js:352`，由 `App.jsx:463` 调用）未接收当前架构参数 `arch`。即使用户在顶栏切换到 **LoongArch**，规则追踪图中的 `'ecall'` 节点（`traces.js:23`）和 `'copyout'` 节点（`traces.js:125`）仍硬编码展示 RISC-V 的 `ecall -> uservec -> usertrap`（`src/kernel/trampoline.S:21`）与 `src/kernel/vm.c:343`，而非 LoongArch64 的 `syscall 0 -> uservec`（`src/kernel/arch/loongarch/uservec.S`）与 `src/kernel/arch/loongarch/vm.c`。
- **根因分析**：
  前端规则追踪器采用静态硬编码行号而非基于符号名动态定位，且 `traceCommand()` 缺少 `arch` 分支适配。
- **潜在影响**：
  用户在 Web 控制台中查看 `ext2` / `fat32` / `vfs` 调用链节点的“源码上下文”时看到无关函数片段；切换到 LoongArch64 模式时仍展示 RISC-V 陷入汇编与页表源码。
- **具体修复建议**：
  1. 在 `/api/source` 接口中支持按函数符号名定位行号（或同步更新 `web/src/traces.js` 中的所有行号）。
  2. 为 `traceCommand(raw, state, result, arch)` 传入 `arch`，在 `arch === 'loongarch'` 时指向 `src/kernel/arch/loongarch/` 对应源码。

---

#### [AUD-036] `src/user/usertests.c` 在 `copyin`、`copyout`、`copyinstr1`、`nowrite` 与 `lazy_copy` 中硬编码 RISC-V Sv39 38 位虚拟地址常量，导致 LoongArch64 下 `TRAPFRAME` 与 `MAXVA` 边界完全未被测试覆盖

- **严重级别**：High
- **当前状态**：未修复
- **发现 Commit**：`07d59f3`
- **涉及文件与位置**：`src/user/usertests.c:35-36`（`copyin`）、`src/user/usertests.c:80-81`（`copyout`）、`src/user/usertests.c:122-123`（`copyinstr1`）、`src/user/usertests.c:2436-2437`（`nowrite`）、`src/user/usertests.c:2677-2684`（`lazy_copy`）
- **涉及架构 / 模块**：LoongArch64（`ARCH=loongarch`）/ 用户态回归测试集 `usertests` 虚拟内存安全边界测试
- **触发路径与现象**：
  1. `src/user/usertests.c` 中的 5 个核心虚拟地址边界与特权级隔离测试均硬编码了 RISC-V Sv39（38 位用户地址空间，`MAXVA = 0x4000000000`）的地址常量：
     
     ```c
     uint64 addrs[] = { 0x80000000LL, 0x3fffffe000, 0x3ffffff000, 0x4000000000,
                        0xffffffffffffffff };
     ```
     
     以及 `lazy_copy()`（L2677-2684）中的 `0x3fffffc000 .. 0x8000000000`。
  2. 在 **RISC-V Sv39** 下，`0x80000000LL` 是 `KERNBASE`，`0x3fffffe000` 是 `TRAPFRAME`（`MAXVA - 2*PGSIZE`），`0x3ffffff000` 是 `TRAMPOLINE`（`MAXVA - PGSIZE`），`0x4000000000` 是 `MAXVA`。
  3. 然而在 **LoongArch64** 下（`src/kernel/arch/loongarch/loongarch.h:336` 与 `src/kernel/memlayout.h:23-30`），用户半区为 47 位（`MAXVA = 1UL << 47 = 0x800000000000`），`TRAPFRAME`（及 `TRAMPOLINE`）位于 **`0x7ffffffff000`**（`MAXVA - PGSIZE`），72 个内核栈 `KSTACK(p)` 映射在 `TRAPFRAME` 下方的 `0x7ffffff6e000 .. 0x7fffffffd000`，`KERNBASE` 位于 `0x9000000000200000UL`。
  4. 因此在 LoongArch64 下，硬编码的 `0x80000000LL`（2 GiB）至 `0x8000000000`（512 GiB）全部落在了普通未分配的低端用户地址空间（`< MAXVA`）内！`copyin`、`copyout`、`copyinstr1`、`nowrite` 和 `lazy_copy` 在 LoongArch64 上**从未测试 `TRAPFRAME`（`0x7ffffffff000`，该页在 `p->pagetable` 中以 `PLV=0` 映射）、`KSTACK` 区域以及 `MAXVA`（`0x800000000000`）边界**。
- **根因分析**：
  `src/user/usertests.c` 第 8-9 行虽然已经 `#include "kernel/memlayout.h"` 和 `#include "kernel/riscv.h"`（在 LoongArch64 下间接引入 `loongarch.h` 中的 `MAXVA`），但上述 5 个测试函数没有使用 `KERNBASE`、`TRAPFRAME`、`TRAMPOLINE`、`MAXVA` 宏，而是直接写死了 RISC-V Sv39 的十六进制字面量。
- **潜在影响**：
  LoongArch64 架构下的 `TRAPFRAME` 用户态越权写防护（`PTE_PLV` 检查）与 `MAXVA`（`1UL << 47`）上界溢出检查在 `usertests` 中处于完全漏测状态（正是这一盲区掩盖了 `AUD-008` 中 LoongArch64 `copyin`/`copyinstr` 缺失 `va0 >= MAXVA` 显式检查的问题）。
- **具体修复建议**：
  将 `src/user/usertests.c` 中 `copyin`、`copyout`、`copyinstr1`、`nowrite` 与 `lazy_copy` 的测试地址数组改为基于宏定义生成：
  
  ```c
  uint64 addrs[] = { KERNBASE, TRAPFRAME - 2*PGSIZE, TRAPFRAME, TRAMPOLINE,
                     MAXVA, 0xffffffffffffffffULL };
  ```

---

#### [AUD-037] `src/user/usertests.c` 中 `pipe1()` 与 `preempt()` 在错误分支误用 `return;` 代替 `exit(1);`，导致测试失败时被 `run()` 误判为 `OK`（假阳性）且残留死循环子进程

- **严重级别**：Medium
- **当前状态**：未修复
- **发现 Commit**：`07d59f3`
- **涉及文件与位置**：`src/user/usertests.c:768-771`（`pipe1`）、`src/user/usertests.c:862-865`（`preempt`）、`src/user/usertests.c:2332-2338`（`bigargtest`）、`src/user/usertests.c:2548-2556`（`sbrklast`）、`src/user/usertests.c:2690-2695`（`lazy_copy`）、`src/user/usertests.c:3081-3091`（`run`）
- **涉及架构 / 模块**：双架构通用 / 用户态回归测试框架 `usertests`
- **触发路径与现象**：
  1. `usertests` 的驱动函数 `run(void f(char *), char *s)`（`src/user/usertests.c:3081-3091`）在子进程中执行：
     
     ```c
     if(pid == 0) {
       f(s);
       exit(0);
     }
     ```
     
     并通过 `wait(&xstatus)` 是否为 `0` 判定测试输出 `"OK"` 还是 `"FAILED"`。
  2. **`pipe1()` 错误分支假阳性（L768-771）**：当管道读取字节校验不匹配（`(buf[i] & 0xff) != (seq++ & 0xff)`）时，代码打印 `"%s: pipe1 oops 2\n"` 后执行了 **`return;`** 而非 `exit(1);`！函数返回到 `run()` 后立即执行 `exit(0)`，不仅使父进程打印 `"OK"`（假阳性通过），还导致 `pipe1` 的写端孙进程未被 `wait()` 回收。
  3. **`preempt()` 错误分支假阳性与失控死循环进程泄漏（L862-865）**：`preempt()` 先 `fork()` 了 3 个执行 `for(;;);` 死循环的子进程 `pid1`、`pid2`、`pid3`；当 L862 `if(read(pfds[0], buf, sizeof(buf)) != 1)` 失败时，代码打印 `"%s: preempt read error"` 后执行了 **`return;`** 而非清理并 `exit(1);`！这不仅让 `run()` 执行 `exit(0)` 报告 `"OK"`，而且直接跳过了 L868-874 的 `kill(pid1); kill(pid2); kill(pid3); wait(0); ...`，导致 3 个死循环子进程被 `init` 收养并永久占满 CPU 与进程槽位！
  4. **测试临时文件与句柄未清理**：`bigargtest()`（L2337）结束后未调用 `unlink("bigarg-ok")`；`sbrklast()`（L2551-2555）第二次 `open(p, O_RDWR)` 后既未 `close(fd)` 也未 `unlink(p)`（遗留文件 `"x"`）；`lazy_copy()`（L2690-2694）循环创建 `"junk"` 后未调用 `unlink("junk")`。
- **根因分析**：
  `pipe1()` 与 `preempt()` 沿用了早期在 `main()` 中内联运行时的 `return` 写法，迁入 `run()` 子进程框架后未改为 `exit(1)`；后新增的 `sbrklast`/`lazy_copy` 遗漏了收尾 `unlink()`。
- **潜在影响**：
  一旦管道传输或抢占测试发生回归故障，`usertests` 仍会输出 `OK`；且在 `usertests -c` 连续压力模式下，残留文件与泄漏句柄会干扰后续轮次。
- **具体修复建议**：
  1. 将 `src/user/usertests.c:770` 的 `return;` 改为 `exit(1);`；
  2. 在 `src/user/usertests.c:864` 的 `preempt read error` 分支中先执行 `kill(pid1); kill(pid2); kill(pid3); wait(0); wait(0); wait(0);` 再调用 `exit(1);`；
  3. 在 `bigargtest()`、`sbrklast()`、`lazy_copy()` 末尾补齐 `close(fd)` 与 `unlink()`。

---

#### [AUD-038] Web 控制台“快速模式”模拟器 `web/server/demo-shell.js` 的预置镜像文件、FAT32 大小写敏感性、父挂载点卸载及 `rm .` 语义与真实内核不一致

- **严重级别**：Low
- **当前状态**：未修复
- **发现 Commit**：`07d59f3`
- **涉及文件与位置**：`web/server/demo-shell.js:71-85, 153-156, 250-258, 302-311`（`createBackendStore`、`DemoShell`）
- **涉及架构 / 模块**：Web 调试控制台快速演示模拟器（`web/server/demo-shell.js`）
- **触发路径与现象**：
  1. **预置文件集合与真实磁盘镜像不符（L71-85）**：`createBackendStore('ext2')` 预置的是 `/hello`、`/lost+found`、`/uop.txt`（而真实 `ext2.img` 在 `src/Makefile:255-258` 中写入的是 `/hello.c`、`/verylongfilename.txt`、`/hostbig.bin`）；`createBackendStore('fat32')` 预置的是 `/HELLO.TXT`、`/TESTS`（而真实 `fat32.img` 在 `src/mkfs/mkfat32.c:97-112` 中写入的是 `/longfilename.txt`、`/ShortLong.txt`）。用户在“快速模式”与“真实 QEMU 模式”之间切换执行同一条 `cat /mnt/hello.c` 或 `cat /mnt/longfilename.txt` 时结果截然不同。
  2. **FAT32 路径匹配大小写敏感（L80-84, L153-156）**：真实内核的 `fat32_locate_entry()` 支持大小写不敏感查找且 `fat32_name_from83()` 将 8.3 短文件名转为小写展示；而 `DemoShell` 直接使用大小写敏感的 JavaScript `Map` 存储 `'/HELLO.TXT'`，执行 `cat /mnt/hello.txt` 会报错 `cat: cannot open`。
  3. **缺少子挂载点占用检查与 `rm .` / 删除当前工作目录拦截（L250-258, L302-311）**：
     - 在 `umount`（L252）中仅检查 `this.cwd === target || this.cwd.startsWith(target + '/')`，未像内核 `vfs_has_child_mount()`（`src/kernel/vfs.c:278-289`）那样拦截“卸载仍包含活跃子挂载点的父挂载点”；
     - 在 `rm`（L305）中未拦截 `args[0] === '.'` / `'..'` 或 `this.cwd === target`，允许用户在快速模式下执行 `cd /tests` 后通过 `rm .` 或 `rm /tests` 将当前所在目录直接删除。
- **根因分析**：
  `DemoShell` 使用平面 `Map` 模拟文件系统时，未同步近期内核在 `ext2.img`/`fat32.img` 上的 LFN 测试夹具变更及 `vfs_umount`/`sys_unlink` 的边界约束。
- **潜在影响**：
  用户在 Web 教学台中对比“快速模式”与“真实 QEMU 模式”时会遇到命令兼容性与语义不一致问题。
- **具体修复建议**：
  1. 在 `createBackendStore()` 中对齐真实 `ext2.img` 与 `fat32.img` 的预置文件，并在 `backend === 'fat32'` 时对键名做大小写不敏感匹配与 14 字符截断别名匹配；
  2. 在 `DemoShell` 的 `umount` 中增加子挂载点检查（拒绝存在 `otherMount.startsWith(target + '/')` 的卸载），并在 `rm` 中禁止删除 `.`、`..` 以及 `this.cwd` 及其祖先目录。

---

#### [AUD-047] `tests/full_usertests.sh` 管道左端 `sleep 780` 在 QEMU 提前退出时阻塞进程组导致 13 分钟假死，且 `src/test-xv6.py` 的 `QEMU.read()` 首包后清零 `deadline` 截断超时窗口

- **严重级别**：Medium
- **当前状态**：未修复
- **发现 Commit**：`07d59f3`
- **涉及文件与位置**：
  - `tests/full_usertests.sh:38-63`
  - `src/test-xv6.py:66-76, 94-109, 169-206, 246-247`
- **涉及架构 / 模块**：双架构通用 / 全量回归与崩溃恢复测试运行器（`tests/full_usertests.sh`、`src/test-xv6.py`）
- **触发路径与现象**：
  1. **`tests/full_usertests.sh` 管道左端 `sleep 780` 阻止子 Shell 在 QEMU 异常退出时终止（`tests/full_usertests.sh:38-62`）**：
     
     ```bash
     setsid bash -c '
       cd "$1" || exit 1
       ( sleep 8; printf "usertests\n"; sleep 780 ) | timeout 800 make ARCH="$2" CPUS=3 qemu
     ' _ "$XV6_DIR" "$ARCH" > "$LOG" 2>&1 &
     runpid=$!
     ...
     for i in $(seq 1 165); do
       if grep -qE "ALL TESTS PASSED|SOME TESTS FAILED|FAILED -- lost|panic:|kerneltrap" "$LOG" 2>/dev/null; then
         break
       fi
       if ! kill -0 "$runpid" 2>/dev/null; then
         break
       fi
       sleep 5
     done
     ```
     
     在 Bash 管道 `A | B` 中，管道整体要等到左端 `A` 与右端 `B` **全部退出**后才会结束。当右端 `make ARCH="$2" CPUS=3 qemu` 因故提前退出（例如 `check-qemu-version` 失败、QEMU 启动报错退出、或顶层 `usertests` 进程因未预期的 `usertrap(): unexpected ...` / `exec usertests failed` 退出而未输出 `grep` 监听的 5 个关键字）时，左端子 Shell 正处于 `sleep 780`（期间不向管道写数据，因此不会收到 `SIGPIPE`）。这导致外层 `setsid bash -c`（`$runpid`）在 QEMU 早已退出的情况下仍持续存活 780 秒（13 分钟），主循环 `kill -0 "$runpid"` 恒为真，白白挂起 13 分钟直到超时！此外，L40 未等待 `$ ` 提示符而是硬编码 `sleep 8; printf "usertests
     "`，在慢速环境下若内核启动加 `ireclaim()` 耗时超过 8 秒会导致命令丢失。
  2. **`src/test-xv6.py` 的 `QEMU.read()` 在读到首块字节后立即将 `deadline` 重置为当前时间，截断剩余等待窗口（`src/test-xv6.py:94-109`）**：
     
     ```python
     def read(self, timeout=0.5):
         fd = self.proc.stdout.fileno()
         deadline = time.time() + timeout
         while True:
             rem = max(0.0, deadline - time.time())
             r, _, _ = select.select([fd], [], [], rem)
             if not r:
                 break
             buf = os.read(fd, 4096)
             ...
             # Keep draining while more bytes are immediately ready.
             deadline = time.time()
     ```
     
     在 L108，只要 `os.read()` 读到第一批串口输出（哪怕只有几个字符），`deadline` 就会被覆写为 `time.time()`，使下一轮 `select.select` 的等待时间 `rem` 变为 `0.0`。由于 QEMU 串口输出通过管道异步分片到达，微秒级的分片间隔就会让 `q.read()` 提前退出。在 `recover_log()`（L172, 177）、`forphan()`（L186）、`dorphan()`（L196）和 `recover_orphan()`（L203）中，脚本仅调用一次 `q.read()` 紧接着执行 `q.match(...)`，极易因串口行只读出一半而发生假阴性匹配失败。
  3. **`QEMU.crash()` 仅查找 `self.proc.pid`（`make`）的直接子进程 `kids[0]`（`src/test-xv6.py:66-76`）**：
     `ps -opid --no-headers --ppid <make_pid>` 只查找 `make` 的直接子进程并取 `kids[0]` 发送 `SIGKILL`；若 `make` 通过子 Shell 启动 QEMU 或存在多个子进程，`SIGKILL` 可能杀死外层 Shell 而非 `qemu-system-*` 进程本身。
- **根因分析**：
  `full_usertests.sh` 使用匿名管道配合长 `sleep` 保持 QEMU 标准输入打开，忽视了右端进程提前退出时左端 `sleep` 不会收到 `SIGPIPE` 的进程生命周期问题；`test-xv6.py` 在 `read()` 中用 `deadline = time.time()` 强行将超时等待降级为非阻塞轮询。
- **潜在影响**：
  当 QEMU 启动失败或 `usertests` 主进程异常退出时，CI / 本地预推送门禁 `full_usertests.sh` 会无响应卡死 13 分钟；`test-xv6.py crash` 在高负载 CI 上易出现偶发超时或假败。
- **具体修复建议**：
  1. 将 `tests/full_usertests.sh` 改为复用 `tests/runner/run_qemu_case.py --prompt-wait`（或在管道右端 QEMU 退出时立即 `kill` 左端喂入进程，并在失败监听正则中加入 `usertrap\(\): unexpected|machine error|exec usertests failed`）；
  2. 在 `src/test-xv6.py:108` 中将 `deadline = time.time()` 改为小幅滑动静默窗口（如 `deadline = min(deadline, time.time() + 0.05)` 或保持原 `deadline` 直到超时/行完整），并在 `crash()` 中按进程名匹配 `qemu-system-*` 发送 `SIGKILL`。

---

#### [AUD-051] 并发压力测试程序 `src/user/grind.c` 中子进程级联 `fork()` 未回收孙进程，且 `iter()` 在子进程报错退出（`st1 != 0`）时仍以 `exit(0)` 返回

- **严重级别**：Low

- **当前状态**：未修复

- **发现 Commit**：`07d59f3`

- **涉及文件与位置**：`src/user/grind.c:116-126, 157-180, 297-351`（`go`、`iter`、`main`）

- **涉及架构 / 模块**：双架构通用 / 用户态并发系统调用压力测试（`grind.c`）

- **触发路径与现象**：
  
  1. **级联 `fork()` 产生未同步孤儿孙进程（`src/user/grind.c:117-126, 163-180`）**：
     在 `what == 14` 与 `what == 19` 分支中，子进程 `if(pid == 0)` 连续调用两次无 `wait` 的 `fork(); fork();`（共产生 1 个子进程 + 2 个孙进程 + 1 个曾孙进程），随后各自直接 `exit(0)`（或在 `what == 19` 中并发读写同一管道后 `exit(0)`）。而外层只执行了一次 `wait(0)` 回收直接子进程 `pid`，其余 3 个后代进程全部变成孤儿进程被 `init` 收养。
  
  2. **`iter()` 与 `main()` 吞没子进程失败退出码（`src/user/grind.c:324-350`）**：
     
     ```c
     int st1 = -1;
     wait(&st1);
     if(st1 != 0){
       kill(pid1);
       kill(pid2);
     }
     int st2 = -1;
     wait(&st2);
     
     exit(0);
     ```
     
     在 `go()` 中存在大量一致性断言（如 L200 `create c failed`、L214 `fstat reports wrong size`、L218 `fstat reports crazy i-number`、L289 `exec pipeline failed`），断言失败时子进程会打印错误并调用 `exit(1)`。然而 `iter()` 捕获到 `st1 != 0` 并杀死另一子进程后，在 L333 却固定调用 `exit(0)`，且 `main()` 在 L346 调用 `wait(0)` 丢弃状态后继续下一轮 `while(1)` 循环，导致测试失败后不会以非零退出码终止 `grind`。

- **根因分析**：
  `iter()` 回收两个并发 worker 后未将非零 `st1` / `st2` 传递给 `exit()`，且 `main()` 未检查 `iter()` 的退出状态。

- **潜在影响**：
  若在自动化脚本中运行 `grind` 进行稳定性验证，即使触发了 `fstat` 大小错误或管道 `exec` 校验失败，`grind` 也不会终止报错，而是继续无限循环刷屏掩盖错误现场。

- **具体修复建议**：
  在 `iter()` 末尾若 `st1 != 0 || st2 != 0` 则调用 `exit(1)`，并在 `main()` 中检查 `int st; wait(&st); if(st != 0) exit(1);`。

---

#### [AUD-053] Web 前端与 AI 追踪器未剥离命令路径前缀（如 `/mnt/echo`、`/cat`）导致规则链退化与源码证据漏采，且 `real` 模式下未同步 `Ctrl-U`/`Ctrl-C` 行清空与失败命令状态

- **严重级别**：Medium
- **当前状态**：未修复
- **发现 Commit**：`07d59f3`
- **涉及文件与位置**：
  - `web/server/ai-trace.js:74-76, 102-121`（`commandName`、`collectEvidence`）
  - `web/src/traces.js:352-407`（`traceCommand`、`updateTeachingState`）
  - `web/src/App.jsx:408-418, 498-511`（`terminal.onData`、`runExample`）
- **涉及架构 / 模块**：Web 调试控制台命令追踪与 AI 源码证据检索（`web/server/ai-trace.js`、`web/src/`）
- **触发路径与现象**：
  1. **带路径前缀的可执行命令（如 `/mnt/echo`、`/fat/echo`、`/cat`）无法匹配规则链且导致 AI 追踪器漏采用户程序源码证据（`web/server/ai-trace.js:74, 102-108`、`web/src/traces.js:354-365`）**：
     - 在 `tests/semantics/cmds.sh` 及跨挂载点执行场景中，常用 `/mnt/echo EXT2_EXEC_OK`、`/fat/echo FAT32_EXEC_OK` 或 `/cat /mnt/hello.c` 测试跨文件系统执行。
     - `web/src/traces.js:354` 直接取 `const command = args.tokens[0] || 'sh'` 与 `'ls'`/`'cat'`/`'echo'` 严格全等比较，未取 `basename`；
     - `web/server/ai-trace.js:74, 102-103` 的 `commandName()` 返回 `"/mnt/echo"` 并拼接 `const programFile = "src/user//mnt/echo.c"`，因文件不存在而彻底跳过该用户程序 `main()` 及其所调用系统调用（`usys.pl`、`syscall.c`、`sysfile.c`）的证据采集！
  2. **`real` QEMU 模式下前端 `commandBufferRef` 未处理 `Ctrl-U`（清行）与 `Ctrl-C`，且 `runExample()` 未清空残留预输入（`web/src/App.jsx:408-418, 498-511`）**：
     - 在 `App.jsx:408-418` 中，`terminal.onData` 仅处理了 `
       `/`
       `、退格 ``/`` 以及 `char >= ' '`。当用户在真实 QEMU 终端中输入错误命令前缀后按 `Ctrl-U`（``，内核 `consoleintr` 会清空当前输入行）重新输入正确命令时，`commandBufferRef.current` 未被清空，导致回车后提交给左侧调用链和 AI 分析器的命令是“旧错误前缀 + 新命令”的拼接串；
     - 在 `runExample()`（L498-511）中，点击“教学命令”前未先发送 `` 或清空 `commandBufferRef.current`，若用户此前在终端框内键入过未回车的半截字符，教学命令会直接拼在半截字符后面发送并执行。
  3. **`real` QEMU 模式下 `updateTeachingState()` 无条件假定 `cd` / `mount` / `umount` 成功（`web/src/traces.js:384-407`）**：
     在 `real` 模式下 `commitCommand()` 传入的 `result` 恒为 `null`。`updateTeachingState()` 在 `result == null` 时无条件按命令字面更新 `next.cwd` 和 `next.mounts`；即使 QEMU 内核中 `mount ext2 /no_such_dir` 失败或 `umount /mnt` 因 `busy` 被拒，前端仍会错误更新挂载状态表，导致后续所有命令的后端归属（`xv6` vs `ext2` vs `FAT32`）全部推导错误。
- **根因分析**：
  前端与 AI 证据收集器假设命令首词必为不含 `'/'` 的裸程序名；`real` 模式仅靠前端按键镜像维护命令缓冲与虚拟挂载状态，未处理行编辑控制符与命令失败输出。
- **潜在影响**：
  执行带路径命令或使用 `Ctrl-U` 修改输入后，左侧命令执行链与 AI 源码分析失真；在真实 QEMU 模式下演示错误路径测试（如 `mount` 失败、`umount` 忙拒绝）会污染前端挂载状态机。
- **具体修复建议**：
  1. 在 `ai-trace.js:commandName()` 与 `traces.js:traceCommand()` 中统一提取首 token 的末级文件名（`.split('/').pop().replace(/^_/, '')`）用于匹配用户程序源码与规则分支；
  2. 在 `App.jsx:410` 的 `terminal.onData` 中当遇到 `char === ''`（`Ctrl-U`）或 `char === ''`（`Ctrl-C`）时清空 `commandBufferRef.current = ''`，并在 `runExample()` 开头先清空缓冲；
  3. 在 `real` 模式下根据终端回显（如匹配 `mount: .*: failed` / `umount: busy or invalid` / `cannot cd`）回滚或校正 `teachingStateRef`。
