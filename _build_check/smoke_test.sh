#!/usr/bin/env bash
# 端到端 smoke test
export MSYS2_ARG_CONV_EXCL='*'
PY="C:/Users/MZPEDH/.workbuddy/binaries/python/versions/3.13.12/python.exe"
VC="C:/Program Files/Microsoft Visual Studio/18/Professional/VC/Tools/MSVC/14.51.36231"
SDK="C:/Program Files (x86)/Windows Kits/10"
SDKV="10.0.26100.0"
CL="$VC/bin/Hostx86/x86/cl.exe"
OUT="C:/Users/MZPEDH/AppData/Local/Temp/zs_check/build"
TMP="C:/Users/MZPEDH/AppData/Local/Temp/zs_check"
export INCLUDE="$VC/include;$VC/atlmfc/include;$SDK/Include/$SDKV/ucrt;$SDK/Include/$SDKV/shared;$SDK/Include/$SDKV/um;$SDK/Include/$SDKV/winrt"
export LIB="$VC/lib/x86;$VC/atlmfc/lib/x86;$SDK/Lib/$SDKV/ucrt/x86;$SDK/Lib/$SDKV/um/x86"

echo "=== 编译 harness ==="
"$CL" -nologo -std:c++20 -EHsc -W3 -MT -O2 -DWIN32 -D_WINDOWS -DUNICODE -D_UNICODE -DNDEBUG \
  -Fo"$OUT/harness_first.obj" -Fe"$OUT/harness_first.exe" \
  "H:/360Downloads/Test_Loader/_build_check/harness_first.cpp" user32.lib > "$OUT/harness_build.log" 2>&1
echo "harness build exit=$?"

# 清理旧痕迹
rm -f "$TMP/harness_received.txt" "$TMP/yxysoft_pending_payload.txt"

echo ""
echo "=== [A] WM_COPYDATA 投递路径（假首实例带窗口+属性） ==="
"$OUT/harness_first.exe" &
HARNESS_PID=$!
sleep 2
"$OUT/Test_Loader_test.exe" "yxysoft://login-42"
echo "第二个实例 exit=$? (预期 0)"
sleep 2
if [ -f "$TMP/harness_received.txt" ]; then
  PAYLOAD=$("$PY" -c "
import struct
d = open(r'C:/Users/MZPEDH/AppData/Local/Temp/zs_check/harness_received.txt','rb').read()
print(d.decode('utf-16-le', errors='replace').rstrip(chr(0)))
")
  echo "harness 收到 payload: [$PAYLOAD]"
  [ "$PAYLOAD" = "login-42" ] && echo "  -> PASS" || echo "  -> FAIL"
else
  echo "  -> FAIL: harness_received.txt 不存在"
fi
kill $HARNESS_PID 2>/dev/null

echo ""
echo "=== [B] splash 期兜底文件路径（只占互斥量、无窗口） ==="
"$OUT/harness_first.exe" --mutexonly &
MUTEX_PID=$!
sleep 2
"$OUT/Test_Loader_test.exe" "yxysoft://login-7"
echo "第二个实例 exit=$? (预期 0)"
sleep 1
if [ -f "$TMP/yxysoft_pending_payload.txt" ]; then
  PAYLOAD=$("$PY" -c "
d = open(r'C:/Users/MZPEDH/AppData/Local/Temp/zs_check/yxysoft_pending_payload.txt','rb').read()
if d[:3] == b'\xef\xbb\xbf': d = d[3:]
print(d.decode('utf-8', errors='replace'))
")
  echo "兜底文件内容: [$PAYLOAD]"
  [ "$PAYLOAD" = "login-7" ] && echo "  -> PASS" || echo "  -> FAIL"
else
  echo "  -> FAIL: 兜底文件不存在"
fi
kill $MUTEX_PID 2>/dev/null

echo ""
echo "=== [C] 首实例正常启动（无 URL） + 协议注册指向 ==="
"$OUT/Test_Loader_test.exe" &
APP_PID=$!
sleep 10
if kill -0 $APP_PID 2>/dev/null; then
  echo "首实例存活 10s (splash+主窗口未崩) -> PASS"
else
  echo "首实例已退出 -> FAIL"
fi
"$PY" -c "
import winreg
try:
    k = winreg.OpenKey(winreg.HKEY_CURRENT_USER, r'Software\\Classes\\yxysoft\\shell\\open\\command')
    v, _ = winreg.QueryValueEx(k, None)
    print('注册表 command =', v)
    print('指向测试 exe:', 'Test_Loader_test.exe' in v)
    winreg.CloseKey(k)
except Exception as e:
    print('注册表读取失败:', e)
"
kill $APP_PID 2>/dev/null

echo ""
echo "=== smoke test 结束 ==="
