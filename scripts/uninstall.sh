#!/bin/sh
# Uninstall goodhaptic: stop service, remove files.
set -e

PREFIX="${1:-/usr/local}"

echo "Stopping and disabling goodhapticd service..."
systemctl stop goodhapticd 2>/dev/null || true
systemctl disable goodhapticd 2>/dev/null || true

echo "Removing installed files..."
rm -f "$PREFIX/bin/goodhaptic"
rm -f "$PREFIX/libexec/goodhapticd"
rm -f "$PREFIX/share/applications/io.github.nwkyz.goodhaptic.desktop"
rm -f "$PREFIX/share/icons/hicolor/256x256/apps/io.github.nwkyz.goodhaptic.png"
rm -f "$PREFIX/lib/systemd/system/goodhapticd.service"
rm -f /etc/goodhaptic.conf

systemctl daemon-reload 2>/dev/null || true

echo "Good Haptic uninstalled."
