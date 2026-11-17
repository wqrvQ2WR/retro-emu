#!/bin/bash
# 맥 앱 빌드: ~/Applications/RetroEmu.app
set -e
cd "$(dirname "$0")/.."
swift build -c release
APP=/Applications/RetroEmu.app; [ -w /Applications ] || APP=~/Applications/RetroEmu.app
rm -rf "$APP"; mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources/cores"
cp .build/release/RetroEmu "$APP/Contents/MacOS/"
cp cores/mac/*.dylib "$APP/Contents/Resources/cores/"
cp assets/AppIcon.icns "$APP/Contents/Resources/"
mkdir -p "$APP/Contents/Resources/consoles"; cp assets/consoles/*.png "$APP/Contents/Resources/consoles/"
cat > "$APP/Contents/Info.plist" <<P
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
<key>CFBundleExecutable</key><string>RetroEmu</string>
<key>CFBundleIdentifier</key><string>com.hayoonjae.retroemu</string>
<key>CFBundleName</key><string>RetroEmu</string>
<key>CFBundleDisplayName</key><string>레트로 에뮬레이터</string>
<key>CFBundleIconFile</key><string>AppIcon</string>
<key>CFBundlePackageType</key><string>APPL</string>
<key>CFBundleShortVersionString</key><string>0.1</string>
<key>LSMinimumSystemVersion</key><string>13.0</string>
<key>NSHighResolutionCapable</key><true/>
</dict></plist>
P
codesign --force --deep -s - "$APP" 2>&1 | tail -1
echo "만듦: $APP"
