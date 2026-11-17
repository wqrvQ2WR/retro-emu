// 저장 동기화(윈도우판). 같은 동기화 코드를 넣은 기기끼리 saves/ 의 상태 저장·썸네일·배터리 세이브를 주고받는다.
// 규칙은 맥·iOS Sync.swift, 안드로이드 Sync.kt 와 같다. 서버는 sync-server/server.py.
// HTTP 는 윈도우에선 WinHTTP, 맥(화면 점검용 빌드)에선 curl 명령을 쓴다.
#ifndef RETRO_SYNC_H
#define RETRO_SYNC_H
#include <time.h>

#define SYNC_DEFAULT_SERVER "https://pi.taild4dc8e.ts.net/retro-sync"

// 설정 파일(data_dir/sync.txt)을 읽는다. 환경변수 RETROEMU_SYNC 가 있으면 서버를 그걸로.
void sync_init(const char *data_dir);
const char *sync_code(void);
void sync_set_code(const char *code);   // 앞뒤 공백 빼고 대문자로 저장
// 서버 주소. 같은 와이파이의 맥(sync-server/install-mac.sh)이나 테일스케일 너머 파이 등.
// "192.168.0.5:8270" 처럼 넣어도 http:// 와 /retro-sync 를 붙여 준다.
const char *sync_server(void);
void sync_set_server(const char *url);
int sync_enabled(void);                 // 코드가 4글자 이상
time_t sync_last(void);
void sync_new_code(char out[10]);       // 예: K7PM-2QXA
void sync_key(const char *code, char out[33]);

// 한 게임 저장을 서버와 맞춘다(막고 기다림). 바뀐 파일 수, 실패하면 -1.
int sync_game(const char *saves_dir, const char *rom_file, int pull);
// 뒤에서 올리기만 / 지운 슬롯 알리기
void sync_push_async(const char *saves_dir, const char *rom_file);
void sync_deleted_async(const char *rom_file, const char *slot);
// 서버에 닿는지
int sync_check(void);
// 뒤에서 도는 동기화가 끝날 때까지 최대 ms 만큼 기다린다(앱 끌 때)
void sync_wait(int ms);
#endif
