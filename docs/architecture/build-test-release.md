# 构建、测试与发布架构

> 本文描述 MacEverything 的本地构建、测试、打包、动态库自包含校验与文档变更验收策略。

## 1. 总览

MacEverything 的工程链路分成两条主线：

1. **Core/CLI/Test 线**：由顶层 `Makefile` 直接调用 `clang++`，编译 C++20 核心、`MacEverything/Core/*.mm` Objective-C++ 对象、测试入口与 CLI/benchmark 工具；它不是完整 Bridge 分类文件的构建链路。
2. **App/Release 线**：由 Xcode 项目构建 SwiftUI app，并在发布阶段通过 `make dmg` 完成 app 构建、Homebrew dylib 捆绑、自包含校验与 DMG 生成。

这两条线共享同一批核心 C++ 源码、RE2/sqlite3 依赖、macOS framework，以及 vendored `llama.cpp` 静态库配置。Makefile 在前 16 行集中定义了编译器、C++20、macOS framework、RE2、sqlite3 与 llama.cpp 静态库输入，是构建图的根配置。
证据：`Makefile:1-16`

## 2. 构建图

### 2.1 Makefile 构建入口

顶层 `Makefile` 的核心变量包括：

- `CXX = clang++`
- `CXXFLAGS = -std=c++20 -O2 -Wall -Wextra`
- macOS frameworks：`CoreServices`、`Quartz`、`Foundation`、`AppKit`
- RE2：`/opt/homebrew/opt/re2`
- sqlite3：`-lsqlite3`
- llama.cpp 静态库：`vendor/llama.cpp/build/...`
- Metal/Accelerate：用于 llama.cpp 后端链接

这些配置共同服务于 `test_all`、`benchmark`、`maceverything-daemon` 以及 sanitizer 测试目标。
证据：`Makefile:1-16`

### 2.2 Objective-C++ 对象编译

Makefile 对 `MacEverything/Core/*.mm` 设置了单独规则：

- 使用 `-fobjc-arc`
- 显式包含 `MacEverything/Core`
- 先将 `.mm` 编译成 `.o`
- 再参与 C++ 测试、benchmark、daemon 链接

这说明测试二进制不是纯 C++，而是会把部分 ObjC++ Core 文件一起纳入链接。
证据：`Makefile:20-27`

### 2.3 Core 测试二进制

`test_all` 目标由三部分组成：

1. `test_all.cpp`
2. `MacEverything/Core/*.cpp`
3. `MacEverything/Core/*.mm` 编译后的对象

链接时同时引入：

- CoreServices / Quartz / Foundation / AppKit
- RE2
- sqlite3
- llama.cpp / ggml 静态库
- Metal / MetalKit / Accelerate

因此 `test_all` 是对核心引擎、Core 目录 ObjC++ 文件、AI 后端和平台 framework 的混合测试入口；Bridge 目录的 ObjC++ 分类由 Xcode app target 和 `lint-bridge` 的子集语法检查覆盖。
证据：`Makefile:26-27`

### 2.4 Benchmark 与 daemon

Makefile 还定义了两个独立构建目标：

- `benchmark`：编译 `benchmark.cpp` 与 Core 源码，用于独立性能测试。
- `maceverything-daemon`：编译 `MacEverything/CLI/daemon_main.cpp` 与 Core 源码，用于 CLI daemon。

二者复用 Core 源码、ObjC++ 对象、RE2、sqlite3、llama.cpp 和 macOS framework 链接配置。
证据：`Makefile:29-35`

## 3. Xcode 工程目标

### 3.1 Native targets

Xcode 项目包含三个 native target：

1. `MacEverything`：主应用，产物为 `MacEverything.app`
2. `MacEverythingUITests`：UI 测试 bundle
3. `MacEverythingMCP`：命令行工具 target

主 app target 依赖 MCP target，并包含 `Copy MCP Binary` 构建阶段。
证据：`MacEverything.xcodeproj/project.pbxproj:419-470`

### 3.2 App source composition

`MacEverything` app target 的 Sources build phase 同时纳入：

- SwiftUI app/UI 文件：`MacEverythingApp.swift`、`ContentView.swift`、`ResultRow.swift` 等
- Objective-C++ 桥接层：`MacSearchBridge.mm`、`MacSearchBridge+Content.mm`、`MacSearchBridge+Semantic.mm`
- Core 引擎：scanner、search engine、persistence、WAL、content index、HTTP server、service engine
- 查询解析：`QueryParser.cpp`、advanced/structured query
- AI 后端：`LiteLLMBackend.cpp`、`NLTranslator.cpp`、`LlamaBackend.cpp`、`ModelManager.cpp`
- 富文本提取：`RichTextExtractor.mm`

