#include <jni.h>
#include <android/bitmap.h>
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
