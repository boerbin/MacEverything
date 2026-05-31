# 179 - 修复竞品分析文档配图相对路径

## 背景

在「配图收尾」（changelog 178）为 `docs/research/competitive_analysis.md` 插入 hero 配图时，
图片引用写成了 `![...](images/hero-competitive-analysis.png)`。

但该文档位于 `docs/research/` 子目录，而 PNG 实际存放在 `docs/images/`。
Markdown 相对路径以**文档自身所在目录**为基准解析，因此：

- 文档目录：`docs/research/`
- 引用 `images/hero-competitive-analysis.png` → 解析为 `docs/research/images/hero-competitive-analysis.png`
- 该路径**不存在**（`docs/research/` 下没有 `images/` 子目录）→ 图片无法显示（断链）

这是配图收尾任务最终校验环节暴露出来的唯一缺陷：12 张配图引用中，
其余 11 张所在文档都直接位于 `docs/`，路径 `images/...` 正确；唯独这一张所在文档下沉了一级目录。

## 根因

相对路径基准目录判断错误：插入引用时套用了 `docs/` 下文档的写法（`images/...`），
未考虑 `competitive_analysis.md` 实际位于 `docs/research/`，需要先 `../` 回到 `docs/` 再进入 `images/`。

属于路径计算错误，而非图片缺失——PNG 文件本身存在且正确（2.57 MB，1536×1024）。

## 修复

将引用从相对路径 `images/hero-competitive-analysis.png` 改为 `../images/hero-competitive-analysis.png`：

```diff
-![...竞品全景...](images/hero-competitive-analysis.png)
+![...竞品全景...](../images/hero-competitive-analysis.png)
```

`docs/research/` + `../images/` → `docs/images/hero-competitive-analysis.png` ✓（文件存在）。

## 验证

1. **路径解析校验**：脚本模拟 Markdown 相对路径解析，确认 `docs/research/` 下的
   `../images/hero-competitive-analysis.png` 解析到真实存在的 `docs/images/hero-competitive-analysis.png`。
2. **全量复扫**：对所有面向人文档（排除 `performance_ana/`、`benchmark/`、`changelog/` 机器报告）
   的 12 处图片引用做解析校验，确认全部 OK，无其它同类断链。

## 影响范围

- 仅 1 个文档 1 行：`docs/research/competitive_analysis.md`
- 无代码、无构建影响；纯文档相对路径修正
