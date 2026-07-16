# test/ — Goodix HID Probe Scripts

调试脚本和测试数据，用于探测 Goodix GXTP5100 触控板的 HID Feature Report。

## 已有脚本

| 脚本 | 目标 | 语言 |
|---|---|---|
| `probe_08.py` | Report 0x08 — Click Force Threshold | Python 3 |
| `probe_08.c` | Report 0x08 — Click Force Threshold | C |

## Report 0x08 — Click Actuation Force Threshold

### 解码信息

```
Usage Page:    Digitizers (0x0D)
Usage:         0x00B0
Physical Min:  110 g
Physical Max:  190 g
Logical Min:   1
Logical Max:   3
Report Size:   2 bits
Type:          Feature (Data, Var, Abs, No Wrap, Linear, Preferred, No Null)
```

### 功能推测

控制触控板震动力度等级，数值 1–3 映射到不同的力度阈值：

| 值 | 力度阈值 | 体验 |
|---|---|---|
| 1 | ~110 g | 轻触即可触发震动反馈 |
| 2 | ~150 g | 中等力度 |
| 3 | ~190 g | 需要较重的按压 |

### 使用

```bash
# Python (推荐快速测试)
sudo python3 test/probe_08.py                  # 读取当前值
sudo python3 test/probe_08.py set 1            # 设为最轻
sudo python3 test/probe_08.py set 3            # 设为最重
sudo python3 test/probe_08.py /dev/hidraw0 set 2

# C
gcc -o test/probe_08 test/probe_08.c
sudo test/probe_08                             # 读取当前值
sudo test/probe_08 set 1                       # 设为最轻
sudo test/probe_08 set 3                       # 设为最重
```

### 测试步骤

1. 先读取当前值：`sudo python3 test/probe_08.py`
2. 在 1/2/3 之间切换
3. 每次切换后在触控板上按压，感受触发震动的力度变化
4. 记录结果到下方的测试日志

---

## 测试日志

### Report 0x08

| 日期 | 设备 | 值 | 观察到效果 |
|---|---|---|---|
| - | - | - | - |

---

## 待添加脚本

| Report | 功能 | 优先级 |
|---|---|---|
| 0x07 | 低延迟模式开关 | P1 |
| 0x02 | 设备能力读取 | P1 |
| 0x0B | Vendor 状态读取 | P2 |
| 0x0D | 固件信息读取 | P2 |
| 0x0C | 校准数据 dump | P2 |
| 0x06 | Vendor 命令探测 | P3 |
| 0x0E | 双向通信探测 | P3 |

## 注意

- 所有脚本需要 root 权限（`sudo`）才能访问 `/dev/hidrawN`
- 操作前确认没有其他程序（如 goodhapticd）正在使用同一设备
- 先停止 goodhapticd：`sudo systemctl stop goodhapticd`
