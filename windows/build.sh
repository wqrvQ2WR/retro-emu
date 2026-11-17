#!/bin/bash
# 윈도우판 빌드(맥에서 zig 로 교차 컴파일). 결과: windows/dist/RetroEmu/ 와 RetroEmu-windows-x64.zip
#   준비: brew install zig, ./fetch-cores.sh windows, ./windows/fetch-deps.sh
set -e
cd "$(dirname "$0")"
V=vendor
SDL=$V/SDL2-2.32.10/x86_64-w64-mingw32
TTF=$V/SDL2_ttf-2.24.0/x86_64-w64-mingw32
[ -d "$SDL" ] && [ -d "$TTF" ] || { echo "vendor 없음: ./windows/fetch-deps.sh 먼저"; exit 1; }
[ -d ../cores/windows ] || { echo "윈도우 코어 없음: ./fetch-cores.sh windows 먼저"; exit 1; }

OUT=dist/RetroEmu
rm -rf dist; mkdir -p $OUT/cores $OUT/fonts $OUT/consoles

zig rc /fo build-icon.res app.rc
zig cc -target x86_64-windows-gnu -O2 -std=gnu11 \
  -I$SDL/include/SDL2 -I$TTF/include/SDL2 -I../host/include -I../host -I$V/stb \
  main.c sync.c ../host/rhost.c build-icon.res \
  $SDL/lib/libSDL2main.a $SDL/lib/libSDL2.dll.a $TTF/lib/libSDL2_ttf.dll.a -lcomdlg32 -lwinhttp \
  -Wl,--subsystem,windows -o $OUT/RetroEmu.exe
rm -f build-icon.res $OUT/RetroEmu.pdb

cp $SDL/bin/SDL2.dll $TTF/bin/SDL2_ttf.dll $OUT/
cp ../cores/windows/*.dll $OUT/cores/
cp $V/font/public/static/Pretendard-Bold.otf $V/font/public/static/Pretendard-ExtraBold.otf $OUT/fonts/
cp ../assets/consoles/*.png $OUT/consoles/
cp icon.png $OUT/
mkdir -p $OUT/licenses
cp $V/font/LICENSE.txt $OUT/licenses/Pretendard-OFL.txt
cp $V/SDL2-2.32.10/LICENSE.txt $OUT/licenses/SDL2.txt 2>/dev/null || true
cp $V/SDL2_ttf-2.24.0/LICENSE.txt $OUT/licenses/SDL2_ttf.txt 2>/dev/null || true
cp ../assets/consoles/CREDITS.md $OUT/licenses/consoles.md
cp 읽어보기.txt $OUT/

(cd dist && rm -f RetroEmu-windows-x64.zip && zip -qr RetroEmu-windows-x64.zip RetroEmu)
ls -la dist/RetroEmu-windows-x64.zip
