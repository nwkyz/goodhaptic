#!/usr/bin/env python3
"""
probe_03.py — Probe Goodix HID Report 0x03 (Input Mode / Device Mode)

Report 0x03 decoded from HID descriptor (offset 0x2C0):
  Usage Page:    Digitizers (0x0D)
  Usage:         0x0E → Device Configuration (Application collection)
  Usage:         0x22 → Finger (Physical collection)
  Usage:         0x52 → Input Mode (Device Mode)
  Logical Min:   0
  Logical Max:   10 (0x0a)
  Report Size:   8 bits × 2 fields = 2 data bytes
  Type:          Feature (Data, Var, Abs)

RESEARCH CONCLUSION (2026-07-23):
  - GET_FEATURE: NOT supported (EINVAL for all buffer sizes)
  - SET_FEATURE: WORKS — but ONLY with 1 data byte (2-byte buffer: [0x03, mode])
    The 3-byte format ([0x03, a, b]) is silently ignored by firmware
  - Descriptor matches Elan 0x300b: 2 fields declared, firmware respects only
    the first byte (kernel fix: commit 73e7d63e)
  - Mode 0 = Mouse mode:  PTP data drops to ~7/s, no multi-touch
  - Mode 3 = PTP mode:    PTP data at ~100/s, full 5-finger multi-touch
  - Mode 1,2,4-10: unknown (untested, likely NOP or vendor-specific)

Usage:
  python3 probe_03.py [device]              show current status and report rate
  python3 probe_03.py [device] mouse        switch to Mouse mode (0)
  python3 probe_03.py [device] ptp          switch to PTP mode (3)
  python3 probe_03.py [device] raw <n>      write raw mode value (0-10)
  python3 probe_03.py [device] bench        benchmark both modes
  python3 probe_03.py [device] sizes        try different buffer sizes
"""

import sys, os, fcntl, time

_IOC_WRITE = 1
_IOC_READ  = 2
_IOC_NRSHIFT = 0
_IOC_TYPESHIFT = 8
_IOC_SIZESHIFT = 16
_IOC_DIRSHIFT = 30

def _IOC(dir_, type_, nr, size):
    return (dir_ << _IOC_DIRSHIFT) | (ord(type_) << _IOC_TYPESHIFT) | \
           (nr << _IOC_NRSHIFT) | (size << _IOC_SIZESHIFT)

def HIDIOCGFEATURE(sz):
    return _IOC(_IOC_WRITE | _IOC_READ, "H", 0x07, sz)

def HIDIOCSFEATURE(sz):
    return _IOC(_IOC_WRITE | _IOC_READ, "H", 0x06, sz)

REPORT_ID = 0x03


def set_mode(device, mode):
    """Write Input Mode with CORRECT 1-byte payload (2-byte buffer total).
    GXTP5100 firmware only respects the first data byte — sending 2 data bytes
    (3-byte buffer) causes the write to be silently ignored (matches Elan bug)."""
    if not (0 <= mode <= 10):
        print(f"  ERROR: mode must be 0-10, got {mode}")
        return False
    fd = os.open(device, os.O_RDWR)
    try:
        buf = bytearray([REPORT_ID, mode])  # 2 bytes: report_id + 1 data byte
        try:
            fcntl.ioctl(fd, HIDIOCSFEATURE(len(buf)), buf)
            return True
        except OSError as e:
            print(f"  SET_FEATURE failed: {e}")
            return False
    finally:
        os.close(fd)


def measure_rate(device, duration=3):
    """Count PTP (0x04) and Mouse (0x01) reports per second."""
    fd = os.open(device, os.O_RDONLY)
    c01, c04 = 0, 0
    start = time.time()
    while time.time() - start < duration:
        try:
            data = os.read(fd, 128)
            for b in data:
                if b == 0x01: c01 += 1
                elif b == 0x04: c04 += 1
        except BlockingIOError:
            pass
    os.close(fd)
    elapsed = time.time() - start
    return c01 / elapsed, c04 / elapsed


def bench(device):
    """Compare mode 3 vs mode 0 with measured report rates."""
    print(f"Device: {device}")
    print()

    for mode, label in [(3, "PTP multi-touch"), (0, "Mouse-only")]:
        set_mode(device, mode)
        time.sleep(0.3)

        fps01, fps04 = measure_rate(device, 3)
        print(f"  Mode {mode} ({label}):")
        print(f"    Mouse (0x01) : {fps01:6.1f}/s")
        print(f"    PTP   (0x04) : {fps04:6.1f}/s")
        print()

    # Restore PTP mode
    set_mode(device, 3)
    print("  Restored mode 3 (PTP).")


