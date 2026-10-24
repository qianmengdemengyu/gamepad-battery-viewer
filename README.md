# Gamepad Battery ｜ 手柄电量

在 Windows 任务栏托盘里，直接用**一个数字**显示 Xbox / XInput 兼容手柄的剩余电量。

> 托盘图标：绿色 **3** = 满，橙色 **2** = 中，红色 **1** = 低，灰色 **0** = 空，灰色 **?** = 未连接。

<!--
截图：把面板截图保存为 docs/screenshot.png，然后删掉上面这行和下面这行的注释符号。

![截图](docs/screenshot.png)
-->

## 特性

- **托盘直接看电量**：图标本身就是电量，数字随档位变化，颜色可一眼分辨
- **零依赖**：只用 Windows 自带的 `xinput1_4.dll` + GDI+，不打包运行时、不装驱动
- **不联网**：没有网络代码，不会上传任何数据
- **轻量**：单文件约 160 KB，内存占用几 MB
- **常驻无打扰**：关闭窗口即缩到托盘，鼠标悬停看完整信息，右键可退出

## 电量为什么只有 0~3 四档？

这是 **XInput 接口本身的限制**。`XINPUT_BATTERY_INFORMATION` 只提供四个等级：

| 值 | 含义 |
|---|---|
| 0 | 空（Empty） |
| 1 | 低（Low） |
| 2 | 中（Medium） |
| 3 | 满（Full） |

要看到精确百分比，只能靠厂商自己的软件（例如 GameSir Nexus、Logitech G HUB 等），因为那需要读取各家的私有 HID 协议，不同型号协议不通用。

## 系统要求

- Windows 10 / 11（x64）
- 一个 **XInput 兼容** 手柄：Xbox 系列手柄、以及大量以「Xbox 360 / Xbox One 兼容」方式工作的第三方手柄

> 如果手柄走的是 DirectInput 或私有协议、没有通过 XInput 暴露，本程序会显示「未连接」。

## 编译

需要 Visual Studio（2019 / 2022 / 2026 均可，带 C++ 桌面开发组件）。

打开 **x64 Native Tools Command Prompt for VS**，在本目录执行：

```bat
build.bat
```

或者手动执行：

```bat
cl /nologo /W3 /EHsc /utf-8 /DUNICODE /D_UNICODE ^
   /Fo:build\ /Fe:build\gamepad-battery.exe ^
   src\gamepad-battery.cpp ^
   user32.lib gdi32.lib shell32.lib gdiplus.lib
```

## 使用

1. 双击 `gamepad-battery.exe`
2. 任务栏右下角出现带数字的图标（如果没看到，点 `^` 展开，或到「设置 → 个性化 → 任务栏 → 其他系统托盘图标」里打开）
3. 左键点图标：显示 / 隐藏面板
4. 右键点图标：打开面板 / 退出
5. 关闭窗口 = 缩到托盘，不会退出

程序会在自身目录写一个 `run.log`（每次启动重写），记录托盘注册结果，仅用于排查问题，可以随时删除。

## 常见问题

**Q：图标显示 `?`**
A：手柄没连上，或手柄不是 XInput 兼容设备。

**Q：第一次运行弹出「Windows 已保护你的电脑」**
A：因为程序没有代码签名证书。点「更多信息 → 仍要运行」即可；也可以自行用源码编译，验证行为。

**Q：能显示百分比吗？**
A：不能。原因见上文「电量为什么只有 0~3 四档」。

## 许可

[MIT](LICENSE)

## 致谢

界面配色与交互参考了 Windows 原生设计语言；图标由程序在运行时用 GDI+ 绘制，不包含任何第三方素材。
