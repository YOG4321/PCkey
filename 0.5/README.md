# PCkey

PCkey 是一款面向 Windows 的轻量级、开源、用户态键盘改键程序。它将
Vial/QMK 风格的键位、Layer、Tap-Hold、Tap Dance、Combo、按键宏和
Key Override 应用到笔记本内置键盘、USB 键盘和蓝牙键盘。

> 当前版本：`0.5.0-dev`

## v0.5 已实现

### 基础与 Layer

- 普通透传、禁用、单键映射；
- `MO(n)` 瞬时层；
- `LT(n)` Layer-Tap；
- `MT(mod)` Mod-Tap；
- 默认 500ms、逐键可调的长按阈值；
- 默认 200ms、逐键可调的 Quick Tap；
- 第二键立即选择 Hold，适合“长按空格切层 + ESDF 方向键”；
- 修饰键释放保护，保留 `Ctrl+Space` 等快捷键；
- 最多 32 个 Layer。

### 常用快捷操作

- 全选、复制、粘贴、剪切；
- 撤销、重做、保存、查找、新建、打开和打印；
- 放大、缩小和恢复缩放；
- 切换窗口、关闭窗口、显示桌面、打开文件资源管理器和锁定电脑；
- 任务管理器和 Windows 系统截图；
- 快捷操作可直接绑定到普通键位，也可以用作 Tap Dance、Combo 和
  Key Override 的输出动作。

### Tap Dance

每个 Tap Dance 可设置四个动作：

- 单击；
- 长按；
- 双击；
- 单击后长按。

可分别设置长按阈值、多击间隔和 Quick Tap。Tap Dance 内可使用普通键、
Layer、媒体键、鼠标键和已创建的按键宏，但不允许递归嵌套
Tap Dance 或 Tap-Hold。

Tap Dance 中的切层与 Layer 面板使用同一个 `MO(n)` 动作：

- 在“长按”或“单击后长按”中选择 `MO(n)`；
- 达到长按条件后启用对应 Layer；
- 松开 Tap Dance 源按键后关闭该 Layer；
- 在源按键尚未达到阈值时按下其他键，也会立即选择 Hold 并先启用 Layer；
- `LT(n)` 本身已经包含一次 Tap/Hold 判定，因此不能再次嵌套进
  Tap Dance。

### Combo

- 每个 Combo 支持 2～4 个成员；
- 按**映射后的键值**识别，而不是按物理位置识别；
- 可设置 20～300ms 判定窗口；
- 可限制为当前 Layer 生效；
- 重叠 Combo 优先选择成员数量更多的规则；
- Combo 输出保持到任意成员释放；
- Combo 优先于 Tap Dance 和 Tap-Hold。

### 按键宏

- 仅录制按键按下、释放和事件间隔；
- 不包含文本宏、脚本、程序启动或剪贴板操作；
- 每个配置最多 32 个宏；
- 每个宏最多 256 个事件；
- 最多 8 个不同宏并行播放；
- 同一个宏播放期间再次触发会被忽略；
- 提供“停止所有宏”动作；
- 宏结束、中止或核心退出时会释放宏持有的按键。

### Key Override

- 按映射后的触发键和当前有效修饰键判断；
- 支持 Ctrl、Shift、Alt、Win，左右任意一侧均可触发；
- 支持必须修饰键、禁止修饰键和精确匹配；
- 可在输出替换键时临时抑制指定修饰键；
- 可限制为当前 Layer 生效；
- 处理顺序为：
  `Combo → Tap Dance/Tap-Hold → Key Override → 输出`。

### 媒体键和鼠标键

- 音量、静音、播放/暂停、上一曲、下一曲、浏览器和启动键；
- 鼠标左键、右键、中键、X1、X2；
- 鼠标上下左右移动；
- 垂直和水平滚轮；
- 鼠标初始速度、最大速度、加速时间、重复间隔和滚轮步长；
- 精细、标准、快速三个鼠标参数预设。

### 编辑器与配置

- 原生 Win32、Direct2D 和 DirectWrite 的 Vial 风格界面；
- 104、87、75、65、60 和通用笔记本布局；
- 配置创建、重命名、删除和手动切换；
- 点击“保存并应用”后才会改变核心当前规则；
- 未应用修改自动保存为独立草稿；
- 高级规则可在对应分类中创建；
- 宏和 Tap Dance 创建后可直接绑定到当前键位；
- 已创建的宏、Tap Dance、Combo 和 Override 可在动作面板中右键删除；
- 已创建的宏、Tap Dance、Combo 和 Override 可在动作面板中右键编辑，
  修改时保留原规则 ID 及所有键位引用；