def try_sizes(device):
    """Probe GET/SET_FEATURE with different buffer sizes."""
    print(f"Device: {device}")
    print(f"Report: 0x{REPORT_ID:02x} — Buffer size sweep\n")

    print("=== GET_FEATURE ===")
    for sz in range(2, 9):
        fd = os.open(device, os.O_RDWR)
        try:
            buf = bytearray([REPORT_ID] + [0] * (sz - 1))
            fcntl.ioctl(fd, HIDIOCGFEATURE(len(buf)), buf)
            hx = " ".join(f"{b:02x}" for b in buf)
            print(f"  GET({sz}): OK  [{hx}]")
        except OSError as e:
            print(f"  GET({sz}): FAIL  ({e})")
        finally:
            os.close(fd)

    print("\n=== SET_FEATURE (1-byte payload) ===")
    for sz in range(2, 9):
        buf = bytearray([REPORT_ID] + [3] * (sz - 1))
        fd = os.open(device, os.O_RDWR)
        try:
            fcntl.ioctl(fd, HIDIOCSFEATURE(len(buf)), buf)
            hx = " ".join(f"{b:02x}" for b in buf)
            print(f"  SET({sz}): OK  [{hx}]")
        except OSError as e:
            print(f"  SET({sz}): FAIL  ({e})")
        finally:
            os.close(fd)

    print("\n  NOTE: SET succeeds for all sizes, but firmware only acts on")
    print("  2-byte buffer [0x03, mode]. Larger buffers are silently ignored.")


def main():
    device = "/dev/hidraw0"
    args = sys.argv[1:]

    # Parse device if first arg is not a known command
    commands = {"mouse", "ptp", "raw", "bench", "sizes"}
    if args and args[0] not in commands:
        device = args[0]
        args = args[1:]

    if not os.path.exists(device):
        print(f"ERROR: {device} not found"); sys.exit(1)

    if not args:
        # Default: show current state
        print(f"Device:  {device}")
        print(f"Report:  0x{REPORT_ID:02x} (Input Mode, Usage 0x52)")
        print()
        fps01, fps04 = measure_rate(device)
        print(f"  Mouse (0x01): {fps01:6.1f}/s")
        print(f"  PTP   (0x04): {fps04:6.1f}/s")
        if fps04 > 50:
            print(f"  → Likely in PTP mode (3)")
        elif fps04 < 15:
            print(f"  → Likely in Mouse mode (0)")
        print()
        print(f"  Commands:")
        print(f"    probe_03.py mouse    → switch to Mouse mode (0)")
        print(f"    probe_03.py ptp      → switch to PTP mode (3)")
        print(f"    probe_03.py raw <n>  → write raw value (0-10)")
        print(f"    probe_03.py bench    → benchmark both modes")
        print(f"    probe_03.py sizes    → buffer size probe")
        return

    cmd = args[0]

    if cmd == "mouse":
        print(f"Switching {device} to Mouse mode (0)...")
        if set_mode(device, 0):
            print("  Done. Touchpad is now basic mouse (no multi-touch).")
            print("  Run 'probe_03.py ptp' to restore.")

    elif cmd == "ptp":
        print(f"Switching {device} to PTP mode (3)...")
        if set_mode(device, 3):
            print("  Done. Touchpad is now Precision Touchpad (multi-touch).")

    elif cmd == "raw":
        if len(args) < 2:
            print("Usage: probe_03.py raw <0-10>"); sys.exit(1)
        mode = int(args[1])
        if not (0 <= mode <= 10):
            print(f"ERROR: mode must be 0-10, got {mode}"); sys.exit(1)
        print(f"Writing mode {mode} to {device}...")
        if set_mode(device, mode):
            print(f"  Done.")
            fps01, fps04 = measure_rate(device)
            print(f"  Mouse (0x01): {fps01:6.1f}/s")
            print(f"  PTP   (0x04): {fps04:6.1f}/s")

    elif cmd == "bench":
        bench(device)

    elif cmd == "sizes":
        try_sizes(device)

    else:
        print(f"Unknown command: {cmd}")
        print(f"Usage: probe_03.py [device] [mouse|ptp|raw|bench|sizes]")


if __name__ == "__main__":
    main()
