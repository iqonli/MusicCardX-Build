# 在 Build 仓库一键发布到正式 MusicCardX

工作流：`.github/workflows/publish-release.yml`（**Publish signed APKs to official MusicCardX**）。

发布目标已固定为 **`iqonli/MusicCardX`**。从 Build 仓库已有的成功构建 Run 下载 ARM64-v8a、x86_64 APK，并在 GitHub Actions 云端上传到正式仓库 Release；**不会重新编译或签名，也不需要经过 Termux**。

## 初次配置：授权发布正式仓库

1. 登录 `iqonli` GitHub 账号，在 **Settings → Developer settings → Personal access tokens → Fine-grained tokens** 创建一个令牌。建议命名 `MusicCardX official release`，设置适当有效期。
2. **Resource owner** 选 `iqonli`；**Repository access** 选 **Only select repositories**，只勾选 `MusicCardX`（不必勾选 `MusicCardX-Build`）。
3. **Repository permissions → Contents** 设为 **Read and write**；其余权限保持默认。生成令牌后妥善保存，不要提交到仓库或粘贴到聊天中。
4. 打开 **`iqonli/MusicCardX-Build` → Settings → Secrets and variables → Actions → New repository secret**。名称必须是 `MCX_MAIN_RELEASE_TOKEN`，值填第 3 步的令牌。

令牌过期或被撤销后需要生成新令牌，并更新同名 Secret。Actions 的内置 `GITHUB_TOKEN` 仅用于读取 Build 的构建信息和下载 Artifact；跨仓库发布使用上述受限令牌。

## 每次正式发布

1. 先把待发布版本的**正式应用源码**同步到主仓库 `iqonli/MusicCardX` 的 `main`；版本号应与 APK 一致。
2. 在 Build 仓库确认一次 **Build MusicCardX APKs** 构建成功，且该次任务同时产生 `MusicCardX-ARM64-signed` 和 `MusicCardX-x86_64-signed`。可以手动启动构建并勾选 `build_x86_64`，也可以让提交消息带 `[build-both-abis]`。
3. 在 Build 仓库的 **Actions** 页面打开 **Publish signed APKs to official MusicCardX**，点 **Run workflow**，输入：
   - `run_id`：上述成功构建任务的 Run ID（不是 Job ID）。
   - `tag`：版本标签，如 `v1.5.2`；必须与该构建的 `pubspec.yaml` 版本名一致。
   - `prerelease`：正式版不勾选，预发布版才勾选。
4. 工作流自动校验源构建成功、两个 APK 均存在、Build 的源码与主仓库 `main` 的运行代码一致，然后上传两个原始 APK 和新生成的 `SHA256SUMS.txt` 至 **`https://github.com/iqonli/MusicCardX/releases`**。

## 防误发布条件

- **主仓库已经存在同名 Release 时会拒绝覆盖。** `v1.5.1` 当前已在正式仓库发布，不能重复用这个标签测试。
- 将 Build 源码中的 `lib/`、`third_party/`、`assets/`、`android/`（除 CI 与本地签名差异对应的 `android/app/build.gradle.kts`）以及 `pubspec.yaml`、`pubspec.lock` 与正式仓库按 Git blob SHA 逐文件比对；缺文件或内容不同会停止，要求先同步源码。
- `README`、归档、测试文档、Build 专用工作流与部署说明不作为 APK 运行源码进行比对。
- 仅接受来自 Build 仓库 `main` 的成功 Android 构建 Run；发布标签必须与构建源版本一致。
- Release 标签指向**正式主仓库**当前经过一致性检查的 `main` 提交，Release 说明会记录 Build 的真实构建 SHA 和 Run ID。

## 已知限制

- 必须先配置 `MCX_MAIN_RELEASE_TOKEN` 才能成功发布，单独提交工作流并不会自动获得跨仓库授权。
- 构建 Artifact 在 Build 仓库仅保留 3 天；过期后应重新构建。同一版本已有 Release 时，不能直接再次执行创建发布。
- 这份工作流只创建简要 Release 说明；如果要使用完整的中文更新说明，可以在正式仓库的 Release 页面编辑。