这意味着 Release app 是 SwiftUI、ObjC++ bridge、C++ Core、HTTP/service、AI 和 rich-text extraction 的单一产物。
证据：`MacEverything.xcodeproj/project.pbxproj:503-559`

### 3.3 Xcode Release build

推荐的 app 构建命令是：

```bash
DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer xcodebuild -project MacEverything.xcodeproj -scheme MacEverything -configuration Release build SYMROOT=build
```

Makefile 的 `app` 目标封装了同一命令；项目说明也将它列为标准构建方式。
证据：`Makefile:65-69`
证据：`CLAUDE.md:10-14`

## 4. 测试架构

### 4.1 测试组织规范

项目约定：独立测试实现应放在 `tests/test_xxx.h`，由 `test_all.cpp` 通过 `#include` 引入。`test_all.cpp` 只负责 include 测试模块、CLI 参数解析和 `main()` 调度。
证据：`CLAUDE.md:74-77`

当前 `test_all.cpp` 符合这一组织方式：它在测试模块区直接 include 大量 `tests/test_*.h` 文件。
证据：`test_all.cpp:41-132`

### 4.2 测试入口 CLI

`test_all` 支持以下主要模式：

- `--fast`：运行快速单元/组件测试集合
- `--slow [root_path]`：运行慢速集成测试集合
- `--bench`：只运行性能 benchmark
- `--part <id>`：运行指定测试分组
- 默认无显式选择时：运行默认完整集合

CLI usage 与 part ID 列表由 `printUsage()` 输出。
证据：`test_all.cpp:138-205`

### 4.3 Fast tests

`make test` 是 `test-fast` 的别名。`test-fast` 会：

1. 构建 `test_all`
2. 执行 `lint-bridge`
3. 运行 `./test_all --fast`

这使快速验证同时覆盖 C++ 测试入口和 `lint-bridge` 所列 Bridge ObjC++ 文件的语法检查；它不代表所有 Bridge 分类文件都被 lint。
证据：`Makefile:53-58`

`--fast` 在 `test_all.cpp` 中显式选择大批 part，包括 mutation、path search、metadata、compaction、ranking、thread safety、content index、WAL、HTTP/service、query parser/filter/modifiers、SIMD、AI 后端、NL translator、rich text、model manager、short query cache 等。
证据：`test_all.cpp:217-225`
证据：`test_all.cpp:269-359`

### 4.4 Slow tests

`make test-slow` 运行 `./test_all --slow`。
证据：`Makefile:59-60`

`--slow` 只选择三个 part：

- `1`：scan + query
- `4`：FSEvents
- `6`：end-to-end

这些测试更接近真实 I/O、文件系统事件和端到端流程。
证据：`test_all.cpp:223-225`
证据：`test_all.cpp:269-277`

### 4.5 Full/default tests

`make test-all` 构建 `test_all` 后直接执行 `./test_all`。
证据：`Makefile:62-63`

当 CLI 没有收到 `--fast`、`--slow`、`--bench` 或 `--part` 时，`test_all.cpp` 会装载默认完整 part 集合。
证据：`test_all.cpp:243-246`

### 4.6 Bench mode

`--bench` 只选择 part `44` 和 `46`：

- `44`：query performance
- `46`：10M-scale query performance

这与普通 fast/slow 测试分离，避免日常验证被大规模性能测试拖慢。
证据：`test_all.cpp:220-222`
证据：`test_all.cpp:319-321`

Makefile 另有 `benchmark` 二进制目标，用于构建独立 benchmark 程序。
证据：`Makefile:29-30`

### 4.7 Sanitizer variants

Makefile 提供两个 sanitizer 目标：

- `test-asan`：AddressSanitizer，`-O1 -g -fsanitize=address -fno-omit-frame-pointer`
- `test-tsan`：ThreadSanitizer，`-O1 -g -fsanitize=thread`

两者构建完成后都运行 fast test 集合。
证据：`Makefile:44-51`

### 4.8 Swift standalone tests

部分 Swift 层测试是独立脚本式测试，不在 Makefile 主测试链路中。例如 `tests/test_highlight_ranges.swift` 文件顶部直接给出了 `swiftc` 编译运行命令。
证据：`tests/test_highlight_ranges.swift:1-7`

## 5. Bridge lint

`lint-bridge` 使用 `clang++ -fsyntax-only -fobjc-arc -x objective-c++` 检查：

- `MacEverything/Bridge/MacSearchBridge.mm`
- `MacEverything/Bridge/MacSearchBridge+Content.mm`

它不生成产物，只验证上述 ObjC++ bridge 文件的语法、ARC 与 include 可用性；当前清单不包含 `MacSearchBridge+Semantic.mm`，因此 AI 翻译桥接分类主要由 Xcode app target 构建覆盖。
证据：`Makefile:37-42`

