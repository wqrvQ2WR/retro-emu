#include <jni.h>
#include <android/bitmap.h>
#include <android/native_window_jni.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include "rhost.h"

#define F(ret, name) JNIEXPORT ret JNICALL Java_com_hayoonjae_retroemu_Native_##name

F(jboolean, open)(JNIEnv *e, jobject o, jstring core, jstring sys, jstring save) {
  const char *c = (*e)->GetStringUTFChars(e, core, 0), *s = (*e)->GetStringUTFChars(e, sys, 0), *v = (*e)->GetStringUTFChars(e, save, 0);
  int ok = rh_open(c, s, v);
  (*e)->ReleaseStringUTFChars(e, core, c); (*e)->ReleaseStringUTFChars(e, sys, s); (*e)->ReleaseStringUTFChars(e, save, v);
  return ok;
}
F(jboolean, load)(JNIEnv *e, jobject o, jstring rom) {
  const char *r = (*e)->GetStringUTFChars(e, rom, 0); int ok = rh_load(r); (*e)->ReleaseStringUTFChars(e, rom, r); return ok;
}
F(void, close)(JNIEnv *e, jobject o) { rh_close(); }
F(jstring, error)(JNIEnv *e, jobject o) { return (*e)->NewStringUTF(e, rh_error()); }
F(void, run)(JNIEnv *e, jobject o) { rh_run(); }
F(void, setInput)(JNIEnv *e, jobject o, jint m) { rh_set_input((uint32_t)m); }
F(jdouble, fps)(JNIEnv *e, jobject o) { return rh_fps(); }
F(jdouble, sampleRate)(JNIEnv *e, jobject o) { return rh_sample_rate(); }
F(jdouble, aspect)(JNIEnv *e, jobject o) { return rh_aspect(); }
F(jlong, serial)(JNIEnv *e, jobject o) { return (jlong)rh_video_serial(); }
// [w,h] 또는 null
F(jintArray, size)(JNIEnv *e, jobject o) {
  const uint8_t *p; int w, h;
  if (!rh_video(&p, &w, &h)) return NULL;
  jintArray a = (*e)->NewIntArray(e, 2); jint v[2] = {w, h}; (*e)->SetIntArrayRegion(e, a, 0, 2, v); return a;
}
// 비트맵 크기가 영상과 같을 때만 복사
F(jboolean, copyFrame)(JNIEnv *e, jobject o, jobject bmp) {
  const uint8_t *p; int w, h;
  if (!rh_video(&p, &w, &h)) return 0;
  AndroidBitmapInfo info; void *px;
  if (AndroidBitmap_getInfo(e, bmp, &info) < 0 || (int)info.width != w || (int)info.height != h) return 0;
  if (AndroidBitmap_lockPixels(e, bmp, &px) < 0) return 0;
  for (int y = 0; y < h; y++) memcpy((uint8_t *)px + (size_t)y * info.stride, p + (size_t)y * w * 4, (size_t)w * 4);
  AndroidBitmap_unlockPixels(e, bmp);
  return 1;
}
F(jint, audioRead)(JNIEnv *e, jobject o, jshortArray buf, jint frames) {
  jshort *b = (*e)->GetShortArrayElements(e, buf, 0);
  int n = rh_audio_read((int16_t *)b, frames);
  (*e)->ReleaseShortArrayElements(e, buf, b, 0);
  return n;
}
F(jint, audioBuffered)(JNIEnv *e, jobject o) { return rh_audio_buffered(); }
#define PATHFN(name, fn) F(jboolean, name)(JNIEnv *e, jobject o, jstring p) { \
  const char *c = (*e)->GetStringUTFChars(e, p, 0); int ok = fn(c); (*e)->ReleaseStringUTFChars(e, p, c); return ok; }
PATHFN(stateSave, rh_state_save)
PATHFN(stateLoad, rh_state_load)
PATHFN(sramSave, rh_sram_save)
PATHFN(sramLoad, rh_sram_load)

