#!/usr/bin/env bash
# Linux/WSL native SDK build. No Docker or content pack needed.
set -euo pipefail
PROJECT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
export NDLESS_SDK="${NDLESS_SDK:-$HOME/Ndless/ndless-sdk}"
export NDLESS_TOOLCHAIN_PATH="${NDLESS_TOOLCHAIN_PATH:-$NDLESS_SDK/toolchain/install/bin}"
export _NDLESS_TOOLCHAIN_PATH="$NDLESS_TOOLCHAIN_PATH"
export PATH="$NDLESS_SDK/bin:$NDLESS_TOOLCHAIN_PATH:$PATH"
if [[ ! -x "$NDLESS_TOOLCHAIN_PATH/arm-none-eabi-g++" ]]; then
  echo "Native Ndless toolchain missing. Set NDLESS_SDK and NDLESS_TOOLCHAIN_PATH." >&2
  exit 1
fi
BUILD_DIR="$(mktemp -d "$HOME/ddlc-launcher-build.XXXXXX")"
mkdir -p "$BUILD_DIR/dist/ndless" "$PROJECT_DIR/work/ddlc-launcher-build"
cp -a "$PROJECT_DIR/cpp" "$BUILD_DIR/"
cd "$BUILD_DIR/cpp/platform/ndless"
if ! make -f Makefile -j"${DDLC_BUILD_JOBS:-4}" ../../../dist/ndless/ddlc.tns > "$BUILD_DIR/build.log" 2>&1; then
  tail -n 60 "$BUILD_DIR/build.log"
  cp "$BUILD_DIR/build.log" "$PROJECT_DIR/work/ddlc-launcher-build/ndless-build.log"
  exit 1
fi
mkdir -p "$PROJECT_DIR/dist/ndless"
cp "$BUILD_DIR/dist/ndless/ddlc.tns" "$PROJECT_DIR/dist/ndless/ddlc.tns"
cp "$BUILD_DIR/dist/ndless/ddlc.tns" "$PROJECT_DIR/work/ddlc-launcher-build/ddlc.tns"
cp "$BUILD_DIR/build.log" "$PROJECT_DIR/work/ddlc-launcher-build/ndless-build.log"
echo "Launcher ready: $PROJECT_DIR/dist/ndless/ddlc.tns"
echo "Build files retained in: $BUILD_DIR"
