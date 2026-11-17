#!/bin/bash
# iOS 앱 빌드(Xcode 프로젝트 없이 swiftc 로 바로). 결과는 ios/dist/
#   ./ios/build.sh device     -> RetroEmu-ios.ipa (서명 안 됨, 사이드로딩용)
#   ./ios/build.sh sim        -> RetroEmu.app (arm64 시뮬레이터)
#   ./ios/build.sh catalyst   -> RetroEmu.app (맥에서 UIKit 화면 점검용, 코어는 맥용)
# 준비: ./fetch-cores.sh ios (catalyst 는 ./fetch-cores.sh)
set -e
cd "$(dirname "$0")"
KIND=${1:-device}
OUT=dist/$KIND
rm -rf $OUT; mkdir -p $OUT
case $KIND in
  device)   SDK=iphoneos;        TARGET=arm64-apple-ios16.0;           PLAT=iPhoneOS ;;
  sim)      SDK=iphonesimulator; TARGET=arm64-apple-ios16.0-simulator; PLAT=iPhoneSimulator ;;
  catalyst) SDK=macosx;          TARGET=arm64-apple-ios16.0-macabi;    PLAT=MacOSX ;;
  *) echo "device | sim | catalyst"; exit 1 ;;
esac
SDKP=$(xcrun --sdk $SDK --show-sdk-path)
EXTRA=()
FW_LINK=(-framework OpenGLES)   # PSP·N64·게임큐브·PS2 코어의 GL 컨텍스트(EAGL)
if [ $KIND = catalyst ]; then
  FW_LINK=()
  EXTRA=(-Fsystem $SDKP/System/iOSSupport/System/Library/Frameworks -I $SDKP/System/iOSSupport/usr/include -L $SDKP/System/iOSSupport/usr/lib)
  APP=$OUT/RetroEmu.app; BIN=$APP/Contents/MacOS; RES=$APP/Contents/Resources; FW=$APP/Contents/Frameworks; PL=$APP/Contents/Info.plist
  mkdir -p $BIN $RES $FW
else
  APP=$OUT/Payload/RetroEmu.app; BIN=$APP; RES=$APP; FW=$APP/Frameworks; PL=$APP/Info.plist
  mkdir -p $FW
fi

