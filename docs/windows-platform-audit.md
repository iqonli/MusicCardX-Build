# 平台接入核对

基线：MusicCardX正式main ddfc3e40；Build main 02ae37c9。lib、assets、pubspec及third_party在两基线上无业务差异。本次仅修改Build分支，未从旧的1.5.0本地目录恢复代码。

| 文件 | Windows处理 |
|---|---|
| main.dart | 原有Editor与CardDocument共用，新增桌面布局、属性区、导出分发、拖放/粘贴导入、独立预览推送 |
| model.dart | 原始MethodChannel替换为兼容同名接口的PlatformIO子类，保留测试mock与Android通道 |
| renderer.dart | 不改渲染算法、参数、最大尺寸确认逻辑 |
| fonts.dart | Windows分支读取DirectWrite字体文件，沿用Sfnt、字体匹配及fvar轴解析，不创建字体副本 |
| file_browser.dart | Windows使用COM对话框，Android继续自定义文件管理器 |
| audio_import.dart | 不改字段、内嵌封面和歌词解析，已有compute在后台执行 |
| image_export.dart | 继续共用PNG/JPG编码与质量参数 |
| seed_backgrounds.dart | 继续现有DLL FFI加载和种子规则 |
| cover_search.dart / song_search.dart | 不修改搜索源、区域、评分和匹配逻辑 |
| song_fields.dart / music_share.dart | 不修改验证、字段标准化和分享解析 |
| assets/defaults.json / pubspec.yaml / pubspec.lock | 保留原默认值与依赖版本，没有新增Dart依赖 |
| third_party/mcx_background | 不修改任何算法文件，Windows CMake照常引入C++17核心DLL |

## 原生调用

状态、preset及storageRoot由Dart适配器实现。Android的requestStorage/scanImage在Windows分别返回可用/无需媒体扫描；不是文件功能的空实现。字体仓库treeRead、uriRead、Android Activity权限、catalogLoad与launchAction保留Android代码，在Windows没有对应入口。

Windows独立通道支持locations、replaceFile、pickFiles、saveFile、pickCover、systemFontFiles、openUrl、copyImage、readClipboard、preview。反向事件为dropFiles、previewAction、closing。选择取消返回空列表，不作为异常。错误返回PlatformException并交由原有busy/message路径显示。

## 当前记录

代码检查完成；没有在容器运行flutter analyze、flutter test或Windows编译。相关命令和DLL确定性测试已放入Windows Actions。Actions完成前不声称已编译成功或交互已验证。手动检查清单见windows.md。
