#!/bin/bash
# Build Arade Renamer with MSVC (cl.exe) + Windows SDK rc.exe
# Run from Git Bash. VS / MSVC / SDK 版本全部自动探测, 换机器不用改脚本。
#
# !! 升版本号: 改 resources/AradeRenamer.rc 里的 VS_VERSION_INFO
# !! (FILEVERSION / PRODUCTVERSION / FileVersion / ProductVersion) —— exe 属性里的
# !! 「版本」标签页读的是它。输出文件名固定为 AradeRenamer.exe, 不带版本号;
# !! 要发带版本号的包就自己从 bin/ 拷一份到 dist/ 再改名。

set -e

# Git Bash mangles any arg starting with '/' into a Windows path.
# Disable MSYS path conversion so MSVC/rc see the switches literally.
export MSYS_NO_PATHCONV=1
export MSYS2_ARG_CONV_EXCL='*'

# ---- 探测工具链 ----
# 硬编码 VS/SDK 版本号在别人机器上一定编不过, 这里全部自动探测;
# 想固定版本就提前设环境变量 VS_ROOT / SDKVER 覆盖。
pick_newest() { ls -1 "$1" 2>/dev/null | grep -E '^[0-9]+(\.[0-9]+)*$' | sort -V | tail -1; }

VS="${VS_ROOT:-}"
if [ -z "$VS" ]; then
    VSWHERE="C:/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe"
    if [ -f "$VSWHERE" ]; then
        VS="$("$VSWHERE" -latest -products '*' -property installationPath 2>/dev/null | tr -d '\r' | tail -1)"
    fi
fi
# vswhere 没装(比如只装了 BuildTools 的精简机器)就退回最常见的两个位置
if [ -z "$VS" ] || [ ! -d "$VS" ]; then
    for c in "C:/Program Files/Microsoft Visual Studio/2022/Community" \
             "C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools"; do
        [ -d "$c" ] && VS="$c" && break
    done
fi
[ -d "$VS" ] || { echo "!! 找不到 Visual Studio, 请设 VS_ROOT=<安装目录> 再跑"; exit 1; }

SDK="${WINSDK:-C:/Program Files (x86)/Windows Kits/10}"
[ -d "$SDK" ] || { echo "!! 找不到 Windows SDK: $SDK"; exit 1; }

MSVCVER="$(pick_newest "$VS/VC/Tools/MSVC")"
[ -n "$MSVCVER" ] || { echo "!! $VS/VC/Tools/MSVC 下没有工具集"; exit 1; }
SDKVER="${SDKVER:-$(pick_newest "$SDK/Include")}"
[ -n "$SDKVER" ] || { echo "!! $SDK/Include 下没有 SDK 版本"; exit 1; }

VC="$VS/VC/Tools/MSVC/$MSVCVER"
HOST="$VC/bin/Hostx64/x64"
RC="$SDK/bin/$SDKVER/x64/rc.exe"
echo "== Toolchain: VS=$VS"
echo "==            MSVC=$MSVCVER  SDK=$SDKVER"

export PATH="$HOST:$(dirname "$RC"):$PATH"

export INCLUDE="$VC/include;$VC/atlmfc/include;$SDK/Include/$SDKVER/um;$SDK/Include/$SDKVER/shared;$SDK/Include/$SDKVER/ucrt"
export LIB="$VC/lib/x64;$SDK/Lib/$SDKVER/um/x64;$SDK/Lib/$SDKVER/ucrt/x64"

mkdir -p build bin

echo "== Compiling resources =="
"$RC" /nologo /fo build/AradeRenamer.res resources/AradeRenamer.rc

echo "== Compiling C++ =="
CL="$HOST/cl.exe"

# 编译选项说明：
# /GL : 全程序优化 (LTCG)，需要把所有 cpp 源文件一起编译才能做全局跨文件优化
# /O2 : 速度优化
# /W4 : 高警告级别（比 W3 更细，能扫出潜在 Bug）
# /Gy : 把每个函数编成独立的 COMDAT，便于链接器删掉没用到的函数
# /MT : 静态链接 C++ 运行库（绿色单文件，免安装）
# /utf-8 : 源码和字符串统一用 UTF-8
"$CL" /nologo /W4 /O2 /GL /Gy /EHsc /MT /std:c++17 /utf-8 /DUNICODE /D_UNICODE \
  src/main.cpp src/engine.cpp src/conv.cpp src/lang.cpp build/AradeRenamer.res \
  /Fobuild/ \
  /link /SUBSYSTEM:WINDOWS /MANIFEST:NO /OUT:bin/AradeRenamer.exe \
  /LTCG /OPT:REF /OPT:ICF \
  user32.lib kernel32.lib gdi32.lib comctl32.lib shell32.lib \
  comdlg32.lib advapi32.lib shlwapi.lib ole32.lib msimg32.lib

echo "== Embedding manifest (comctl32 v6 visual styles) =="
MT="$(dirname "$RC")/mt.exe"
"$MT" -manifest resources/AradeRenamer.manifest -outputresource:bin/AradeRenamer.exe\;#1

echo "== Done: bin/AradeRenamer.exe =="
ls -la bin/AradeRenamer.exe
