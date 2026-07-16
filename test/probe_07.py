#!/usr/bin/env python3
"""
probe_07.py — Probe Goodix HID Report 0x07 (Click/Tap Mode)

Report 0x07 decoded from HID descriptor:
  Usage Page:    Digitizers (0x0D)
  Usage:         0x60
  Logical Min:   0
  Logical Max:   1
  Report Size:   1 bit data + 15 bits const padding = 2 data bytes
  Buffer:        3 bytes [report_id, byte0, byte1]
  Type:          Feature (Data, Var, Abs)

Values: 0 = Normal (cursor moves), 1 = Click-on-touch

Usage:
  python3 probe_07.py [device]              read current value
  python3 probe_07.py [device] set <0|1>    write new value
"""

import sys, os, fcntl, ctypes

_IOC_NONE = 0
_IOC_WRITE = 1
_IOC_READ = 2
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

REPORT_ID = 0x07

def report_07_read(device):
    fd = os.open(device, os.O_RDWR)
    try:
        # 3 bytes: report_id + 2 data bytes (1 bit data + 15 bits const padding)
        buf = bytearray([REPORT_ID, 0, 0])
        try:
            fcntl.ioctl(fd, HIDIOCGFEATURE(len(buf)), buf)
            val = buf[1] & 0x01
            print(f"  Raw: {[hex(b) for b in buf]}")
            return val
        except OSError:
            print("  GET_FEATURE not supported (hardware does not allow readback)")
            return None
    finally:
        os.close(fd)

def report_07_write(device, value):
    if value not in (0, 1):
        print(f"  ERROR: value must be 0 or 1, got {value}")
        return False
    fd = os.open(device, os.O_RDWR)
    try:
        # 3 bytes: report_id + 2 data bytes (1 bit data + 15 bits const padding)
        buf = bytearray([REPORT_ID, value, 0])
        fcntl.ioctl(fd, HIDIOCSFEATURE(len(buf)), buf)
        return True
    except OSError as e:
        print(f"  SET_FEATURE failed: {e}")
        return False
    finally:
        os.close(fd)

def main():
    device = "/dev/hidraw0"
    args = sys.argv[1:]
    if args and args[0] not in ("set",):
        device = args[0]; args = args[1:]

    if not os.path.exists(device):
        print(f"ERROR: {device} not found"); sys.exit(1)

    print(f"Device:  {device}")
    print(f"Report:  0x{REPORT_ID:02x} (Low-Latency Mode)")
    print()

    if args and args[0] == "set":
        if len(args) < 2:
            print("Usage: probe_07.py [device] set <0|1>"); sys.exit(1)
        val = int(args[1])
        print(f"=== Write value={val} ({'Low Latency' if val else 'Normal'}) ===")
        if report_07_write(device, val):
            print(f"  SUCCESS: wrote {val} to Report 0x07")
        else:
            sys.exit(1)
    else:
        print("=== Read Report 0x07 ===")
        val = report_07_read(device)
        if val is not None:
            print(f"  Current value: {val} ({'Low Latency' if val else 'Normal'})")
        print("\n  Commands:")
        print("    sudo python3 test/probe_07.py set 0   # normal")
        print("    sudo python3 test/probe_07.py set 1   # low latency")

if __name__ == "__main__":
    main()
