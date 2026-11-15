// 빠른 점검용: test <코어> <롬> [프레임수] -> 마지막 프레임을 out.ppm 으로 저장
#include "rhost.h"
#include <stdio.h>
#include <stdlib.h>
int main(int c, char **v) {
  if (c < 3) return 2;
  if (!rh_open(v[1], ".", ".") || !rh_load(v[2])) { fprintf(stderr, "fail: %s\n", rh_error()); return 1; }
  int n = c > 3 ? atoi(v[3]) : 120;
  for (int i = 0; i < n; i++) rh_run();
  const uint8_t *p; int w, h;
  if (!rh_video(&p, &w, &h)) { fprintf(stderr, "no video\n"); return 1; }
  FILE *f = fopen("out.ppm", "wb"); fprintf(f, "P6 %d %d 255\n", w, h);
  for (int i = 0; i < w * h; i++) fwrite(p + i * 4, 1, 3, f);
  fclose(f);
  printf("%s %dx%d fps=%.2f audio=%d\n", rh_core_name(), w, h, rh_fps(), rh_audio_buffered());
  rh_close(); return 0;
}
