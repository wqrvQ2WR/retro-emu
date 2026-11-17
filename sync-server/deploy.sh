#!/bin/sh
# 파이 배포: 전송 -> systemd 등록/재시작 -> 응답 확인. 저장 데이터는 ~/retro-sync-data 에 따로 있어서 안 덮어쓴다.
#   PI_HOST=aaaa@<주소> ./sync-server/deploy.sh   (기본: 테일스케일 이름 pi)
set -e
APP=retro-sync
FILES="server.py retro-sync.service"
PUBLIC_CHECK="https://pi.taild4dc8e.ts.net/retro-sync/health"
HOST=${PI_HOST:-aaaa@pi.taild4dc8e.ts.net}
cd "$(dirname "$0")"

echo "-> 전송"
COPYFILE_DISABLE=1 tar --no-xattrs -cf - $FILES | ssh -o BatchMode=yes "$HOST" "mkdir -p ~/$APP && cd ~/$APP && tar xf - 2>/dev/null"
echo "-> 서비스 등록/재시작"
ssh -o BatchMode=yes "$HOST" "sudo -n cp ~/$APP/$APP.service /etc/systemd/system/ && sudo -n systemctl daemon-reload && sudo -n systemctl enable $APP >/dev/null 2>&1; sudo -n systemctl restart $APP"
sleep 2
if ! ssh -o BatchMode=yes "$HOST" "systemctl is-active --quiet $APP"; then
  echo "!! 서비스가 안 떴어요"; ssh -o BatchMode=yes "$HOST" "journalctl -u $APP -n 30 --no-pager"; exit 1
fi
ssh -o BatchMode=yes "$HOST" "curl -s -o /dev/null -w 'LAN    %{http_code}\n' http://127.0.0.1:8270/retro-sync/health"
curl -s -o /dev/null -w "공개   %{http_code}  $PUBLIC_CHECK\n" --max-time 15 "$PUBLIC_CHECK" || true
