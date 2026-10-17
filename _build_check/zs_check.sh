#!/usr/bin/env bash
# /Zs 语法+语义检查：对齐 Test_Loader Release|Win32 真实构建配置
export MSYS2_ARG_CONV_EXCL='*'

VC="C:/Program Files/Microsoft Visual Studio/18/Professional/VC/Tools/MSVC/14.51.36231"
SDK="C:/Program Files (x86)/Windows Kits/10"
SDKV="10.0.26100.0"
CL="$VC/bin/Hostx86/x86/cl.exe"
OUT="C:/Users/MZPEDH/AppData/Local/Temp/zs_check"
mkdir -p "$OUT"

export INCLUDE="$VC/include;$VC/atlmfc/include;$SDK/Include/$SDKV/ucrt;$SDK/Include/$SDKV/shared;$SDK/Include/$SDKV/um;$SDK/Include/$SDKV/winrt"

PROJ="H:/360Downloads/Test_Loader/Test_Loader"
FILES=(
  "$PROJ/UrlProtocol.cpp"
  "$PROJ/SingleInstance.cpp"
  "$PROJ/Test_Loader.cpp"
  "$PROJ/CMainDlg.cpp"
)

for f in "${FILES[@]}"; do
  base=$(basename "$f")
  "$CL" -nologo -Zs -std:c++20 -EHsc -permissive- -W3 -MT \
    -DWIN32 -D_WINDOWS -DUNICODE -D_UNICODE -DNDEBUG -D_CRT_SECURE_NO_WARNINGS -DHV_STATICLIB -DDUITHINKLIB \
    "/IH:/360Downloads/Test_Loader/Test_Loader" \
    "/IH:/360Downloads/DUIThink_git/DUIThink" \
    "/IH:/vs2022_Space/include" \
    "/IH:/OpenSSL-Win32/include" \
    "/IH:/vs2022_Space/include/hv/include" \
    "$f" > "$OUT/$base.log" 2>&1
  echo "=== $base exit=$? ==="
done

python -c "
import glob, os
for p in sorted(glob.glob(r'C:/Users/MZPEDH/AppData/Local/Temp/zs_check/*.log')):
    t = open(p,'rb').read().decode('gbk', errors='replace')
    lines = [l.rstrip() for l in t.split(chr(10)) if l.strip()]
    errs = [l for l in lines if ' error ' in l or 'fatal error' in l]
    print(os.path.basename(p), '-> error=%d' % len(errs))
    for l in errs[:15]: print('   E ', l[:180])
"
