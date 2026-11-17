#include "rhost.h"
#include "libretro.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#ifdef _WIN32
// 윈도우: 코어는 DLL, 경로는 UTF-8 로 받아서 넓은 문자로 바꿔 연다(한글 폴더 대응).
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
static wchar_t *widen(const char *s) {
  int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
  wchar_t *w = malloc(sizeof(wchar_t) * (n > 0 ? n : 1)); w[0] = 0;
  if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s, -1, w, n);
  return w;
}
static FILE *u8fopen(const char *p, const char *mode) {
  wchar_t *wp = widen(p), *wm = widen(mode); FILE *f = _wfopen(wp, wm); free(wp); free(wm); return f;
}
static int u8replace(const char *from, const char *to) {  // 있으면 덮어쓰기
  wchar_t *a = widen(from), *b = widen(to);
  int ok = MoveFileExW(a, b, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
  free(a); free(b); return ok ? 0 : -1;
}
static int u8remove(const char *p) { wchar_t *w = widen(p); int r = _wremove(w); free(w); return r; }
static void *lib_open(const char *p) { wchar_t *w = widen(p); HMODULE h = LoadLibraryW(w); free(w); return h; }
static void *lib_sym(void *h, const char *n) { return (void *)GetProcAddress((HMODULE)h, n); }
static void lib_close(void *h) { FreeLibrary((HMODULE)h); }
static const char *lib_err(void) { static char b[64]; snprintf(b, sizeof b, "윈도우 오류 %lu", GetLastError()); return b; }
typedef SRWLOCK mutex_t;
#define MUTEX_INIT SRWLOCK_INIT
#define mutex_lock(m) AcquireSRWLockExclusive(m)
#define mutex_unlock(m) ReleaseSRWLockExclusive(m)
#else
#include <dlfcn.h>
#include <pthread.h>
#define u8fopen fopen
#define u8replace rename
#define u8remove remove
static void *lib_open(const char *p) { return dlopen(p, RTLD_NOW | RTLD_LOCAL); }
static void *lib_sym(void *h, const char *n) { return dlsym(h, n); }
static void lib_close(void *h) { dlclose(h); }
static const char *lib_err(void) { return dlerror(); }
typedef pthread_mutex_t mutex_t;
#define MUTEX_INIT PTHREAD_MUTEX_INITIALIZER
#define mutex_lock pthread_mutex_lock
#define mutex_unlock pthread_mutex_unlock
#endif

#ifdef __ANDROID__
#include <android/log.h>
#define LOG(...) __android_log_print(ANDROID_LOG_INFO, "rhost", __VA_ARGS__)
#else
#define LOG(...) (fprintf(stderr, "[rhost] " __VA_ARGS__), fputc('\n', stderr))
#endif

// 코어 함수들
static struct {
  void *lib;
  void (*set_environment)(retro_environment_t);
  void (*set_video_refresh)(retro_video_refresh_t);
  void (*set_audio_sample)(retro_audio_sample_t);
  void (*set_audio_sample_batch)(retro_audio_sample_batch_t);
  void (*set_input_poll)(retro_input_poll_t);
  void (*set_input_state)(retro_input_state_t);
  void (*init)(void);
  void (*deinit)(void);
  void (*get_system_info)(struct retro_system_info *);
  void (*get_system_av_info)(struct retro_system_av_info *);
  bool (*load_game)(const struct retro_game_info *);
  void (*unload_game)(void);
  void (*run)(void);
  size_t (*serialize_size)(void);
  bool (*serialize)(void *, size_t);
  bool (*unserialize)(const void *, size_t);
  void *(*get_memory_data)(unsigned);
  size_t (*get_memory_size)(unsigned);
  void (*set_controller_port_device)(unsigned, unsigned);
} C;

static char g_err[256];
static char g_sys[1024], g_save[1024];
static char g_name[128];
static int g_loaded;
static enum retro_pixel_format g_fmt = RETRO_PIXEL_FORMAT_0RGB1555;
static struct retro_system_av_info g_av;
static void *g_rom;

// 영상
static uint8_t *g_pix;
static size_t g_pix_cap;
static int g_w, g_h;
static uint64_t g_serial;

// 소리 고리 버퍼(프레임 단위, 스테레오)
#define RING_FRAMES 16384
static int16_t g_ring[RING_FRAMES * 2];
static volatile unsigned g_rd, g_wr; // 단조 증가
static mutex_t g_mu = MUTEX_INIT;

static uint32_t g_input;

static void set_err(const char *fmt, ...) {
  va_list ap; va_start(ap, fmt); vsnprintf(g_err, sizeof g_err, fmt, ap); va_end(ap);
  LOG("error: %s", g_err);
}
const char *rh_error(void) { return g_err; }
const char *rh_core_name(void) { return g_name; }

static void log_cb(enum retro_log_level lvl, const char *fmt, ...) {
  if (lvl < RETRO_LOG_INFO) return;
  char b[512]; va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
  size_t n = strlen(b); if (n && b[n - 1] == '\n') b[n - 1] = 0;
  LOG("core: %s", b);
}

static bool env_cb(unsigned cmd, void *data) {
  switch (cmd) {
  case RETRO_ENVIRONMENT_GET_CAN_DUPE: *(bool *)data = true; return true;
  case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT: g_fmt = *(enum retro_pixel_format *)data; return true;
  case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY: *(const char **)data = g_sys; return true;
  case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY: *(const char **)data = g_save; return true;
  case RETRO_ENVIRONMENT_GET_LIBRETRO_PATH: return false;
  case RETRO_ENVIRONMENT_GET_LOG_INTERFACE: ((struct retro_log_callback *)data)->log = log_cb; return true;
  case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS: return true;
  case RETRO_ENVIRONMENT_SET_VARIABLES: return true;
  case RETRO_ENVIRONMENT_GET_VARIABLE: { // 몇 개만 바꾸고 나머지는 코어 기본값
    struct retro_variable *v = data;
    static const char *opts[][2] = {
      {"mgba_sgb_borders", "OFF"},  // 슈퍼 게임보이 테두리를 빼서 게임보이 화면을 크게(색은 그대로)
    };
    for (size_t i = 0; v && v->key && i < sizeof opts / sizeof *opts; i++)
      if (!strcmp(v->key, opts[i][0])) { v->value = opts[i][1]; return true; }
    return false;
  }
  case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE: *(bool *)data = false; return true;
  case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION: *(unsigned *)data = 0; return true;
  case RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL: return true;
  case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME: return true;
  case RETRO_ENVIRONMENT_SET_GEOMETRY: if (data) g_av.geometry = *(struct retro_game_geometry *)data; return true;
  case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO: if (data) g_av = *(struct retro_system_av_info *)data; return true;
  case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO: return true;
  case RETRO_ENVIRONMENT_SET_MEMORY_MAPS: return true;
  case RETRO_ENVIRONMENT_SET_SERIALIZATION_QUIRKS: return true;
  case RETRO_ENVIRONMENT_GET_INPUT_BITMASKS: return true;
  default: return false;
  }
}

static void ensure_pix(int w, int h) {
  size_t need = (size_t)w * h * 4;
  if (need > g_pix_cap) { free(g_pix); g_pix = malloc(need); g_pix_cap = need; }
  g_w = w; g_h = h;
}

static void video_cb(const void *data, unsigned w, unsigned h, size_t pitch) {
  if (!data || !w || !h) return; // 같은 프레임 반복
  ensure_pix(w, h);
  for (unsigned y = 0; y < h; y++) {
    uint8_t *d = g_pix + (size_t)y * w * 4;
    const uint8_t *s = (const uint8_t *)data + y * pitch;
    if (g_fmt == RETRO_PIXEL_FORMAT_XRGB8888) {
      const uint32_t *p = (const uint32_t *)s;
      for (unsigned x = 0; x < w; x++) {
        uint32_t v = p[x]; d[0] = v >> 16; d[1] = v >> 8; d[2] = v; d[3] = 255; d += 4;
      }
    } else if (g_fmt == RETRO_PIXEL_FORMAT_RGB565) {
      const uint16_t *p = (const uint16_t *)s;
      for (unsigned x = 0; x < w; x++) {
        uint16_t v = p[x];
        uint8_t r = (v >> 11) & 31, g = (v >> 5) & 63, b = v & 31;
        d[0] = (r << 3) | (r >> 2); d[1] = (g << 2) | (g >> 4); d[2] = (b << 3) | (b >> 2); d[3] = 255; d += 4;
      }
    } else { // 0RGB1555
      const uint16_t *p = (const uint16_t *)s;
      for (unsigned x = 0; x < w; x++) {
        uint16_t v = p[x];
        uint8_t r = (v >> 10) & 31, g = (v >> 5) & 31, b = v & 31;
        d[0] = (r << 3) | (r >> 2); d[1] = (g << 3) | (g >> 2); d[2] = (b << 3) | (b >> 2); d[3] = 255; d += 4;
      }
    }
  }
  g_serial++;
}

static size_t audio_batch_cb(const int16_t *data, size_t frames) {
  mutex_lock(&g_mu);
  for (size_t i = 0; i < frames; i++) {
    if (g_wr - g_rd >= RING_FRAMES) g_rd++; // 넘치면 오래된 것부터 버림
    unsigned k = (g_wr++ % RING_FRAMES) * 2;
    g_ring[k] = data[i * 2]; g_ring[k + 1] = data[i * 2 + 1];
  }
  mutex_unlock(&g_mu);
  return frames;
}
static void audio_cb(int16_t l, int16_t r) { int16_t s[2] = {l, r}; audio_batch_cb(s, 1); }
static void poll_cb(void) {}
static int16_t input_cb(unsigned port, unsigned dev, unsigned idx, unsigned id) {
  if (port != 0 || dev != RETRO_DEVICE_JOYPAD) return 0;
  if (id == RETRO_DEVICE_ID_JOYPAD_MASK) return (int16_t)(g_input & 0xFFFF);
  return id < 16 ? (g_input >> id) & 1 : 0;
}

#define SYM(f, n) do { *(void **)&C.f = lib_sym(C.lib, n); if (!C.f) { set_err("심볼 없음: %s", n); return 0; } } while (0)

int rh_open(const char *core_path, const char *system_dir, const char *save_dir) {
  rh_close();
  g_err[0] = 0;
  snprintf(g_sys, sizeof g_sys, "%s", system_dir ? system_dir : ".");
  snprintf(g_save, sizeof g_save, "%s", save_dir ? save_dir : ".");
  C.lib = lib_open(core_path);
  if (!C.lib) { set_err("코어를 못 열었어요: %s", lib_err()); return 0; }
  SYM(set_environment, "retro_set_environment");
  SYM(set_video_refresh, "retro_set_video_refresh");
  SYM(set_audio_sample, "retro_set_audio_sample");
  SYM(set_audio_sample_batch, "retro_set_audio_sample_batch");
  SYM(set_input_poll, "retro_set_input_poll");
  SYM(set_input_state, "retro_set_input_state");
  SYM(init, "retro_init"); SYM(deinit, "retro_deinit");
  SYM(get_system_info, "retro_get_system_info");
  SYM(get_system_av_info, "retro_get_system_av_info");
  SYM(load_game, "retro_load_game"); SYM(unload_game, "retro_unload_game");
  SYM(run, "retro_run");
  SYM(serialize_size, "retro_serialize_size");
  SYM(serialize, "retro_serialize"); SYM(unserialize, "retro_unserialize");
  SYM(get_memory_data, "retro_get_memory_data"); SYM(get_memory_size, "retro_get_memory_size");
  SYM(set_controller_port_device, "retro_set_controller_port_device");
  struct retro_system_info si = {0};
  C.get_system_info(&si);
  snprintf(g_name, sizeof g_name, "%s %s", si.library_name ? si.library_name : "?", si.library_version ? si.library_version : "");
  C.set_environment(env_cb);
  C.init();
  C.set_video_refresh(video_cb);
  C.set_audio_sample(audio_cb);
  C.set_audio_sample_batch(audio_batch_cb);
  C.set_input_poll(poll_cb);
  C.set_input_state(input_cb);
  return 1;
}

int rh_load(const char *rom_path) {
  if (!C.lib) { set_err("코어가 안 열려 있어요"); return 0; }
  struct retro_system_info si = {0};
  C.get_system_info(&si);
  struct retro_game_info gi = {0};
  gi.path = rom_path;
  if (!si.need_fullpath) {
    FILE *f = u8fopen(rom_path, "rb");
    if (!f) { set_err("롬 파일을 못 읽었어요: %s", rom_path); return 0; }
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    g_rom = malloc(n);
    if (fread(g_rom, 1, n, f) != (size_t)n) { fclose(f); set_err("롬 읽기 실패"); return 0; }
    fclose(f);
    gi.data = g_rom; gi.size = n;
  }
  if (!C.load_game(&gi)) { set_err("코어가 이 롬을 거부했어요"); return 0; }
  C.get_system_av_info(&g_av);
  C.set_controller_port_device(0, RETRO_DEVICE_JOYPAD);
  g_loaded = 1; g_serial = 0; g_rd = g_wr = 0; g_w = g_h = 0;
  LOG("loaded: %s %.2ffps %.0fHz %ux%u", g_name, g_av.timing.fps, g_av.timing.sample_rate,
      g_av.geometry.base_width, g_av.geometry.base_height);
  return 1;
}

void rh_close(void) {
  if (C.lib) {
    if (g_loaded) C.unload_game();
    C.deinit();
    lib_close(C.lib);
  }
  memset(&C, 0, sizeof C);
  g_loaded = 0; free(g_rom); g_rom = NULL; g_input = 0;
}

void rh_run(void) { if (g_loaded) C.run(); }

int rh_video(const uint8_t **p, int *w, int *h) {
  if (!g_w) return 0;
  *p = g_pix; *w = g_w; *h = g_h; return 1;
}
uint64_t rh_video_serial(void) { return g_serial; }
double rh_fps(void) { return g_av.timing.fps > 1 ? g_av.timing.fps : 60.0; }
double rh_sample_rate(void) { return g_av.timing.sample_rate > 1 ? g_av.timing.sample_rate : 44100.0; }
double rh_aspect(void) {
  double a = g_av.geometry.aspect_ratio;
  if (a > 0.1) return a;
  return g_h ? (double)g_w / g_h : 4.0 / 3.0;
}

int rh_audio_read(int16_t *out, int frames) {
  mutex_lock(&g_mu);
  int n = 0;
  while (n < frames && g_rd != g_wr) {
    unsigned k = (g_rd++ % RING_FRAMES) * 2;
    out[n * 2] = g_ring[k]; out[n * 2 + 1] = g_ring[k + 1]; n++;
  }
  mutex_unlock(&g_mu);
  if (n < frames) memset(out + n * 2, 0, (size_t)(frames - n) * 4);
  return n;
}
int rh_audio_buffered(void) { return (int)(g_wr - g_rd); }

void rh_set_input(uint32_t m) { g_input = m; }

size_t rh_state_size(void) { return g_loaded ? C.serialize_size() : 0; }
// 임시 파일에 다 쓰고 나서 바꿔치기한다. 쓰다 죽어도 예전 저장은 남는다.
static int write_file(const char *p, const void *d, size_t n) {
  char tmp[2048]; snprintf(tmp, sizeof tmp, "%s.tmp", p);
  FILE *f = u8fopen(tmp, "wb"); if (!f) return 0;
  size_t w = fwrite(d, 1, n, f);
  if (fclose(f) != 0 || w != n) { u8remove(tmp); return 0; }
  return u8replace(tmp, p) == 0;
}
static void *read_file(const char *p, size_t *n) {
  FILE *f = u8fopen(p, "rb"); if (!f) return NULL;
  fseek(f, 0, SEEK_END); *n = ftell(f); fseek(f, 0, SEEK_SET);
  void *b = malloc(*n ? *n : 1);
  if (fread(b, 1, *n, f) != *n) { free(b); b = NULL; }
  fclose(f); return b;
}
int rh_state_save(const char *path) {
  size_t n = rh_state_size(); if (!n) return 0;
  void *b = malloc(n); int ok = C.serialize(b, n) && write_file(path, b, n);
  free(b); return ok;
}
int rh_state_load(const char *path) {
  if (!g_loaded) return 0;
  size_t n; void *b = read_file(path, &n); if (!b) return 0;
  int ok = C.unserialize(b, n); free(b); return ok;
}
int rh_sram_save(const char *path) {
  if (!g_loaded) return 0;
  size_t n = C.get_memory_size(RETRO_MEMORY_SAVE_RAM); void *d = C.get_memory_data(RETRO_MEMORY_SAVE_RAM);
  if (!n || !d) return 0;
  return write_file(path, d, n);
}
int rh_sram_load(const char *path) {
  if (!g_loaded) return 0;
  size_t n = C.get_memory_size(RETRO_MEMORY_SAVE_RAM); void *d = C.get_memory_data(RETRO_MEMORY_SAVE_RAM);
  size_t fn; void *b = read_file(path, &fn);
  if (!b || !n || !d) { free(b); return 0; }
  memcpy(d, b, fn < n ? fn : n); free(b); return 1;
}
