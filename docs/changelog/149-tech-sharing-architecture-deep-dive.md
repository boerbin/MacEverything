# 149 - 架构深度剖析技术分享文档

## 变更类型
文档新增

## 变更内容
新增 `docs/tech_sharing_architecture_deep_dive.md`，面向工程/系统/算法同学的架构深度剖析技术分享文档。

### 文档结构（16 节 + 3 附录）
1. 系统架构全景 — 四层架构（SwiftUI → ObjC++ Bridge → C++20 Core → macOS Kernel）
2. SoA 列式数据模型 — 结构体数组 vs 数组结构体的选型与缓存行分析
3. 扫描引擎 — getattrlistbulk 批量系统调用、两阶段启动策略
4. 搜索引擎多路径策略 — 子串/前缀/后缀/glob/正则的分路优化
5. Trigram 倒排索引 — 三字符组索引构建、竞争候选集选择算法
6. SIMD 向量化搜索 — ARM NEON first-last byte + 2x unroll 实现
7. 查询解析器 — 递归下降解析、AST 构建与优化
8. 持久化与 WAL — v6 Flat 格式、CRC32 校验、COW 三阶段压缩
9. FSEvents 实时监控 — 事件合并、批量变更处理
10. 并发模型 — 读写锁分离、batchMutate 300-op 分块、无锁查询取消
11. 内容搜索 — mmap 并行扫描、上下文提取
12. 性能演进 — 从 v1 到当前版本的优化时间线
13. 竞品对比 — vs Everything (Windows)、mdfind、fd、find
14. 设计得失 — 已验证决策与待改进方向
15. 演化方向 — 未来技术路线
16. 总结

附录：关键数据结构、性能基准、参考资料

## 实施过程
- 通过深入阅读 Core 层全部源码，提取架构设计意图和实现细节
- 结合已有性能基准测试数据和 benchmark 报告
- 以功能模块为主线，串联技术选型的 why/how/tradeoff

## 关联文件
- `docs/tech_sharing_architecture_deep_dive.md` (新增，1005 行)

## 附注
- 修复了 pre-commit hook 在 x86_64 git 下的架构不匹配问题（git 是 x86_64 导致 hook 通过 Rosetta 运行，使 clang++ 目标架构变为 x86_64，无法链接 arm64 的 re2 库）
- 修复方式：在 hook 中使用 `arch -arm64 make test-fast`
