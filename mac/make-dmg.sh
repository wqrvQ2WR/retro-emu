#!/bin/bash
# 배포용 dmg: mac/dist/RetroEmu-mac-arm64.dmg (앱 + 응용 프로그램 바로가기)
set -e
cd "$(dirname "$0")"
rm -rf dist; mkdir -p dist/stage
APP="$PWD/dist/stage/RetroEmu.app" ./build-app.sh
ln -s /Applications dist/stage/Applications
hdiutil create -volname RetroEmu -srcfolder dist/stage -ov -format UDZO dist/RetroEmu-mac-arm64.dmg >/dev/null
rm -rf dist/stage
ls -la dist/RetroEmu-mac-arm64.dmg
