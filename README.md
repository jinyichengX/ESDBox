# ESDBox

> 嵌入式软件工具箱：轻量 GUI 框架 · 网络协议栈 · 机器人 / 音频实验

![C99](https://img.shields.io/badge/language-C99-blue.svg)
![Platform](https://img.shields.io/badge/platform-STM32%20%7C%20i.MX6U%20%7C%20SDL2-green.svg)
![Status](https://img.shields.io/badge/status-developer%20preview-orange.svg)

ESDBox 是一个面向嵌入式设备的软件集合，主体是自研的 GUI 框架 **ESDBox_IPGUI**，
同时收纳了嵌入式网络协议栈 **elNET**、激光雷达 / 路径规划实验 **RoboKit**、音频算法实验 **lovlis**
以及品牌资产 **IPGUI_Brand**。所有模块均可独立裁剪使用，仓库根目录的 `main.c` 是一个把 IPGUI 与 SDL2
组合起来的桌面级演示程序。

---

## 目录

- [模块一览](#模块一览)
- [Demo](#demo)
- [目录结构](#目录结构)
- [快速开始](#快速开始)
- [文档导航](#文档导航)
- [技术特点](#技术特点)
- [第三方组件](#第三方组件)

---

## 模块一览

| 模块 | 语言 | 说明 |
|---|---|---|
| [ESDBox_IPGUI](ESDBox_IPGUI) | C99 | 轻量级嵌入式 GUI 框架：绘图引擎（线 / 弧 / 三角 / 多边形 / 圆角矩形 / 阴影 / 渐变 / 蒙版）、图像解码、控件树、布局、动画系统、输入分发、PFB 脏矩形渲染 |
| [elNET](elNET) | C | 轻量级嵌入式 TCP/IP 协议栈：ARP / IPv4 / ICMP / UDP / TCP + DHCP、DNS、HTTP、MQTT、TFTP，自带 socket 风格 API 与零拷贝 nbuf |
| [RoboKit](RoboKit) | C++17 | RPLIDAR A1 激光雷达扫描数据采集 + RRT 路径规划实验，含 PX4 消息定义，可视化复用 IPGUI + SDL2 |
| [lovlis](lovlis) | Python | 音频处理实验：FFT 频谱分析、变调、音色变换 |
| [IPGUI_Brand](IPGUI_Brand) | SVG / PNG | IPGUI 品牌资产：Logo、应用图标、开机动画与分镜 |

## Demo

平台一：STM32板卡

<img width="640" alt="STM32 demo" src="https://github.com/user-attachments/assets/32b62f53-7c2d-43ba-b559-f172ad7c98fb" />

https://github.com/user-attachments/assets/a803f5e3-f3d6-406a-acba-20efd1d83370

平台二：i.MX6U板卡

https://github.com/user-attachments/assets/526d786a-7cdf-415e-93cd-566b23a5d4fe

平台三：SDL2

<img width="802" alt="SDL2 demo" src="https://github.com/user-attachments/assets/8fac0674-9787-49cc-bdbe-9446d18c0142" />

仓库根目录的 `main.c` 是 SDL2 平台上的完整示例：**CAP-AGING 多路电容老化测试系统上位机**，
800×480、深色工业风、纯矢量绘制、脏矩形局部刷新，3072 路通道数据由本地仿真器产生。

## 目录结构

```
ESDBox/
├── ESDBox_IPGUI/            # 轻量级嵌入式 GUI 框架
│   ├── Include/             # 全局配置与核心类型（ipgui_conf.h 等）
│   ├── base/                # 基础库：内存池 / 数学 / AVL / 队列 / 链表
│   ├── al/                  # 硬件抽象层：屏幕驱动、PFB、输入分发、VFS
│   ├── core/                # 绘图引擎：gfx / composite / image / vector_render / ui / ipml
│   ├── widget/              # 控件实现（圆弧、曲线、表盘、亮度条等）
│   ├── port/                # 平台适配：sdl / arm
│   ├── docs/                # 文档与绘图 API 图文参考
│   └── tools/               # 辅助调试用的 HTML 工具（渐变、阴影、布局、事件冒泡…）
├── elNET/                   # 轻量级嵌入式 TCP/IP 协议栈
├── RoboKit/                 # RPLIDAR 采集 + RRT 路径规划
├── lovlis/                  # 音频处理实验（Python）
├── IPGUI_Brand/             # 品牌资产
├── main.c                   # 桌面演示（IPGUI + elNET + SDL2）
├── Makefile                 # 顶层构建脚本
└── 移植手册.md               # IPGUI 最小移植路径
```

## 快速开始

### 桌面运行（SDL2）

依赖：`gcc`（MinGW-w64）、`SDL2`（仓库内已带 `SDL2.dll`）。

```bash
make            # 产物输出到 build/ESDBox.exe
./build/ESDBox.exe
```


## 文档导航

| 文档 | 内容 |
|---|---|
| [绘图 API 参考手册](ESDBox_IPGUI/docs/%E7%BB%98%E5%9B%BEAPI%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C.md) | `core/gfx` 全部绘图能力的图文详解：逐个 API 配一张效果图 + 可直接运行的调用代码（像素、直线、三角形、实心圆、圆弧、多边形、圆角背景 / 边框 / 外阴影、图像、文字、图标、渐变、混合、蒙版） |


移植手册和其他API参考手册还在路上呢。。。



## 移植到嵌入式板卡上
