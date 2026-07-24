![](https://raw.githubusercontent.com/nwkyz/nwkyz-picbed/main/storage/goodhaptic-banner2.png)

<p align="center">Control Goodix haptic touchpads on Linux</p>  

<p align="center">[ <a href="readme_zh.md">中文</a> / English ]</p>


## Known Compatibility

- **Device**: Goodix GXTP5100 (USB `27c6:659a`, HID `27C6:01E7`)
- **Interface**: `/dev/hidrawN`, control via HID feature report writes
- **Tested Device**: Lenovo ThinkBook 16 G8+ IPD

## Features

| Feature | HID Report | Description |
|---|---|---|
| Vibration Strength | `0x09` Set Feature | 0–100, controls haptic feedback intensity |
| Click Sensitivity | `0x08` Set Feature | 3 levels (Light/Mid/Firm), adjusts the pressure needed to register a click |
| Touchpad Mode | `0x03` Set Feature | Switch between Precision Touchpad and Mouse mode |
| Selective Reporting | `0x05` Set Feature | Independently control touch/button data reporting |
| Device Capability | `0x02` Get Feature | Reads maximum contact count and touchpad type |
| Latency Mode | `0x07` Set Feature | Normal / high-latency power-save mode (diagnostic) |
| Firmware Version | sysfs | Reads firmware version number |
| Touch Monitor | `0x04` Input | Real-time finger position, pressure and button state |

### Vibration

- **Preset Mode**: Light / Mid / Firm / Max (25 / 50 / 75 / 100)
- **Stepless Adjustment**: When enabled, use a 0–100 slider for fine-grained control (may not work on some devices)
- **Toggle**: Disable the vibration switch to temporarily mute haptic feedback

### Click

Adjust click sensitivity (Report `0x08`) with three levels mapping to different pressure thresholds:

| Level | Force Threshold | Experience |
|---|---|---|
| Light | ~110 g | Light tap triggers a click |
| Mid | ~150 g | Moderate pressure |
| Firm | ~190 g | Requires a heavier press |

### Touch Monitor

Real-time visualisation of touchpad state (daemon continuously reads Report `0x04`):

- 5-finger touch position overlay (touchpad plane + coordinates)
- Per-finger pressure bar chart
- Real-time contact count / button state / scan time
- Automatically adapts to logical max coordinates from the HID descriptor

### Device Info

Automatically reads and displays:
- Max contact count
- Touchpad type (Touchpad / Clickpad / Precision Touchpad)

## Dependencies

- meson (>= 1.0)
- gtk4 (>= 4.10)
- libadwaita-1 (>= 1.4)
- systemd

Install dependencies:

```bash
# Debian/Ubuntu
sudo apt install meson libgtk-4-dev libadwaita-1-dev
```

## Quick Start

### Build

```bash
meson setup build
ninja -C build
```

### Install & Uninstall

```bash
# Install (auto-enables and starts goodhapticd service)
sudo meson install -C build

# Uninstall
sudo scripts/uninstall.sh
```

After installation, the GUI can be launched via the `goodhaptic` command or from the application menu.

## Restore on Startup

When `Restore settings on startup` is enabled in the `System` section, goodhapticd automatically writes the saved strength and click sensitivity to the hardware at boot, preventing reset to factory defaults after a reboot or power cycle.

## Configuration File

Location:
`/etc/goodhaptic.conf`

Example:
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

| Field | Description |
|---|---|
| `device` | hidraw device path |
| `strength` | Current strength (0–100) |
| `threshold` | Click sensitivity (1=light, 2=medium, 3=firm) |
| `inputmode` | Touchpad mode (0=Mouse, 3=Precision Touchpad) |
| `persist` | Restore on boot (0=off, 1=on) |
| `stepless` | Stepless adjustment (0=presets, 1=slider) |
| `selective_surface` | Touch reporting (0=off, 1=on) |
| `selective_button` | Button reporting (0=off, 1=on) |
| `persist` | Restore on startup (0=off, 1=on) |
| `stepless` | Stepless adjustment (0=presets, 1=slider) |
| `selective_surface` | Touch reporting (0=off, 1=on) |
| `selective_button` | Button reporting (0=off, 1=on) |

## Help Translate

Translation files are in the `po/` directory, using GNU gettext.

```bash
# Generate/update translation template (POT)
ninja -C build goodhaptic-pot

# Add a new language (replace xx with language code)
msginit --locale=xx --input=po/goodhaptic.pot --output=po/xx.po

# Or update existing translations
msgmerge --update po/xx.po po/goodhaptic.pot

# Edit po/xx.po with translations, then add the language code to po/LINGUAS
echo "xx" >> po/LINGUAS

# Rebuild
meson setup build --reconfigure
ninja -C build
```

## DEB Packaging

All build artifacts and intermediate files (including `.deb` / `.ddeb` / `.buildinfo`) are contained within the `deb-build/` directory.

```bash
./scripts/build-deb.sh

# Install
sudo apt install ./deb-build/goodhaptic_1.0-1_amd64.deb
```

## Known Information

1. **Locating the input device**: `/sys/class/input/inputN` → `GXTP5100:00 27C6:01E7 Touchpad`, driver `hid-multitouch`
2. **HID Feature Report overview**:

| Report | Type | Size | Purpose | Status |
|---|---|---|---|---|
| 0x02 | Feature | 2 bytes | Device capability (max contacts, pad type) | ✅ Implemented |
| 0x04 | Input | ~38 bytes | Precision Touchpad data (5 fingers) | ✅ Implemented |
| 0x08 | Feature | 2 bytes | Click force threshold (1–3) | ✅ Implemented |
| 0x09 | Feature | 2 bytes | Haptic strength (0–100) | ✅ Implemented |
| 0x03 | Feature | 2 bytes (1 effective) | Input Mode (0=Mouse 3=PTP)¹ | ✅ Implemented |
| 0x05 | Feature | 2 bytes | Selective reporting (Surface/Button Switch) | ✅ Implemented |
| 0x07 | Feature | 2 bytes | Latency Mode (Usage 0x60: 0=normal 1=high-power-save)³ | ✅ Implemented|
| 0x06 | Feature | 256 bytes | Vendor init blob (Usage 0xC5)⁵ | ⚠ Researched (handled by kernel) |
| 0x0B | Feature | 66 bytes | Vendor status (Usage 0xC7)⁶ | ⚠ Researched (ignored by kernel) |
| 0x0C | Feature | 736 bytes | Vendor config (Usage 0xC6)⁶ | ⚠ Researched (ignored by kernel) |
| 0x0D | Feature | 4 bytes | Firmware command (Usage 0xC4)² | ⚠ Researched |
| 0x0F | Feature | any size | Not in descriptor, read-only all zeros⁴ | ⚠ Researched |

3. **Windows analysis**: Confirmed via Procmon that the Windows "Touchpad → Vibration Strength" setting is essentially a HID feature report write
4. **Approach**: Write feature report + data to `/dev/hidrawN`; the hardware responds in real time
5. **¹**: Report 0x03's HID descriptor declares 2 Input Mode fields (Report Size 8 × Report Count 2), but the firmware only respects the first byte. Writing 1 data byte (`[0x03, mode]`) works; 2 bytes (`[0x03, a, b]`) are silently ignored. This matches the Elan 0x300b quirk exactly ([kernel fix](https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/commit/?id=73e7d63efb4d774883a338997943bfa59e127085))
6. **²**: Report 0x0D GET_FEATURE is unsupported (EINVAL). SET_FEATURE succeeds but serves as a firmware command write entry, not readable. Firmware version is read from `/sys/class/input/inputN/id/version`, not via HID report.
7. **³**: Report 0x07 is Latency Mode (Usage 0x60). 0 = normal, 1 = high-latency power-save mode (report rate drops drastically, touchpad barely usable). Managed automatically by the kernel (sets 0 at runtime, 1 during suspend) — no user-space action needed.
8. **⁴**: Reports 0x0F–0xFF are not declared in the HID descriptor, yet GET_FEATURE succeeds for all of them, always returning all zeros. Purpose unknown; no write operations are performed.
9. **⁵**: Report 0x06 is a Win8 initialization blob (Vendor Page 0xFF00, Usage 0xC5). The Linux kernel's `hid-multitouch` reads it automatically during probe to activate the device — no user-space intervention needed.
10. **⁶**: Reports 0x0B (Usage 0xC7) and 0x0C (Usage 0xC6) also belong to Vendor Page 0xFF00. The kernel's `mt_input_mapping` handles them via the generic `case 0xff000000: return -1` — no special handling or input mapping. GET_FEATURE is unsupported; no user-space read path is available.

## Architecture

```
goodhapticd (systemd service, root)
  ├── Reads /etc/goodhaptic.conf at startup
  ├── Restores strength and sensitivity to hardware when persist=1
  ├── Listens on /run/goodhaptic/sock for GUI commands
  ├── Commands: STRENGTH, THRESHOLD, INPUTMODE, SELECTIVE, PERSIST, DEVICE, STEPLESS
  ├── Queries:   CAPABILITY, RESOLUTION
  └── Streaming: MONITOR (Report 0x04 real-time touch data)

GUI (unprivileged user)
  ├── Communicates with daemon via Unix socket — no root required
  ├── Vibration: preset/slider toggle, on/off switch
  ├── Click: 3-level sensitivity toggle, touchpad mode switch
  ├── System: startup restore, advanced selective reporting
  ├── Touch Monitor: real-time position/pressure visualisation
  └── Data: device capability, firmware version
```

## Limitations

- **Current strength/sensitivity cannot be read back**: `HIDIOCGFEATURE` does not work for reports `0x08` and `0x09`; the hardware does not support readback

## Test Tools

Standalone probe scripts in the `test/` directory; root permissions required:

```bash
# Read device capability (Report 0x02)
sudo python3 test/probe_02.py

# Read current sensitivity (Report 0x08)
sudo python3 test/probe_08.py

# Set sensitivity (1=Light, 2=Mid, 3=Firm)
sudo python3 test/probe_08.py set 1

# Compile the C version
gcc -o test/probe_08 test/probe_08.c
sudo test/probe_08 set 2
```

Stop the daemon first: `sudo systemctl stop goodhapticd`

## License

GPLv3
