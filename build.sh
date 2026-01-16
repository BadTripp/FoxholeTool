#!/usr/bin/env bash
set -euo pipefail

SRC_FILE="clicker.c"
LINUX_OUT="foxholetool"
WIN_OUT="foxholetool.exe"
WIN_OUT_CONSOLE="foxholetool_console.exe"

LINUX_CFLAGS="${CFLAGS:- -O2}"
LINUX_LDFLAGS="${LDFLAGS:-} -lX11 -lXtst -lpthread"

WIN_CFLAGS="${WIN_CFLAGS:- -O2 -mwindows}"
WIN_LDFLAGS="${WIN_LDFLAGS:-} -luser32 -lgdi32"

CC_LINUX="${CC_LINUX:-${CC:-gcc}}"
CC_WIN="${CC_WIN:-x86_64-w64-mingw32-gcc}"

usage() {
  cat <<EOF
Usage: $0 [linux|windows|windows-console|all|clean]

Targets:
  linux           - build native Linux binary (${LINUX_OUT})
  windows         - build Windows GUI .exe via MinGW (${WIN_OUT})
  windows-console - build Windows CONSOLE .exe via MinGW (${WIN_OUT_CONSOLE})
  all             - build Linux + Windows GUI (default)
  clean           - remove built binaries

Env vars:
  CC_LINUX   - compiler for Linux build (default: ${CC_LINUX})
  CC_WIN     - compiler for Windows build (default: ${CC_WIN})
  CFLAGS     - extra flags for Linux build
  WIN_CFLAGS - extra flags for Windows build
EOF
}

build_linux() {
  echo "==> Building Linux binary (${LINUX_OUT}) with ${CC_LINUX}"
  ${CC_LINUX} ${LINUX_CFLAGS} "${SRC_FILE}" -o "${LINUX_OUT}" ${LINUX_LDFLAGS}
}

build_windows() {
  if ! command -v "${CC_WIN}" >/dev/null 2>&1; then
    echo "!! Windows cross-compiler '${CC_WIN}' not found in PATH." >&2
    echo "   Install MinGW (e.g. 'x86_64-w64-mingw32-gcc') or set CC_WIN." >&2
    return 1
  fi
  echo "==> Building Windows GUI binary (${WIN_OUT}) with ${CC_WIN}"
  "${CC_WIN}" ${WIN_CFLAGS} "${SRC_FILE}" -o "${WIN_OUT}" ${WIN_LDFLAGS}
}

build_windows_console() {
  if ! command -v "${CC_WIN}" >/dev/null 2>&1; then
    echo "!! Windows cross-compiler '${CC_WIN}' not found in PATH." >&2
    echo "   Install MinGW (e.g. 'x86_64-w64-mingw32-gcc') or set CC_WIN." >&2
    return 1
  fi
  echo "==> Building Windows CONSOLE binary (${WIN_OUT_CONSOLE}) with ${CC_WIN}"
  "${CC_WIN}" -O2 "${SRC_FILE}" -o "${WIN_OUT_CONSOLE}" ${WIN_LDFLAGS}
}

clean_build() {
  rm -f "${LINUX_OUT}" "${WIN_OUT}" "${WIN_OUT_CONSOLE}"
  echo "Cleaned: ${LINUX_OUT} ${WIN_OUT} ${WIN_OUT_CONSOLE}"
}

TARGET="${1:-all}"

case "${TARGET}" in
  linux)
    build_linux
    ;;
  windows)
    build_windows
    ;;
  windows-console)
    build_windows_console
    ;;
  all)
    build_linux
    build_windows || echo "Windows build failed (Linux build succeeded)."
    ;;
  clean)
    clean_build
    ;;
  -h|--help|help)
    usage
    ;;
  *)
    echo "Unknown target: ${TARGET}" >&2
    usage
    exit 1
    ;;
esac