由于 `test-fast` 依赖 `lint-bridge`，快速测试失败面既包括 C++ 测试失败，也包括桥接层编译语法失败。
证据：`Makefile:56-57`

## 6. 发布与打包流水线

### 6.1 打包分支约束

项目明确要求：打包总是在 `master` 分支上进行。
证据：`CLAUDE.md:16-18`

这条约束的含义是：feature/fix worktree 完成、测试通过、合回 master 后，才进入 DMG 验收和发布产物生成。

### 6.2 make dmg 标准路径

推荐发布入口是：

```bash
make dmg
```

该目标展开后执行：

1. `make app`
2. `make bundle`
3. `scripts/bundle-dylibs.sh`
4. `scripts/verify-bundle.sh`
5. `hdiutil create ... MacEverything.dmg`

Makefile 中 `dmg` 依赖 `bundle`，`bundle` 又依赖 `app`，因此 `make dmg` 表示「构建 Release app → 捆绑外部 dylib → 校验自包含 → 生成 DMG」。
证据：`Makefile:65-86`
证据：`CLAUDE.md:20-31`

### 6.3 手动分步路径

项目说明也给出手动分步命令：

1. Xcode Release build
2. `bash scripts/bundle-dylibs.sh build/Release/MacEverything.app MacEverything/MacEverything.entitlements`
3. `bash scripts/verify-bundle.sh build/Release/MacEverything.app`
4. `hdiutil create ... MacEverything.dmg`

这些步骤与 `make dmg` 的展开顺序一致。
证据：`CLAUDE.md:23-31`

## 7. dylib 捆绑约束

### 7.1 问题来源

Xcode 工程链接 RE2 和 abseil 的 Homebrew 路径，并设置 app runtime search path：

- `LD_RUNPATH_SEARCH_PATHS` 包含 `@executable_path/../Frameworks`
- Header search path 包含 `/opt/homebrew/opt/re2/include` 与 `/opt/homebrew/opt/abseil/include`
- Library search path 包含 `/opt/homebrew/opt/re2/lib` 与 `/opt/homebrew/opt/abseil/lib`
- Link flags 包含 `-lre2` 与 `-lsqlite3`

证据：`MacEverything.xcodeproj/project.pbxproj:699-724`

如果不在打包阶段复制 RE2/abseil 依赖，app 会保留 `/opt/homebrew/opt/re2/lib/libre2.11.dylib` 这类绝对路径；没有 Homebrew/re2 的用户机器会发生 dyld 加载失败。
证据：`CLAUDE.md:20-21`
证据：`scripts/bundle-dylibs.sh:9-12`

### 7.2 bundle-dylibs.sh 行为

`bundle-dylibs.sh` 的目标是让 `.app` 自包含。脚本说明中明确写出：

- 递归解析每个非系统动态库依赖
- 将 re2 与 abseil closure 复制到 `Contents/Frameworks/`
- 将 install names 改写为 `@rpath/<lib>`
- 对复制的 dylib 和 app 本体进行 ad-hoc re-sign
- 重复运行应保持幂等

证据：`scripts/bundle-dylibs.sh:1-21`

脚本内部以 `Contents/MacOS` 下所有 Mach-O 文件为 BFS seed，递归查找外部依赖，外部路径前缀包括：

- `/opt/homebrew/`
- `/usr/local/`
- `/opt/local/`

证据：`scripts/bundle-dylibs.sh:37-45`
证据：`scripts/bundle-dylibs.sh:71-96`

随后脚本会：

1. 检查 basename collision
2. 复制 dylib 到 `Contents/Frameworks`
3. 对 copied dylib 设置 `install_name_tool -id @rpath/<base>`
4. 改写 app binary 与 dylib 内部对外部路径的引用
5. 确保 app binary 有 `@executable_path/../Frameworks` rpath
6. 重新签名 dylib 和 app

证据：`scripts/bundle-dylibs.sh:98-157`

### 7.3 verify-bundle.sh 行为

`verify-bundle.sh` 是发布闸门：它扫描 `Contents/MacOS` 和 `Contents/Frameworks` 中的 Mach-O 文件，如果仍然链接以下外部绝对路径则失败：

- `/opt/homebrew/`
- `/usr/local/`
- `/opt/local/`

证据：`scripts/verify-bundle.sh:1-12`
证据：`scripts/verify-bundle.sh:22-39`

退出语义：

- `0`：app self-contained
- `1`：发现 external leak

脚本最后输出检查的 Mach-O 数量和 leak 数量；只要 leak 数量大于 0，就打印失败并 `exit 1`。
证据：`scripts/verify-bundle.sh:53-59`

