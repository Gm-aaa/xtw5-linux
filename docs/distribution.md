# Linux 二进制包

GitHub Actions 在各发行版容器中独立编译、运行全部离线测试，并对生成的压缩包执行无显示器模拟启动检查。
目前目标架构为 **x86_64**：Ubuntu 22.04 / 24.04、Debian 12 / 13（DEB），Fedora 44、Rocky Linux 9、openSUSE Tumbleweed（RPM），Arch Linux rolling（pkg.tar.zst）。每个平台另外生成 tar.gz。其他版本及 ARM 尚未纳入构建矩阵。

从 Release 选择与系统匹配的包。下载后在下载目录运行 `sha256sum -c SHA256SUMS --ignore-missing`。

- Ubuntu / Debian：`sudo apt install ./xtw5-linux-*.deb`
- Fedora / Rocky：`sudo dnf install ./xtw5-linux-*.rpm`
- openSUSE：`sudo zypper install ./xtw5-linux-*.rpm`
- Arch：`sudo pacman -U ./xtw5-linux-*.pkg.tar.zst`

包管理器会处理 Qt 6、libusb、OpenSSL 依赖。桌面入口名为 XTW Studio。DEB / RPM 安装时重载 udev 规则；Arch 可执行 `sudo udevadm control --reload-rules`。安装后重新插拔编程器，以当前桌面用户启动，无需 root。

Rocky Linux 9 的 Qt 6 来自 EPEL，首次安装前需执行 `sudo dnf install epel-release dnf-plugins-core` 和 `sudo dnf config-manager --set-enabled crb`。

## tar.gz

这些是**对应发行版的动态链接二进制归档**，不是包含所有依赖的跨发行版静态包。应选择与当前系统匹配的归档，并安装 Qt 6 Widgets / Concurrent 及其平台插件、libusb、OpenSSL；Wayland 会话建议安装 Qt 6 Wayland 插件，中文显示需 CJK 字体。

解压后从顶层目录运行 `./usr/bin/xtw5-linux`，或以 `--demo` 启动模拟模式。USB 规则在 `usr/lib/udev/rules.d/70-xtw5.rules`，首次使用可复制到 `/etc/udev/rules.d/`，重载规则并重新插拔设备。归档也包含桌面入口、图标及文档。

## 首次使用

不分发原厂 EXE、数据库、解密后的芯片库或任何固件备份。首次运行请通过“设置 → 导入芯片库”导入自己持有的 20250524 资料包中的 mdb.yg 和已解包的 XTW-5.exe，或导入自己的 JSON 芯片库；也可添加自定义型号。模拟模式无需导入数据库。

## 自动发布

`main`、PR 和手动触发只构建测试并保留 Actions artifacts。推送与 CMake 版本一致的 `v*` 标签后，全部矩阵任务成功才会创建 GitHub 预发行版，附二进制、tar.gz 和 SHA256SUMS。构建任务只读仓库；仅发布任务具备 contents:write。工作流没有实机测试或 USB 透传。