- 删除宏或 Tap Dance 时会同时清理失效引用；
- 提供“按键测试”页面，可在“物理按键（映射前）”和
  “配置输出（映射后）”两种模式间切换；
- UTF-8 配置格式 v4，并兼容读取 v1/v2/v3。

## 使用方法

1. 将 ZIP **完整解压**到普通文件夹；
2. 双击 `PCkeyCore.exe`；
3. Core 会启动后台改键服务并自动打开 `PCkeyEditor.exe`；
4. 点击左侧“新建配置”；
5. 选择键盘布局；
6. 点击上方键盘中的源按键；
7. 在下方分类面板选择目标动作；
8. 创建需要的 Tap Dance、Combo、宏或 Key Override；
9. 点击“保存并应用”。

以后再次双击 `PCkeyCore.exe` 或双击托盘中的紫色 `P` 图标，都会打开
或恢复现有编辑器窗口。Windows 11 可能把新托盘图标放在任务栏右下角的
“隐藏的图标”箭头中。

也可以直接双击 `PCkeyEditor.exe`。编辑器在保存并应用时会自动以后台
模式启动 Core，不会反向重复打开编辑器。

如果旧编辑器进程卡住且没有窗口，新版本会忽略该残留进程并直接创建新的
健康窗口；不会再因为单实例锁而静默退出。

Core 和 Editor 使用可响应的健康检查窗口判断实例状态，避免残留进程阻止
新实例启动。托盘菜单支持安全退出，会先停止键盘钩子、释放合成按键并删除
托盘图标。顶部的“按键测试”支持物理模式和配置模式，测试事件只在内存中
显示，不写入日志或配置文件。

要恢复原始键盘行为，选择左侧不可编辑的“普通模式”，再点击
“保存并应用”。

### 配置路径

正式配置：

```text
%LOCALAPPDATA%\PCkey\config.pckey
```

未应用草稿：

```text
%LOCALAPPDATA%\PCkey\config.pckey.draft
```

再次打开编辑器会恢复草稿；点击“放弃修改”可回到最近一次正式配置。

## 运行结构

- `PCkeyCore.exe`
  - 常驻托盘；
  - 安装 `WH_KEYBOARD_LL` 键盘钩子；
  - 执行映射、定时判定和 `SendInput` 注入；
  - 不显示普通操作提示，仅错误时弹窗。
- `PCkeyEditor.exe`
  - 仅编辑配置时运行；
  - 保存后通过同一桌面会话内的窗口消息通知核心原子热加载。

## 构建

要求：

- Windows 10 22H2 或 Windows 11；
- Visual Studio 2022 Build Tools；
- “使用 C++ 的桌面开发”工作负载；
- Windows 10/11 SDK；
- CMake 3.24 或更高版本。

当前开发机可运行：

```powershell
tools\build.cmd Debug
tools\build.cmd Release
```

自动化测试或便携调试时可临时指定配置文件：

```powershell
$env:PCKEY_CONFIG_PATH = 'D:\path\to\config.pckey'
```

## 安全边界

- 仅保证登录后的普通 Windows 桌面；
- 不保证 UAC 安全桌面、登录界面、锁屏和反作弊程序；
- 用户态钩子不能稳定修改高于 PCkey 完整性级别的进程；
- 所有键盘共用当前配置，不区分具体设备；
- 不记录按键历史；
- 不联网，不上传遥测；
- 不包含文本宏、脚本执行、云同步或按应用自动切换；
- 固定紧急旁路：同时按住左 Shift、右 Shift 和 Esc 两秒；
- 崩溃或退出后用户态钩子会被系统移除；
- 紧急旁路、退出和配置热加载会释放 PCkey 持有的合成按键。

## 当前边界

v0.5 已完成 Tap Dance、Combo、按键宏、Key Override、媒体键、鼠标键和
常用系统快捷操作的创建、保存与运行。宏、Tap Dance、Combo 和 Override
均可直接编辑，不再需要删除后重新创建。

尚未实现的 Vial/QMK 功能包括 `TG`、`TO`、`DF`、`OSL`、`TT`、
Repeat、Alt Repeat、Caps Word、Layer Lock、RGB 和键盘固件控制。

详细设计：

- `docs/ARCHITECTURE.md`
- `docs/CONFIG_FORMAT.md`

## License

Apache License 2.0。
