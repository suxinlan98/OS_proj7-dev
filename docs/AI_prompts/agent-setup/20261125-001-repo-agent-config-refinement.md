# 仓库级 Agent 配置与领域 Skills 规范化重构

- **日期**：2026-11-25
- **作者**：ffbecn
- **工具**：Antigravity CLI
- **模型**：Gemini 4 Neon（High）

## 任务说明

依据 `repo-agent-setup` 规范重构仓库级全英文 [AGENTS.md](../../../AGENTS.md)、三个正交领域技能（[xv6-dual-arch-pitfalls](../../../.agents/skills/xv6-dual-arch-pitfalls/SKILL.md)、[xv6-fs-compatibility](../../../.agents/skills/xv6-fs-compatibility/SKILL.md)、[xv6-ipc-boundary](../../../.agents/skills/xv6-ipc-boundary/SKILL.md)）及配套触发评估样例（`.agents/evals/*.json`）。重点落地高质量低耦合约束、`-AI` 阶段性提交与工作区/远端 Git 安全规则、禁止面向测试脚本改答案、自维护过时文件、关键提示词记录、双架构 Tier 1 验证范围、三类技能严格正交切分以及独立工程批判原则。

## Prompt

### 1. 基于 `repo-agent-setup` 调整仓库级 Agent 配置
~~~text
根据repo skills setup 这个skills调整当前的配置，有以下要求：
1.保证高质量代码和低耦合度；
2.不能自行commit,如果是大任务执行过程中需要commit方便操作的话才能自行commit,且无需批准，不过后缀需要添加-AI
3.若没过测试脚本不要针对脚本评测面向答案修改，而是要找到根因并修复
4.agent可以自行更新滞后性文件或者错误说辞文件
5.全英文
~~~

### 2. 配置复审与边界、验证、Git/批判规则收敛
~~~text
我发现有些问题：
1.没有说明要记录关键提示词
2.Tier 1 的双架构验证范围不够明确
3.xv6-ipc-boundary 和 xv6-dual-arch-pitfalls 存在交叉领域
4.git的规则还不是很明确，要规定不要覆盖用户已有的未提交修改；不要擅自向远端推送
5.Eval 与 Skill 的验证要求有一个不一致
xv6-fs-compatibility/SKILL.md：
Run the focused VFS/filesystem suite ... on both architectures


但对应的 Eval JSON：
"verification_command": "make ext2 && make run"
6.不要将提示词视为真理，可以大胆反驳并提出更好的方案

其他你看看有没有需要补充优化，仍需要保持现在精简的风格
~~~
