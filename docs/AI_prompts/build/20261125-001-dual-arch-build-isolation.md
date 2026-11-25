# 双架构构建目录隔离评估与实施（build/riscv 与 build/loongarch）

- **日期**：2026-11-25
- **作者**：ffbecn
- **工具**：Antigravity CLI
- **模型**：Gemini 4 Neon（High）

## 任务说明

全面评估将 `src/Makefile` 的 `.arch-$(ARCH)` 覆盖式构建改造为按架构隔离的 `build/riscv/` 与 `build/loongarch/` 目录结构的影响面；确认改动集中在 `src/Makefile`、`src/mkfs/mkfs.c`、`src/test-xv6.py`、顶层 `Makefile`、`tests/*/run.sh` 及 `Dockerfile`，且通过 `.PHONY` 兼容目标保持全部既有命令与 GitHub Actions CI 工作流零破坏后，直接落地实施并完成双架构验证。

## Prompt

~~~text
4. 评估并优化双架构构建目录
目前两个架构的编译产物采用 .arch-$(ARCH) 方式区分。

需要评估是否改成：
build/
├── riscv/
│   └── kernel/
└── loongarch/
    └── kernel/


这样两个架构的编译产物完全隔离，避免相互覆盖。
- 如果改动范围小，而且不会破坏现有测试脚本和 CI，就直接实施。
- 如果涉及太多地方，就先提出最小改造方案，不要贸然修改。
~~~