xcrun --sdk $SDK clang -target $TARGET -isysroot $SDKP -O2 -I../host/include -I../host -c ../host/rhost.c -o $OUT/rhost.o
xcrun --sdk $SDK swiftc -target $TARGET -sdk $SDKP "${EXTRA[@]}" -O -parse-as-library \
  "${FW_LINK[@]}" -import-objc-header bridge.h Sources/*.swift ../mac/Sources/RetroEmu/Sync.swift ../mac/Sources/RetroEmu/SyncView.swift $OUT/rhost.o -o $BIN/RetroEmu
rm -f $OUT/rhost.o

# 코어
if [ $KIND = catalyst ]; then
  cp ../cores/mac/*.dylib $FW/
else
  [ -d ../cores/ios ] || { echo "iOS 코어 없음: ./fetch-cores.sh ios 먼저"; exit 1; }
  for f in ../cores/ios/*.dylib; do
    if [ $KIND = sim ]; then
      # 기기용 코어를 시뮬레이터 플랫폼으로 표시만 바꾼다(같은 arm64)
      xcrun vtool -set-build-version iossim 16.0 18.0 -replace -output $FW/$(basename $f) $f
    else
      cp $f $FW/
    fi
  done
fi
mkdir -p $RES/consoles; cp ../assets/consoles/*.png $RES/consoles/
[ -d ../cores/sysassets ] && cp -R ../cores/sysassets $RES/   # PSP·게임큐브 코어 자료

# 아이콘: actool(Xcode)이 있으면 에셋 카탈로그로, 없으면 png 그대로
ICON_KEYS=""
if [ $KIND != catalyst ]; then
  if xcrun --find actool >/dev/null 2>&1; then
    T=$OUT/xcassets/Assets.xcassets/AppIcon.appiconset; mkdir -p $T
    cp ../assets/icon.png $T/icon.png
    echo '{"images":[{"filename":"icon.png","idiom":"universal","platform":"ios","size":"1024x1024"}],"info":{"author":"xcode","version":1}}' > $T/Contents.json
    xcrun actool $OUT/xcassets/Assets.xcassets --compile $APP --platform $SDK --minimum-deployment-target 16.0 \
      --app-icon AppIcon --target-device iphone --target-device ipad --output-partial-info-plist $OUT/icon.plist >/dev/null
    rm -rf $OUT/xcassets
  else
    sips -z 120 120 ../assets/icon.png --out $APP/AppIcon60x60@2x.png >/dev/null
    sips -z 180 180 ../assets/icon.png --out $APP/AppIcon60x60@3x.png >/dev/null
    ICON_KEYS='<key>CFBundleIcons</key><dict><key>CFBundlePrimaryIcon</key><dict><key>CFBundleIconFiles</key><array><string>AppIcon60x60</string></array></dict></dict>'
  fi
fi

cat > $PL <<P
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
<key>CFBundleExecutable</key><string>RetroEmu</string>
<key>CFBundleIdentifier</key><string>com.hayoonjae.retroemu</string>
<key>CFBundleName</key><string>RetroEmu</string>
<key>CFBundleDisplayName</key><string>레트로 에뮬</string>
<key>CFBundlePackageType</key><string>APPL</string>
<key>CFBundleShortVersionString</key><string>0.1.0</string>
<key>CFBundleVersion</key><string>1</string>
<key>CFBundleSupportedPlatforms</key><array><string>$PLAT</string></array>
<key>MinimumOSVersion</key><string>16.0</string>
<key>LSMinimumSystemVersion</key><string>13.0</string>
<key>UIDeviceFamily</key><array><integer>1</integer><integer>2</integer></array>
<key>UILaunchScreen</key><dict/>
<key>UIRequiresFullScreen</key><true/>
<key>UIApplicationSupportsIndirectInputEvents</key><true/>
<key>UISupportedInterfaceOrientations</key><array><string>UIInterfaceOrientationPortrait</string><string>UIInterfaceOrientationLandscapeLeft</string><string>UIInterfaceOrientationLandscapeRight</string></array>
<key>UISupportedInterfaceOrientations~ipad</key><array><string>UIInterfaceOrientationPortrait</string><string>UIInterfaceOrientationPortraitUpsideDown</string><string>UIInterfaceOrientationLandscapeLeft</string><string>UIInterfaceOrientationLandscapeRight</string></array>
<key>UIFileSharingEnabled</key><true/>
<key>LSSupportsOpeningDocumentsInPlace</key><true/>
<key>GCSupportsControllerUserInteraction</key><true/>
<key>NSAppTransportSecurity</key><dict><key>NSAllowsArbitraryLoads</key><true/><key>NSAllowsLocalNetworking</key><true/></dict>
<key>NSLocalNetworkUsageDescription</key><string>같은 와이파이의 맥과 저장을 주고받아요.</string>
<key>GCSupportedGameControllers</key><array><dict><key>ProfileName</key><string>ExtendedGamepad</string></dict></array>
$ICON_KEYS
</dict></plist>
P
# actool 이 만든 아이콘 키를 합친다(중첩 dict 라 PlistBuddy 로)
if [ -f $OUT/icon.plist ]; then /usr/libexec/PlistBuddy -c "Merge $OUT/icon.plist" $PL >/dev/null; rm -f $OUT/icon.plist; fi
plutil -lint $PL >/dev/null || { echo "Info.plist 깨짐"; exit 1; }

# 임시 서명(사이드로딩 도구가 다시 서명한다). 코어 먼저, 앱 나중.
for f in $FW/*.dylib; do codesign -fs - "$f" 2>/dev/null; done
codesign -fs - "$APP" 2>/dev/null
if [ $KIND = device ]; then
  (cd $OUT && zip -qr ../RetroEmu-ios.ipa Payload)
  ls -la dist/RetroEmu-ios.ipa
else
  echo "만듦: $APP"
fi
