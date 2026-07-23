#!/bin/bash
# Build Debian package. All artifacts stay inside deb-build/ — nothing leaks into
# the project directory or its parent.
set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$PROJECT_DIR/deb-build"

cd "$PROJECT_DIR"

# Sync version from meson.build → debian/changelog
VERSION=$(grep -oP "version\s*:\s*'\K[^']+" meson.build)
if [ -n "$VERSION" ] && ! grep -q "($VERSION-" debian/changelog; then
    sed -i "1s/(.*)/($VERSION-1)/" debian/changelog
    echo "Synced debian/changelog to version $VERSION"
fi

# Clean previous build output and staging area
rm -rf "$BUILD_DIR" debian/goodhaptic debian/files debian/.debhelper
mkdir -p "$BUILD_DIR"

# Run dpkg-buildpackage.
# The .deb / .buildinfo / .changes still land in ../ by dpkg-buildpackage convention,
# but the meson build tree lives inside deb-build/ (see debian/rules).
dpkg-buildpackage -b -us -uc

# Move all generated artifacts from the parent directory into deb-build/
shopt -s nullglob
artifacts=("$PROJECT_DIR"/../goodhaptic_* "$PROJECT_DIR"/../goodhaptic-*)
if [ ${#artifacts[@]} -gt 0 ]; then
    mv "${artifacts[@]}" "$BUILD_DIR/"
fi

# Clean up temporary staging files that dpkg-buildpackage leaves in the source tree
rm -rf debian/goodhaptic debian/files debian/.debhelper

echo ""
echo "===== Build complete ====="
echo "Output in: $BUILD_DIR/"
ls -lh "$BUILD_DIR/"
