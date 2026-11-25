# xv6 Dual-Architecture Microkernel (`OS_proj7-dev`)

面向 **RISC-V 64**（Sv39 / PLIC / VirtIO-MMIO）与 **LoongArch64**（4 级页表 / LS7A APIC + EXTIOI / VirtIO-PCI）双架构的 xv6 操作系统项目，在统一 HAL 与 VFS 之上支持 **xv6fs（含 WAL 日志）**、**ext2** 与 **FAT32（含 LFN）** 三种文件系统，并以微内核架构（用户态常驻文件系统/设备驱动服务、IPC 能力授权、服务故障隔离与自愈）为核心演进方向。

## 目录结构

```text
.
├── src/                  # 操作系统核心源码、用户态程序、镜像制作工具与崩溃恢复脚本
│   ├── kernel/           # 内核源码与双架构 HAL（含 arch/loongarch/）
│   ├── user/             # 用户态基础工具、文件系统测试程序与 usertests
│   ├── mkfs/             # xv6fs (mkfs) 与 FAT32 (mkfat32) 镜像生成工具
│   ├── Makefile          # 双架构内核、用户程序与复合磁盘镜像构建脚本
│   └── test-xv6.py       # 自动化 QEMU 驱动与 WAL 崩溃恢复测试脚本
├── tests/                # 11 套双架构自动化 QEMU 测试套件与通用判分器
│   ├── runner/           # 通用 QEMU 串口交互与断言运行器 (run_qemu_case.py)
│   ├── check_env.sh      # 宿主工具链与 QEMU 环境自检脚本
│   ├── run_all.sh        # 双架构常规测试套件串联回归脚本
│   └── full_usertests.sh # 单架构完整 usertests（含慢速压测）回归脚本
├── docs/                 # 架构设计、基线审计与工程约束文档
│   ├── architecture/     # 双架构微内核总体架构、IPC/能力 ABI 与故障恢复契约
│   ├── baseline/         # 原始基线缺陷清单 (bugs.md) 与工程陷阱手册 (traps.md)
│   ├── AI_prompts/       # 关键工程提示词归档
│   └── official/         # 赛事章程与赛题技术文档
├── web/                  # 双架构交互式终端与观测前端（Node.js + Vite）
├── .github/              # GitHub Actions 双架构 CI 工作流与静态仓库检查脚本
├── Dockerfile            # 预装双架构交叉工具链与 QEMU 8.2+ 的参考开发镜像
├── Makefile              # 顶层双架构测试准备、运行与全量验证入口
└── AGENTS.md             # 仓库级开发规范与分层验证约束
```

## 环境准备

### 1. 本地环境检查

项目依赖 `make`、`gcc`、`python3`、`perl`、`e2fsprogs`（`mkfs.ext2` / `debugfs`）、RISC-V 与 LoongArch64 交叉工具链以及 QEMU（`>= 7.2`，LoongArch64 推荐 `>= 8.2`）。运行环境自检脚本核对依赖：

```bash
bash tests/check_env.sh
```

### 2. Docker 参考环境

仓库根目录提供基于 Ubuntu 24.04 的 [`Dockerfile`](Dockerfile)，内置完整双架构交叉编译工具链与 QEMU：

```bash
docker build -t xv6-microkernel .
docker run --rm -it xv6-microkernel
```

## 双架构构建与运行

> `src/Makefile` 默认使用 `src/.arch-$(ARCH)` 标记当前目标架构；在同一工作树下切换架构构建与运行时请串行执行。

### RISC-V 64（默认）

```bash
# 编译内核与复合磁盘镜像（xv6fs + ext2 + FAT32）
make -C src ARCH=riscv kernel/kernel fs.img

# 启动 3 核 QEMU 交互终端（按 Ctrl-A X 退出）
make -C src ARCH=riscv CPUS=3 qemu
```

### LoongArch64

```bash
# 编译内核与复合磁盘镜像
make -C src ARCH=loongarch kernel/kernel fs.img

# 启动 3 核 QEMU 交互终端（按 Ctrl-A X 退出）
make -C src ARCH=loongarch CPUS=3 qemu
```

## 验证与测试

### 1. 静态仓库检查

校验 Markdown 相对链接、禁止追踪的构建产物/敏感文件及内核隔离规则：

```bash
python3 .github/scripts/check_repo.py
```

### 2. 定向 QEMU 测试套件

顶层 [`Makefile`](Makefile) 为每个测试套件提供 RISC-V 与 LoongArch64 两套目标（LoongArch64 以 `loongarch_` 为前缀，基线快速回归为 `test_baseline` / `loongarch_baseline`）：

| 套件目标（RISC-V） | 套件目标（LoongArch64） | 覆盖范围 |
| :--- | :--- | :--- |
| `test_baseline` | `loongarch_baseline` | `usertests -q` 快速基线回归 |
| `public_vfs` | `loongarch_public_vfs` | 跨文件系统 VFS 公共接口测试 |
| `ext2` | `loongarch_ext2` | ext2 挂载、读写、覆盖与删除 |
| `fat32` | `loongarch_fat32` | FAT32 8.3 / LFN 创建、读写与删除 |
| `error` | `loongarch_error` | VFS 非法参数与错误路径稳定性 |
| `semantics` | `loongarch_semantics` | 挂载父目录、`..` 与链接语义 |
| `persist` | `loongarch_persist` | 跨重启 ext2 / FAT32 数据持久化 |
| `smp` | `loongarch_smp` | 多核并发 I/O 与卸载压测 |
| `mount_lifecycle` | `loongarch_mount_lifecycle` | 挂载/卸载生命周期边界 |
| `remount_io` | `loongarch_remount_io` | 反复重挂载 I/O 一致性 |
| `mount_storm` | `loongarch_mount_storm` | 高频挂载/卸载风暴压测 |

示例（准备镜像并运行单套测试）：

```bash
make public_vfs && make run
make loongarch_public_vfs && make run
```

### 3. 全量双架构回归与崩溃恢复

```bash
# 全量双架构回归（环境检查 + 22 项 QEMU 套件 + 双架构完整 usertests）
make verify-all

# WAL 日志与孤儿 inode 崩溃恢复测试
(cd src && ARCH=riscv python3 test-xv6.py crash)
(cd src && ARCH=loongarch python3 test-xv6.py crash)
```

### 4. Web 控制台（可选）

Web 观测台位于 [`web/`](web/README.md)，支持演示模式与真实 QEMU 终端接入：

```bash
cd web && npm ci && npm test && npm run build
```

## 核心文档索引

- **架构设计**：[`docs/architecture/README.md`](docs/architecture/README.md) — 双架构微内核服务拓扑、IPC/能力 ABI、用户态驱动与故障恢复契约
- **基线审计与工程陷阱**：[`docs/baseline/README.md`](docs/baseline/README.md) — 原始基线缺陷清单（[`bugs.md`](docs/baseline/bugs.md)）与重构陷阱手册（[`traps.md`](docs/baseline/traps.md)）
- **开发与验证规范**：[`AGENTS.md`](AGENTS.md) — 分层验证矩阵、仓库不变量与安全边界
