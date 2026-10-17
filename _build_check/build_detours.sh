#!/usr/bin/env bash
# build_detours.sh —— 从本地源码编译 detours x86/x64 静态库（v4.0.1）
# 源码：H:/2022_WorkSpace/smtv2/detour/（注意 uimports.cpp 被 creatwth.cpp include，不单独编译）
# 产物：_build_check/detours_x86/detours.lib（加载器 x86 用，另复制一份 detour_143.lib 供 pragma 解析）
#       _build_check/detours_x64/detours.lib（TestLoaderHook x64 DLL 用）
export MSYS2_ARG_CONV_EXCL='*'

VC="C:/Program Files/Microsoft Visual Studio/18/Professional/VC/Tools/MSVC/14.51.36231"
SDK="C:/Program Files (x86)/Windows Kits/10"
SDKV="10.0.26100.0"
SRC="H:/2022_WorkSpace/smtv2/detour"
ROOT="H:/360Downloads/Test_Loader/_build_check"

SRCS="detours disasm modules disolarm disolarm64 disolia64 disolx64 disolx86 image creatwth"

export INCLUDE="$VC/include;$SDK/Include/$SDKV/ucrt;$SDK/Include/$SDKV/shared;$SDK/Include/$SDKV/um;$SDK/Include/$SDKV/winrt"

build_one() {
    local ARCH="$1" CL="$2" LIBTOOL="$3" OUTDIR="$4" LIBDIR="$5"
    mkdir -p "$OUTDIR"
    rm -f "$OUTDIR"/*.obj "$OUTDIR/detours.lib"
    for f in $SRCS; do
        "$CL" -nologo -c -std:c++20 -EHsc -W3 -MT -O2 \
            -DWIN32 -D_WINDOWS -DUNICODE -D_UNICODE -DNDEBUG -D_CRT_SECURE_NO_WARNINGS \
            -Fo"$OUTDIR/$f.obj" "$SRC/$f.cpp" > "$OUTDIR/$f.log" 2>&1 || {
            echo "  $f.cpp FAILED"; grep -a "error" "$OUTDIR/$f.log" | head -3; return 1; }
    done
    LIB="$LIBDIR" "$LIBTOOL" -nologo -out:"$OUTDIR/detours.lib" \
        "$OUTDIR/detours.obj" "$OUTDIR/modules.obj" "$OUTDIR/disasm.obj" "$OUTDIR/image.obj" \
        "$OUTDIR/creatwth.obj" "$OUTDIR/disolx86.obj" "$OUTDIR/disolx64.obj" "$OUTDIR/disolia64.obj" \
        "$OUTDIR/disolarm.obj" "$OUTDIR/disolarm64.obj" || return 1
    # GameLaunch.cpp 的 #pragma comment(lib,"detour_143.lib") 需要此名（VS 工程亦走 pragma）
    cp -f "$OUTDIR/detours.lib" "$OUTDIR/detour_143.lib"
    echo "  -> $OUTDIR/detours.lib + detour_143.lib ok"
}

echo "=== detours x86 ==="
build_one x86 "$VC/bin/Hostx86/x86/cl.exe" "$VC/bin/Hostx86/x86/lib.exe" \
    "$ROOT/detours_x86" "$VC/lib/x86;$SDK/Lib/$SDKV/ucrt/x86;$SDK/Lib/$SDKV/um/x86" || exit 1

echo "=== detours x64 ==="
build_one x64 "$VC/bin/Hostx64/x64/cl.exe" "$VC/bin/Hostx64/x64/lib.exe" \
    "$ROOT/detours_x64" "$VC/lib/x64;$SDK/Lib/$SDKV/ucrt/x64;$SDK/Lib/$SDKV/um/x64" || exit 1

echo "=== detours libs done ==="
