#!/usr/bin/env python3
"""
probe_03.py — Probe Goodix HID Report 0x03 (Digitizer Config)

Report 0x03 decoded from HID descriptor:
  Usage Page:    Digitizers (0x0D)
  Usage:         0x22 → 0x52 (Physical collection)
  Logical Min:   0
  Logical Max:   10 (0x0a)
  Report Size:   8 bits × 2 fields = 2 data bytes
  Buffer:        3 bytes [report_id, byte0, byte1]
  Type:          Feature (Data, Var, Abs)

Usage:
  python3 probe_03.py [device]              read current config
  python3 probe_03.py [device] set <a> <b>  write config (0-10 each)
"""

import sys, os, fcntl

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

def read(device):
    fd = os.open(device, os.O_RDWR)
    try:
        buf = bytearray([REPORT_ID, 0, 0])
        try:
            fcntl.ioctl(fd, HIDIOCGFEATURE(len(buf)), buf)
            a, b = buf[1], buf[2]
            print(f"  Raw:  [{buf[0]:#04x}, {buf[1]:#04x}, {buf[2]:#04x}]")
            print(f"  Field 0:  {a}  (range 0-10)")
            print(f"  Field 1:  {b}  (range 0-10)")
            return a, b
        except OSError as e:
            print(f"  GET_FEATURE failed: {e}")
            return None
    finally:
        os.close(fd)

def write(device, a, b):
    if not (0 <= a <= 10 and 0 <= b <= 10):
        print(f"  ERROR: values must be 0-10, got {a}, {b}")
        return False
    fd = os.open(device, os.O_RDWR)
    try:
        buf = bytearray([REPORT_ID, a, b])
        try:
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
    print(f"Report:  0x{REPORT_ID:02x} (Digitizer Config, Usage 0x52)")
    print()

    if args and args[0] == "set":
        if len(args) < 3:
            print("Usage: probe_03.py [device] set <field0> <field1>")
            print("  Values: 0-10 each")
            sys.exit(1)
        a, b = int(args[1]), int(args[2])
        print(f"=== Write: field0={a}, field1={b} ===")
        if write(device, a, b):
            print(f"  SUCCESS")
            print(f"\n=== Verify ===")
            read(device)
    else:
        print("=== Read Report 0x03 ===")
        read(device)
        print(f"\n  Usage: probe_03.py {device} set <0-10> <0-10>")

if __name__ == "__main__":
    main()
