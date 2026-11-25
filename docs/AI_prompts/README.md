# AI Prompt Records

## 1. 记录范围

仅记录具有实际工程价值的关键提示词,普通问答、简单代码生成及重复提示词无需记录。

## 2. 目录分类

按照提示词涉及的技术领域分类存储。

- 优先使用已有分类目录。

- 没有合适分类时，创建新的目录。

- 新建分类后，同步更新本 README 中的分类说明。

- 连续的提示词可以保存在同一个md文件

- 目录名称使用简洁、明确的英文技术术语。

## 3. 命名规范

`YYYYMMDD-NNN-brief-description.md`

示例：`20261124-001-microkernel-architecture.md`

## 4. 记录格式

```text
# 标题

- **日期**：YYYY-MM-DD
- **作者**：Git 用户名
- **工具**：AI 工具名称
- **模型**：模型名称

## 任务说明

简要说明任务背景。

## Prompt

记录实际使用的完整关键提示词。
```

## 5. 记录原则

1. 保留实际使用的原始提示词。

2. 仅记录关键交互，避免重复和冗余。

3. 不得包含 API Key、密码等敏感信息。

4. 所有记录纳入 Git 版本管理。

## 6. 已有分类

- [architecture](architecture/20261124-001-microkernel-architecture.md) 保存架构取舍、基线探索与验证设计的关键提示词。
- [agent-setup](agent-setup/20261125-001-repo-agent-config-refinement.md) 保存仓库级 `AGENTS.md`、`.agents/skills/` 与工程协作约束配置的关键提示词。
- [debugging](debugging/20261125-001-baseline-bugs-and-ci-fixes.md) 保存基线缺陷分阶段修复与 CI/运行时根因排查的关键提示词。
- [documentation](documentation/20261125-001-docs-cleanup-and-root-readme.md) 保存项目文档体系整理与长期维护文档重构的关键提示词。
- [build](build/20261125-001-dual-arch-build-isolation.md) 保存双架构构建系统、产物隔离与编译流水线改造的关键提示词。
