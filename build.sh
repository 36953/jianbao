#!/usr/bin/env bash
# build.sh —— 用 ziglang 自带的 C++ 工具链构建原生 EXE
set -e

ZIGDIR="C:/Users/Administrator/.workbuddy/binaries/python/versions/3.13.12/Lib/site-packages/ziglang"
ZIG="$ZIGDIR/zig.exe"
export ZIG_LIB_DIR="$ZIGDIR/lib"

cd "$(dirname "$0")"
mkdir -p dist build

NAME="见报"
EXE="dist/${NAME}.exe"

# 资源：图标 + 版本信息（仅在缺失或更新时重编）
if [ ! -f app.res ] || [ app.rc -nt app.res ]; then
    "$ZIG" rc app.rc app.res
fi

"$ZIG" c++ -target x86_64-windows-gnu \
    -O2 -std=c++17 -municode -Wl,--subsystem,windows \
    -Wall -Wextra -Wno-unused-parameter -Wno-address-of-temporary \
    -o "$EXE" \
    src/main.cpp src/util.cpp src/sources.cpp src/pdfmerge.cpp \
    src/job.cpp src/ui_draw.cpp src/ui_paint.cpp src/ui_app.cpp \
    app.res \
    -lgdiplus -ldwmapi -lwinhttp -lcomctl32 -lshell32 -lole32 -luuid \
    -luser32 -lgdi32 -ladvapi32 -lshlwapi

echo "构建完成：$EXE"
ls -la "$EXE"
