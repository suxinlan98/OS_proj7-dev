# 双架构基线 57 项缺陷分阶段修复与 CI 根因排查

- **日期**：2026-11-25
- **作者**：ffbecn
- **工具**：Antigravity CLI
- **模型**：Gemini 4 Neon（High）

## 任务说明

基于 [基线缺陷诊断清单](../../baseline/bugs.md) 与 [双架构工程陷阱手册](../../baseline/traps.md)，在 `fix/baseline-bugs` 分支上按第六节的四个阶段顺序逐阶段完成 57 项双架构（RISC-V / LoongArch64）基线缺陷的根因修复，保证低耦合与代码质量，完成全量回归与 PR 提交，随后进一步排查并根因修复 PR #1 的 5 项 GitHub Actions CI 失败。

## Prompt

### 1. 初始任务指令（原文）
~~~text
先从当前基线切一个新分支（例如 `fix/baseline-bugs`），然后阅读 `AGENTS.md` 和 `docs/baseline/` 下的 `README.md`、`bugs.md`、`traps.md`，按 `bugs.md` 第六节的阶段顺序逐阶段修复基线缺陷。途中可以自行commit,坚持根因修复，保证代码质量，完成后用gh提pr即可
~~~

### 2. 后续关键交互概括
- **代码质量与低耦合约束**：强调修复过程中禁止补丁式 `if` 嵌套与耦合度上升，按阶段提交（每阶段一次 commit），严格保证代码质量。
- **CI 失败根因排查与收尾**：针对 PR #1 中 5/6 个失败的 CI checks，要求定位根因并彻底修复（涵盖 GCC 13 `-Werror=maybe-uninitialized`、`loongarch_error` SMP 控制台输出交错、`AI_prompts` 历史断链、`web` 单测硬编码行号），全面复查无误后推送并补记关键提示词。
