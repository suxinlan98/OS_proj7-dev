# 项目文档整理与根目录 README 重写

- **日期**：2026-11-25
- **作者**：ffbecn
- **工具**：Antigravity CLI
- **模型**：Gemini 4 Neon（High）

## 任务说明

修复 `docs/baseline/README.md` 与 `traps.md` 中失效/过时文档引用，更新 `docs/architecture/README.md` 明确 57 项基线缺陷已全部前置修复，并重写根目录 `README.md`（仅保留长期有效的项目介绍、目录组织以及 RISC-V / LoongArch64 双架构构建与验证指南）。

## Prompt

~~~text
1. 整理项目文档
- 修复 docs/baseline/README.md 中失效的链接。
- 更新架构文档，明确说明 57 个基线缺陷已经全部提前修复。
- 重写根目录 README.md，介绍项目是什么、目录怎么组织，以及 RISC-V 和 LoongArch 双架构如何构建和验证。
- README 只保留长期有效的信息，不记录开发过程。
~~~
