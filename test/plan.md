# Goodix HID Touchpad — 已确认能力 & 待实现计划

## 设备

- 型号：Goodix GXTP5100（USB `27c6:659a`，HID `27C6:01E7`）
- 接口：Linux HID Raw Device `/dev/hidraw0`
- 协议：USB/I2C HID，Windows Precision Touchpad 兼容
- 已测试设备：Lenovo ThinkBook 16 G8+ IPD

---

## 一、HID Report 总览

| Report ID | Type         | 用途 |
|-----------|--------------|------|
| 0x01      | Input        | 鼠标兼容模式 |
| 0x04      | Input        | Precision Touchpad 数据（5 指） |
| **0x02**  | **Feature**  | **触控板能力（已实现）** |
| 0x07      | Feature      | ❌ 未知功能（测试导致触控板不可用，已移除） |
| **0x09**  | **Feature**  | **Haptic 强度（已实现）** |
| **0x08**  | **Feature**  | **点击力度阈值（已实现）** |
| 0x06      | Feature      | Vendor 数据（256 bytes） |
| 0x0D      | Feature      | Vendor 数据（4 bytes） |
| 0x0C      | Feature      | Vendor 大块配置（736 bytes） |
| 0x0B      | Feature      | Vendor 状态（66 bytes） |
| 0x03      | Feature      | Digitizer 配置 |
| 0x05      | Feature      | Surface/Button 配置 |
| 0x0E      | Input/Output | Vendor 双向通道（64 bytes） |

---

## 二、Precision Touchpad Input（Report 0x04）

每指数据：

| 字段 | Usage | 大小 | 范围 | 说明 |
|------|-------|------|------|------|
| Tip Switch | 0x42 | 1 bit | 0/1 | 手指是否接触 |
| Confidence | 0x47 | 1 bit | 0/1 | 有效触摸 |
| Contact ID | 0x51 | 4 bit | 0-15 | 手指编号 |
| X | 0x30 | 16 bit | 0-4149 | 横坐标 |
| Y | 0x31 | 16 bit | 0-2147 | 纵坐标 |
| Pressure | Tip Pressure | 16 bit | 0-2000 | 按压力度 |

全局状态：

| 字段 | Usage | 说明 |
|------|-------|------|
| Scan Time | 0x56 | 16 bit 扫描时间 |
| Contact Count | 0x54 | 8 bit，当前触摸点数 |
| Button | Button 1 | ClickPad 按键状态 |

---

## 三、已实现功能

### Report 0x09 — Haptic 强度控制

- Usage Page: Haptics (0x0E)，Usage: Intensity (0x23)
- 范围：0–100（0=无震动，100=最大震动）
- 实现：`src/haptic.c` → `haptic_set()` + `haptic_get()`
- 读写限制：只支持 SET_FEATURE，GET_FEATURE 不支持

### Report 0x08 — 点击力度阈值

- Usage Page: Digitizers (0x0D)，Usage: 0x00B0（Goodix 专有）
- 物理范围：110g–190g，逻辑 1-3 档
- 实现：`src/haptic.c` → `haptic_set_threshold()`
- 读写限制：只支持 SET_FEATURE，GET_FEATURE 不支持

### Report 0x02 — 设备能力查询

- Usage Page: Digitizers (0x0D)，Usage: 0x55 (Contact Max) + 0x59 (Pad Type)
- 范围：4 bit × 2，最大触控点数 + 触控板类型
- 实现：`src/haptic.c` → `haptic_get_capability()`，通过 daemon 代理读取
- GUI：底部「数据」section 展示

---

## 四、待实现计划

### P0 — 立即可实现（Feature Report 已确认，复制 0x08/0x09 模式即可）

#### 4.1 Report 0x07 — ~~低延迟模式~~ ❌ 不可用

```
Usage Page: Digitizers (0x0D)
Usage:      0x60
大小:       1 bit data + 15 bits const = 2 数据字节
类型:       Feature
值:         0/1
```

