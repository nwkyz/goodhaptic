#!/usr/bin/env python3
"""
monitor_03.py — Compare Report 0x04 timing while toggling Report 0x03.

Measures PTP report interval from /dev/hidrawN, prints stats line
every second.  Write 0x03 values by typing two digits (0-10 each).

Usage:
  sudo python3 test/monitor_03.py [/dev/hidrawN]
"""

import sys, os, fcntl, time, select

_IOC_WRITE = 1
def HIDIOCSFEATURE(sz): return (3 << 30) | (ord('H') << 8) | (0x06 << 0) | (sz << 16)

def write_03(dev, a, b):
    fd = os.open(dev, os.O_RDWR)
    buf = bytearray([0x03, a, b])
    try:
        fcntl.ioctl(fd, HIDIOCSFEATURE(len(buf)), buf)
        return True
    except OSError as e:
        print(f"\nSET_FEATURE(0x03, {a}, {b}): {e}")
        return False
    finally:
        os.close(fd)


def main():
    dev = sys.argv[1] if len(sys.argv) > 1 else "/dev/hidraw0"

    fd = os.open(dev, os.O_RDONLY)
    if fd < 0:
        print(f"ERROR: cannot open {dev}"); sys.exit(1)

    # Non-blocking reads
    flags = fcntl.fcntl(fd, fcntl.F_GETFL, 0)
    fcntl.fcntl(fd, fcntl.F_SETFL, flags | os.O_NONBLOCK)

    print("\n\033[2J\033[H")  # clear
    print("=" * 70)
    print("  Report 0x03 Probe — Live Stats Monitor")
    print("  Type two digits (0-10) to write 0x03, type 'q' to quit")
    print("  r=reset(5,5)  z=zero(0,0)  x=max(10,10)")
    print("=" * 70)
    print(f"  {'Time':>8s} │ {'FPS':>5s} │ {'Interval':>9s} │ {'Contacts':>8s} │ {'Last write':>14s}")
    print(f"  {'─'*8}─┼─{'─'*5}─┼─{'─'*9}─┼─{'─'*8}─┼─{'─'*14}")

    # State
    a, b = 5, 5
    last_write = '—'
    start_time = time.time()
    frame_times = []    # timestamps of last N frames
    frame_contacts = []  # contact counts per frame
    partial = ''
    buf = b''

    while True:
        # Check stdin
        r, _, _ = select.select([sys.stdin], [], [], 0.01)
        if r:
            c = os.read(sys.stdin.fileno(), 1)
            if not c:
                break

            ch = c.decode('utf-8', errors='replace').lower()
            wrote = False
            if ch == 'q':
                break
            elif ch == 'r':
                a, b = 5, 5; wrote = True
            elif ch == 'z':
                a, b = 0, 0; wrote = True
            elif ch == 'x':
                a, b = 10, 10; wrote = True
            elif ch in '0123456789':
                partial += ch
                if len(partial) >= 2:
                    a = min(int(partial[0]), 10)
                    b = min(int(partial[1]), 10)
                    wrote = True
                    partial = ''
            elif ch == '\n' and partial:
                a = min(int(partial[0]), 10)
                b = 5  # default
                wrote = True
                partial = ''

            if wrote:
                write_03(dev, a, b)
                last_write = f'({a},{b})'
                # Reset stats window
                frame_times.clear()
                frame_contacts.clear()

        # Read hidraw
        try:
            data = os.read(fd, 4096)
            buf += data
        except BlockingIOError:
            data = b''

        # Parse 0x04 reports
        while len(buf) >= 40:
            if buf[0] == 0x04:
                ts = time.time()
                contact_count = buf[38] if len(buf) > 38 else 0
                frame_times.append(ts)
                frame_contacts.append(contact_count)

                # Keep last 100 frames
                if len(frame_times) > 100:
                    frame_times = frame_times[-100:]
                    frame_contacts = frame_contacts[-100:]

                buf = buf[40:]  # Move past this report
            else:
                # Skip non-0x04 (0x01 mouse reports are smaller)
                # Try to find next 0x04
                idx = buf.find(b'\x04', 1)
                if idx < 0:
                    buf = buf[-1:]  # Keep last byte
                    break
                buf = buf[idx:]

        # Print stats every second
        now = time.time()
        elapsed = now - start_time

        # Calculate FPS and interval from last 1 second
        if frame_times:
            recent = [t for t in frame_times if now - t < 1.0]
            fps = len(recent)
            if len(recent) >= 2:
                intervals = [recent[i] - recent[i-1] for i in range(1, len(recent))]
                avg_interval = sum(intervals) / len(intervals) * 1000  # ms
                interval_str = f'{avg_interval:6.1f}ms'
            else:
                interval_str = '—'
            avg_contacts = (sum(frame_contacts[-100:]) / max(len(frame_contacts[-100:]), 1)) if frame_contacts else 0
            contacts_str = f'{avg_contacts:5.1f}'
        else:
            fps = 0
            interval_str = '—'
            contacts_str = '—'

        # Overwrite current line
        sys.stdout.write(
            f'\r  {elapsed:7.1f}s │ {fps:4d}  │ {interval_str:>9s} │ {contacts_str:>8s} │ {last_write:>14s}  ')
        sys.stdout.flush()

    print('\n')
    os.close(fd)


if __name__ == '__main__':
    main()
