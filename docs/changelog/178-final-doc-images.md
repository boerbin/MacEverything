# 178 - 配图收尾：竞品分析 hero + SIMD 基准信息图

- **类型**：docs
- **日期**：2026-05-31
- **分支**：`feat/final-doc-images`
- **影响文件**：2 个文档 + 2 张 PNG + 2 个提示词 + changelog

## 背景与动机

changelog 176/177 已为 4 个主力技术/CTR 文档配满插图。本轮做**收尾**：扫描 `docs/` 下
**剩余的、已纳入 git 跟踪的、面向人的文档**，发现还有 2 个零插图：

- `docs/research/competitive_analysis.md`（685 行）—— 竞品分析报告
- `docs/string_search_benchmark_report.md`（124 行）—— SIMD 字符串搜索基准报告

为它们各配 1 张发布级配图，达成清晰的收尾标准：**所有已跟踪的面向人文档均有配图**。
延续"叠加而非替换"策略，保留所有现有表格/正文不动，仅**额外**插入概览配图。

> 注：`docs/semantic-search/*.md`（performance_report、requirements）仍是**未跟踪 WIP**，
> 本轮不纳入——把更多 WIP 入库并配图超出"更新插图"的范围，留待其正式落地后处理。
> `docs/performance_ana/`（666+ 个）、`docs/changelog/`、`docs/benchmark/` 等为机器报告，不配图。

## 范围（做什么）

| 文件 | 用途 | 落点 |
|------|------|------|
| `hero-competitive-analysis.png` | 竞品全景 hero | `competitive_analysis.md` H1 之后 |
| `infographic-simd-benchmark.png` | SIMD 吞吐量条形图 | `string_search_benchmark_report.md` 元信息块之后 |

### 内容准确性（逐张视觉 QA）

- **竞品 hero**：四平台分组准确（Windows=Everything / Linux=FSearch·plocate /
  macOS=Spotlight·Alfred·Find Any File / 跨平台=fd·ripgrep·fzf），中心搜索徽标 +
  trie 索引示意 + "10x+" 速度表盘；无真实品牌 logo（合规）。
- **SIMD 信息图**：**每个 GB/s 数值与正文测试表逐一一致**——
  单线程：NEON 2x=11.56 / NEON=4.93 / std::find=1.20 / memmem=1.05 / KMP=0.66 /
  Rabin-Karp=0.14；多线程（12 核）：NEON 2x+MT=74.30 / NEON+MT=50.42 /
  std::find MT=12.59 / memmem MT=9.60；含 LPDDR5 ~100-120 GB/s 带宽上限虚线 + "9.5×" 徽章。

## 实施流程

1. **全 docs 扫描**：统计每个顶层/子目录 md 的行数、`![]()` 数、代码图数，定位零插图的
   已跟踪人面向文档（排除机器报告与未跟踪 WIP）。
2. **读取数据源**：从竞品文档章节标题抽取平台/工具名、从基准报告结果表抽取算法吞吐量数值，
   写入提示词确保标签准确。
3. **并行生成**：2 张图 `run_in_background` 并行生成，均 `EXIT=0`。
4. **逐张视觉 QA**：Read 查看两张图，确认标签/数值准确、配色合规、无标签错乱。
5. **插入引用 + 校验**：2 处 `![](images/...)`，grep 校验全 docs 图片引用零断链。

## 提示词留痕

`docs/images/_prompts/hero-competitive-analysis.txt` 与 `infographic-simd-benchmark.txt`
（遵循 skill 的 ≥50 词 + ≥4 风格关键词 + 统一调色板 + negative prompt 规则）。

## 验证

- 2 张 PNG 均为有效 1536×1024 PNG，生成日志 `EXIT=0 / Success`
- 全 docs 图片引用 grep 校验零断链（本次 2 处 + 既有 10 处 = 共 12 处）
- 纯文档/资源变更，不涉及 C++/Swift 代码与运行时行为，无需 `make dmg` / HTTP 功能验收

## 配图任务全局收尾

至此 6 个面向人的已跟踪文档共有 **12 张发布级配图**：架构深度（4）、Agent 开发（2）、
CTR 调研（2）、CTR 序列建模（2）、竞品分析（1）、SIMD 基准（1）。所有现有
dot/graphviz/mermaid 精确技术图均原样保留。
