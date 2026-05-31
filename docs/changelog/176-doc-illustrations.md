# 176 - 文档配图：用 gpt-image-2 生成发布级插图

- **类型**：docs
- **日期**：2026-05-31
- **分支**：`docs/doc-illustrations`
- **影响文件**：4 个文档 + 8 张 PNG + changelog

## 背景与动机

各技术分享/调研文档此前只有 ```dot/```graphviz/```mermaid 代码块（精确技术图），缺少
封面与概念级配图，可读性与传播性不足。本次用 `gpt-image-2` skill（codex CLI + GPT Image 2，
delegated model `gpt-image-2`，model `gpt-5.5`）为关键文档生成**发布级编辑插图**。

**策略（叠加而非替换）**：保留所有现有 dot/graphviz 精确技术图不动（它们承载标签/框线的准确性），
**额外**为文档生成 hero 封面 + 关键章节概念信息图，用 `![](images/...)` 插入。这既符合
「MD 用 dot/Mermaid」的既有偏好，又发挥 GPT Image 2 在封面/概念图上的真正强项，规避其
"画歪技术标签" 的弱点。

## 范围（做什么）

### 新增 8 张图（`docs/images/`，1536×1024，共 ~10MB）

| 文件 | 用途 | 落点 |
|------|------|------|
| `hero-architecture.png` | 架构文档 hero 封面 | architecture_deep_dive.md 顶部 |
| `infographic-three-layer.png` | 三层架构概念图 | §2 整体架构（graphviz 之前，概念→精确） |
| `infographic-short-query-cache.png` | 短查询缓存 O(1) 0.37ms | §9 ShortQueryCache |
| `infographic-ai-search.png` | AI 自然语言搜索管线 | §14 内置 LLM |
| `hero-agent-driven.png` | Agent 开发文档 hero（4 项指标） | agent_driven_development.md 顶部 |
| `infographic-agent-workflow.png` | 开发工作流闭环（规划→TDD→worktree→评审→合并） | §6 方法论 |
| `hero-ctr-survey.png` | CTR 调研报告 hero | ctr_model_survey.md 顶部 |
| `hero-ctr-plan.png` | CTR 序列建模规划 hero | ctr_sequential_modeling_plan.md 顶部 |

### 顺带纳入版本控制

`tech_sharing_agent_driven_development.md`、`ctr_model_survey.md`、`ctr_sequential_modeling_plan.md`
此前是**未跟踪**的 WIP 文档。为给它们配图并持久化，本次一并纳入 git
（符合「禁止在未纳入版本控制的路径上长期开发」）。

## 实施流程

1. **意图澄清**：发现文档无任何 PNG/`![]()`，全是代码图 → 与用户确认采用"叠加编辑级配图"策略，
   范围为两个 tech_sharing 文档 + 扫描全 docs（过滤掉 666 个 performance_ana 机器报告等，
   只取人面向的顶层文档）。
2. **冒烟测试**：先生成 1 张 hero 验证 codex→PNG 管线（auth/网络/配额）可用，再并行扩展。
3. **并行生成**：7 张图并行 `run_in_background` 生成，全部 `ok:true`。
4. **逐张视觉 QA**：用 Read 查看全部 8 张，确认标签准确、配色合规、无 GPT Image 2 常见的
   "标签画歪/框线错乱"，技术内容（702 key、O(1) 0.37ms、模型名、4 项指标、DIN/DIEN/BST/SIM、
   3-phase 路线图）均正确。
5. **插入引用 + 校验**：8 处 `![](images/...)`，grep 校验全部解析到真实文件，零断链。

## 提示词留痕

每张图的提示词保存在 `docs/images/_prompts/*.txt`（遵循 skill 的 ≥50 词 + 必含 ≥4 个风格关键词 +
统一调色板 #2563EB/#F97316/#1E293B/#10B981 + negative prompt 规则），可复现/可迭代。

## 验证

- 8 张 PNG 均为有效 `PNG image data, 1536 x 1024`，生成日志全部 `ok: true / Success`
- 8 处图片引用 grep 校验零断链
- 纯文档/资源变更，不涉及 C++/Swift 代码与运行时行为，无需 `make dmg` / HTTP 功能验收

## 备注（仓库体积）

8 张 PNG 共约 10MB 进入 git 历史。考虑到是发布级分享文档的长期资产、且数量少，可接受；
若后续配图增多，建议评估 Git LFS 或外部资源托管。