## 8. 发布后验收

每次新变更合并到 master 后，项目要求执行发布验收：

1. 退出当前运行的 app
2. 在 master 上构建并打包
3. 启动打包好的 app，并使用 `open MacEverything.app --args --minimized` 最小化启动
4. 通过 HTTP 服务对 `http://localhost:19860` 做与本次变更相关的验证

证据：`CLAUDE.md:79-96`

这说明 DMG 不是单纯的文件产物；发布验收还要求实际启动 app，并通过本地 HTTP 服务验证运行时行为。

## 9. 清理与辅助目标

Makefile 提供 `clean` 目标，删除：

- `test_all`
- `test_all_asan`
- `test_all_tsan`
- `benchmark`
- `maceverything-daemon`
- `MacEverything/Core/*.o`
- `build/`

证据：`Makefile:88-92`

`help` 目标列出常用入口，包括 test、test-fast、test-slow、test-all、app、bundle、verify-bundle、dmg、daemon、benchmark、clean。
证据：`Makefile:94-108`

## 10. 提交前 hook 与文档-only 策略

### 10.1 当前 pre-commit 行为

当前 git hook 是无条件的：每次 commit 都运行 `arch -arm64 make test-fast`，失败则中止提交。

这意味着即使是纯文档修改，只要本机缺少构建依赖、架构环境异常或 vendored 静态库不可用，提交也可能被 `make test-fast` 阻断。

### 10.2 docs-only 验证策略

当前 hook 没有内置 docs-only skip。对于纯文档变更，应遵守以下策略：

1. 优先执行能运行的文档校验，例如标题结构、锚点格式、图表代码块约定、链接一致性。
2. 如果没有 C++/Swift 代码、构建脚本、Xcode 配置、运行时行为变化，可不执行 `make dmg` 与 HTTP 功能验收。
3. 如果 pre-commit 因本地构建依赖或架构问题阻断，而变更确认为纯文档，可在 changelog 中记录原因后显式使用 `git commit --no-verify`。
4. `--no-verify` 只能用于明确的纯文档场景；涉及代码、构建、打包、脚本或配置变化时不得跳过 fast tests。

历史 docs-only changelog 已记录类似先例：纯文档变更不涉及 C++/Swift 代码、构建产物或运行时行为，因此无需 `make dmg` / HTTP 功能验收。
证据：`docs/changelog/175-arch-doc-refresh.md:64-70`

## 11. 推荐使用矩阵

| 场景 | 推荐命令 | 说明 |
|---|---|---|
| 日常快速验证 | `make test-fast` | 构建 `test_all`，lint bridge，运行 fast suite |
| 默认测试集合 | `make test-all` | 运行 `./test_all` 默认集合 |
| 慢速集成验证 | `make test-slow` | 覆盖 scan/query、FSEvents、E2E |
| 指定测试分组 | `./test_all --part <id>` | 定位单个模块或回归 |
| 性能分组 | `./test_all --bench` | 运行 query perf 与 10M benchmark |
| 独立 benchmark 二进制 | `make benchmark` | 构建 `benchmark` |
| AddressSanitizer | `make test-asan` | 构建 ASan 版本并跑 fast suite |
| ThreadSanitizer | `make test-tsan` | 构建 TSan 版本并跑 fast suite |
| Release app 构建 | `make app` | Xcode Release build |
| dylib 捆绑与校验 | `make bundle` | 构建 app 后复制并校验外部 dylib |
| 自包含复检 | `make verify-bundle` | 只检查已构建 app |
| 发布 DMG | `make dmg` | build + bundle + verify + hdiutil |
| 清理产物 | `make clean` | 删除本地产物和 build 目录 |

## 12. 架构边界总结

构建与发布链路的关键边界如下：

- `Makefile` 负责本地 C++/ObjC++ 测试、benchmark、daemon、Xcode build 和 DMG orchestration。
- `MacEverything.xcodeproj` 负责 app、UI tests 和 MCP target 的 Xcode 原生构建。
- `test_all.cpp` 是 C++ 测试调度器，不应承载测试实现。
- `tests/test_xxx.h` 是 C++ 测试实现的归属位置。
- Swift standalone tests 需要按文件头说明单独执行。
- `scripts/bundle-dylibs.sh` 负责复制和改写外部 dylib closure。
- `scripts/verify-bundle.sh` 负责阻断 `/opt/homebrew`、`/usr/local`、`/opt/local` 这类外部绝对依赖泄漏。
- `make dmg` 是发布主入口，必须在 master 上执行。
- 代码或构建行为变化合并后，需要 DMG 启动与 HTTP 验收。
- 纯文档变更可采用 docs-only 验证策略，但应在 changelog 中说明未执行运行时验收的原因。
