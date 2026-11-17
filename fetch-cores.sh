#!/bin/bash
# libretro 공식 빌드봇에서 코어를 받아 cores/ 와 안드로이드 jniLibs 에 둔다.
# ./fetch-cores.sh windows  -> 윈도우(x64) 코어만 cores/windows 에 받는다.
# ./fetch-cores.sh ios      -> iOS(arm64) 코어만 cores/ios 에 받는다(이름은 <코어>_libretro.dylib 로 맞춤).
set -e
cd "$(dirname "$0")"
B=https://buildbot.libretro.com/nightly
CORES="fceumm mgba snes9x genesis_plus_gx pcsx_rearmed melonds ppsspp mupen64plus_next dolphin play"
# 코어가 시스템 폴더에서 찾는 자료(PSP 글꼴 등, 게임큐브 Sys). cores/sysassets 에 풀어 두고 앱이 들고 간다.
fetch_assets() {
  mkdir -p cores/sysassets
  for a in PPSSPP Dolphin; do
    [ -d "cores/sysassets/$([ $a = Dolphin ] && echo dolphin-emu || echo $a)" ] && continue
    d=$(mktemp -d); curl -sSfL -o $d/a.zip https://buildbot.libretro.com/assets/system/$a.zip && unzip -q -o $d/a.zip -d cores/sysassets; rm -rf $d
  done
}
fetch_assets
if [ "$1" = windows ]; then
  mkdir -p cores/windows
  for c in $CORES; do
    d=$(mktemp -d)
    curl -sSfL -o $d/w.zip $B/windows/x86_64/latest/${c}_libretro.dll.zip && unzip -q -o $d/w.zip -d cores/windows
    rm -rf $d; echo "$c ok"
  done
  exit 0
fi
if [ "$1" = ios ]; then
  mkdir -p cores/ios
  for c in $CORES; do
    d=$(mktemp -d)
    # mgba 만 이름에 _ios 가 안 붙어 있다
    curl -sfL -o $d/i.zip $B/apple/ios-arm64/latest/${c}_libretro_ios.dylib.zip \
      || curl -sSfL -o $d/i.zip $B/apple/ios-arm64/latest/${c}_libretro.dylib.zip
    unzip -q -o $d/i.zip -d $d/x
    mv $d/x/*.dylib cores/ios/${c}_libretro.dylib
    rm -rf $d; echo "$c ok"
  done
  exit 0
fi
mkdir -p cores/mac cores/android android/app/src/main/jniLibs/arm64-v8a
for c in $CORES; do
  d=$(mktemp -d)
  curl -sSfL -o $d/m.zip $B/apple/osx/arm64/latest/${c}_libretro.dylib.zip && unzip -q -o $d/m.zip -d cores/mac
  an=$c; [ $c = mupen64plus_next ] && an=mupen64plus_next_gles3   # 안드로이드 N64 는 GLES3 판
  curl -sSfL -o $d/a.zip $B/android/latest/arm64-v8a/${an}_libretro_android.so.zip && unzip -q -o $d/a.zip -d cores/android
  cp cores/android/${an}_libretro_android.so android/app/src/main/jniLibs/arm64-v8a/lib${c}_core.so
  rm -rf $d; echo "$c ok"
done
