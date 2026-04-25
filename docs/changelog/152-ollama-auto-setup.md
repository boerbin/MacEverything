# 152 - Ollama 自动安装引导对话框

## 背景

用户启用 AI 搜索模式时，如果本地没有安装/运行 Ollama，之前没有任何引导提示，AI 搜索会静默回退到原始文本搜索。用户需要自己手动查找并安装 Ollama、下载模型，体验不佳。

## 需求

- 当用户切换 AI 搜索模式且 Ollama 未运行时，弹出安装引导对话框
- 对话框显示 Ollama 安装状态（已安装/已运行/模型可用）
- 列出需要执行的命令（brew install ollama、启动服务、拉取模型）
- 用户点击「Execute in Terminal」后，自动打开 Terminal.app 执行安装脚本
- 如果安装过程出错，终端窗口保持打开，让用户看到错误信息

## 变更内容

### 1. 新增 `OllamaSetupHelper.swift`

工具类，提供以下静态方法：
- `isOllamaInstalled()` — 通过 `which ollama` 检测是否已安装
- `isOllamaRunning()` — HTTP 请求 `localhost:11434/api/tags` 检测 Ollama 服务状态
- `isModelAvailable()` — 解析 `/api/tags` 响应，检查 `qwen2.5:3b` 模型是否已下载
- `executeSetupInTerminal()` — 将安装脚本写入 `/tmp`，通过 `NSAppleScript` 调用 Terminal.app 执行

安装脚本流程：
1. 检查 Homebrew 是否存在
2. 安装 Ollama（如未安装）
3. 启动 Ollama 服务
4. 拉取 qwen2.5:3b 模型
5. 任何步骤失败都会显示错误并等待用户按键退出

### 2. 新增 `OllamaSetupView.swift`

SwiftUI Sheet 视图（~105 行）：
- 标题栏带 CPU 图标和 "AI Search Setup"
- 三个状态指示器（绿/红点）：Ollama installed、Ollama running、Model qwen2.5:3b
- 等比例字体代码块展示即将执行的命令
- 底部按钮：Cancel + Execute in Terminal
- 如果所有组件已就绪，显示 "All set!" 并改为 Done 按钮
- 进入视图时异步检查所有状态

### 3. 修改 `SearchViewModel.swift`

- 新增 `@Published var showOllamaSetup = false`
- 增强 `checkAIServiceAvailability()`：当 AI 服务不可用时，进一步检查 Ollama 是否运行，如果未运行则设置 `showOllamaSetup = true` 触发对话框

### 4. 修改 `ContentView.swift`

- 在 `.frame(minWidth: 600, minHeight: 400)` 后添加 `.sheet(isPresented: $viewModel.showOllamaSetup)` 修饰器

### 5. 修改 `project.pbxproj`

- 注册 `OllamaSetupHelper.swift` 和 `OllamaSetupView.swift` 到 Xcode 工程

## 文件清单

| 文件 | 操作 |
|------|------|
| `MacEverything/App/OllamaSetupHelper.swift` | 新增 |
| `MacEverything/App/OllamaSetupView.swift` | 新增 |
| `MacEverything/App/SearchViewModel.swift` | 修改 |
| `MacEverything/App/ContentView.swift` | 修改 |
| `MacEverything.xcodeproj/project.pbxproj` | 修改 |

## 验证

- xcodebuild Release 构建通过（BUILD SUCCEEDED）
- 对话框在 Ollama 未运行时正确弹出
- Execute 按钮通过 NSAppleScript 打开 Terminal.app 执行安装脚本
- 已安装环境下显示 "All set!" 状态