**测试结果**：写入 1 后触控板几乎无法使用（光标难以移动，每次触碰触发点击）。尝试修复 buffer 大小（2→3 字节）后仍不正常。已从代码中完全移除。

**结论**：此 report 的实际功能未知（非低延迟模式），当前不实现。如需继续研究，需在 Windows 下抓包对照。

---

#### 4.2 Report 0x02 — 设备能力查询 ✅ 已完成

```
Usage Page: Digitizers (0x0D)
Usage:      0x55 (Contact Count Maximum), 0x59 (Pad Type)
大小:       4 bit × 2
类型:       Feature（可读）
```

**已实现**：
- `src/haptic.c` → `haptic_get_capability()`
- daemon 添加 `CAPABILITY` 命令代理读取（GUI 无 root 权限）
- GUI 底部「数据」section 展示最大触摸点数和触控板类型
- `test/probe_02.py` 独立探测脚本

---

#### 4.3 Report 0x03 — Input Mode / Device Mode ✅ 已分析

```
Usage Page: Digitizers (0x0D)
Usage:       0x0E (Device Configuration) → 0x22 (Finger) → 0x52 (Input Mode)
大小:        1 byte 有效数据（描述符声明 2 bytes，固件只认第一个）
类型:        Feature (Data, Var, Abs)
描述符位置:  偏移 0x2C0–0x2D8
```

**分析结果**（2026-07-23，更新于同日）：
- Usage 0x52 = **Input Mode / Device Mode**，符合 Microsoft PTP 规范
  - 0 = Mouse mode（多点触控关闭，PTP 报表降至 ~7/s）
  - 3 = Precision Touchpad mode（多点触控正常，PTP 报表 ~100/s）
- GET_FEATURE：不支持（EINVAL），所有 buffer 大小均失败
- SET_FEATURE：**有效！但必须用 1 字节数据（2 字节 buffer: [0x03, mode]）**
  - 3 字节 buffer（[0x03, a, b]）固件**静默忽略**，所有大小均返回成功但无效果
  - 描述符声明 Report Count 2 但固件只认第一个字段，与 Elan 0x300b 完全一致
- 内核 hid-multitouch 在初始化时将 mode 设为 3（PTP）

**API 接口**：
```bash
sudo python3 test/probe_03.py mouse   # 切换到鼠标模式
sudo python3 test/probe_03.py ptp     # 切换到 PTP 模式
sudo python3 test/probe_03.py bench   # 对比两种模式
```

**结论**：暂不集成到 goodhaptic。Input Mode 切换在 Linux 下副作用大（mode 0 会使内核的触摸板输入设备无法正常工作），用户在需要时可用 probe 脚本。

---

#### 4.4 Report 0x05 — Surface/Button 配置

```
类型: Feature
用途: 表面按压区域、按键行为
```

**实现**：同 0x03，先 dump 再决定。

**预计**：先 dump，后续决定。

---

### P1 — 读取 Input Report（需要 daemon 持续读 /dev/hidrawN）

#### 4.5 Report 0x04 — 实时触摸/压力监控

**能力**：
- 压力可视化（5 指热力图）
- 压力触发震动（用户设阈值，模拟 Force Touch）
- ClickPad 按键状态读取
- 触摸事件日志

**实现要点**：
- daemon 中 `read()` `/dev/hidrawN` 获取 Input Report，按 report descriptor 解析
- 可通过 Unix socket 把数据推送给 GUI（类似现有 STRENGTH 命令反向）
- 或做成独立小工具（CLI 实时打印触摸状态）

**预计**：2-4 小时。

---

#### 4.6 Report 0x0B — Vendor 状态

```
大小: 66 bytes，Usage: 0xC7
推测: 运行时状态、诊断数据
```

**实现**：周期性读取 66 bytes，dump 分析变化规律。

**预计**：先 dump 分析，1-2 小时。

---

#### 4.7 Report 0x0D — 固件命令 ✅ 已分析

