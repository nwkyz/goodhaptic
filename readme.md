![](https://raw.githubusercontent.com/nwkyz/nwkyz-picbed/main/storage/goodhaptic-banner2.png)
<center>在Linux中控制Goodix压感触控板</center>

## 已知兼容

- **设备**：Goodix GXTP5100（USB `27c6:659a`，HID `27C6:01E7`）
- **接口**：`/dev/hidrawN`，通过 HID feature report `0x09` 写入力度值（0–100）
- **已测试兼容设备**：Lenovo ThinkBook 16 G8+ IPD

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

`系统`中开启`开机恢复力度`后，goodhapticd 会在每次开机时自动将保存的力度写入硬件，防止重启后恢复默认值。

## 配置文件

位于:
`/etc/goodhaptic.conf`

例如:
```
device /dev/hidraw0
strength 50
persist 1
stepless 0
```

| 字段 |  |
|---|---|
| `device` | hidraw 设备路径 |
| `strength` | 当前力度值 (0–100) |
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
2. **尝试读取 HID feature report**：测试了 `0x02`、`0x06`、`0x09`、`0x0b`、`0x0c`、`0x0d`，部分可读
3. **Windows 逆向**：通过 Procmon 确认 Windows 设置「触控板 → 震动力度」本质上是写 HID feature report
4. **方案**：向 `/dev/hidrawN` 写入 feature report `0x09` + 力度值，震动强度实时变化

## 结构

```
goodhapticd (systemd 服务, root)
  ├── 启动时读取 /etc/goodhaptic.conf
  ├── persist=1 时恢复力度到硬件
  └── 监听 /run/goodhaptic/sock 接受 GUI 命令

GUI (普通用户)
  └── 通过 Unix socket 与 daemon 通信，无需 root
```

## 限制

- **无法读取当前力度**：`HIDIOCGFEATURE` 对 report `0x09` 不生效，硬件不支持回读
- **断电后可能恢复默认值**：硬件在完全断电（关机）后震动强度可能重置，可通过「开机恢复力度」功能弥补
- **设备检测**：通过扫描 `/sys/class/hidraw` 自动发现，但仅匹配 HID 设备名，不限定具体型号

## 许可证

GPLv3