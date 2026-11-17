// 테스트용 PSP 프로그램: 화면 메모리(VRAM, 512 간격 32비트)에 무늬를 그리고, 그 주소를 화면으로 정한 뒤 멈춘다.
// 시스템 함수는 sceDisplay 두 개만 쓴다. 그 가져오기 표(stub)는 mk_psp.py 가 ELF 뒤에 붙이고,
// 함수 주소는 STUB0 + 8*n 이다(빌드 때 -DSTUB0=... 로 넣음).
typedef unsigned int u32;
typedef int (*SetFrameBuf)(void *, int, int, int);
typedef int (*WaitVblank)(void);
__attribute__((noreturn, section(".text.start"))) void start(void) {
  volatile u32 *fb = (volatile u32 *)0x44000000;   // VRAM(캐시 안 거침)
  for (u32 y = 0; y < 272; y++)
    for (u32 x = 0; x < 480; x++) {
      u32 r = x * 255 / 479, g = y * 255 / 271, b = ((x ^ y) & 32) ? 220 : 40;
      fb[y * 512 + x] = 0xFF000000 | (b << 16) | (g << 8) | r;   // ABGR
    }
  ((SetFrameBuf)(STUB0))((void *)0x04000000, 512, 3, 1);   // 8888, 바로
  for (;;) ((WaitVblank)(STUB0 + 8))();
}
