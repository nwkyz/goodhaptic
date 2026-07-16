![](https://raw.githubusercontent.com/nwkyz/nwkyz-picbed/main/storage/goodhaptic-banner2.png)
<center>在 Linux 中控制 Goodix 压感触控板</center>
<center>[ 中文 / <a href="readme.md">English</a> ]</center>

## 已知兼容

- **设备**：Goodix GXTP5100（USB `27c6:659a`，HID `27C6:01E7`）
- **接口**：`/dev/hidrawN`，通过 HID feature report 写入控制
- **已测试兼容设备**：Lenovo ThinkBook 16 G8+ IPD

## 功能

| 功能 | HID Report | 说明 |
|---|---|---|
| 振动力度 | `0x09` Set Feature | 0–100，控制触控板震动反馈强度 |
| 点击灵敏度 | `0x08` Set Feature | 1–3 档（轻/中/重），调节触发点击所需的按压力度 |
| 设备能力查询 | `0x02` Get Feature | 读取最大触摸点数和触控板类型 |
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
persist 1
stepless 0
```

| 字段 | 说明 |
|---|---|
| `device` | hidraw 设备路径 |
| `strength` | 当前力度值 (0–100) |
| `threshold` | 点击灵敏度 (1=轻, 2=中, 3=重) |
| `persist` | 开机恢复 (0=关, 1=开) |
| `stepless` | 无级调节 (0=档位, 1=滑块) |

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

## DEB 打包 (暂时不可用)

所有构建产物和中间文件都收拢在 `deb-build/` 目录

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
| 0x04 | Input | ~38 bytes | Precision Touchpad 数据（5 指） | ✅ 已实现（监控） |
| 0x08 | Feature | 2 bytes | 点击力度阈值 (1–3) | ✅ 已实现 |
| 0x09 | Feature | 2 bytes | 震动强度 (0–100) | ✅ 已实现 |
| 0x03 | Feature | — | Digitizer 配置 | ⏳ 待研究 |
| 0x05 | Feature | — | Surface/Button 配置 | ⏳ 待研究 |
| 0x06 | Feature | 256 bytes | Vendor 命令 | ⏳ 待逆向 |
| 0x0B | Feature | 66 bytes | Vendor 状态 | ⏳ 待逆向 |
| 0x0C | Feature | 736 bytes | 校准数据 | ⏳ 待逆向 |
| 0x0D | Feature | 4 bytes | 固件信息 | ⏳ 待实现 |

3. **Windows 逆向**：通过 Procmon 确认 Windows 设置「触控板 → 震动力度」本质上是写 HID feature report
4. **方案**：向 `/dev/hidrawN` 写入 feature report + 数据，硬件实时响应

## 结构

```
goodhapticd (systemd 服务, root)
  ├── 启动时读取 /etc/goodhaptic.conf
  ├── persist=1 时恢复力度和灵敏度到硬件
  ├── 监听 /run/goodhaptic/sock 接受 GUI 命令
  ├── 支持命令：STRENGTH, THRESHOLD, PERSIST, DEVICE, STEPLESS
  ├── 支持查询：CAPABILITY, RESOLUTION
  └── 支持流式推送：MONITOR (Report 0x04 实时触摸数据)

GUI (普通用户)
  ├── 通过 Unix socket 与 daemon 通信，无需 root
  ├── 振动：档位/滑块切换、开关
  ├── 点击：灵敏度三档切换
  ├── 触摸监控：实时位置/压力可视化
  └── 数据：设备能力信息展示
```

## 限制

- **无法读取当前力度/灵敏度**：`HIDIOCGFEATURE` 对 report `0x08`、`0x09` 不生效，硬件不支持回读
- **断电后可能恢复默认值**：硬件在完全断电（关机）后震动强度和灵敏度可能重置，可通过「开机恢复设置」功能弥补
- **设备检测**：通过扫描 `/sys/class/hidraw` 自动发现，但仅匹配 HID 设备名，不限定具体型号
- **监控需要 daemon 支持**：触摸监控功能需要 daemon 持续读取 `/dev/hidrawN`，仅在有设备连接时可用
- **无级调节兼容性**：部分设备可能在无级调节模式下表现异常，可切换回档位模式使用

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
