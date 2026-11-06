# MusicCardX-Build 自动编译仓库部署说明

本包是 Library `MusicCardX-1.5.0-source(1).zip` (1.5.0+35) 的完整源码，加上两个 GitHub Actions 工作流和 CI 环境变量签名支持。

## 仓库初始化（Termux）

1. 将本 ZIP 放到手机 Download 目录，确保已授权 Termux 存储权限（`termux-setup-storage`）。
2. 建立独立公开仓库（不要用 GitHub Fork 功能）：

```sh
gh repo create iqonli/MusicCardX-Build --public --description 'Independent CI build repository for MusicCardX'
```

3. 从原仓库克隆，以保留 Git 提交历史，再覆盖成本包的完整 1.5.0+35 源码：

```sh
cd ~
git clone https://github.com/iqonli/MusicCardX.git MusicCardX-Build
cd ~/MusicCardX-Build
unzip -oq /sdcard/Download/MusicCardX-Build-1.5.0+35-ready.zip -d .
git remote set-url origin https://github.com/iqonli/MusicCardX-Build.git
git add -A
git -c user.name='IQ Online Studio' -c user.email='build@localhost' commit -m 'Add independent MusicCardX CI for v1.5.0+35'
git push -u origin HEAD:main
```

建议先运行 `git status` 和 `git diff --stat` 预览本地改动，再提交。

## 签名（签名 JKS 不要提交仓库）

需要三个 GitHub Actions Secrets：
- `MCX_KEYSTORE_B64`: 现有 `iqonli-1` JKS 文件的 Base64 内容（原始二进制，去除换行）。
- `MCX_STORE_PASSWORD`: JKS 密钥库密码。
- `MCX_KEY_PASSWORD`: JKS 中私钥条目密码；如果与前者相同，也仍需设置。

可选：
- `MCX_EXPECTED_CERT_SHA256`: 已发布 MusicCardX 版本的签名证书 SHA-256，用于防止误用其他密钥。

别名固定为 `iqonli-1`，由工作流环境变量设置；若实际别名不同，需要调整工作流。用户尚未提供 JKS 的本地路径，本包不预设路径。

使用 `gh secret set` 时避免在命令行或工作流里硬编码密码；使用 `read` + 管道，或 GitHub 仓库设置页面。

## 构建

`.github/workflows/build-android.yml` 在推送 `main` 或手动启动时执行。
- 运行 Flutter 3.47.9、JDK 21、Gradle 9.3.1、Android SDK36、NDK 28.2.13676358、CMake 3.22.1。
- 运行 `flutter analyze` 与 `flutter test`；这两步失败不会阻止后续构建，但报告中会保留失败信息。
- 使用原始 JKS 生成正式 Release APK，分别包含 `arm64-v8a`、`x86_64`，并检查签名和本地原生库 `libmcx_background.so`。
- APK Artifacts 保留 3 天。每次构建上传两个 APK Artifacts 和一份测试/校验报告。

首次上传工作流后，如缺少密钥 Secrets，构建会在签名准备前明确报错；完成 Secrets 配置即可重新运行，不需要修改源码。

## 直接从 Actions 发布 Release（不经过手机）

1. 打开新仓库 Actions，找到 **Publish Release from an Actions run**，点击 Run workflow。
2. `run_id` 填入**已经成功构建**的 Build MusicCardX APKs Run ID，且必须是新仓库的 main 分支构建。
3. `tag` 填入 `v1.5.0` 等有效标签。勾选 `prerelease` 可创建预发行版。
4. 发布工作流使用 `actions/download-artifact@v4` 直接读取该 Run 的两个 APK，再由 `gh release create` 上传到新仓库的 Releases。整个过程都在 GitHub 云端进行；本地无需下载或重新上传。

要求原 Run 的 Artifacts 尚未过期（本包默认保留 3 天）；Release 文件本身不随原 Artifact 到期而删除。
如提示权限不足，确认仓库 Actions 的 GITHUB_TOKEN 已允许对应工作流的 `contents: write` 权限，仓库策略未禁止创建 Release。

## 已知注意事项

- 本包取自 Library 的 1.5.0+35 源码，补齐原远端缺失的 `third_party/mcx_background`，不会自动跟踪原仓库的后续提交；后续按需手动同步。
- 源码 `android/gradlew` 和 `gradle-wrapper.jar` 被旧 `.gitignore` 排除。CI 由 setup-gradle 供应 Gradle 9.3.1，运行时生成一个临时 `android/gradlew` 启动脚本。
- 源码的 `pubspec.lock` 使用了国内 pub 镜像 URL；CI 统一设置 `PUB_HOSTED_URL=https://pub.dev`。
- 未持有旧 `iqonli-1` 的正确 JKS 与密码时，无法产生能覆盖安装旧版的正式签名 APK。
- 工作流未在真实 GitHub Actions 环境执行过，环境或上游插件兼容性可能需要根据首次日志微调；不预先宣称构建成功。
