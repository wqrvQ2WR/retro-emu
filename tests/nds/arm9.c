// 테스트용 DS 롬의 ARM9 코드: 위 화면(엔진 A, VRAM 직접 표시)에 무늬를 그리고, A 버튼을 누르면 색을 바꾼다.
// clang --target=armv5te-none-eabi 로 컴파일해서 .text 만 떼어 쓴다(mk_nds.py).
typedef unsigned short u16; typedef unsigned int u32;
#define REG(a) (*(volatile u32 *)(a))
#define REG16(a) (*(volatile u16 *)(a))
__attribute__((noreturn)) void start(void) {
  REG(0x04000304) = 0x0003;            // POWCNT1: 화면 켜기, 2D 엔진 A
  REG(0x04000240) = 0x80;              // VRAMCNT_A: LCDC 모드로 켜기
  REG(0x04000000) = 0x00020000;        // DISPCNT A: VRAM 표시 모드, VRAM A
  volatile u16 *vram = (volatile u16 *)0x06800000;
  u16 last = 0xFFFF;
  for (;;) {
    u16 keys = REG16(0x04000130);      // KEYINPUT(눌리면 0)
    u16 a = !(keys & 1);
    if (a != last) {
      last = a;
      for (u32 y = 0; y < 192; y++)
        for (u32 x = 0; x < 256; x++) {
          u16 r = x >> 3, g = y * 31 / 191, b = a ? 31 : ((x ^ y) & 16 ? 20 : 4);
          vram[y * 256 + x] = r | (g << 5) | (b << 10) | 0x8000;
        }
    }
  }
}
