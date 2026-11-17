// rhost: libretro 코어를 불러서 돌리는 작은 호스트. 맥(Swift)과 안드로이드(JNI)가 같이 쓴다.
// 한 번에 코어 하나만 돈다(전역 상태).
#ifndef RHOST_H
#define RHOST_H
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// 조이패드 비트(libretro RETRO_DEVICE_ID_JOYPAD_* 번호와 같음)
enum {
  RH_B = 0, RH_Y, RH_SELECT, RH_START, RH_UP, RH_DOWN, RH_LEFT, RH_RIGHT,
  RH_A, RH_X, RH_L, RH_R, RH_L2, RH_R2, RH_L3, RH_R3
};

// 코어 열기. 실패하면 0, 이유는 rh_error().
int rh_open(const char *core_path, const char *system_dir, const char *save_dir);
// 롬 열기. 성공하면 1.
int rh_load(const char *rom_path);
void rh_close(void);
const char *rh_error(void);

// 한 프레임 진행.
void rh_run(void);

// 마지막 영상. RGBA8888, 줄 간격은 width*4. 아직 영상이 없으면 0.
int rh_video(const uint8_t **pixels, int *width, int *height);
// 영상이 새로 바뀐 횟수(같은 프레임인지 구분용)
uint64_t rh_video_serial(void);
double rh_fps(void);
double rh_sample_rate(void);
double rh_aspect(void);

// 소리: 스테레오 16비트. 요청한 프레임 수만큼 읽고 모자라면 0으로 채운다. 읽은 프레임 수를 돌려준다.
int rh_audio_read(int16_t *out, int frames);
int rh_audio_buffered(void);

// 입력: 비트마스크 (1 << RH_*)
void rh_set_input(uint32_t mask);

// 아날로그 스틱(N64·게임큐브·PSP·PS2): stick 0=왼쪽 1=오른쪽, x·y 는 -1~1(아래·오른쪽이 +)
void rh_set_analog(int stick, double x, double y);

// 터치(DS 아래 화면 등): 코어가 낸 화면(rh_video 의 그림) 기준 0~1 좌표. 손을 떼면 pressed=0.
void rh_set_pointer(double x, double y, int pressed);

// OpenGL 로 그리는 코어(PSP)용. 앱이 컨텍스트를 만들어 준다.
// 맥과 안드로이드는 rhost 가 직접 만들어서(CGL, EGL) 따로 안 넣어도 된다.
// rhost 는 코어를 부를 때마다 make_current(1), 끝나면 make_current(0) 을 부른다(스레드가 바뀌어도 되게).
typedef struct {
  int (*create)(int gles, int major, int minor);   // gles: 0=데스크탑 GL(major>=3 이면 코어 프로필), 1=GLES. 성공하면 1
  void (*make_current)(int on);
  void *(*get_proc)(const char *name);
  void (*destroy)(void);
} rh_gl_provider;
void rh_set_gl_provider(const rh_gl_provider *p);
// 지금 코어가 OpenGL 로 그리는지
int rh_uses_gl(void);

// 세이브 스테이트 / 배터리 세이브(SRAM)
size_t rh_state_size(void);
int rh_state_save(const char *path);
int rh_state_load(const char *path);
int rh_sram_save(const char *path);
int rh_sram_load(const char *path);

const char *rh_core_name(void);

#ifdef __cplusplus
}
#endif
#endif
