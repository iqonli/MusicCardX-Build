# MusicCardX-Build → MusicCardX 同步排除清单

本文件用于从 `iqonli/MusicCardX-Build` 向正式仓库 `iqonli/MusicCardX` 同步代码时，区分 **Build 专用文件**、**需逐项检查的差异** 和 **可以同步的正式版本内容**。

基于 2026-11-06 两个仓库的 `main` 分支核对结果。以后如仓库结构或 CI 方案变化，应重新比较文件内容，不能将此清单视为永久不变的规则。

## 一、默认不向主仓库同步的文件

| Build 仓库路径 | 原因 | 处理方式 |
| --- | --- | --- |
| `.github/workflows/build-android.yml` | Build 仓库专用的 Android CI：自动构建、签名、ABI 检查、保留 3 天的 APK Artifact。 | 保留在 Build；除非明确要在主仓库部署同一工作流，否则不复制。 |
| `.github/workflows/publish-release.yml` | 从 Build 仓库指定的 Actions Run 下载 ARM64 和 x86_64 APK，并发布到 **当前仓库** Release。 | 不直接复制到主仓库；若要跨仓库发布，须重新设计仓库目标、权限及下载逻辑。 |
| `BUILD_SETUP.md` | Build 仓库专用的 CI 初始化、签名 Secrets 和发布操作说明。 | 仅留在 Build。 |
| `docs/1.5.1-beta1.md` | 1.5.1 预发布测试期间的中间变更记录。 | 正式版同步时默认跳过；需保留历史时再单独决定。 |

## 二、不要无差别覆盖的文件

| 路径 | 差异与建议 |
| --- | --- |
| `android/app/build.gradle.kts` | Build 版本额外支持从环境变量 `MCX_KEYSTORE_PATH`、`MCX_STORE_PASSWORD`、`MCX_KEY_PASSWORD`、`MCX_KEY_ALIAS` 获取签名配置，同时保留 `signing.properties` 回退方式；主仓库目前已有 1.5.1 版本号。该逻辑并非有害，但没有必要只为同步版本而整份覆盖；如主仓库也需要 CI 签名能力，可单独审查后采纳。 |
| `README.md` | 两仓库的版本文字存在差异，且都可能滞后于实际版本。主仓库 README 应以主仓库内容为准，只针对版本和功能进行局部编辑，不直接用 Build 仓库的 README 覆盖。 |

## 三、可以按需要同步的内容

- `docs/1.5.1.md`：正式版更新记录，目前在 Build 仓库中，可考虑补充到主仓库。
- 核心代码，例如 `third_party/mcx_background/src/light_flow.cpp`、`third_party/mcx_background/src/flowbench.cpp`、`lib/model.dart`、`lib/about_page.dart`、`pubspec.yaml`：**先按 Git Blob SHA 或实际 diff 比对**。截至本文件建立时，这些 1.5.1 文件已经与主仓库一致，无须重复复制。
- 后续正式功能变更：只选取对应功能文件及必要依赖，不把 Build 整棵文件树当作主仓库的权威版本。

## 四、特别注意主仓库独有的历史文件

比对时，正式仓库还包含 Build 仓库未收录的归档、测试和文档文件，包括：

- `archive/dart-backgrounds-beta1/`
- `archive/light-flow-v7-beta2/`
- `docs/1.4.6.md`、`docs/1.4.7*.md`、`docs/1.4.8.md`、`docs/1.5.0*.md` 等
- `docs/validation/1.4.8/`
- 部分 `test/` 测试及 `test/fixtures/` 文件
- `COPYRIGHT`

**不要用覆盖式同步或 `git add -A` 的目录替换方式，将这些“Build 缺失”的主仓库文件误删。** 两仓库不是严格镜像。

## 五、建议同步流程

1. 先检查两个仓库各自 `main` 的最新提交和 `git diff`，确认代码差异。
2. 将要同步的正式源码文件单独挑选；对本清单第一节文件默认排除，对第二节文件做人工检查。
3. 在主仓库检查 `git diff --cached --stat` 和 `git diff --cached`，确保没有意外删除、签名配置覆盖或历史文件丢失。
4. 提交到正式主仓库；Build 专用 Actions 和产物保留策略仍在 Build 仓库独立管理。

本清单只说明**文件同步策略**，不表示 APK 已完成测试、也不代替正式版本的发布验证。
