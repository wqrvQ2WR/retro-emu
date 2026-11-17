#!/bin/bash
# 맥을 같은 와이파이의 동기화 서버로: 로그인하면 자동으로 뜨는 LaunchAgent 로 깐다(관리자 암호 필요 없음).
# 서버 파일과 저장 데이터는 ~/Library/Application Support/RetroEmuSync 에 둔다(데스크탑 폴더 권한 문제를 피함).
#   ./sync-server/install-mac.sh          깔기/다시 깔기
#   ./sync-server/install-mac.sh remove   지우기(저장 데이터는 남김)
set -e
cd "$(dirname "$0")"
LABEL=com.hayoonjae.retrosync
DIR="$HOME/Library/Application Support/RetroEmuSync"
PL="$HOME/Library/LaunchAgents/$LABEL.plist"
launchctl bootout gui/$(id -u)/$LABEL 2>/dev/null || true
if [ "$1" = remove ]; then rm -f "$PL"; echo "지움"; exit 0; fi
mkdir -p "$DIR/data"
cp server.py "$DIR/server.py"
cat > "$PL" <<P
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
<key>Label</key><string>$LABEL</string>
<key>ProgramArguments</key><array><string>/usr/bin/python3</string><string>$DIR/server.py</string></array>
<key>EnvironmentVariables</key><dict>
  <key>HOST</key><string>0.0.0.0</string>
  <key>PORT</key><string>8270</string>
  <key>DATA</key><string>$DIR/data</string>
</dict>
<key>RunAtLoad</key><true/>
<key>KeepAlive</key><true/>
<key>StandardErrorPath</key><string>$DIR/server.log</string>
<key>StandardOutPath</key><string>$DIR/server.log</string>
</dict></plist>
P
launchctl bootstrap gui/$(id -u) "$PL"
sleep 1
IP=$(ipconfig getifaddr en0 || ipconfig getifaddr en1 || echo "맥IP")
curl -s -o /dev/null -w "이 맥  %{http_code}\n" http://127.0.0.1:8270/retro-sync/health
echo "같은 와이파이 기기에 넣을 주소: http://$IP:8270/retro-sync"
echo "이 맥 앱에 넣을 주소:          http://127.0.0.1:8270/retro-sync"
