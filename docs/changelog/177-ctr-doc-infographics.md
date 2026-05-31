# 177 - CTR 文档配图补全：演进时间线 + 落地路线图

- **类型**：docs
- **日期**：2026-05-31
- **分支**：`feat/ctr-infographics`
- **影响文件**：2 个 CTR 文档 + 2 张 PNG + 2 个提示词 + changelog

## 背景与动机

上一轮配图（changelog 176）为两个 `tech_sharing_*` 文档各配了 **hero 封面 + 关键章节信息图**，
但两个 CTR 文档只配了 **hero 封面**，缺少章节级概念图，配图密度不对等。本次将两个 CTR
文档补齐到与 tech 文档**同等的配图水平**，延续 176 确立的"叠加而非替换"策略：保留所有
现有 Markdown 表格/ASCII 时间线不动，**额外**用 `gpt-image-2` 生成发布级信息图作为概览。

## 范围（做什么）

### 新增 2 张信息图（`docs/images/`，1536×1024）

| 文件 | 用途 | 落点 |
|------|------|------|
| `infographic-ctr-evolution.png` | CTR 模型演进时间线（4 个时代 × 16 个模型） | `ctr_model_survey.md` §2 经典模型演进（章节简介之后） |
| `infographic-ctr-roadmap.png` | 序列建模 16 周落地路线图（3 Phase + 3 Go/No-Go 门） | `ctr_sequential_modeling_plan.md` §四 推荐技术方案（章节标题之后） |

### 内容准确性（逐张视觉 QA）

- **演进时间线**：4 个时代分组准确（SHALLOW / DEEP DUAL-PATH / SEQUENCE-INTEREST /
  SYSTEM·MULTI-TASK），16 个模型及年份与 §7 对比总结表一致（FM 2010、FFM 2016、
  DeepFM 2017、xDeepFM 2018、DIN 2018、DIEN 2019、BST 2019、SIM 2020、DLRM 2019、
  MMOE 2018、PLE 2020、STAR 2021），含橙色"模型复杂度递增"箭头。
- **落地路线图**：周标尺 1–16，三段 Phase（DIN W1-4 蓝 / BST W5-8 橙 / SIM·SDIM W9-16 绿），
  子阶段 chip（数据/开发/评估/AB），三道琥珀色 Go/No-Go 决策门含"2wk 观测"，
  详情矩阵（数据/开发/评估/AB 测试/产出/目标）的 CTR 目标 +5%/+10%/+15% 与 §4 正文一致。

## 实施流程

1. **盘点差距**：对比 176 产出，确认 CTR 文档配图密度不对等 → 补章节级信息图至对等。
2. **读取数据源**：从 §7 对比总结表抽取模型/年份、从 §4.7 时间线总览抽取周次/Phase/门控，
   写入提示词以确保标签准确。
3. **并行生成**：2 张图 `run_in_background` 并行生成，均 `EXIT=0`。
4. **逐张视觉 QA**：Read 查看两张图，确认标签准确、配色合规（#2563EB/#F97316/#10B981/
   #F59E0B/#1E293B/#94A3B8）、无 GPT Image 2 常见的标签错乱。
5. **插入引用 + 校验**：2 处 `![](images/...)`，grep 校验全部解析到真实文件，零断链。

## 提示词留痕

`docs/images/_prompts/infographic-ctr-evolution.txt` 与 `infographic-ctr-roadmap.txt`
（遵循 skill 的 ≥50 词 + ≥4 风格关键词 + 统一调色板 + negative prompt 规则），可复现/可迭代。

## 验证

- 2 张 PNG 均为有效 1536×1024 PNG，生成日志 `EXIT=0 / Success`
- 全 docs 图片引用 grep 校验零断链（含本次 2 处与既有 8 处共 10 处）
- 纯文档/资源变更，不涉及 C++/Swift 代码与运行时行为，无需 `make dmg` / HTTP 功能验收

## 已知取舍

演进图中 LR/POLY2 标注年份 "2010"，而 §7 表格此处为 "-"（这两个模型无公认单一发表年）；
鉴于权威对比表就在配图下方、且 2010 作为时代锚点可辩护，未为此单点重绘整张布局完好的图。
