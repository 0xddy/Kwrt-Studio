<div align="center">

# Kwrt Studio

**保留插件，刷机前配好网络。**

KWRT 固件布局转换 · 网络预配置 · 插件查看

[下载最新版](https://github.com/0xddy/Kwrt-Studio/releases/latest) · [更新记录](CHANGELOG.md) · [开发文档](docs/ADAPTERS.md)

[![Release](https://img.shields.io/github/v/release/0xddy/Kwrt-Studio?style=flat-square&color=315be2)](https://github.com/0xddy/Kwrt-Studio/releases/latest)
[![Build](https://github.com/0xddy/Kwrt-Studio/actions/workflows/windows.yml/badge.svg)](https://github.com/0xddy/Kwrt-Studio/actions/workflows/windows.yml)

</div>

![Kwrt Studio 固件选择界面](docs/images/home.jpg)

## 一次设置，每次刷机都省心

将 KWRT 固件适配到设备对应的 stock 布局，保留原有软件包，并将网络设置写入新固件，省去手动处理布局和刷机后重复配置。

| 功能 | 能做什么 |
| :--- | :--- |
| **固件转换** | 适配已支持设备的 stock 布局，保留原有插件，另存新固件。 |
| **网络预设** | 提前设置拨号、Wi-Fi、后台地址和密码，自动区分主路由与旁路由。 |
| **插件查看** | 搜索固件内的插件与软件包，查看版本、说明和安装状态。 |
| **设备定制** | 设置主机名、自定义签名及 KWRT 作者链接开关。 |

## 兼容范围

| 项目 | 当前支持 |
| :--- | :--- |
| 软件平台 | Windows 10 / 11 x64 |
| 固件转换 | 红米 AX6000 · KWRT 单个 110 MiB UBI → stock 30 MiB 内核 + 80 MiB 系统 |
| 插件查看 | 支持读取 opkg / apk 软件包记录 |

已是 stock 的镜像、U-Boot mod 和其他机型暂不支持转换。Linux / macOS 暂未支持。

---

[MIT License](LICENSE) · [第三方组件](THIRD-PARTY.md)

本项目与 KWRT、OpenWrt、ImmortalWrt 无官方隶属关系。
