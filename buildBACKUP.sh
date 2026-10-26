#!/bin/bash
# Build AradeRenamer with MSVC (cl.exe) + Windows SDK rc.exe
# Run from Git Bash. Uses the VS 2022 BuildTools paths detected on this machine.

set -e

# Git Bash mangles any arg starting with '/' (e.g. /nologo, /W3, /OUT:) into a
# Windows path. Disable MSYS path conversion so MSVC/rc see the switches literally.
export MSYS_NO_PATHCONV=1
export MSYS2_ARG_CONV_EXCLUDE='*'

VS="C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools"
VC="$VS/VC/Tools/MSVC/14.44.35207"
SDK="C:/Program Files (x86)/Windows Kits/10"
SDKVER="10.0.26100.0"
HOST="$VC/bin/Hostx64/x64"
RC="$SDK/bin/$SDKVER/x64/rc.exe"

export PATH="$HOST:$(dirname "$RC"):$PATH"

export INCLUDE="$VC/include;$VC/atlmfc/include;$SDK/Include/$SDKVER/um;$SDK/Include/$SDKVER/shared;$SDK/Include/$SDKVER/ucrt"
export LIB="$VC/lib/x64;$SDK/Lib/$SDKVER/um/x64;$SDK/Lib/$SDKVER/ucrt/x64"

mkdir -p build bin

echo "== Compiling resources =="
"$RC" /nologo /fo build/AradeRenamer.res resources/AradeRenamer.rc

echo "== Compiling C++ =="
CL="$HOST/cl.exe"
"$CL" /nologo /W3 /O2 /EHsc /MT /std:c++17 /utf-8 /DUNICODE /D_UNICODE \
  src/main.cpp src/engine.cpp src/conv.cpp build/AradeRenamer.res \
  /Fobuild/ \
  /link /SUBSYSTEM:WINDOWS /MANIFEST:NO /OUT:bin/AradeRenamer.exe \
  user32.lib kernel32.lib gdi32.lib comctl32.lib shell32.lib \
  comdlg32.lib advapi32.lib shlwapi.lib ole32.lib msimg32.lib

echo "== Embedding manifest (comctl32 v6 visual styles) =="
MT="$(dirname "$RC")/mt.exe"
"$MT" -manifest resources/AradeRenamer.manifest -outputresource:bin/AradeRenamer.exe\;#1

echo "== Done: bin/AradeRenamer.exe =="
ls -la bin/AradeRenamer.exe
