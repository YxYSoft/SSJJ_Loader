#!/usr/bin/env bash
# run_tests.sh —— P1/P2 全部 harness（build_all.sh 之后运行）
# 依赖 build_all.sh 产物：OUT 目录下的 *.obj（DbWork/CppSQLite3/sqlite3）与 TestLoaderHook.dll
export MSYS2_ARG_CONV_EXCL='*'

VC="C:/Program Files/Microsoft Visual Studio/18/Professional/VC/Tools/MSVC/14.51.36231"
SDK="C:/Program Files (x86)/Windows Kits/10"
SDKV="10.0.26100.0"
BC="H:/360Downloads/Test_Loader/_build_check"
OUT="C:/Users/MZPEDH/AppData/Local/Temp/zs_check/build"

export INCLUDE="$VC/include;$SDK/Include/$SDKV/ucrt;$SDK/Include/$SDKV/shared;$SDK/Include/$SDKV/um;$SDK/Include/$SDKV/winrt;H:/2022_WorkSpace/smtv2/detour;H:/360Downloads/Test_Loader/Test_Loader;H:/vs2022_Space/include;H:/360Downloads/Test_Loader/Test_Loader/sqlite3"
export LIB="$VC/lib/x86;$SDK/Lib/$SDKV/ucrt/x86;$SDK/Lib/$SDKV/um/x86;H:/OpenSSL-Win32/lib/VC/x86/MT;H:/360Downloads/Test_Loader/_build_check/detours_x86"
CL="$VC/bin/Hostx86/x86/cl.exe"
COMMON="-nologo -std:c++20 -EHsc -W3 -MT -O2 -DWIN32 -D_WINDOWS -DUNICODE -D_UNICODE -DNDEBUG -D_CRT_SECURE_NO_WARNINGS"

FAIL=0

echo "=== P1 harness（双客户端账号/菜单契约） ==="
$CL $COMMON -Fo"$OUT/test_p1.obj" -Fe"$OUT/test_p1.exe" "$BC/test_p1_twaccount.cpp" \
    "$OUT/DbWork.obj" "$OUT/CppSQLite3.obj" "$OUT/sqlite3.obj" \
    libcrypto_static.lib ws2_32.lib crypt32.lib user32.lib advapi32.lib 2>&1 | grep -a -i " error" | head -3
"$OUT/test_p1.exe" > "$OUT/test_p1.out" 2>&1 || FAIL=1
tail -2 "$OUT/test_p1.out"

echo ""
echo "=== P2 harness（共享内存契约/启动链/端到端注入） ==="
$CL $COMMON -Fo"$OUT/test_p2.obj" -Fe"$OUT/test_p2.exe" "$BC/test_p2_launch.cpp" \
    user32.lib "H:/360Downloads/Test_Loader/_build_check/detours_x86/detours.lib" 2>&1 | grep -a -i " error" | head -3
"$OUT/test_p2.exe" > "$OUT/test_p2.out" 2>&1 || FAIL=1
tail -2 "$OUT/test_p2.out"

echo ""
echo "=== P3 hook login 测试（stub libcef 离线双端链路，x64） ==="
CL64="$VC/bin/Hostx64/x64/cl.exe"
export INCLUDE64="$VC/include;$SDK/Include/$SDKV/ucrt;$SDK/Include/$SDKV/shared;$SDK/Include/$SDKV/um;$SDK/Include/$SDKV/winrt;H:/2022_WorkSpace/smtv2/detour;H:/360Downloads/Test_Loader/TestLoaderHook;H:/360Downloads/Test_Loader/Test_Loader;C:/Users/MZPEDH/Downloads/cef_binary_3.2357.1291-master/cef_binary_3.2357.1291.g47e6d4b_windows64;H:/vs2022_Space/include"
"$OUT/test_hook_login.exe" 4399 > "$OUT/test_hook_4399.out" 2>&1 || FAIL=1
tail -2 "$OUT/test_hook_4399.out"
"$OUT/test_hook_login.exe" 4399raw > "$OUT/test_hook_4399raw.out" 2>&1 || FAIL=1
tail -2 "$OUT/test_hook_4399raw.out"
"$OUT/test_hook_login.exe" 4399b > "$OUT/test_hook_4399b.out" 2>&1 || FAIL=1
tail -2 "$OUT/test_hook_4399b.out"
"$OUT/test_hook_login.exe" tw > "$OUT/test_hook_tw.out" 2>&1 || FAIL=1
tail -2 "$OUT/test_hook_tw.out"

echo ""
[ $FAIL -eq 0 ] && echo "=== ALL HARNESS PASS ===" || echo "=== HARNESS FAILED ==="
exit $FAIL