// ---- 화면 출력: SurfaceView 버퍼에 바로 쓴다(컴포즈를 거치지 않음) ----
// 픽셀은 정수배로 키워서 넣고, 남은 비율은 하드웨어 합성기가 맞춘다(선명함 유지).
static pthread_mutex_t g_wmu = PTHREAD_MUTEX_INITIALIZER;
static ANativeWindow *g_win;
static int g_gw, g_gh;  // 지금 버퍼 크기

F(void, setSurface)(JNIEnv *e, jobject o, jobject surf) {
  pthread_mutex_lock(&g_wmu);
  if (g_win) { ANativeWindow_release(g_win); g_win = NULL; }
  if (surf) g_win = ANativeWindow_fromSurface(e, surf);
  g_gw = g_gh = 0;
  pthread_mutex_unlock(&g_wmu);
}

// maxW/maxH: 화면 뷰의 실제 픽셀 크기
// rot: 시계 방향 90도 단위(0~3). 러브플러스처럼 옆으로 들고 하는 게임용.
static uint32_t *g_rot; static size_t g_rot_n;
F(jboolean, present)(JNIEnv *e, jobject o, jint maxW, jint maxH, jint rot) {
  const uint8_t *p; int w, h;
  if (!rh_video(&p, &w, &h) || w <= 0 || h <= 0) return 0;
  rot &= 3;
  if (rot) {
    size_t n = (size_t)w * h;
    if (n > g_rot_n) { free(g_rot); g_rot = malloc(n * 4); g_rot_n = g_rot ? n : 0; if (!g_rot) return 0; }
    const uint32_t *s = (const uint32_t *)p;
    int nw = (rot & 1) ? h : w, nh = (rot & 1) ? w : h;
    for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
      int dx, dy;
      if (rot == 1) { dx = h - 1 - y; dy = x; } else if (rot == 2) { dx = w - 1 - x; dy = h - 1 - y; } else { dx = y; dy = w - 1 - x; }
      g_rot[(size_t)dy * nw + dx] = s[(size_t)y * w + x];
    }
    p = (const uint8_t *)g_rot; w = nw; h = nh;
  }
  pthread_mutex_lock(&g_wmu);
  if (!g_win) { pthread_mutex_unlock(&g_wmu); return 0; }
  int k = maxW / w; if (maxH / h < k) k = maxH / h;
  if (k < 1) k = 1; if (k > 6) k = 6;
  int bw = w * k, bh = h * k;
  if (bw != g_gw || bh != g_gh) {
    ANativeWindow_setBuffersGeometry(g_win, bw, bh, WINDOW_FORMAT_RGBX_8888);
    g_gw = bw; g_gh = bh;
  }
  ANativeWindow_Buffer b;
  if (ANativeWindow_lock(g_win, &b, NULL) < 0) { pthread_mutex_unlock(&g_wmu); return 0; }
  if (b.width < bw || b.height < bh) { ANativeWindow_unlockAndPost(g_win); pthread_mutex_unlock(&g_wmu); return 0; }
  const uint32_t *src = (const uint32_t *)p;
  uint32_t *dst = (uint32_t *)b.bits;
  for (int y = 0; y < h; y++) {
    uint32_t *row = dst + (size_t)y * k * b.stride;
    const uint32_t *s = src + (size_t)y * w;
    if (k == 1) memcpy(row, s, (size_t)w * 4);
    else for (int x = 0; x < w; x++) { uint32_t v = s[x]; uint32_t *d = row + x * k; for (int i = 0; i < k; i++) d[i] = v; }
    for (int r = 1; r < k; r++) memcpy(row + (size_t)r * b.stride, row, (size_t)bw * 4);
  }
  ANativeWindow_unlockAndPost(g_win);
  pthread_mutex_unlock(&g_wmu);
  return 1;
}

F(void, setAnalog)(JNIEnv *e, jclass c, jint stick, jfloat x, jfloat y) { (void)e; (void)c; rh_set_analog(stick, x, y); }
F(void, setPointer)(JNIEnv *e, jclass c, jfloat x, jfloat y, jboolean down) { (void)e; (void)c; rh_set_pointer(x, y, down); }
