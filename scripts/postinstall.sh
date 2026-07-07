#!/bin/sh
# Post-install: enable and start goodhapticd service.
# Skip if installing into a staging directory (DESTDIR is set).

if [ -z "$DESTDIR" ]; then
    gtk-update-icon-cache -f "${MESON_INSTALL_PREFIX}/share/icons/hicolor" 2>/dev/null || true
    systemctl daemon-reload
    systemctl enable --now goodhapticd 2>/dev/null || true
fi
