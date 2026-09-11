# 开发与设备适配

## 架构

`src/main.cpp` 是原生 Win32 GUI；`common.cpp` 负责配置、编码、文件和校验；`formats.cpp` 负责 OpenWrt 元数据、TAR、FDT 及文件清单；`converter.cpp` 组织通用转换过程。

`src/adapter.hpp` 定义 `DeviceAdapter`，`adapters.cpp` 是注册表。`adapter_ax6000.cpp` 包含 AX6000 的实际布局、设备别名、升级逻辑与容量规则；`init_template.hpp` 是该适配器的初始化模板。主流程不包含 AX6000 的闪存偏移或容量常量。

当前引擎支持带 OpenWrt FWx0 元数据、含 CONTROL/kernel/root 的 sysupgrade TAR、XZ SquashFS 4，以及 opkg 软件包清单。FDT/FIT 修改在设备适配器中完成。并非所有 KWRT 镜像都采用这些容器或包管理格式。

## 新增型号

1. 获取该型号、硬件版本和目标固件的原始镜像，以及实际路由器的分区、启动和网口资料。原厂布局与修改过的 U-Boot 布局需分别对待。
2. 新建一个实现 `DeviceAdapter` 的 C++ 文件。严格匹配发行版、target、board 和 supported_devices；不能只匹配品牌或文件名。
3. 在 `validateInput()` 核对真正的设备树、原始分区及依赖的系统结构，防止错误镜像仅靠元数据伪装成可转换文件。
4. 实现内核/设备树修改、文件系统适配、容量计算及输出元数据。保持启动代码和其他硬件节点不变；不支持的压缩、签名或容器必须明确拒绝。
5. `transformFilesystem()` 声明所有允许修改、新增的路径。主流程自动验证：无原文件删除、无未声明的修改、原软件包列表不变，并检查重打包后的权限、所有者、时间、设备节点和链接。
6. 新增该设备的网络初始化规则，核对 LAN/WAN、VLAN、无线频段、板级配置顺序，以及固件自带向导的行为。
7. 在 `adapters.cpp` 注册工厂函数，并加入 CMake 构建源文件列表。适配器编译进 EXE；当前没有任意 JSON 改分区或加载外部脚本的入口。
8. 增加已知正确镜像、错误型号、容量不足、校验损坏和升级分派测试。独立解包回读、路由器只读镜像测试与实际启动测试是不同层次，应分别记录。

新增设备如果不是本引擎支持的 TAR/XZ/opkg 组合，还要扩展容器、文件系统或软件包解析层。不能只添加一个分区表就宣称兼容。

## 构建

