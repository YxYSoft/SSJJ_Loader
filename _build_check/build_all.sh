#!/usr/bin/env bash
# 完整手工构建：对齐 Test_Loader Release|Win32 配置（MSBuild 被环境拦截时的替代通道）
export MSYS2_ARG_CONV_EXCL='*'

VC="C:/Program Files/Microsoft Visual Studio/18/Professional/VC/Tools/MSVC/14.51.36231"
SDK="C:/Program Files (x86)/Windows Kits/10"
SDKV="10.0.26100.0"
CL="$VC/bin/Hostx86/x86/cl.exe"
LINK="$VC/bin/Hostx86/x86/link.exe"
RC="$SDK/bin/$SDKV/x86/rc.exe"
OUT="C:/Users/MZPEDH/AppData/Local/Temp/zs_check/build"
mkdir -p "$OUT"

export INCLUDE="$VC/include;$VC/atlmfc/include;$SDK/Include/$SDKV/ucrt;$SDK/Include/$SDKV/shared;$SDK/Include/$SDKV/um;$SDK/Include/$SDKV/winrt;H:/2022_WorkSpace/smtv2/detour"
export LIB="$VC/lib/x86;$VC/atlmfc/lib/x86;$SDK/Lib/$SDKV/ucrt/x86;$SDK/Lib/$SDKV/um/x86;H:/360Downloads/DUIThink_git/lib;H:/OpenSSL-Win32/lib/VC/x86/MT;H:/vs2022_Space/include/hv/lib/x86;H:/360Downloads/Test_Loader/_build_check/detours_x86"

PROJ="H:/360Downloads/Test_Loader/Test_Loader"
BC="H:/360Downloads/Test_Loader/_build_check"
COMMON="-nologo -c -EHsc -permissive- -W3 -MT -O2 -Gy -Oi -DWIN32 -D_WINDOWS -DUNICODE -D_UNICODE -DNDEBUG -D_CRT_SECURE_NO_WARNINGS -DHV_STATICLIB -DDUITHINKLIB"
INC="/I$PROJ /IH:/360Downloads/DUIThink_git/DUIThink /IH:/vs2022_Space/include /IH:/OpenSSL-Win32/include /IH:/vs2022_Space/include/hv/include"

CPPS=(
  "$PROJ/DbWork.cpp" "$PROJ/NetWork.cpp" "$PROJ/ReadUserTxt.cpp"
  "$PROJ/SingleInstance.cpp" "$PROJ/UrlProtocol.cpp" "$PROJ/SystemTray.cpp"
  "$PROJ/Test_Loader.cpp" "$PROJ/CMainDlg.cpp" "$PROJ/CAddUserDlg.cpp"
  "$PROJ/CInputBoxDlg.cpp" "$PROJ/CSettingDlg.cpp" "$PROJ/CSplashDlg.cpp"
  "$PROJ/GameLaunch.cpp"
  "$PROJ/sqlite3/CppSQLite3.cpp"
)

echo "=== 编译 C++ TU (${#CPPS[@]} 个) ==="
OBJS=""
FAIL=0
for f in "${CPPS[@]}"; do
  base=$(basename "$f" .cpp)
  "$CL" $COMMON -std:c++20 $INC "/Fo$OUT/$base.obj" "$f" > "$OUT/$base.cl.log" 2>&1
  rc=$?
  errs=$(grep -c "error" "$OUT/$base.cl.log" 2>/dev/null || echo 0)
  echo "  $base.cpp exit=$rc errors=$errs"
  if [ $rc -ne 0 ]; then FAIL=1; grep "error" "$OUT/$base.cl.log" | head -5; fi
  OBJS="$OBJS $OUT/$base.obj"
done

echo "=== 编译 sqlite3.c (C) ==="
"$CL" -nologo -c -TC -MT -O2 -Gy -Oi -DWIN32 -D_WINDOWS -DUNICODE -D_UNICODE -DNDEBUG -D_CRT_SECURE_NO_WARNINGS \
  $INC "/Fo$OUT/sqlite3.obj" "$PROJ/sqlite3/sqlite3.c" > "$OUT/sqlite3.cl.log" 2>&1
echo "  sqlite3.c exit=$? errors=$(grep -c 'error' "$OUT/sqlite3.cl.log" 2>/dev/null || echo 0)"
[ $? -ne 0 ] && FAIL=1
OBJS="$OBJS $OUT/sqlite3.obj"

echo "=== 编译资源 ==="
"$RC" -nologo -fo "$OUT/Test_Loader.res" \
  -i "$VC/atlmfc/include" -i "$VC/include" \
  -i "$SDK/Include/$SDKV/ucrt" -i "$SDK/Include/$SDKV/shared" -i "$SDK/Include/$SDKV/um" \
  "$PROJ/Test_Loader.rc" > "$OUT/rc.log" 2>&1
echo "  rc exit=$?"
[ $? -ne 0 ] && { FAIL=1; cat "$OUT/rc.log"; }

if [ $FAIL -ne 0 ]; then echo "=== 编译阶段失败，停止 ==="; exit 1; fi

echo "=== 链接 ==="
"$LINK" -nologo -subsystem:WINDOWS -incremental:no -opt:ref -opt:icf \
  -out:"$OUT/Test_Loader_test.exe" $OBJS "$OUT/Test_Loader.res" \
  winmm.lib crypt32.lib shell32.lib ole32.lib comdlg32.lib ws2_32.lib \
  libcrypto_static.lib libssl_static.lib hv_static_MT.lib \
  MMHelper.2017_Static_MT.lib DUIThink.2017_Static_MT.lib tinyxml2.2017_MT.lib \
  libwebp.2017_MT.lib libwebpdemux.2017_MT.lib libwebpdecoder.2017_MT.lib \
  detours.lib \
  > "$OUT/link.log" 2>&1
