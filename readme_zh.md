![](https://raw.githubusercontent.com/nwkyz/nwkyz-picbed/main/storage/goodhaptic-banner2.png)

<p align="center">在 Linux 中控制 Goodix 压感触控板</p>  

<p align="center">[ 中文 / <a href="readme.md">English</a> ]</p>


## 已知兼容

- **设备**：Goodix GXTP5100（USB `27c6:659a`，HID `27C6:01E7`）
- **接口**：`/dev/hidrawN`，通过 HID feature report 写入控制
- **已测试兼容设备**：Lenovo ThinkBook 16 G8+ IPD

## 功能

| 功能 | HID Report | 说明 |
|---|---|---|
| 振动力度 | `0x09` Set Feature | 0–100，控制触控板震动反馈强度 |
| 点击灵敏度 | `0x08` Set Feature | 1–3 档（轻/中/重），调节触发点击所需的按压力度 |
| 触控板模式 | `0x03` Set Feature | 切换精确触控板 / 鼠标模式 |
| 选择性上报 | `0x05` Set Feature | 独立控制触摸/按键数据的上报 |
| 设备能力查询 | `0x02` Get Feature | 读取最大触摸点数和触控板类型 |
| 固件版本 | sysfs | 读取固件版本号 |
| 触摸监控 | `0x04` Input | 实时显示手指位置、压力与按键状态 |

### 振动

- **档位模式**：轻 / 中 / 重 / 非常重（25 / 50 / 75 / 100）
- **无级调节**：开启后使用滑块 0–100 精确调节（部分设备可能不支持）
- **开关**：关闭振动开关可临时禁用震动反馈

### 点击

调节触控板点击灵敏度（Report `0x08`），三个档位对应不同按压力度阈值：

| 档位 | 力度阈值 | 体验 |
|---|---|---|
| 轻 | ~110 g | 轻触即可触发点击 |
| 中 | ~150 g | 中等力度 |
| 重 | ~190 g | 需要较重的按压 |

### 触摸监控

实时可视化触控板状态（通过 daemon 持续读取 Report `0x04`）：

- 5 指触摸位置可视化（触控板平面图 + 坐标）
- 每指压力柱状图
- 触点数 / 按键状态 / 扫描时间实时显示
- 自动适配 HID 描述符中的逻辑最大坐标

### 设备信息

自动读取并展示：
- 最大触摸点数
- 触控板类型（Touchpad / Clickpad / Precision Touchpad）

## 依赖

- meson (>= 1.0)
- gtk4 (>= 4.10)
- libadwaita-1 (>= 1.4)
- systemd

安装依赖:

```bash
# Debian/Ubuntu
sudo apt install meson libgtk-4-dev libadwaita-1-dev
```

## 快速使用

### 构建

```bash
meson setup build
ninja -C build
```

### 安装与卸载

```bash
# 安装（自动启用并启动 goodhapticd 守护进程）
sudo meson install -C build

# 卸载
sudo scripts/uninstall.sh
```

安装后 GUI 可通过 `goodhaptic` 命令或在应用菜单中启动。

## 开机恢复

`系统`中开启`开机恢复设置`后，goodhapticd 会在每次开机时自动将保存的力度和点击灵敏度写入硬件，防止重启后恢复默认值。

## 配置文件

位于:
`/etc/goodhaptic.conf`

例如:
```
device /dev/hidraw0
strength 50
threshold 2
inputmode 3
selective_surface 1
selective_button 1
persist 1
stepless 0
```

| 字段 | 说明 |
|---|---|
| `device` | hidraw 设备路径 |
| `strength` | 当前力度值 (0–100) |
| `threshold` | 点击灵敏度 (1=轻, 2=中, 3=重) |
| `inputmode` | 触控板模式 (0=鼠标, 3=精确触控板) |
| `persist` | 开机恢复 (0=关, 1=开) |
| `stepless` | 无级调节 (0=档位, 1=滑块) |
| `selective_surface` | 触摸上报开关 (0=关, 1=开) |
| `selective_button` | 按键上报开关 (0=关, 1=开) |

## 帮助翻译

翻译文件位于 `po/` 目录，使用 GNU gettext。

```bash
# 生成/更新翻译模板 (POT)
ninja -C build goodhaptic-pot

# 添加新语言（替换 xx 为语言代码）
msginit --locale=xx --input=po/goodhaptic.pot --output=po/xx.po

# 或从已有翻译更新
msgmerge --update po/xx.po po/goodhaptic.pot

# 编辑 po/xx.po 填入翻译后，将语言代码添加到 po/LINGUAS
echo "xx" >> po/LINGUAS

# 重新构建
meson setup build --reconfigure
ninja -C build
```

## DEB 打包

所有构建产物和中间文件（含 `.deb` / `.ddeb` / `.buildinfo`）都收拢在 `deb-build/` 目录

```bash
./scripts/build-deb.sh

# 安装
sudo apt install ./deb-build/goodhaptic_1.0-1_amd64.deb
```

## 已知信息

1. **定位输入设备**：`/sys/class/input/inputN` → `GXTP5100:00 27C6:01E7 Touchpad`，驱动 `hid-multitouch`
2. **HID Feature Report 清单**：

| Report | 类型 | 大小 | 用途 | 状态 |
|---|---|---|---|---|
| 0x02 | Feature | 2 bytes | 设备能力（最大触点数、触控板类型） | ✅ 已实现 |
| 0x04 | Input | ~38 bytes | Precision Touchpad 数据（5 指） | ✅ 已实现 |
| 0x08 | Feature | 2 bytes | 点击力度阈值 (1–3) | ✅ 已实现 |
| 0x09 | Feature | 2 bytes | 震动强度 (0–100) | ✅ 已实现 |
| 0x03 | Feature | 2 bytes (1 有效) | Input Mode（0=鼠标 3=PTP）¹ | ✅ 已实现 |
| 0x05 | Feature | 2 bytes | 选择性上报（Surface/Button Switch） | ✅ 已实现 |
| 0x07 | Feature | 2 bytes | 用途不明（写入后触控板几乎无法使用）³ | ⚠ 不实现 |
| 0x06 | Feature | 256 bytes | Vendor 配置数据 | ⏳ 待分析 |
| 0x0B | Feature | 66 bytes | Vendor 状态数据 | ⏳ 待分析 |
| 0x0C | Feature | 736 bytes | Vendor 校准数据 | ⏳ 待分析 |
| 0x0D | Feature | 4 bytes | 固件命令（Usage 0xC4）² | ⚠ 已分析 |
| 0x0F | Feature | any size | 未在描述符声明，只读全零⁴ | ⚠ 已分析 |

3. **Windows 分析**：通过 Procmon 确认 Windows 设置「触控板 → 震动力度」本质上是写 HID feature report
4. **方案**：向 `/dev/hidrawN` 写入 feature report + 数据，硬件实时响应
5. **¹**：Report 0x03 的 HID 描述符声明了 2 个 Input Mode 字段（Report Size 8 × Report Count 2），但固件仅响应第一个字节。写入 1 字节数据（`[0x03, mode]`）有效，2 字节（`[0x03, a, b]`）会被静默忽略。此行为与 Elan 0x300b 完全一致（[内核修复](https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/commit/?id=73e7d63efb4d774883a338997943bfa59e127085)）
6. **²**：Report 0x0D 的 GET_FEATURE 不支持（EINVAL），SET_FEATURE 成功但为固件命令写入入口，不可读。固件版本信息来自 `/sys/class/input/inputN/id/version`，非 HID report。
7. **³**：Report 0x07 写入值 1 后触控板几乎无法使用（光标难以移动，每次触碰触发点击），已从代码中完全移除。
8. **⁴**：Report 0x0F–0xFF 均未在 HID 描述符中声明，但 GET_FEATURE 全部返回成功，数据始终为全零。用途不明，不做写入操作。

## 结构

```
goodhapticd (systemd 服务, root)
  ├── 启动时读取 /etc/goodhaptic.conf
  ├── persist=1 时恢复力度和灵敏度到硬件
  ├── 监听 /run/goodhaptic/sock 接受 GUI 命令
  ├── 支持命令：STRENGTH, THRESHOLD, INPUTMODE, SELECTIVE, PERSIST, DEVICE, STEPLESS
  ├── 支持查询：CAPABILITY, RESOLUTION
  └── 支持流式推送：MONITOR (Report 0x04 实时触摸数据)

GUI (普通用户)
  ├── 通过 Unix socket 与 daemon 通信，无需 root
  ├── 振动：档位/滑块切换、开关
  ├── 点击：灵敏度三档切换、触控板模式切换
  ├── 系统：开机恢复、高级选择性上报
  ├── 触摸监控：实时位置/压力可视化
  └── 数据：设备能力、固件版本
```

## 限制

- **无法读取当前力度/灵敏度**：`HIDIOCGFEATURE` 对 report `0x08`、`0x09` 不生效，硬件不支持回读

## 测试工具

`test/` 目录下有独立探测脚本，需要 root 权限：

```bash
# 读取设备能力 (Report 0x02)
sudo python3 test/probe_02.py

# 读取当前灵敏度 (Report 0x08)
sudo python3 test/probe_08.py

# 设置灵敏度 (1=轻, 2=中, 3=重)
sudo python3 test/probe_08.py set 1

# 编译 C 版本工具
gcc -o test/probe_08 test/probe_08.c
sudo test/probe_08 set 2
```

使用前请先停止 daemon：`sudo systemctl stop goodhapticd`

## 许可证

GPLv3
