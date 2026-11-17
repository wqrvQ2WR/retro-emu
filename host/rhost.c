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

// ---------------------------------------------------------------- OpenGL (PSP 같은 코어)
// GL 함수는 링크하지 않고 이름으로 찾는다(get_proc). 그래서 GL 을 안 쓰는 앱 빌드는 바뀌는 게 없다.
#define GL_FRAMEBUFFER 0x8D40
#define GL_RENDERBUFFER 0x8D41
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_DEPTH_STENCIL_ATTACHMENT 0x821A
#define GL_STENCIL_ATTACHMENT 0x8D20
#define GL_DEPTH_ATTACHMENT 0x8D00
#define GL_DEPTH24_STENCIL8 0x88F0
#define GL_DEPTH_COMPONENT16 0x81A5
#define GL_TEXTURE_2D 0x0DE1
#define GL_RGBA 0x1908
#define GL_RGBA8 0x8058
#define GL_UNSIGNED_BYTE 0x1401
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_NEAREST 0x2600
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#define GL_PACK_ALIGNMENT 0x0D05
typedef unsigned int GLuint;
static struct {
  void (*GenFramebuffers)(int, GLuint *); void (*BindFramebuffer)(unsigned, GLuint); void (*DeleteFramebuffers)(int, const GLuint *);
  void (*GenTextures)(int, GLuint *); void (*BindTexture)(unsigned, GLuint); void (*DeleteTextures)(int, const GLuint *);
  void (*TexImage2D)(unsigned, int, int, int, int, int, unsigned, unsigned, const void *); void (*TexParameteri)(unsigned, unsigned, int);
  void (*FramebufferTexture2D)(unsigned, unsigned, unsigned, GLuint, int);
  void (*GenRenderbuffers)(int, GLuint *); void (*BindRenderbuffer)(unsigned, GLuint); void (*DeleteRenderbuffers)(int, const GLuint *);
  void (*RenderbufferStorage)(unsigned, unsigned, int, int); void (*FramebufferRenderbuffer)(unsigned, unsigned, unsigned, GLuint);
  unsigned (*CheckFramebufferStatus)(unsigned); void (*ReadPixels)(int, int, int, int, unsigned, unsigned, void *);
  void (*PixelStorei)(unsigned, int);
} GL;

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif
#if defined(__APPLE__) && TARGET_OS_OSX
// 맥: 화면 없는 CGL 컨텍스트. OpenGL 프레임워크도 dlopen 으로 연다(앱 링크 설정을 안 바꾸려고).
#include <dlfcn.h>
typedef void *CGLCtx; typedef void *CGLPix;
static void *cgl_lib; static CGLCtx cgl_ctx;
static int (*cglChoosePixelFormat)(const int *, CGLPix *, int *);
static int (*cglCreateContext)(CGLPix, CGLCtx, CGLCtx *);
static int (*cglDestroyPixelFormat)(CGLPix);
static int (*cglSetCurrentContext)(CGLCtx);
static int (*cglDestroyContext)(CGLCtx);
static int cgl_create(int gles, int major, int minor) {
  (void)minor;
  if (gles) return 0;
  if (!cgl_lib) cgl_lib = dlopen("/System/Library/Frameworks/OpenGL.framework/OpenGL", RTLD_NOW | RTLD_GLOBAL);
  if (!cgl_lib) return 0;
  *(void **)&cglChoosePixelFormat = dlsym(cgl_lib, "CGLChoosePixelFormat");
  *(void **)&cglCreateContext = dlsym(cgl_lib, "CGLCreateContext");
  *(void **)&cglDestroyPixelFormat = dlsym(cgl_lib, "CGLDestroyPixelFormat");
  *(void **)&cglSetCurrentContext = dlsym(cgl_lib, "CGLSetCurrentContext");
  *(void **)&cglDestroyContext = dlsym(cgl_lib, "CGLDestroyContext");
  if (!cglChoosePixelFormat || !cglCreateContext) return 0;
  // kCGLPFAAccelerated=73, kCGLPFAOpenGLProfile=99, kCGLOGLPVersion_Legacy=0x1000, 3_2_Core=0x3200, ColorSize=8, DepthSize=12, StencilSize=13
  int attrs[] = {73, 99, major >= 3 ? 0x3200 : 0x1000, 8, 24, 12, 24, 13, 8, 0};
  CGLPix pf = NULL; int n = 0;
  if (cglChoosePixelFormat(attrs, &pf, &n) || !pf) return 0;
  int e = cglCreateContext(pf, NULL, &cgl_ctx); cglDestroyPixelFormat(pf);
  return e == 0 && cgl_ctx;
}
static void cgl_current(int on) { if (cglSetCurrentContext) cglSetCurrentContext(on ? cgl_ctx : NULL); }
static void *cgl_proc(const char *n) { return cgl_lib ? dlsym(cgl_lib, n) : NULL; }
static void cgl_destroy(void) { if (cgl_ctx) { cglSetCurrentContext(NULL); cglDestroyContext(cgl_ctx); cgl_ctx = NULL; } }
static const rh_gl_provider builtin_gl = {cgl_create, cgl_current, cgl_proc, cgl_destroy};
#define HAVE_BUILTIN_GL 1
#elif defined(__ANDROID__)
// 안드로이드: 화면 없는 EGL pbuffer + GLES3. 코어를 부르는 스레드가 바뀌어서 매번 붙였다 뗀다.
#include <EGL/egl.h>
#include <dlfcn.h>
static EGLDisplay egl_dpy = EGL_NO_DISPLAY; static EGLContext egl_ctx = EGL_NO_CONTEXT; static EGLSurface egl_surf = EGL_NO_SURFACE;
static void *gles_lib;
static int egl_create(int gles, int major, int minor) {
  (void)minor;
  if (!gles) return 0;
  egl_dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
  if (egl_dpy == EGL_NO_DISPLAY || !eglInitialize(egl_dpy, NULL, NULL)) return 0;
  int es3 = major >= 3;
  EGLint cfg_attr[] = {EGL_RENDERABLE_TYPE, es3 ? 0x40 /* EGL_OPENGL_ES3_BIT */ : EGL_OPENGL_ES2_BIT, EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
                       EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_DEPTH_SIZE, 24, EGL_STENCIL_SIZE, 8, EGL_NONE};
  EGLConfig cfg; EGLint n = 0;
  if (!eglChooseConfig(egl_dpy, cfg_attr, &cfg, 1, &n) || n < 1) return 0;
  EGLint ctx_attr[] = {EGL_CONTEXT_CLIENT_VERSION, es3 ? 3 : 2, EGL_NONE};
  egl_ctx = eglCreateContext(egl_dpy, cfg, EGL_NO_CONTEXT, ctx_attr);
  EGLint pb[] = {EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE};
  egl_surf = eglCreatePbufferSurface(egl_dpy, cfg, pb);
  if (!gles_lib) gles_lib = dlopen(es3 ? "libGLESv3.so" : "libGLESv2.so", RTLD_NOW);
  return egl_ctx != EGL_NO_CONTEXT && egl_surf != EGL_NO_SURFACE;
}
static void egl_current(int on) {
  if (egl_dpy == EGL_NO_DISPLAY) return;
  if (on) eglMakeCurrent(egl_dpy, egl_surf, egl_surf, egl_ctx);
  else eglMakeCurrent(egl_dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
}
static void *egl_proc(const char *n) {
  void *f = gles_lib ? dlsym(gles_lib, n) : NULL;
  return f ? f : (void *)eglGetProcAddress(n);
}
static void egl_destroy(void) {
  if (egl_dpy == EGL_NO_DISPLAY) return;
  eglMakeCurrent(egl_dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
  if (egl_surf != EGL_NO_SURFACE) eglDestroySurface(egl_dpy, egl_surf);
  if (egl_ctx != EGL_NO_CONTEXT) eglDestroyContext(egl_dpy, egl_ctx);
  egl_surf = EGL_NO_SURFACE; egl_ctx = EGL_NO_CONTEXT;
}
static const rh_gl_provider builtin_gl = {egl_create, egl_current, egl_proc, egl_destroy};
#define HAVE_BUILTIN_GL 1
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
static int16_t g_ptr_x, g_ptr_y; static int g_ptr_down;   // 터치(-0x7fff~0x7fff)
static int16_t g_axis[2][2];   // 아날로그 [왼/오][x/y]

// OpenGL 상태
static rh_gl_provider g_glp;
static int g_glp_set;
static struct retro_hw_render_callback g_hw;
static int g_hw_wanted, g_hw_ready;
static GLuint g_fbo, g_fbo_tex, g_fbo_rb;
static int g_fbo_w, g_fbo_h;
static uint8_t *g_readback; static size_t g_readback_cap;
static void gl_on(void) { if (g_hw_ready && g_glp.make_current) g_glp.make_current(1); }
static void gl_off(void) { if (g_hw_ready && g_glp.make_current) g_glp.make_current(0); }
static uintptr_t hw_get_fbo(void) { return g_fbo; }
static retro_proc_address_t hw_get_proc(const char *n) { return (retro_proc_address_t)(g_glp.get_proc ? g_glp.get_proc(n) : NULL); }

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

// 코어가 알려 준 옵션의 기본값(SET_VARIABLES 의 "설명; 기본|다른것" 에서 첫 값). 우리가 안 정한 옵션은 이걸 준다.
// 값이 없다고 답하면 기본값 대신 꺼짐으로 보는 코어가 있다(melonDS 의 바로 부팅 등).
#define MAXVARS 256
static struct { char key[64]; char val[64]; } g_vars[MAXVARS];
static int g_nvars;
static void remember_vars(const struct retro_variable *v) {
  for (; v && v->key && g_nvars < MAXVARS; v++) {
    const char *semi = v->value ? strstr(v->value, "; ") : NULL;
    if (!semi) continue;
    const char *d = semi + 2, *bar = strchr(d, '|');
    size_t n = bar ? (size_t)(bar - d) : strlen(d);
    if (n >= sizeof g_vars[0].val) n = sizeof g_vars[0].val - 1;
    snprintf(g_vars[g_nvars].key, sizeof g_vars[0].key, "%s", v->key);
    memcpy(g_vars[g_nvars].val, d, n); g_vars[g_nvars].val[n] = 0;
    g_nvars++;
  }
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
  case RETRO_ENVIRONMENT_SET_VARIABLES: remember_vars(data); return true;
  case RETRO_ENVIRONMENT_GET_VARIABLE: { // 몇 개만 바꾸고 나머지는 코어 기본값
    struct retro_variable *v = data;
    static const char *opts[][2] = {
      {"mgba_sgb_borders", "OFF"},  // 슈퍼 게임보이 테두리를 빼서 게임보이 화면을 크게(색은 그대로)
      {"melonds_touch_mode", "Touch"},  // DS 아래 화면: 마우스(상대 이동) 말고 터치 좌표로
      {"melonds_boot_directly", "enabled"},  // BIOS 없이 바로 게임
    };
    for (size_t i = 0; v && v->key && i < sizeof opts / sizeof *opts; i++)
      if (!strcmp(v->key, opts[i][0])) { v->value = opts[i][1]; return true; }
    for (int i = 0; v && v->key && i < g_nvars; i++)
      if (!strcmp(v->key, g_vars[i].key)) { v->value = g_vars[i].val; return true; }
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
  case RETRO_ENVIRONMENT_GET_PREFERRED_HW_RENDER: {
    if (!g_glp_set) return false;
#if defined(__ANDROID__) || (defined(__APPLE__) && !TARGET_OS_OSX)
    *(unsigned *)data = RETRO_HW_CONTEXT_OPENGLES3;
#else
    *(unsigned *)data = RETRO_HW_CONTEXT_OPENGL_CORE;
#endif
    return true;
  }
  case RETRO_ENVIRONMENT_SET_HW_RENDER: {
    struct retro_hw_render_callback *cb = data;
    if (!g_glp_set || !cb) return false;
    int t = cb->context_type;
#if defined(__ANDROID__) || (defined(__APPLE__) && !TARGET_OS_OSX)
    int ok = t == RETRO_HW_CONTEXT_OPENGLES2 || t == RETRO_HW_CONTEXT_OPENGLES3 || t == RETRO_HW_CONTEXT_OPENGLES_VERSION;
#else
    int ok = t == RETRO_HW_CONTEXT_OPENGL || t == RETRO_HW_CONTEXT_OPENGL_CORE;
#endif
    LOG("hw render 요청: type=%d %u.%u -> %s", t, cb->version_major, cb->version_minor, ok ? "받음" : "거절");
    if (!ok) return false;
    cb->get_current_framebuffer = hw_get_fbo;
    cb->get_proc_address = hw_get_proc;
    g_hw = *cb; g_hw_wanted = 1;
    return true;
  }
  case RETRO_ENVIRONMENT_SET_HW_SHARED_CONTEXT: return g_glp_set;
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
  if (data == RETRO_HW_FRAME_BUFFER_VALID) {   // GL 로 그린 프레임: FBO 에서 읽어서 같은 RGBA 로
    if (!g_hw_ready || !g_fbo) return;
    if ((int)w > g_fbo_w) w = g_fbo_w; if ((int)h > g_fbo_h) h = g_fbo_h;
    size_t need = (size_t)w * h * 4;
    if (need > g_readback_cap) { free(g_readback); g_readback = malloc(need); g_readback_cap = need; }
    GL.BindFramebuffer(GL_FRAMEBUFFER, g_fbo);
    GL.PixelStorei(GL_PACK_ALIGNMENT, 1);
    GL.ReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, g_readback);
    ensure_pix(w, h);
    for (unsigned y = 0; y < h; y++) {   // GL 은 아래가 0번 줄
      unsigned sy = g_hw.bottom_left_origin ? h - 1 - y : y;
      uint8_t *d = g_pix + (size_t)y * w * 4; const uint8_t *src = g_readback + (size_t)sy * w * 4;
      memcpy(d, src, (size_t)w * 4);
      for (unsigned x = 0; x < w; x++) d[x * 4 + 3] = 255;
    }
    g_serial++;
    return;
  }
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
  if (port == 0 && dev == RETRO_DEVICE_POINTER && idx == 0) {
    if (id == RETRO_DEVICE_ID_POINTER_X) return g_ptr_x;
    if (id == RETRO_DEVICE_ID_POINTER_Y) return g_ptr_y;
    if (id == RETRO_DEVICE_ID_POINTER_PRESSED) return (int16_t)g_ptr_down;
    if (id == RETRO_DEVICE_ID_POINTER_COUNT) return (int16_t)g_ptr_down;
    return 0;
  }
  if (port == 0 && dev == RETRO_DEVICE_ANALOG) {
    if (idx == RETRO_DEVICE_INDEX_ANALOG_BUTTON)   // 아날로그 트리거(게임큐브 등): 눌림 = 끝까지
      return id < 16 && ((g_input >> id) & 1) ? 0x7fff : 0;
    if (idx < 2 && id < 2) return g_axis[idx][id];
    return 0;
  }
  if (port != 0 || dev != RETRO_DEVICE_JOYPAD) return 0;
  if (id == RETRO_DEVICE_ID_JOYPAD_MASK) return (int16_t)(g_input & 0xFFFF);
  return id < 16 ? (g_input >> id) & 1 : 0;
}

static int load_gl(void) {
#define GLF(f) if (!(*(void **)&GL.f = g_glp.get_proc("gl" #f))) { set_err("GL 함수 없음: gl" #f); return 0; }
  GLF(GenFramebuffers) GLF(BindFramebuffer) GLF(DeleteFramebuffers) GLF(GenTextures) GLF(BindTexture) GLF(DeleteTextures)
  GLF(TexImage2D) GLF(TexParameteri) GLF(FramebufferTexture2D) GLF(GenRenderbuffers) GLF(BindRenderbuffer) GLF(DeleteRenderbuffers)
  GLF(RenderbufferStorage) GLF(FramebufferRenderbuffer) GLF(CheckFramebufferStatus) GLF(ReadPixels) GLF(PixelStorei)
#undef GLF
  return 1;
}

// 로드가 끝난 뒤 컨텍스트를 만들고, 코어가 그릴 FBO 를 붙이고, context_reset 을 부른다
static int hw_setup(void) {
  int t = g_hw.context_type, gles = t == RETRO_HW_CONTEXT_OPENGLES2 || t == RETRO_HW_CONTEXT_OPENGLES3 || t == RETRO_HW_CONTEXT_OPENGLES_VERSION;
  int major = g_hw.version_major, minor = g_hw.version_minor;
  if (t == RETRO_HW_CONTEXT_OPENGLES3 && major < 3) major = 3;
  if (t == RETRO_HW_CONTEXT_OPENGL_CORE && major < 3) { major = 3; minor = 2; }
  if (!g_glp.create(gles, major, minor)) { set_err("OpenGL 을 못 만들었어요"); return 0; }
  g_glp.make_current(1);
  if (!load_gl()) { g_glp.make_current(0); return 0; }
  g_fbo_w = g_av.geometry.max_width ? (int)g_av.geometry.max_width : (int)g_av.geometry.base_width;
  g_fbo_h = g_av.geometry.max_height ? (int)g_av.geometry.max_height : (int)g_av.geometry.base_height;
  if (g_fbo_w < 1) g_fbo_w = 640; if (g_fbo_h < 1) g_fbo_h = 480;
  // 코어가 알려 주는 최대 크기는 해상도 배율을 다 올렸을 때라 너무 크다(Play! 5120x3584). 기본 배율로만 그리니 줄인다.
  int cap = gles ? 2048 : 4096;
  if (g_fbo_w > cap) g_fbo_w = cap; if (g_fbo_h > cap) g_fbo_h = cap;
  GL.GenTextures(1, &g_fbo_tex); GL.BindTexture(GL_TEXTURE_2D, g_fbo_tex);
  GL.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); GL.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  // GLES2 는 크기 붙은 형식(RGBA8)을 못 받는다. RGBA 는 GLES2·3 둘 다 된다.
  GL.TexImage2D(GL_TEXTURE_2D, 0, gles ? GL_RGBA : GL_RGBA8, g_fbo_w, g_fbo_h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
  GL.GenFramebuffers(1, &g_fbo); GL.BindFramebuffer(GL_FRAMEBUFFER, g_fbo);
  GL.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g_fbo_tex, 0);
  if (g_hw.depth) {
    GL.GenRenderbuffers(1, &g_fbo_rb); GL.BindRenderbuffer(GL_RENDERBUFFER, g_fbo_rb);
    GL.RenderbufferStorage(GL_RENDERBUFFER, g_hw.stencil ? GL_DEPTH24_STENCIL8 : GL_DEPTH_COMPONENT16, g_fbo_w, g_fbo_h);
    if (g_hw.stencil && gles) {   // GLES2 엔 DEPTH_STENCIL_ATTACHMENT 가 없어서 깊이·스텐실에 따로 붙인다(GLES3 에서도 같은 뜻)
      GL.FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, g_fbo_rb);
      GL.FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_RENDERBUFFER, g_fbo_rb);
    } else
      GL.FramebufferRenderbuffer(GL_FRAMEBUFFER, g_hw.stencil ? GL_DEPTH_STENCIL_ATTACHMENT : GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, g_fbo_rb);
  }
  unsigned st = GL.CheckFramebufferStatus(GL_FRAMEBUFFER);
  if (st != GL_FRAMEBUFFER_COMPLETE) { set_err("FBO 를 못 만들었어요(%x)", st); g_glp.make_current(0); return 0; }
  g_hw_ready = 1;
  LOG("OpenGL 준비: %s %d.%d, FBO %dx%d", gles ? "GLES" : "GL", major, minor, g_fbo_w, g_fbo_h);
  if (g_hw.context_reset) g_hw.context_reset();
  g_glp.make_current(0);
  return 1;
}

