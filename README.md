# Kwrt Studio

用于 **KWRT 固件转换、网络预配置和插件查看**的 Windows 桌面工具。

原生 C++ / Win32，完整解压即可使用，无需安装 Python、Docker、WSL 或额外运行库。

> 当前已验证的转换机型只有红米 AX6000，详细范围见下方。其他型号可以按支持的镜像格式查看软件包，但不代表可以转换。

## 能做什么

- **转换为对应的 stock 布局**：保留原有软件包，不自行裁剪插件或增加核心。
- **提前设置网络**：拨号、LAN 地址、Wi-Fi、后台密码以及 IPv6，刷入后自动初始化。
- **识别旁路由**：隐藏拨号和 Wi-Fi 输入框，显示主路由网关、DNS 和 DHCP 设置；无法确定时手动选择。
- **修改设备信息**：主机名、自定义署名，以及支持的 KWRT 作者链接开关。
- **查看插件列表**：支持 opkg / apk 包记录，显示名称、版本和状态，可搜索并切换全部软件包。
- **检查生成结果**：校验元数据、FIT、分区、文件权限和链接，重打包后再次回读；不覆盖原始镜像。

## 开始使用

1. 完整解压 Windows x64 下载包，运行 `KwrtStudio.exe`，保留旁边的 `tools` 文件夹。
2. 选择原始 KWRT `sysupgrade.bin`，或将文件拖入窗口。
3. 等待识别完成，在“网络设置”确认模式和账号信息。需要改主机名或签名时，打开“设备信息 · 可选”。
4. 点击“开始转换”，完成后点击“打开结果”。每次转换保存到独立目录。
5. 要应用填写的初始化设置，刷入时取消“保留配置”。工具本身不会连接或刷写路由器。

公开下载包不包含个人配置。界面密码默认明文显示，取消“显示密码”可隐藏。点击“保存设置”后，设置写入程序旁的 `profile.json`；该文件及个人固件应自行保管。

### 主路由与旁路由

| 模式 | 需要填写 | 初始化行为 |
| --- | --- | --- |
| 主路由（拨号） | 宽带账号密码、LAN 地址、Wi-Fi、后台密码 | 设置 PPPoE，生成名称分别带 `_2.4G`、`_5G` 后缀的 Wi-Fi |
| 旁路由 | LAN 地址、主路由网关、DNS、DHCP、后台密码 | 通过 LAN 连接主路由，停用 WAN 拨号，保留固件原有无线设置 |

旁路由地址和主路由网关必须位于同一网段，且不能相同。DNS 留空时使用网关。启用旁路由 DHCP 前应关闭主路由 DHCP；关闭旁路由 DHCP 时，需自行安排客户端的网关和 DNS。当前旁路由初始化使用 IPv4，关闭 WAN6 和 LAN IPv6 地址分配及通告。

### 插件列表的含义

“查看插件”读取固件内的软件包记录，不要求先转换。默认显示 `luci-app-*`，取消“只看插件”后显示全部包。

未安装或未标记安装的记录仍会列出；列表中出现名称不等于已经可用。没有包记录的手动复制核心文件不会出现在列表里。

## 转换兼容范围

| 输入 | 输出 | 状态 |
| --- | --- | --- |
| Xiaomi Redmi Router AX6000，KWRT 常规版，已核对的单个 110 MiB UBI 布局 | stock：30 MiB 内核分区 + 80 MiB 系统分区 | 已实现适配与隔离初始化测试 |
| 已经是 stock 的镜像、U-Boot mod 布局、其他型号或其他发行版 | — | 当前不支持转换 |

**stock 不是跨型号统一的分区格式。** 工具需要匹配具体硬件、启动布局与固件结构，不负责首次修改 U-Boot 或启动环境。内部结构有变化的新版固件可能被拒绝，需要更新适配规则。

包列表读取与转换适配独立：已验证 KWRT 的 opkg 列表和 ImmortalWrt 的 apk 列表，并不表示 ImmortalWrt 支持转换。

开发与新增型号说明：[docs/ADAPTERS.md](docs/ADAPTERS.md)。

## 自行编译

Windows x64，安装 Visual Studio 2022 C++ 桌面开发工具、Windows SDK 和 CMake 3.21+：

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
cmake --install build --config Release --prefix dist/Kwrt-Studio
```

生成 `build/Release/KwrtStudio.exe`。安装步骤将程序、组件、文档、示例和对应源码放入 `dist/Kwrt-Studio`，不会复制个人配置。GitHub Actions 会执行构建、自检和打包。

仓库的 `tools/` 是固定版本的 Windows 原生 SquashFS 工具，主程序运行前验证其哈希。对应源码与许可证一并提供，组件升级时应同步更新并重新验证。

### 命令行

```text
KwrtStudio.exe --self-test
KwrtStudio.exe --catalog
KwrtStudio.exe --inspect firmware.bin
KwrtStudio.exe --packages firmware.bin
KwrtStudio.exe --convert firmware.bin --profile profile.json --output result
```

`profile.example.json` 只含示例值。自动调用 GUI 子系统程序时，需等待进程退出并检查退出码，不能只以终端提示符返回判断完成。

## 测试与许可证

自检覆盖校验算法、TAR/FDT、配置校验、Shell 转义、软件包记录、旁路由识别等。已使用本地固件完成独立解包、全量文件比对及隔离环境内实际 ARM 初始化验证。真实路由器启动、无线性能和代理使用效果需单独验证。

本项目原创代码采用 [MIT](LICENSE)；随包组件保留各自的许可证，详见 [THIRD-PARTY.md](THIRD-PARTY.md)。本项目与 KWRT、OpenWrt、ImmortalWrt 均无官方隶属关系。