安装 Visual Studio 2022 C++ 桌面开发工具、Windows SDK 和 CMake 3.21+，在仓库根目录运行：

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
cmake --install build --config Release --prefix dist/Kwrt-Studio
```

程序生成于 `build/Release/KwrtStudio.exe`；安装包目录为 `dist/Kwrt-Studio`，包含程序、组件、文档、示例配置及对应源码。GitHub Actions 执行相同的构建、测试和打包流程。

主程序运行前会核对 `tools/` 组件的哈希。替换组件时，同步更新对应源码、许可证和 `tool_hashes.hpp` 中的 SHA-256。

## 命令行

```text
KwrtStudio.exe --self-test
KwrtStudio.exe --catalog
KwrtStudio.exe --inspect <firmware.bin>
KwrtStudio.exe --packages <firmware.bin>
KwrtStudio.exe --convert <firmware.bin> --profile <profile.json> --output <empty-directory>
```

配置示例见 `profile.example.json`。自动调用时须等待进程退出并检查退出码；PowerShell 提示符返回不代表转换完成。

## 自动构建与发布

`windows.yml` 在提交到 main 或创建 PR 时编译、自检并保存下载包。`release.yml` 复用同一构建流程，在推送 `v版本号` 标签时将 ZIP 和 SHA-256 校验文件发布到 GitHub Release。

标签必须与 `CMakeLists.txt` 和 EXE 的版本一致，例如 `v1.3.2`。也可在 Actions 中手动运行 Release，按所选提交的版本号创建标签并发布。已有版本标签须指向同一提交，已发布的附件不会被覆盖；未完成的草稿可重新运行补齐。发布说明取自 `CHANGELOG.md` 的对应版本。

构建阶段只有读取权限，发布阶段使用仓库自带的 `GITHUB_TOKEN`。通过构建、自检、版本核对和下载包校验后才发布，无需额外配置令牌。

## 平台支持

当前构建目标为 Windows x64。Win32 界面、CNG 校验、进程管理和随包 SquashFS 组件依赖 Windows；支持 Linux/macOS 需要移植这些部分并验证对应工具，不能仅增加工作流矩阵。

## AX6000 适配范围

首个适配器验证的输入是本地测试用 KWRT 25.12-SNAPSHOT / mediatek/filogic / Redmi AX6000 镜像，SHA-256：

```text
6885c13bd3f9ce9c83ba676907dc6364918e98c1ad586020757d257635f8711a
```

目标是已核对的 NMBM stock 分区：`ubi_kernel` 位于 `0x600000`、大小 `0x1e00000`；`ubi` 位于 `0x2400000`、大小 `0x5000000`。容量检查按已验证的 LEB 大小和保守可用块数计算，并保留至少 16 MiB 原始数据卷空间；实际 UBIFS 可用空间会更少。

这些数值仅属于此适配器。旧版 18.06、未知改版、其他 U-Boot 以及其他路由器，不能据此推断兼容。

## 插件查看

`readFirmwarePackages()` 在只读流程中校验元数据、读取 SquashFS，再解析固件内的 opkg 或 apk 文本数据库。该流程不调用设备转换规则，不读取个人网络配置，也不改写输入镜像。

`packages.cpp` 解析包名、版本、说明和安装状态；所有包记录均保留，未标记安装的项目另行显示状态。`package_view.cpp` 提供原生列表、关键词筛选和插件/全部软件包切换。插件分类依据包名 `luci-app-*`，不把手动复制的无包记录文件推断为已安装软件包。

命令行可用 `KwrtStudio.exe --packages <firmware.bin>` 输出 JSON 列表。新增的解析测试包含 CRLF、多行描述、安装状态、apk 文件记录、缺失版本和取消读取。

## 网络模式与设备信息

`network_mode.cpp` 只读取已知 UCI 配置和脚本中的常量，不执行镜像中的代码。明确的 `wizard.default.siderouter` 优先；动态值或冲突值返回 unknown。本工具生成的固件通过自己的模式常量识别。识别结果和网关、DNS、DHCP 默认值随 `--packages` 的 `network_defaults` 返回。未知模式需要用户选择。

`customization.cpp` 实现 KWRT 的签名位置校验及替换，修改 `usr/lib/os-release` 中的作者字段和 `etc/banner` 的署名，并对 Shell 特殊字符转义。去除作者外链使用固件现有的 `base_config.@status[0].links` 开关；不删除许可证或第三方插件文件。主机名在首次启动时通过 UCI 设置。

个人配置增加可选字段 `routing_mode`（router/side）、`side_gateway`、`side_dns`、`side_dhcp`、`hostname`、`signature`、`remove_author_links`。旧配置仍可加载；界面选取固件后以检测结果选择网络模式。旁路由允许拨号及 Wi-Fi 字段为空，网关须和后台地址处于同一网段且不同址。DNS 留空时使用网关。

旁路由初始化保留固件无线设置，停用 WAN 拨号、WAN6 与 LAN 的 IPv6 地址分配/通告，设置 LAN 网关/DNS 和 LAN 区域 IPv4 masquerading。DHCP 开启时通告本机 LAN 地址为客户端的网关和 DNS；关闭时停止 LAN DHCP。用户应按自己的网络安排主路由 DHCP。初始化同步 KWRT 向导，避免向导重新应用相反的 DHCP 模式。

## 验证

自检覆盖校验算法、TAR/FDT、配置校验、Shell 转义、软件包解析及模式识别。转换结果已完成独立解包、全量文件比对和 ARM 初始化隔离测试；这些检查不替代真实设备的启动、无线及插件功能测试。

## 可选 SSH 检测

`router_view.cpp` 提供独立窗口，不改变 `Request`、转换按钮条件或命令行转换流程。未选固件时可查看路由器布局；选定固件后再比对对应的设备适配器和镜像结构。

`ssh_client.cpp` 使用静态 libssh2 / Windows CNG，支持 IPv4、IPv6 地址和密码认证。默认网关来自 Windows IPv4 路由表，筛选已连接的以太网与 Wi-Fi 接口，按路由与接口的合计 metric 排序；手动填写不会被后台刷新覆盖。SSH 当前支持 RSA / ECDSA，暂不支持仅提供 Ed25519 的服务器或密钥登录。

登录前核对 SHA-256 主机指纹，已确认的指纹仅保留在当前程序会话。密码不写入配置、报告、日志或命令参数。连接、登录和查询分别有超时，可取消。查询只执行固定的只读命令，各文件通过独立 SSH 通道传输，不要求路由器提供 base64，不上传任何文件。

读取 `/tmp/sysinfo/board_name`、`/proc/mtd`、`/sys/firmware/fdt`、`/proc/cmdline`、`/proc/mounts` 及可用的 MTD sysfs 偏移。`router_check.cpp` 解析有界数据，`DeviceAdapter::routerAdvice()` 保存各机型规则。不得根据 `/proc/mtd` 列表累加偏移，因为父设备可能与分区重叠。

AX6000 规则同时核对板号、设备树 compatible、NMBM、全部分区的位置和大小、实际 MTD 大小及擦除块；若 sysfs 提供偏移也必须一致。恢复环境、分区覆盖参数、重复分区、缺失或矛盾信息均不作匹配结论。返回结果只确认观测到的布局，不能证明引导程序兼容，也不作为刷写许可。

原生自检包含 stock、110 MiB UBI、同容量不同偏移、父子 MTD 重叠、未知分区、重复记录、设备树损坏、恢复启动、错误板号及输入固件错误等用例。SSH 传输另经本机 RSA/ECDSA 测试服务器验证完整读取、指纹变化、错误密码、取消、超时和中途断开。