static void hw_teardown(void) {
  if (!g_hw_ready) return;
  g_glp.make_current(1);
  if (g_hw.context_destroy) g_hw.context_destroy();
  if (g_fbo) GL.DeleteFramebuffers(1, &g_fbo);
  if (g_fbo_tex) GL.DeleteTextures(1, &g_fbo_tex);
  if (g_fbo_rb) GL.DeleteRenderbuffers(1, &g_fbo_rb);
  g_fbo = g_fbo_tex = g_fbo_rb = 0;
  g_glp.make_current(0);
  if (g_glp.destroy) g_glp.destroy();
  g_hw_ready = 0;
}

void rh_set_gl_provider(const rh_gl_provider *p) {
  if (p) { g_glp = *p; g_glp_set = 1; } else { memset(&g_glp, 0, sizeof g_glp); g_glp_set = 0; }
}
int rh_uses_gl(void) { return g_hw_ready; }
void rh_set_analog(int stick, double x, double y) {
  if (stick < 0 || stick > 1) return;
  if (x < -1) x = -1; if (x > 1) x = 1; if (y < -1) y = -1; if (y > 1) y = 1;
  g_axis[stick][0] = (int16_t)(x * 32767); g_axis[stick][1] = (int16_t)(y * 32767);
}
void rh_set_pointer(double x, double y, int pressed) {
  if (x < 0) x = 0; if (x > 1) x = 1; if (y < 0) y = 0; if (y > 1) y = 1;
  g_ptr_x = (int16_t)(x * 65534 - 32767); g_ptr_y = (int16_t)(y * 65534 - 32767); g_ptr_down = pressed != 0;
}