LRC=$?
echo "  link exit=$LRC"
if [ $LRC -ne 0 ]; then
  echo "--- link.log 错误行 ---"
  grep -i "error\|LNK" "$OUT/link.log" | head -30
  exit 1
fi
ls -la "$OUT/Test_Loader_test.exe"

echo "=== 构建 TestLoaderHook.dll (x64) ==="
CL64="$VC/bin/Hostx64/x64/cl.exe"
HOOKSRC="H:/360Downloads/Test_Loader/TestLoaderHook"
export INCLUDE64="$VC/include;$SDK/Include/$SDKV/ucrt;$SDK/Include/$SDKV/shared;$SDK/Include/$SDKV/um;$SDK/Include/$SDKV/winrt;H:/2022_WorkSpace/smtv2/detour;$HOOKSRC;H:/360Downloads/Test_Loader/Test_Loader;C:/Users/MZPEDH/Downloads/cef_binary_3.2357.1291-master/cef_binary_3.2357.1291.g47e6d4b_windows64;H:/vs2022_Space/include"
export LIB64="$VC/lib/x64;$SDK/Lib/$SDKV/ucrt/x64;$SDK/Lib/$SDKV/um/x64;H:/360Downloads/Test_Loader/_build_check/detours_x64"
INCLUDE="$INCLUDE64" LIB="$LIB64" "$CL64" -nologo -c -EHsc -permissive- -W3 -MT -O2 -std:c++20 \
  -DWIN32 -D_WINDOWS -DUNICODE -D_UNICODE -DNDEBUG -D_CRT_SECURE_NO_WARNINGS \
  -Fo"$OUT/TestLoaderHook.obj" "$HOOKSRC/TestLoaderHook.cpp" > "$OUT/hook.cl.log" 2>&1
RC=$?
echo "  hook compile exit=$RC errors=$(grep -c 'error' "$OUT/hook.cl.log" 2>/dev/null || echo 0)"
if [ $RC -ne 0 ]; then grep "error" "$OUT/hook.cl.log" | head -5; exit 1; fi
INCLUDE="$INCLUDE64" LIB="$LIB64" "$VC/bin/Hostx64/x64/link.exe" -nologo -subsystem:WINDOWS -incremental:no -opt:ref \
  -DLL -out:"$OUT/TestLoaderHook.dll" "$OUT/TestLoaderHook.obj" \
  -DEF:"$HOOKSRC/TestLoaderHook.def" detours.lib > "$OUT/hook.link.log" 2>&1
LRC=$?
echo "  hook link exit=$LRC"
if [ $LRC -ne 0 ]; then grep -i "error\|LNK" "$OUT/hook.link.log" | head -20; exit 1; fi
ls -la "$OUT/TestLoaderHook.dll"

echo "=== 构建 stub libcef.dll + hook 登录测试 (x64，离线双端验证) ==="
INCLUDE="$INCLUDE64" LIB="$LIB64" "$CL64" -nologo -c -EHsc -permissive- -W3 -MT -O2 -std:c++20 \
  -DWIN32 -D_WINDOWS -DUNICODE -D_UNICODE -DNDEBUG -D_CRT_SECURE_NO_WARNINGS \
  -Fo"$OUT/test_stub_libcef.obj" "$BC/test_stub_libcef.cpp" > "$OUT/stub.cl.log" 2>&1
RC=$?
echo "  stub compile exit=$RC errors=$(grep -c 'error' "$OUT/stub.cl.log" 2>/dev/null || echo 0)"
if [ $RC -ne 0 ]; then grep "error" "$OUT/stub.cl.log" | head -5; exit 1; fi
INCLUDE="$INCLUDE64" LIB="$LIB64" "$VC/bin/Hostx64/x64/link.exe" -nologo -subsystem:WINDOWS -incremental:no -opt:ref \
  -DLL -DEF:"$BC/test_stub_libcef.def" -out:"$OUT/libcef.dll" "$OUT/test_stub_libcef.obj" > "$OUT/stub.link.log" 2>&1
LRC=$?
echo "  stub link exit=$LRC"
if [ $LRC -ne 0 ]; then grep -i "error\|LNK" "$OUT/stub.link.log" | head -10; exit 1; fi
ls -la "$OUT/libcef.dll"

INCLUDE="$INCLUDE64" LIB="$LIB64" "$CL64" -nologo -c -EHsc -permissive- -W3 -MT -O2 -std:c++20 \
  -DWIN32 -D_WINDOWS -DUNICODE -D_UNICODE -DNDEBUG -D_CRT_SECURE_NO_WARNINGS \
  -Fo"$OUT/test_hook_login.obj" "$BC/test_hook_login.cpp" > "$OUT/hooktest.cl.log" 2>&1
RC=$?
echo "  hooktest compile exit=$RC errors=$(grep -c 'error' "$OUT/hooktest.cl.log" 2>/dev/null || echo 0)"
if [ $RC -ne 0 ]; then grep "error" "$OUT/hooktest.cl.log" | head -5; exit 1; fi
INCLUDE="$INCLUDE64" LIB="$LIB64" "$VC/bin/Hostx64/x64/link.exe" -nologo -subsystem:CONSOLE -incremental:no -opt:ref \
  -out:"$OUT/test_hook_login.exe" "$OUT/test_hook_login.obj" user32.lib > "$OUT/hooktest.link.log" 2>&1
LRC=$?
echo "  hooktest link exit=$LRC"
if [ $LRC -ne 0 ]; then grep -i "error\|LNK" "$OUT/hooktest.link.log" | head -10; exit 1; fi
ls -la "$OUT/test_hook_login.exe"

echo "=== 构建成功 ==="