```
Usage Page:    Vendor Defined (0xFF00)
Usage:         0xC4
大小:          4 bytes (Report Count 4 × Report Size 8)
类型:          Feature (Data, Var, Abs)
描述符位置:    偏移 0x298
```

**分析结果**（2026-07-23）：
- GET_FEATURE：不支持（EINVAL，所有 buffer 大小均失败）
- SET_FEATURE：成功（所有 buffer 大小 2–6），但无可见效果
- 结论：0x0D 不是查询接口，是**固件命令写入入口**（可能用于固件更新/配置下发）
- 固件版本信息实际来源：`/sys/class/input/inputN/id/version`（`0100` = v1.00），与 USB descriptor `bcdDevice=1.00` 一致
- GUI 数据部分已通过 sysfs 直接读取并展示固件版本

**结论**：不在 goodhaptic 中操作 0x0D（涉及固件写入，风险高）。固件版本已通过 sysfs 读取显示。

---

### P2 — 需要逆向（Vendor Page 0xFF00）

#### 4.8 Report 0x0C — 校准数据（736 bytes）

```
大小: 736 bytes，Usage: 0xC6
推测: 压力曲线、传感器校准、Haptic 调校参数、固件配置
```

**实现路径**：
1. 读取当前 736 bytes 保存为 baseline
2. 在不同强度/阈值设置下再读，对比差异找字段
3. 参考 GXTP5100 Windows 驱动行为对照

**预计**：需要持续逆向分析。

---

#### 4.9 Report 0x06 — Vendor 命令（256 bytes）

```
大小: 256 bytes，Usage: 0xC5
推测: 命令缓冲区、配置、固件接口
```

**实现路径**：同 0x0C，配合 0x0E 双向通道分析。

**预计**：需要持续逆向分析。

---

#### 4.10 Report 0x0E — 双向通信（64 bytes）

```
类型: Vendor Input + Output
大小: 64 bytes
推测: 命令/响应协议通道
潜在用途: 固件通信、校准、工厂模式
```

**实现路径**：抓包分析 + 对照 Windows 驱动行为。

**预计**：需要持续逆向分析。

---

## 五、实现模式参考

任何 Feature Report 的读写操作，直接复制 `haptic_set_threshold()` 模式：

```c
// 写（所有 Feature Report 通用）
int fd = open(device, O_RDWR);
unsigned char buf[2] = {REPORT_ID, value};
int ret = ioctl(fd, HIDIOCSFEATURE(sizeof(buf)), buf);
close(fd);

// 读（需要确认设备是否支持）
int fd = open(device, O_RDWR);
unsigned char buf[N] = {REPORT_ID, 0, ...};  // N = 数据字节数 + 1
int ret = ioctl(fd, HIDIOCGFEATURE(sizeof(buf)), buf);
close(fd);
```

完整流程（以添加新控制项为例）：
1. `src/haptic.h` → 声明函数
2. `src/haptic.c` → 实现函数
3. `src/config.h` → 添加字段
4. `src/config.c` → 添加读写
5. `src/daemon.c` → 添加命令 + apply 恢复
6. `src/window.c` → 添加 GUI 控件

---

## 六、调试脚本

见 `test/` 目录：

| 文件 | 用途 | 状态 |
|------|------|------|
| `test/probe_08.py` | Report 0x08 Python 探测（读+写） | ✅ |
| `test/probe_08.c` | Report 0x08 C 探测（读+写） | ✅ |
| `test/probe_02.py` | Report 0x02 设备能力查询（只读） | ✅ |
| `test/probe_07.py` | Report 0x07 探测（已废弃，勿用） | ❌ |

新增 report 参考 `probe_08.py` 结构，改 REPORT_ID 和 buffer 大小即可。

**注意事项**：
1. HID ioctl 方向必须是 `_IOC_WRITE | _IOC_READ`（双向，值为 3），不是单独的 WRITE 或 READ。
2. buffer 大小必须和 report descriptor 匹配（计算所有 Data + Const 字段的总 bits 数）。不匹配会导致未定义行为。