#define SYM(f, n) do { *(void **)&C.f = lib_sym(C.lib, n); if (!C.f) { set_err("심볼 없음: %s", n); return 0; } } while (0)

int rh_open(const char *core_path, const char *system_dir, const char *save_dir) {
  rh_close();
  g_err[0] = 0;
  snprintf(g_sys, sizeof g_sys, "%s", system_dir ? system_dir : ".");
  snprintf(g_save, sizeof g_save, "%s", save_dir ? save_dir : ".");
#ifdef HAVE_BUILTIN_GL
  if (!g_glp_set) rh_set_gl_provider(&builtin_gl);
#endif
  g_hw_wanted = 0; memset(&g_hw, 0, sizeof g_hw); g_nvars = 0;
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
  if (g_hw_wanted && !hw_setup()) { C.unload_game(); return 0; }
  C.set_controller_port_device(0, RETRO_DEVICE_JOYPAD);
  g_loaded = 1; g_serial = 0; g_rd = g_wr = 0; g_w = g_h = 0;
  LOG("loaded: %s %.2ffps %.0fHz %ux%u", g_name, g_av.timing.fps, g_av.timing.sample_rate,
      g_av.geometry.base_width, g_av.geometry.base_height);
  return 1;
}

void rh_close(void) {
  if (C.lib) {
    // RetroArch 와 같은 순서: GL 정리(context_destroy) -> 게임 내리기 -> deinit -> 컨텍스트 없애기
    if (g_hw_ready) { gl_on(); if (g_hw.context_destroy) g_hw.context_destroy(); gl_off(); g_hw.context_destroy = NULL; }
    gl_on();
    if (g_loaded) C.unload_game();
    C.deinit();
    gl_off();
    hw_teardown();
    lib_close(C.lib);
  }
  g_hw_wanted = 0; g_ptr_down = 0; memset(g_axis, 0, sizeof g_axis);
  memset(&C, 0, sizeof C);
  g_loaded = 0; free(g_rom); g_rom = NULL; g_input = 0;
}

void rh_run(void) { if (g_loaded) { gl_on(); C.run(); gl_off(); } }

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
  void *b = malloc(n); gl_on(); int ok = C.serialize(b, n); gl_off(); ok = ok && write_file(path, b, n);
  free(b); return ok;
}
int rh_state_load(const char *path) {
  if (!g_loaded) return 0;
  size_t n; void *b = read_file(path, &n); if (!b) return 0;
  gl_on(); int ok = C.unserialize(b, n); gl_off(); free(b); return ok;
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
