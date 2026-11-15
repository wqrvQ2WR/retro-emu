#!/bin/bash
# libretro 공식 빌드봇에서 코어를 받아 cores/ 와 안드로이드 jniLibs 에 둔다.
set -e
cd "$(dirname "$0")"
B=https://buildbot.libretro.com/nightly
mkdir -p cores/mac cores/android android/app/src/main/jniLibs/arm64-v8a
for c in fceumm mgba snes9x genesis_plus_gx pcsx_rearmed; do
  d=$(mktemp -d)
  curl -sSfL -o $d/m.zip $B/apple/osx/arm64/latest/${c}_libretro.dylib.zip && unzip -q -o $d/m.zip -d cores/mac
  curl -sSfL -o $d/a.zip $B/android/latest/arm64-v8a/${c}_libretro_android.so.zip && unzip -q -o $d/a.zip -d cores/android
  cp cores/android/${c}_libretro_android.so android/app/src/main/jniLibs/arm64-v8a/lib${c}_core.so
  rm -rf $d; echo "$c ok"
done
