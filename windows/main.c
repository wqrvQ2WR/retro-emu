// RetroEmu 윈도우판 (SDL2). 같은 코드가 맥에서도 돌아서 맥에서 화면을 확인할 수 있다.
// 게임 목록 -> 게임 화면 -> 저장 목록(슬롯 6 + 자동), 키보드·마우스·게임패드.
#include <SDL.h>
#include <SDL_ttf.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include "rhost.h"
#include "sync.h"
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO
#include "stb_image_write.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#define CORE_EXT ".dll"
#else
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#define CORE_EXT ".dylib"
#endif

// ---------------------------------------------------------------- 파일 (UTF-8 경로)

#ifdef _WIN32
static wchar_t *widen(const char *s) {
  int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
  wchar_t *w = malloc(sizeof(wchar_t) * (n > 0 ? n : 1)); w[0] = 0;
  if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s, -1, w, n);
  return w;
}
static char *narrow(const wchar_t *w) {
  int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);
  char *s = malloc(n > 0 ? n : 1); s[0] = 0;
  if (n > 0) WideCharToMultiByte(CP_UTF8, 0, w, -1, s, n, NULL, NULL);
  return s;
}
static void mkdir_u8(const char *p) { wchar_t *w = widen(p); CreateDirectoryW(w, NULL); free(w); }
static int remove_u8(const char *p) { wchar_t *w = widen(p); int ok = DeleteFileW(w); free(w); return ok ? 0 : -1; }
static int exists_u8(const char *p) { wchar_t *w = widen(p); DWORD a = GetFileAttributesW(w); free(w); return a != INVALID_FILE_ATTRIBUTES; }
static time_t mtime_u8(const char *p) {
  wchar_t *w = widen(p); WIN32_FILE_ATTRIBUTE_DATA d; BOOL ok = GetFileAttributesExW(w, GetFileExInfoStandard, &d); free(w);
  if (!ok) return 0;
  ULARGE_INTEGER u; u.LowPart = d.ftLastWriteTime.dwLowDateTime; u.HighPart = d.ftLastWriteTime.dwHighDateTime;
  return (time_t)((u.QuadPart - 116444736000000000ULL) / 10000000ULL);
}
static int rename_u8(const char *a, const char *b) {
  wchar_t *wa = widen(a), *wb = widen(b); BOOL ok = MoveFileExW(wa, wb, MOVEFILE_REPLACE_EXISTING); free(wa); free(wb); return ok ? 0 : -1;
}
// 폴더 안 파일 이름들(UTF-8). 끝은 NULL.
static char **list_dir(const char *dir, int *count) {
  char pat[2048]; snprintf(pat, sizeof pat, "%s/*", dir);
  wchar_t *w = widen(pat); WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileW(w, &fd); free(w);
  int n = 0, cap = 16; char **out = malloc(sizeof(char *) * cap);
  if (h != INVALID_HANDLE_VALUE) {
    do {
      if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
      if (n + 1 >= cap) { cap *= 2; out = realloc(out, sizeof(char *) * cap); }
      out[n++] = narrow(fd.cFileName);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
  }
  out[n] = NULL; *count = n; return out;
}
#else
static void mkdir_u8(const char *p) { mkdir(p, 0755); }
static int remove_u8(const char *p) { return remove(p); }
static int exists_u8(const char *p) { struct stat st; return stat(p, &st) == 0; }
static time_t mtime_u8(const char *p) { struct stat st; return stat(p, &st) == 0 ? st.st_mtime : 0; }
static int rename_u8(const char *a, const char *b) { return rename(a, b); }
static char **list_dir(const char *dir, int *count) {
  int n = 0, cap = 16; char **out = malloc(sizeof(char *) * cap);
  DIR *d = opendir(dir);
  if (d) {
    struct dirent *e;
    while ((e = readdir(d))) {
      if (e->d_name[0] == '.') continue;
      char p[2048]; snprintf(p, sizeof p, "%s/%s", dir, e->d_name);
      struct stat st; if (stat(p, &st) != 0 || !S_ISREG(st.st_mode)) continue;
      if (n + 1 >= cap) { cap *= 2; out = realloc(out, sizeof(char *) * cap); }
      out[n++] = strdup(e->d_name);
    }
    closedir(d);
  }
  out[n] = NULL; *count = n; return out;
}
#endif
static void free_list(char **l) { for (char **p = l; *p; p++) free(*p); free(l); }

static void *read_all(const char *p, size_t *n) {
  SDL_RWops *f = SDL_RWFromFile(p, "rb"); if (!f) return NULL;
  Sint64 sz = SDL_RWsize(f); if (sz < 0) { SDL_RWclose(f); return NULL; }
  void *b = malloc(sz ? sz : 1);
  size_t got = SDL_RWread(f, b, 1, sz); SDL_RWclose(f);
  if ((Sint64)got != sz) { free(b); return NULL; }
  *n = (size_t)sz; return b;
}
static int write_all(const char *p, const void *d, size_t n) {
  char tmp[2100]; snprintf(tmp, sizeof tmp, "%s.tmp", p);
  SDL_RWops *f = SDL_RWFromFile(tmp, "wb"); if (!f) return 0;
  size_t w = SDL_RWwrite(f, d, 1, n); SDL_RWclose(f);
  if (w != n) { remove_u8(tmp); return 0; }
  return rename_u8(tmp, p) == 0;
}
static int copy_file(const char *from, const char *to) {
  size_t n; void *b = read_all(from, &n); if (!b) return 0;
  int ok = write_all(to, b, n); free(b); return ok;
}
static const char *base_name(const char *p) {
  const char *a = strrchr(p, '/'), *b = strrchr(p, '\\');
  const char *s = a > b ? a : b; return s ? s + 1 : p;
}
static const char *ext_of(const char *name) { const char *d = strrchr(name, '.'); return d ? d + 1 : ""; }

// ---------------------------------------------------------------- 기종

typedef struct {
  const char *id, *title, *core;
  const char *exts[6];
  SDL_Color c1, c2;
  int tall;      // 세로로 긴 휴대용
  int xy, shoulders;
  int stick_first;   // 아날로그 스틱이 주된 조작(키보드 방향키를 왼쪽 스틱으로)
  int analog;        // 아날로그 스틱이 있음(패드 스틱을 방향키로 안 바꿈)
  int touch;         // 아래 화면 터치(마우스)
  const char *assets;   // 처음 켤 때 시스템 폴더로 복사할 자료(sysassets/<이름>)
} System;

static const System SYS[] = {
  {"nes", "패미컴", "fceumm", {"nes", "fds", "unf"}, {255, 77, 102, 255}, {255, 158, 51, 255}, 0},
  {"gb", "게임보이", "mgba", {"gb"}, {115, 217, 64, 255}, {26, 194, 140, 255}, 1},
  {"gbc", "게임보이 컬러", "mgba", {"gbc"}, {158, 92, 255, 255}, {61, 169, 255, 255}, 1},
  {"gba", "게임보이 어드밴스", "mgba", {"gba"}, {102, 77, 255, 255}, {255, 77, 158, 255}, 0},
  {"snes", "슈퍼패미컴", "snes9x", {"sfc", "smc", "swc", "fig"}, {158, 92, 255, 255}, {255, 77, 158, 255}, 0},
  {"md", "메가드라이브", "genesis_plus_gx", {"md", "gen", "smd", "bin"}, {31, 140, 255, 255}, {51, 217, 230, 255}, 0},
  {"ps1", "플레이스테이션", "pcsx_rearmed", {"cue", "chd", "pbp", "m3u"}, {255, 184, 26, 255}, {255, 97, 51, 255}, 0, 0, 0, 0, 1},
  {"n64", "닌텐도 64", "mupen64plus_next", {"n64", "z64", "v64"}, {38, 191, 89, 255}, {255, 204, 38, 255}, 0, 0, 0, 1, 1},
  {"nds", "닌텐도 DS", "melonds", {"nds"}, {64, 179, 255, 255}, {158, 102, 255, 255}, 1, 0, 0, 0, 0, 1},
  {"gc", "게임큐브", "dolphin", {"gcm", "rvz", "gcz", "ciso", "wbfs"}, {115, 77, 242, 255}, {64, 204, 217, 255}, 0, 0, 0, 1, 1, 0, "dolphin-emu"},
  {"psp", "PSP", "ppsspp", {"cso"}, {51, 77, 230, 255}, {242, 77, 179, 255}, 0, 0, 0, 0, 1, 0, "PPSSPP"},
  {"ps2", "플레이스테이션 2", "play", {NULL}, {38, 102, 255, 255}, {140, 64, 242, 255}, 1, 0, 0, 0, 1},
};
enum { SYS_PS1 = 6, SYS_GC = 9, SYS_PSP = 10, SYS_PS2 = 11 };
#define NSYS (int)(sizeof SYS / sizeof *SYS)

// 파일 일부만 읽기(디스크 이미지는 커서 다 읽지 않는다)
static int read_at(const char *p, long long off, void *buf, int n) {
#ifdef _WIN32
  wchar_t *w = widen(p); FILE *f = _wfopen(w, L"rb"); free(w);
  if (!f) return 0; _fseeki64(f, off, SEEK_SET);
#else
  FILE *f = fopen(p, "rb"); if (!f) return 0; fseeko(f, off, SEEK_SET);
#endif
  int got = (int)fread(buf, 1, n, f); fclose(f); return got;
}
// .iso: 게임큐브·위 표식 -> 게임큐브, ISO9660 시스템 이름 "PSP GAME" -> PSP, 그 밖 -> PS2 (맥·안드로이드와 같은 규칙)
static int sniff_iso(const char *path) {
  unsigned char h[0x20], v[40];
  if (read_at(path, 0, h, 0x20) == 0x20) {
    if ((h[0x1C] == 0xC2 && h[0x1D] == 0x33 && h[0x1E] == 0x9F && h[0x1F] == 0x3D) ||
        (h[0x18] == 0x5D && h[0x19] == 0x1C && h[0x1A] == 0x9E && h[0x1B] == 0xA3)) return SYS_GC;
  }
  if (read_at(path, 0x8000, v, 40) == 40 && !memcmp(v + 1, "CD001", 5) && !memcmp(v + 8, "PSP GAME", 8)) return SYS_PSP;
  return SYS_PS2;
}
// .pbp: DATA.PSAR 머리가 PSISOIMG/PSTITLEIMG 면 PS1 게임, 아니면 PSP
static int sniff_pbp(const char *path) {
  unsigned char h[0x28]; char tag[9] = {0};
  if (read_at(path, 0, h, 0x28) != 0x28 || memcmp(h, "\0PBP", 4)) return SYS_PS1;
  long long off = h[0x24] | h[0x25] << 8 | h[0x26] << 16 | (long long)h[0x27] << 24;
  read_at(path, off, tag, 8);
  return !strcmp(tag, "PSISOIMG") || !strncmp(tag, "PSTITLEI", 8) ? SYS_PS1 : SYS_PSP;
}
static int sys_of_name(const char *name);
static int sys_of_path(const char *path) {
  const char *e = ext_of(base_name(path));
  if (!SDL_strcasecmp(e, "iso")) return sniff_iso(path);
  if (!SDL_strcasecmp(e, "pbp")) return sniff_pbp(path);
  return sys_of_name(base_name(path));
}
static int sys_of_name(const char *name) {
  const char *e = ext_of(name);
  for (int i = 0; i < NSYS; i++)
    for (int k = 0; SYS[i].exts[k]; k++)
      if (!SDL_strcasecmp(e, SYS[i].exts[k])) return i;
  return -1;
}

// .cue 가 가리키는 .bin 이름들(소문자)인지
static int cue_refers(const char *dir, const char *bin) {
  int n; char **l = list_dir(dir, &n); int hit = 0;
  for (int i = 0; i < n && !hit; i++) {
    if (SDL_strcasecmp(ext_of(l[i]), "cue")) continue;
    char p[2048]; snprintf(p, sizeof p, "%s/%s", dir, l[i]);
    size_t sz; char *t = read_all(p, &sz); if (!t) continue;
    t = realloc(t, sz + 1); t[sz] = 0;
    for (char *q = t; (q = strchr(q, '"')); ) {
      char *e = strchr(q + 1, '"'); if (!e) break;
      *e = 0; if (!SDL_strcasecmp(q + 1, bin)) hit = 1; q = e + 1;
    }
    free(t);
  }
  free_list(l); return hit;
}

// ---------------------------------------------------------------- 앱 상태

static SDL_Window *win;
static SDL_Renderer *ren;
static float S = 1;          // 화면 배율(고해상도 모니터)
static int W, H;             // 렌더러 픽셀 크기
static TTF_Font *fonts[2][64];   // [0]=Bold [1]=ExtraBold, 크기별
static char base_dir[2048], data_dir[2048], roms_dir[2048], saves_dir[2048], sys_dir[2048], cores_dir[2048];
static SDL_Texture *photo[NSYS];
static int photo_w[NSYS], photo_h[NSYS];

typedef struct { char *name; char *title; int sys; int locked; } Game;
static Game games[512];
static int ngames;

enum { LIB, PLAY };
static int screen = LIB;
static int sel = 0;
static float scroll_y = 0;
static int mouse_x, mouse_y;

// 토스트
static char toast_msg[256];
static Uint32 toast_until;
static void toast(const char *m) { snprintf(toast_msg, sizeof toast_msg, "%s", m); toast_until = SDL_GetTicks() + 2000; }

// 확인 창
typedef void (*ConfirmFn)(int arg);
static struct { int open; char title[128], body[256], ok[32]; ConfirmFn fn; int arg; int focus; } confirm;
static void ask(const char *title, const char *body, const char *ok, ConfirmFn fn, int arg) {
  confirm.open = 1; snprintf(confirm.title, sizeof confirm.title, "%s", title);
  snprintf(confirm.body, sizeof confirm.body, "%s", body); snprintf(confirm.ok, sizeof confirm.ok, "%s", ok);
  confirm.fn = fn; confirm.arg = arg; confirm.focus = 1;
}

// 게임 중
static Game *cur;
static char cur_base[2200];
static SDL_Texture *game_tex;
static int game_w, game_h;
static uint64_t last_serial;
static double frame_acc;
static Uint64 last_counter;
static int frames_run;
static uint32_t key_mask, pad_mask;
static uint32_t rstick_keys;   // I/J/K/L(오른쪽 스틱): 위·아래·왼·오 비트 0~3
static SDL_FRect game_rect;    // 게임 그림이 그려진 자리(마우스 터치 좌표 계산용)
static SDL_AudioDeviceID audio;
static SDL_GameController *pad;

// 저장 목록
static const char *SLOTS[] = {"auto", "1", "2", "3", "4", "5", "6"};
#define NSLOT 7
static struct { int open; int sel; time_t when[NSLOT]; SDL_Texture *thumb[NSLOT]; int tw[NSLOT], th[NSLOT]; } panel;
static int quick_slot = 1;   // F1/F2 가 쓰는 슬롯(SLOTS 인덱스)

// ---------------------------------------------------------------- 그리기 도구

static SDL_Color lerpc(SDL_Color a, SDL_Color b, float t) {
  if (t < 0) t = 0; if (t > 1) t = 1;
  return (SDL_Color){(Uint8)(a.r + (b.r - a.r) * t), (Uint8)(a.g + (b.g - a.g) * t), (Uint8)(a.b + (b.b - a.b) * t), (Uint8)(a.a + (b.a - a.a) * t)};
}
static const SDL_Color PINK = {255, 77, 158, 255}, VIOLET = {155, 92, 255, 255}, SKY = {61, 169, 255, 255};
static SDL_Color accent_at(float t) { return t < 0.5f ? lerpc(PINK, VIOLET, t * 2) : lerpc(VIOLET, SKY, (t - 0.5f) * 2); }
static SDL_Color rgba(int r, int g, int b, int a) { return (SDL_Color){r, g, b, a}; }

// 둥근 사각형. mode 0=단색(c1) 1=가로 그라디언트 2=대각선 그라디언트 3=강조색(분홍-보라-하늘) 가로
static void rrect(float x, float y, float w, float h, float r, SDL_Color c1, SDL_Color c2, int mode) {
  if (r > w / 2) r = w / 2; if (r > h / 2) r = h / 2;
  enum { SEG = 8 };
  SDL_Vertex v[1 + 4 * (SEG + 1)];
  int idx[3 * 4 * (SEG + 1)];
  // 오른쪽 위 -> 오른쪽 아래 -> 왼쪽 아래 -> 왼쪽 위, 시계 방향
  float cx[4] = {x + w - r, x + w - r, x + r, x + r}, cy[4] = {y + r, y + h - r, y + h - r, y + r};
  float a0[4] = {-M_PI / 2, 0, M_PI / 2, M_PI};
  int n = 1;
  v[0].position = (SDL_FPoint){x + w / 2, y + h / 2};
  for (int c = 0; c < 4; c++)
    for (int i = 0; i <= SEG; i++) {
      float a = a0[c] + (float)i / SEG * (M_PI / 2);
      v[n++].position = (SDL_FPoint){cx[c] + cosf(a) * r, cy[c] + sinf(a) * r};
    }
  for (int i = 0; i < n; i++) {
    float tx = (v[i].position.x - x) / w, ty = (v[i].position.y - y) / h, t = mode == 2 ? (tx + ty) / 2 : tx;
    v[i].color = mode == 0 ? c1 : mode == 3 ? accent_at(t) : lerpc(c1, c2, t);
    v[i].tex_coord = (SDL_FPoint){0, 0};
  }
  int k = 0;
  for (int i = 1; i < n; i++) { idx[k++] = 0; idx[k++] = i; idx[k++] = i + 1 < n ? i + 1 : 1; }
  SDL_RenderGeometry(ren, NULL, v, n, idx, k);
}
static void shadow(float x, float y, float w, float h, float r, int alpha) {
  for (int i = 3; i >= 1; i--) rrect(x - i * S, y + (2 + i) * S, w + 2 * i * S, h + 2 * i * S, r + i * S, rgba(60, 30, 90, alpha / 3), rgba(0, 0, 0, 0), 0);
}

// 글자. 하얀색으로 그린 텍스처를 캐시해 두고 색은 color mod 로 입힌다.
typedef struct { char *s; int font, size; SDL_Texture *t; int w, h; Uint32 used; } TextEntry;
static TextEntry tcache[512];
static TTF_Font *font(int bold, int px) {
  if (px < 6) px = 6; if (px > 63) px = 63;
  if (!fonts[bold][px]) {
    char p[2100]; snprintf(p, sizeof p, "%sfonts/%s", base_dir, bold ? "Pretendard-ExtraBold.otf" : "Pretendard-Bold.otf");
    fonts[bold][px] = TTF_OpenFont(p, px);
    if (!fonts[bold][px]) { SDL_Log("font: %s", TTF_GetError()); exit(1); }
  }
  return fonts[bold][px];
}
static TextEntry *text_tex(const char *s, int bold, int px) {
  if (!*s) return NULL;
  TextEntry *free_e = NULL, *old = &tcache[0];
  for (int i = 0; i < 512; i++) {
    TextEntry *e = &tcache[i];
    if (e->s && e->font == bold && e->size == px && !strcmp(e->s, s)) { e->used = SDL_GetTicks(); return e; }
    if (!e->s && !free_e) free_e = e;
    if (e->used < old->used) old = e;
  }
  TextEntry *e = free_e ? free_e : old;
  if (e->s) { free(e->s); SDL_DestroyTexture(e->t); e->s = NULL; }
  SDL_Surface *surf = TTF_RenderUTF8_Blended(font(bold, px), s, (SDL_Color){255, 255, 255, 255});
  if (!surf) return NULL;
  e->t = SDL_CreateTextureFromSurface(ren, surf); e->w = surf->w; e->h = surf->h; SDL_FreeSurface(surf);
  e->s = strdup(s); e->font = bold; e->size = px; e->used = SDL_GetTicks();
  return e;
}
// align: 0 왼쪽 1 가운데 2 오른쪽. 그린 너비를 돌려준다.
static float text(const char *s, float x, float y, float size, int bold, SDL_Color c, int align) {
  TextEntry *e = text_tex(s, bold, (int)(size * S + 0.5f)); if (!e) return 0;
  float dx = align == 1 ? x - e->w / 2.0f : align == 2 ? x - e->w : x;
  SDL_SetTextureColorMod(e->t, c.r, c.g, c.b); SDL_SetTextureAlphaMod(e->t, c.a);
  SDL_FRect d = {dx, y, e->w, e->h}; SDL_RenderCopyF(ren, e->t, NULL, &d);
  return e->w;
}
static float text_w(const char *s, float size, int bold) { TextEntry *e = text_tex(s, bold, (int)(size * S + 0.5f)); return e ? e->w : 0; }
// 강조색 그라디언트 글자(세로 띠로 나눠 색을 입힘)
static void text_accent(const char *s, float x, float y, float size) {
  TextEntry *e = text_tex(s, 1, (int)(size * S + 0.5f)); if (!e) return;
  int strips = 24;
  for (int i = 0; i < strips; i++) {
    int x0 = e->w * i / strips, x1 = e->w * (i + 1) / strips;
    SDL_Color c = accent_at((i + 0.5f) / strips);
    SDL_SetTextureColorMod(e->t, c.r, c.g, c.b); SDL_SetTextureAlphaMod(e->t, 255);
    SDL_Rect src = {x0, 0, x1 - x0, e->h}; SDL_FRect d = {x + x0, y, x1 - x0, e->h};
    SDL_RenderCopyF(ren, e->t, &src, &d);
  }
}
// 줄바꿈(최대 lines 줄, 넘치면 …)
static float text_wrap(const char *s, float x, float y, float maxw, float size, int bold, SDL_Color c, int lines) {
  char line[512]; const char *p = s; float lh = size * 1.25f * S; int drawn = 0;
  for (int ln = 0; ln < lines && *p; ln++) {
    int n = 0, last_space = -1, fit = 0;
    while (p[n]) {
      int cl = 1; unsigned char ch = p[n]; if (ch >= 0xF0) cl = 4; else if (ch >= 0xE0) cl = 3; else if (ch >= 0xC0) cl = 2;
      memcpy(line, p, n + cl); line[n + cl] = 0;
      if (text_w(line, size, bold) > maxw) break;
      if (p[n] == ' ') last_space = n;
      n += cl; fit = n;
    }
    if (p[fit] && last_space > 0 && ln < lines - 1) fit = last_space;
    if (p[fit] && ln == lines - 1) {  // 마지막 줄인데 남았으면 …
      while (fit > 0) {
        int b = fit - 1; while (b > 0 && ((unsigned char)p[b] & 0xC0) == 0x80) b--;
        memcpy(line, p, b); strcpy(line + b, "…"); fit = b;
        if (text_w(line, size, bold) <= maxw) break;
      }
    } else { memcpy(line, p, fit); line[fit] = 0; }
    text(line, x, y + ln * lh, size, bold, c, 0);
    p += fit; while (*p == ' ') p++;
    drawn = ln + 1;
  }
  return y + drawn * lh;   // 마지막 줄 아래 y
}

static SDL_Texture *load_png(const char *path, int *w, int *h) {
  size_t n; void *b = read_all(path, &n); if (!b) return NULL;
  int c; unsigned char *px = stbi_load_from_memory(b, (int)n, w, h, &c, 4); free(b);
  if (!px) return NULL;
  SDL_Texture *t = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, *w, *h);
  SDL_UpdateTexture(t, NULL, px, *w * 4); SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
  stbi_image_free(px); return t;
}
static void draw_fit(SDL_Texture *t, int tw, int th, float x, float y, float w, float h, int align_right) {
  float s = fminf(w / tw, h / th), dw = tw * s, dh = th * s;
  SDL_FRect d = {align_right ? x + w - dw : x + (w - dw) / 2, align_right ? y : y + (h - dh) / 2, dw, dh};
  SDL_RenderCopyF(ren, t, NULL, &d);
}
static int hit(float x, float y, float w, float h) { return mouse_x >= x && mouse_x < x + w && mouse_y >= y && mouse_y < y + h; }

// ---------------------------------------------------------------- 게임 목록

static int game_cmp(const void *a, const void *b) { return SDL_strcasecmp(((Game *)a)->title, ((Game *)b)->title); }
static void load_library(void) {
  for (int i = 0; i < ngames; i++) { free(games[i].name); free(games[i].title); }
  ngames = 0;
  int n; char **l = list_dir(roms_dir, &n);
  for (int i = 0; i < n && ngames < 512; i++) {
    char full[2400]; snprintf(full, sizeof full, "%s/%s", roms_dir, l[i]);
    int s = sys_of_path(full);
    if (s < 0) continue;
    if (!SDL_strcasecmp(ext_of(l[i]), "bin") && cue_refers(roms_dir, l[i])) continue;  // PS1 데이터
    Game *g = &games[ngames++];
    g->name = strdup(l[i]); g->title = strdup(l[i]); g->sys = s;
    char *d = strrchr(g->title, '.'); if (d) *d = 0;
  }
  free_list(l);
  qsort(games, ngames, sizeof *games, game_cmp);
  if (sel >= ngames) sel = ngames - 1; if (sel < 0) sel = 0;
  // 잠긴 게임: data/locked.txt 에 롬 파일 이름 한 줄씩
  char lp[2100]; snprintf(lp, sizeof lp, "%s/locked.txt", data_dir);
  size_t n2; char *t = read_all(lp, &n2);
  if (t) {
    t = realloc(t, n2 + 1); t[n2] = 0;
    for (char *line = strtok(t, "\r\n"); line; line = strtok(NULL, "\r\n"))
      for (int i = 0; i < ngames; i++) if (!strcmp(games[i].name, line)) games[i].locked = 1;
    free(t);
  }
}
static void save_locks(void) {
  char lp[2100]; snprintf(lp, sizeof lp, "%s/locked.txt", data_dir);
  size_t cap = 1, len = 0; for (int i = 0; i < ngames; i++) if (games[i].locked) cap += strlen(games[i].name) + 1;
  char *b = malloc(cap); b[0] = 0;
  for (int i = 0; i < ngames; i++) if (games[i].locked) len += sprintf(b + len, "%s\n", games[i].name);
  if (len) write_all(lp, b, len); else remove_u8(lp);
  free(b);
}

static int add_rom(const char *path) {
  const char *name = base_name(path);
  int s = sys_of_path(path);
  if (s < 0) return 0;
  char to[2300]; snprintf(to, sizeof to, "%s/%s", roms_dir, name);
  if (!copy_file(path, to)) return 0;
  // .cue 면 같은 폴더의 .bin 도 같이 가져온다
  if (!SDL_strcasecmp(ext_of(name), "cue")) {
    size_t sz; char *t = read_all(path, &sz);
    if (t) {
      t = realloc(t, sz + 1); t[sz] = 0;
      char dir[2048]; snprintf(dir, sizeof dir, "%s", path); char *slash = (char *)base_name(dir); *slash = 0;
      for (char *q = t; (q = strchr(q, '"')); ) {
        char *e = strchr(q + 1, '"'); if (!e) break; *e = 0;
        char from[2300], dst[2300]; snprintf(from, sizeof from, "%s%s", dir, q + 1); snprintf(dst, sizeof dst, "%s/%s", roms_dir, q + 1);
        copy_file(from, dst); q = e + 1;
      }
      free(t);
    }
  }
  return 1;
}
static void add_files(char **paths, int n) {
  int ok = 0, bad = 0;
  for (int i = 0; i < n; i++) { if (add_rom(paths[i])) ok++; else bad++; }
  load_library();
  char m[128];
  if (ok && bad) snprintf(m, sizeof m, "%d개 추가 · %d개는 롬이 아니에요", ok, bad);
  else if (ok) snprintf(m, sizeof m, "%d개 추가했어요", ok);
  else snprintf(m, sizeof m, "열 수 있는 롬이 없어요");
  toast(m);
}

static void pick_files(void) {
#ifdef _WIN32
  static wchar_t buf[32768]; buf[0] = 0;
  OPENFILENAMEW o = {0};
  o.lStructSize = sizeof o;
  o.lpstrFilter = L"롬 파일\0*.nes;*.fds;*.unf;*.gb;*.gbc;*.gba;*.sfc;*.smc;*.swc;*.fig;*.md;*.gen;*.smd;*.bin;*.cue;*.chd;*.pbp;*.m3u;*.n64;*.z64;*.v64;*.nds;*.iso;*.cso;*.gcm;*.rvz;*.gcz;*.ciso;*.wbfs\0모든 파일\0*.*\0";
  o.lpstrFile = buf; o.nMaxFile = 32768;
  o.Flags = OFN_ALLOWMULTISELECT | OFN_EXPLORER | OFN_FILEMUSTEXIST;
  if (!GetOpenFileNameW(&o)) return;
  // 하나면 전체 경로, 여러 개면 폴더\0이름\0이름\0\0
  char *paths[256]; int n = 0;
  wchar_t *dir = buf, *p = buf + wcslen(buf) + 1;
  if (!*p) { paths[n++] = narrow(dir); }
  else for (; *p && n < 256; p += wcslen(p) + 1) {
    wchar_t full[2048]; _snwprintf(full, 2048, L"%ls\\%ls", dir, p); full[2047] = 0; paths[n++] = narrow(full);
  }
  add_files(paths, n);
  for (int i = 0; i < n; i++) free(paths[i]);
#else
  toast("롬 파일을 창에 끌어다 놓아요");
#endif
}

// 잠금을 풀 때 경고(맥·안드로이드·iOS 와 같은 문구)
#define LOCK_WARNING "예전엔 게임이 비싸서 하나를 끝까지 파고들었죠. 잠금을 풀면 그 맛이 흐려져요. 지금 하는 게임을 충분히 즐긴 뒤에 푸는 걸 추천해요."
static void unlock_game(int i) { if (i >= 0 && i < ngames) { games[i].locked = 0; save_locks(); toast("잠금을 풀었어요"); } }
static void toggle_lock(int i) {
  if (i < 0 || i >= ngames) return;
  if (games[i].locked) {
    char t[128]; snprintf(t, sizeof t, "%s 잠금을 풀까요?", games[i].title);
    ask(t, LOCK_WARNING, "잠금 풀기", unlock_game, i);
  } else { games[i].locked = 1; save_locks(); toast("잠갔어요 · 풀 때는 오른쪽 클릭이나 L 키"); }
}

static void delete_game(int i) {
  if (i < 0 || i >= ngames) return;
  char p[2300]; snprintf(p, sizeof p, "%s/%s", roms_dir, games[i].name);
  remove_u8(p);
  load_library();
  toast("목록에서 지웠어요");
}

// ---------------------------------------------------------------- 게임 실행

static void audio_cb(void *u, Uint8 *stream, int len) { (void)u; rh_audio_read((int16_t *)stream, len / 4); }

static void slot_paths(int s, char *state, char *png) {
  if (state) sprintf(state, "%s.%s.state", cur_base, SLOTS[s]);
  if (png) sprintf(png, "%s.%s.png", cur_base, SLOTS[s]);
}

static void png_cb(void *ctx, void *data, int size) {
  void **b = ctx; size_t *n = (size_t *)b[1];
  b[0] = realloc(b[0], *n + size); memcpy((char *)b[0] + *n, data, size); *n += size;
}
static int save_slot(int s) {
  char st[2300], pn[2300]; slot_paths(s, st, pn);
  if (!rh_state_save(st)) return 0;
  const uint8_t *px; int w, h;
  if (rh_video(&px, &w, &h)) {
    size_t n = 0; void *ctx[2] = {NULL, &n};
    stbi_write_png_to_func(png_cb, ctx, w, h, 4, px, w * 4);
    if (ctx[0]) { write_all(pn, ctx[0], n); free(ctx[0]); }
  } else remove_u8(pn);
  return 1;
}

static void panel_refresh(void) {
  for (int i = 0; i < NSLOT; i++) {
    char st[2300], pn[2300]; slot_paths(i, st, pn);
    panel.when[i] = exists_u8(st) ? mtime_u8(st) : 0;
    if (panel.thumb[i]) { SDL_DestroyTexture(panel.thumb[i]); panel.thumb[i] = NULL; }
    if (panel.when[i]) {
      panel.thumb[i] = load_png(pn, &panel.tw[i], &panel.th[i]);
      if (panel.thumb[i]) SDL_SetTextureScaleMode(panel.thumb[i], SDL_ScaleModeNearest);
    }
  }
}
static void panel_open(int open) {
  panel.open = open;
  if (open) { panel_refresh(); panel.sel = quick_slot; key_mask = 0; }
  if (audio) SDL_PauseAudioDevice(audio, open);
  last_counter = SDL_GetPerformanceCounter(); frame_acc = 0;
}

#define NACT_ROWS 20
// Esc 일시정지 메뉴: 이어하기 / 저장·불러오기 / 조작법 / 나가기
static struct { int open, sel; } pmenu;
static const char *PMENU[] = {"이어하기", "저장 / 불러오기", "조작법", "나가기"};
#define NPMENU 4
// 조작법 화면: 줄마다 동작 번호와 이름(코어가 알려 준 이름 그대로)
static struct { int open, sel, capture, n; int act[NACT_ROWS]; char name[NACT_ROWS][96]; } ctl;
static void pmenu_open(int open) {
  pmenu.open = open; pmenu.sel = 0; key_mask = 0;
  if (audio) SDL_PauseAudioDevice(audio, open);
  last_counter = SDL_GetPerformanceCounter(); frame_acc = 0;
}

static void slot_name(int s, char *out) { if (s) sprintf(out, "%d번 슬롯", s); else strcpy(out, "자동 저장"); }
static void do_save(int s) {
  char m[128], nm[32]; slot_name(s, nm);
  if (save_slot(s)) { if (s) quick_slot = s; snprintf(m, sizeof m, "%s에 저장함", nm); sync_push_async(saves_dir, cur->name); }
  else snprintf(m, sizeof m, "이 게임은 상태 저장을 못 해요");
  toast(m);
  if (panel.open) panel_refresh();
}
static void do_load(int s) {
  char st[2300]; slot_paths(s, st, NULL);
  char m[128], nm[32]; slot_name(s, nm);
  if (rh_state_load(st)) { snprintf(m, sizeof m, "%s 불러옴", nm); last_serial = 0; if (s) quick_slot = s; }
  else snprintf(m, sizeof m, "%s이 비어 있어요", nm);
  toast(m);
}
static void delete_slot(int s) {
  char st[2300], pn[2300]; slot_paths(s, st, pn); remove_u8(st); remove_u8(pn); panel_refresh();
  sync_deleted_async(cur->name, SLOTS[s]);
}
static void confirm_save(int s) { do_save(s); }

static void sram_path(char *out) { sprintf(out, "%s.srm", cur_base); }

// 폴더째 복사(PSP·게임큐브 코어 자료를 처음 한 번 시스템 폴더로)
static void copy_tree(const char *from, const char *to) {
  mkdir_u8(to);
#ifdef _WIN32
  char pat[2400]; snprintf(pat, sizeof pat, "%s/*", from);
  wchar_t *w = widen(pat); WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileW(w, &fd); free(w);
  if (h == INVALID_HANDLE_VALUE) return;
  do {
    if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
    char *nm = narrow(fd.cFileName), a[2400], b[2400];
    snprintf(a, sizeof a, "%s/%s", from, nm); snprintf(b, sizeof b, "%s/%s", to, nm); free(nm);
    if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) copy_tree(a, b); else copy_file(a, b);
  } while (FindNextFileW(h, &fd));
  FindClose(h);
#else
  DIR *d = opendir(from); if (!d) return;
  struct dirent *e;
  while ((e = readdir(d))) {
    if (e->d_name[0] == '.') continue;
    char a[2400], b[2400]; snprintf(a, sizeof a, "%s/%s", from, e->d_name); snprintf(b, sizeof b, "%s/%s", to, e->d_name);
    struct stat st; if (stat(a, &st)) continue;
    if (S_ISDIR(st.st_mode)) copy_tree(a, b); else copy_file(a, b);
  }
  closedir(d);
#endif
}
static void install_assets(const System *sy) {
  if (!sy->assets) return;
  char dst[2300], src[2300];
  snprintf(dst, sizeof dst, "%s/%s", sys_dir, sy->assets); snprintf(src, sizeof src, "%ssysassets/%s", base_dir, sy->assets);
  if (exists_u8(dst) || !exists_u8(src)) return;
  copy_tree(src, dst);
}

// PSP·N64·게임큐브·PS2 코어용 OpenGL: 보이지 않는 SDL 창에 GL 컨텍스트를 만든다(같은 스레드에서만 씀)
static SDL_Window *gl_win; static SDL_GLContext gl_ctx;
static int sdl_gl_create(int gles, int major, int minor) {
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, gles ? SDL_GL_CONTEXT_PROFILE_ES : major >= 3 ? SDL_GL_CONTEXT_PROFILE_CORE : 0);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, major ? major : 2);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, minor);
#ifdef __APPLE__
  if (!gles && major >= 3) SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
#endif
  SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24); SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
  gl_win = SDL_CreateWindow("gl", 0, 0, 16, 16, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
  if (!gl_win) return 0;
  gl_ctx = SDL_GL_CreateContext(gl_win);
  if (!gl_ctx) { SDL_DestroyWindow(gl_win); gl_win = NULL; return 0; }
  return 1;
}
static void sdl_gl_current(int on) { if (gl_win) SDL_GL_MakeCurrent(gl_win, on ? gl_ctx : NULL); }
static void *sdl_gl_proc(const char *n) { return SDL_GL_GetProcAddress(n); }
static void sdl_gl_destroy(void) {
  if (gl_ctx) { SDL_GL_MakeCurrent(gl_win, NULL); SDL_GL_DeleteContext(gl_ctx); gl_ctx = NULL; }
  if (gl_win) { SDL_DestroyWindow(gl_win); gl_win = NULL; }
}
static const rh_gl_provider sdl_gl = {sdl_gl_create, sdl_gl_current, sdl_gl_proc, sdl_gl_destroy};

static void draw_bg(void);
static float text(const char *s, float x, float y, float size, int bold, SDL_Color c, int align);
static SDL_Color rgba(int r, int g, int b, int a);
static int start_game(Game *g) {
  char core[2300], rom[2300];
  int sync_failed = 0;
  if (sync_enabled()) {   // 다른 기기 저장부터 받는다(받는 동안 한 장 그려 둠)
    draw_bg();
    text("다른 기기 저장을 받는 중", W / 2, H / 2 - 10 * S, 17, 1, rgba(91, 51, 191, 255), 1);
    SDL_RenderPresent(ren);
    sync_failed = sync_game(saves_dir, g->name, 1) < 0;
  }
  snprintf(core, sizeof core, "%s/%s_libretro" CORE_EXT, cores_dir, SYS[g->sys].core);
  snprintf(rom, sizeof rom, "%s/%s", roms_dir, g->name);
  install_assets(&SYS[g->sys]);
  if (!rh_open(core, sys_dir, saves_dir) || !rh_load(rom)) {
    char m[600]; snprintf(m, sizeof m, "%s", rh_error()); rh_close();
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_WARNING, "열 수 없어요", m, win);
    return 0;
  }
  cur = g;
  snprintf(cur_base, sizeof cur_base, "%s/%s", saves_dir, g->name);
  char s[2300]; sram_path(s); rh_sram_load(s);
  // 예전 한 칸짜리 저장은 1번 슬롯으로
  char old[2300], one[2300]; snprintf(old, sizeof old, "%s.state", cur_base); slot_paths(1, one, NULL);
  if (exists_u8(old) && !exists_u8(one)) rename_u8(old, one);
  last_serial = 0; frames_run = 0; frame_acc = 0; key_mask = 0; quick_slot = 1;
  last_counter = SDL_GetPerformanceCounter();
  SDL_AudioSpec want = {0}, got;
  want.freq = (int)(rh_sample_rate() + 0.5); want.format = AUDIO_S16SYS; want.channels = 2; want.samples = 1024; want.callback = audio_cb;
  audio = SDL_OpenAudioDevice(NULL, 0, &want, &got, 0);
  if (audio) SDL_PauseAudioDevice(audio, 0);
  screen = PLAY;
  char st[2300]; slot_paths(0, st, NULL);
  if (exists_u8(st)) panel_open(1);   // 지난번 자동 저장이 있으면 목록부터
  else toast("Esc 메뉴 · F1 저장 · F2 불러오기 · F3 저장 목록");
  if (sync_failed) toast("동기화 서버에 못 닿아서 이 기기 저장으로 시작해요");
  return 1;
}

static void stop_game(void) {
  if (!cur) return;
  if (audio) { SDL_CloseAudioDevice(audio); audio = 0; }
  if (frames_run >= 300) save_slot(0);   // 5초 넘게 했으면 자동 저장
  char s[2300]; sram_path(s); rh_sram_save(s);
  rh_close();
  sync_push_async(saves_dir, cur->name);   // 다른 기기용으로 올리기(뒤에서)
  cur = NULL; panel.open = 0; pmenu.open = 0; ctl.open = 0;
  if (game_tex) { SDL_DestroyTexture(game_tex); game_tex = NULL; }
  for (int i = 0; i < NSLOT; i++) if (panel.thumb[i]) { SDL_DestroyTexture(panel.thumb[i]); panel.thumb[i] = NULL; }
  screen = LIB;
}

static void run_frames(void) {
  Uint64 now = SDL_GetPerformanceCounter();
  double dt = (double)(now - last_counter) / SDL_GetPerformanceFrequency();
  last_counter = now;
  if (panel.open || pmenu.open || ctl.open || confirm.open) return;
  frame_acc += dt;
  double ft = 1.0 / rh_fps();
  if (frame_acc > ft * 4) frame_acc = ft * 4;   // 멈췄다 돌아오면 따라잡지 않음
  // 아날로그: 스틱이 주된 기종은 방향키를 왼쪽 스틱으로, I/J/K/L 은 오른쪽 스틱. 패드 스틱이 더 기울었으면 그걸로.
  const System *sy = &SYS[cur->sys];
  uint32_t keys = key_mask;
  double lx = 0, ly = 0, rx = 0, ry = 0;
  if (sy->stick_first) {
    if (keys & (1u << RH_LEFT)) lx -= 1; if (keys & (1u << RH_RIGHT)) lx += 1;
    if (keys & (1u << RH_UP)) ly -= 1; if (keys & (1u << RH_DOWN)) ly += 1;
    keys &= ~((1u << RH_LEFT) | (1u << RH_RIGHT) | (1u << RH_UP) | (1u << RH_DOWN));
  }
  if (rstick_keys & 1) ry -= 1; if (rstick_keys & 2) ry += 1; if (rstick_keys & 4) rx -= 1; if (rstick_keys & 8) rx += 1;
  if (pad) {
    double px = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTX) / 32767.0, py = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTY) / 32767.0;
    double qx = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_RIGHTX) / 32767.0, qy = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_RIGHTY) / 32767.0;
    if (fabs(px) + fabs(py) > fabs(lx) + fabs(ly) && fabs(px) + fabs(py) > 0.25) { lx = px; ly = py; }
    if (fabs(qx) + fabs(qy) > fabs(rx) + fabs(ry) && fabs(qx) + fabs(qy) > 0.25) { rx = qx; ry = qy; }
  }
  rh_set_analog(0, lx, ly); rh_set_analog(1, rx, ry);
  while (frame_acc >= ft) {
    rh_set_input(keys | pad_mask);
    rh_run(); frames_run++;
    if (frames_run % 600 == 0) { char s[2300]; sram_path(s); rh_sram_save(s); }
    frame_acc -= ft;
  }
  uint64_t ser = rh_video_serial();
  if (ser != last_serial) {
    const uint8_t *px; int w, h;
    if (rh_video(&px, &w, &h)) {
      if (!game_tex || w != game_w || h != game_h) {
        if (game_tex) SDL_DestroyTexture(game_tex);
        game_tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, w, h);
        SDL_SetTextureScaleMode(game_tex, SDL_ScaleModeNearest);
        game_w = w; game_h = h;
      }
      SDL_UpdateTexture(game_tex, NULL, px, w * 4);
      last_serial = ser;
    }
  }
}

// ---------------------------------------------------------------- 입력

// 기종별 키 배정. 동작 0~15 는 패드 버튼(RH_*), 16~19 는 오른쪽 스틱 위·아래·왼쪽·오른쪽.
// data/keys.txt 에 "기종 동작 스캔코드" 한 줄씩(맥·안드로이드와 따로 저장).
#define NACT 20
static SDL_Scancode keymap[NSYS][NACT];
static const SDL_Scancode KEY_DEFAULT[NACT] = {
  [RH_UP] = SDL_SCANCODE_UP, [RH_DOWN] = SDL_SCANCODE_DOWN, [RH_LEFT] = SDL_SCANCODE_LEFT, [RH_RIGHT] = SDL_SCANCODE_RIGHT,
  [RH_B] = SDL_SCANCODE_Z, [RH_A] = SDL_SCANCODE_X, [RH_Y] = SDL_SCANCODE_A, [RH_X] = SDL_SCANCODE_S,
  [RH_L] = SDL_SCANCODE_Q, [RH_R] = SDL_SCANCODE_W, [RH_L2] = SDL_SCANCODE_E, [RH_R2] = SDL_SCANCODE_R,
  [RH_START] = SDL_SCANCODE_RETURN, [RH_SELECT] = SDL_SCANCODE_TAB,
  [16] = SDL_SCANCODE_I, [17] = SDL_SCANCODE_K, [18] = SDL_SCANCODE_J, [19] = SDL_SCANCODE_L,
};
/// 메뉴용이라 게임 버튼으로 못 쓰는 키
static int key_reserved(SDL_Scancode k) {
  return k == SDL_SCANCODE_ESCAPE || k == SDL_SCANCODE_F1 || k == SDL_SCANCODE_F2 || k == SDL_SCANCODE_F3 || k == SDL_SCANCODE_F11;
}
static void keys_path(char *out, size_t n) { snprintf(out, n, "%s/keys.txt", data_dir); }
static void keys_load(void) {
  for (int s = 0; s < NSYS; s++) memcpy(keymap[s], KEY_DEFAULT, sizeof KEY_DEFAULT);
  char p[2100]; keys_path(p, sizeof p);
  size_t n; char *t = read_all(p, &n); if (!t) return;
  t = realloc(t, n + 1); t[n] = 0;
  int cleared[64] = {0};
  for (char *line = strtok(t, "\r\n"); line; line = strtok(NULL, "\r\n")) {
    char id[32]; int a, k;
    if (sscanf(line, "%31s %d %d", id, &a, &k) != 3 || a < 0 || a >= NACT) continue;
    for (int s = 0; s < NSYS; s++) if (!strcmp(SYS[s].id, id)) {
      if (!cleared[s]) { memset(keymap[s], 0, sizeof keymap[s]); cleared[s] = 1; }   // 저장된 기종은 파일 내용이 전부
      keymap[s][a] = (SDL_Scancode)k;
    }
  }
  free(t);
}
static void keys_save(void) {
  char p[2100]; keys_path(p, sizeof p);
  char *b = malloc(NSYS * NACT * 40 + 1); size_t len = 0;
  for (int s = 0; s < NSYS; s++) {
    if (!memcmp(keymap[s], KEY_DEFAULT, sizeof KEY_DEFAULT)) continue;   // 기본값인 기종은 안 적는다
    int any = 0;
    for (int a = 0; a < NACT; a++) if (keymap[s][a]) { len += sprintf(b + len, "%s %d %d\n", SYS[s].id, a, (int)keymap[s][a]); any = 1; }
    if (!any) len += sprintf(b + len, "%s 0 0\n", SYS[s].id);
  }
  if (len) write_all(p, b, len); else remove_u8(p);
  free(b);
}
/// 지금 기종에서 이 키가 맡은 동작(-1: 없음)
static int key_action(SDL_Scancode k) {
  if (!cur || !k || key_reserved(k)) return -1;
  for (int a = 0; a < NACT; a++) if (keymap[cur->sys][a] == k) return a;
  return -1;
}
// 패드: 위치 기준(아래=B, 오른쪽=A, 왼쪽=Y, 위=X)
static int pad_bit(int b) {
  switch (b) {
  case SDL_CONTROLLER_BUTTON_A: return RH_B; case SDL_CONTROLLER_BUTTON_B: return RH_A;
  case SDL_CONTROLLER_BUTTON_X: return RH_Y; case SDL_CONTROLLER_BUTTON_Y: return RH_X;
  case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: return RH_L; case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: return RH_R;
  case SDL_CONTROLLER_BUTTON_START: return RH_START; case SDL_CONTROLLER_BUTTON_BACK: return RH_SELECT;
  case SDL_CONTROLLER_BUTTON_DPAD_UP: return RH_UP; case SDL_CONTROLLER_BUTTON_DPAD_DOWN: return RH_DOWN;
  case SDL_CONTROLLER_BUTTON_DPAD_LEFT: return RH_LEFT; case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: return RH_RIGHT;
  default: return -1;
  }
}
static void update_pad_axes(void) {
  if (!pad) return;
  uint32_t m = pad_mask & ~((1u << RH_L2) | (1u << RH_R2));
  // 스틱은 버튼 방향키와 합친다(버튼 쪽 비트는 이벤트로 관리)
  static uint32_t stick = 0;
  m &= ~stick;
  int x = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTX), y = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTY);
  stick = 0;
  if (cur && SYS[cur->sys].analog) x = y = 0;   // 아날로그 기종은 스틱을 스틱으로 보낸다
  if (x < -16000) stick |= 1u << RH_LEFT; if (x > 16000) stick |= 1u << RH_RIGHT;
  if (y < -16000) stick |= 1u << RH_UP; if (y > 16000) stick |= 1u << RH_DOWN;
  if (SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_TRIGGERLEFT) > 16000) m |= 1u << RH_L2;
  if (SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > 16000) m |= 1u << RH_R2;
  pad_mask = m | stick;
}

// 메뉴용 공통 명령
enum { NAV_NONE, NAV_UP, NAV_DOWN, NAV_LEFT, NAV_RIGHT, NAV_OK, NAV_BACK, NAV_ALT, NAV_DEL, NAV_LOCK };

static void toggle_fullscreen(void) {
  Uint32 f = SDL_GetWindowFlags(win);
  SDL_SetWindowFullscreen(win, (f & SDL_WINDOW_FULLSCREEN_DESKTOP) ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
}

// 레이아웃 값(그리기와 클릭이 같이 씀)
static int lib_cols;
static float lib_x0, lib_y0, card_w, card_h, gap;

static void lib_layout(void) {
  float pad_ = 28 * S; gap = 16 * S;
  float avail = W - pad_ * 2;
  lib_cols = (int)((avail + gap) / (200 * S + gap)); if (lib_cols < 1) lib_cols = 1;
  card_w = (avail - gap * (lib_cols - 1)) / lib_cols; card_h = 150 * S;
  lib_x0 = pad_; lib_y0 = 96 * S;
}
static void lib_ensure_visible(void) {
  lib_layout();
  int row = sel / (lib_cols ? lib_cols : 1);
  float top = row * (card_h + gap), bottom = top + card_h, view = H - lib_y0 - 20 * S;
  if (top < scroll_y) scroll_y = top;
  if (bottom > scroll_y + view) scroll_y = bottom - view;
}

// 목록 카드 오른쪽 클릭 메뉴: 잠그기·잠금 풀기 / 지우기
static struct { int open, i; float x, y; } cmenu;

static void lib_open(int i) {
  if (i < 0 || i >= ngames) return;
  if (games[i].locked) { toast("잠긴 게임이에요 · 오른쪽 클릭이나 L 키로 잠금을 풀어요"); return; }
  start_game(&games[i]);
}

static void lib_nav(int n) {
  if (confirm.open) return;
  if (!ngames) { if (n == NAV_OK) pick_files(); return; }
  lib_layout();
  if (n == NAV_LEFT && sel > 0) sel--;
  if (n == NAV_RIGHT && sel < ngames - 1) sel++;
  if (n == NAV_UP && sel - lib_cols >= 0) sel -= lib_cols;
  if (n == NAV_DOWN && sel + lib_cols < ngames) sel += lib_cols;
  if (n == NAV_OK) lib_open(sel);
  if (n == NAV_LOCK) toggle_lock(sel);
  if (n == NAV_DEL) {
    char b[256]; snprintf(b, sizeof b, "%s 롬 파일을 앱 폴더에서 지워요. 저장은 남아요.", games[sel].title);
    ask("목록에서 지울까요?", b, "지우기", delete_game, sel);
  }
  lib_ensure_visible();
}

static void panel_try_save(int s) {
  if (s == 0) return;
  if (frames_run == 0) { toast("게임을 시작한 뒤에 저장할 수 있어요"); return; }  // 켜자마자 뜬 목록에서 빈 상태로 덮어쓰기 방지
  if (panel.when[s]) {
    char nm[32], b[128]; slot_name(s, nm); snprintf(b, sizeof b, "%s에 있던 저장이 지워져요.", nm);
    ask("덮어쓸까요?", b, "덮어쓰기", confirm_save, s);
  } else do_save(s);
}
static void panel_try_load(int s) {
  if (!panel.when[s]) return;
  panel_open(0); do_load(s);
}
static void panel_nav(int n) {
  int cols = 4;
  if (n == NAV_LEFT && panel.sel > 0) panel.sel--;
  if (n == NAV_RIGHT && panel.sel < NSLOT - 1) panel.sel++;
  if (n == NAV_UP && panel.sel - cols >= 0) panel.sel -= cols;
  if (n == NAV_DOWN && panel.sel + cols < NSLOT) panel.sel += cols;
  if (n == NAV_OK) panel_try_load(panel.sel);
  if (n == NAV_ALT) panel_try_save(panel.sel);
  if (n == NAV_BACK) panel_open(0);
  if (n == NAV_DEL && panel.when[panel.sel]) {
    char nm[32]; slot_name(panel.sel, nm); char t[64]; snprintf(t, sizeof t, "%s 지우기", nm);
    ask(t, "이 저장을 지워요. 되돌릴 수 없어요.", "지우기", delete_slot, panel.sel);
  }
}
static void confirm_nav(int n) {
  if (n == NAV_LEFT) confirm.focus = 0;
  if (n == NAV_RIGHT) confirm.focus = 1;
  if (n == NAV_BACK) confirm.open = 0;
  if (n == NAV_OK) { confirm.open = 0; if (confirm.focus) confirm.fn(confirm.arg); }
}
static void ctl_open(void) {
  static const int order[16] = {RH_UP, RH_DOWN, RH_LEFT, RH_RIGHT, RH_A, RH_B, RH_X, RH_Y, RH_L, RH_R, RH_L2, RH_R2, RH_L3, RH_R3, RH_START, RH_SELECT};
  static const char *generic[16] = {[RH_UP] = "위", [RH_DOWN] = "아래", [RH_LEFT] = "왼쪽", [RH_RIGHT] = "오른쪽", [RH_A] = "A", [RH_B] = "B",
    [RH_X] = "X", [RH_Y] = "Y", [RH_L] = "L", [RH_R] = "R", [RH_L2] = "L2", [RH_R2] = "R2", [RH_START] = "Start", [RH_SELECT] = "Select"};
  static const char *dirs[4] = {"위", "아래", "왼쪽", "오른쪽"};
  const System *sy = &SYS[cur->sys];
  int has_core = 0; for (int i = 0; i < 20; i++) if (rh_button_name(i)) has_core = 1;
  ctl.n = 0;
  for (int j = 0; j < 16; j++) {
    int a = order[j]; const char *nm = rh_button_name(a);
    if (sy->stick_first && a >= RH_UP && a <= RH_RIGHT) snprintf(ctl.name[ctl.n], 96, "왼쪽 스틱 %s", dirs[a - RH_UP]);   // 방향 키가 왼쪽 스틱으로 감
    else if (nm) snprintf(ctl.name[ctl.n], 96, "%s", nm);
    else if (!has_core && generic[a]) snprintf(ctl.name[ctl.n], 96, "%s", generic[a]);
    else continue;
    ctl.act[ctl.n++] = a;
  }
  if (sy->analog) for (int i = 0; i < 4 && ctl.n < NACT_ROWS; i++) {
    const char *nm = rh_button_name(16 + i);
    snprintf(ctl.name[ctl.n], 96, "%s %s", nm ? nm : "오른쪽 스틱", dirs[i]);
    ctl.act[ctl.n++] = 16 + i;
  }
  ctl.open = 1; ctl.sel = 0; ctl.capture = -1;
}
static void ctl_close(void) { ctl.open = 0; ctl.capture = -1; pmenu.open = 1; pmenu.sel = 2; }   // 일시정지 메뉴로 돌아감(오디오는 멈춘 채)
static void ctl_reset(void) {
  memcpy(keymap[cur->sys], KEY_DEFAULT, sizeof KEY_DEFAULT); keys_save();
  key_mask = 0; rstick_keys = 0; toast("기본 키로 돌렸어요");
}
static void ctl_key(SDL_Scancode k) {
  int a = ctl.capture; ctl.capture = -1;
  if (k == SDL_SCANCODE_ESCAPE) return;
  if (key_reserved(k)) { toast("그 키는 메뉴용이라 못 써요"); return; }
  for (int i = 0; i < NACT; i++) if (keymap[cur->sys][i] == k) keymap[cur->sys][i] = 0;   // 같은 키를 쓰던 동작은 비움
  keymap[cur->sys][a] = k; keys_save();
  key_mask = 0; rstick_keys = 0;
}
static void ctl_nav(int n) {
  if (n == NAV_UP && ctl.sel >= 2) ctl.sel -= 2;
  if (n == NAV_DOWN && ctl.sel + 2 < ctl.n) ctl.sel += 2;
  if (n == NAV_LEFT && ctl.sel > 0) ctl.sel--;
  if (n == NAV_RIGHT && ctl.sel < ctl.n - 1) ctl.sel++;
  if (n == NAV_OK && ctl.n) ctl.capture = ctl.act[ctl.sel];
  if (n == NAV_DEL) ctl_reset();
  if (n == NAV_BACK) ctl_close();
}

static void pmenu_pick(int i) {
  if (i == 0) pmenu_open(0);
  else if (i == 1) { pmenu.open = 0; panel_open(1); }   // 오디오는 저장 목록이 이어서 멈춰 둔다
  else if (i == 2) { pmenu.open = 0; ctl_open(); }
  else { pmenu.open = 0; stop_game(); }
}
static void pmenu_nav(int n) {
  if (n == NAV_UP && pmenu.sel > 0) pmenu.sel--;
  if (n == NAV_DOWN && pmenu.sel < NPMENU - 1) pmenu.sel++;
  if (n == NAV_OK) pmenu_pick(pmenu.sel);
  if (n == NAV_BACK) pmenu_open(0);
}
static void nav(int n) {
  if (confirm.open) { confirm_nav(n); return; }
  if (cmenu.open) { if (n == NAV_BACK) cmenu.open = 0; return; }
  if (screen == LIB) lib_nav(n);
  else if (panel.open) panel_nav(n);
  else if (ctl.open) ctl_nav(n);
  else if (pmenu.open) pmenu_nav(n);
  else if (n == NAV_BACK) pmenu_open(1);
}

// ---------------------------------------------------------------- 화면 그리기

static void draw_bg(void) {
  SDL_Vertex v[4]; int idx[6] = {0, 1, 2, 0, 2, 3};
  SDL_Color a = {255, 238, 247, 255}, b = {237, 235, 255, 255}, c = {230, 244, 255, 255};
  v[0].position = (SDL_FPoint){0, 0}; v[0].color = a;
  v[1].position = (SDL_FPoint){W, 0}; v[1].color = b;
  v[2].position = (SDL_FPoint){W, H}; v[2].color = c;
  v[3].position = (SDL_FPoint){0, H}; v[3].color = b;
  for (int i = 0; i < 4; i++) v[i].tex_coord = (SDL_FPoint){0, 0};
  SDL_RenderGeometry(ren, NULL, v, 4, idx, 6);
}

static int btn(const char *label, float x, float y, float w, float h, int filled, int enabled) {
  int over = enabled && hit(x, y, w, h);
  SDL_Color soft = rgba(237, 232, 255, 255);
  if (filled) rrect(x, y, w, h, h / 2, PINK, SKY, 3); else rrect(x, y, w, h, h / 2, soft, soft, 0);
  if (!enabled) rrect(x, y, w, h, h / 2, rgba(255, 255, 255, 150), rgba(0, 0, 0, 0), 0);
  if (over) rrect(x, y, w, h, h / 2, rgba(255, 255, 255, 50), rgba(0, 0, 0, 0), 0);
  TextEntry *e = text_tex(label, 0, (int)(13 * S + .5f));
  if (e) text(label, x + w / 2, y + (h - e->h) / 2, 13, 0, filled ? rgba(255, 255, 255, 255) : rgba(91, 51, 191, enabled ? 255 : 120), 1);
  return over;
}

static void draw_library(void) {
  draw_bg();
  lib_layout();
  text_accent("레트로 에뮬레이터", 28 * S, 28 * S, 30);
  float bw = 112 * S, bh = 38 * S, bx = W - 28 * S - bw, by = 30 * S;
  rrect(bx, by, bw, bh, bh / 2, PINK, SKY, 3);
  SDL_SetRenderDrawColor(ren, 255, 255, 255, 255);
  SDL_FRect pl[2] = {{bx + 18 * S, by + bh / 2 - 1.2f * S, 12 * S, 2.4f * S}, {bx + 24 * S - 1.2f * S, by + bh / 2 - 6 * S, 2.4f * S, 12 * S}};
  SDL_RenderFillRectsF(ren, pl, 2);
  TextEntry *te = text_tex("롬 추가", 0, (int)(14 * S + .5f));
  if (te) text("롬 추가", bx + 38 * S, by + (bh - te->h) / 2, 14, 0, rgba(255, 255, 255, 255), 0);
  {  // 동기화 버튼(롬 추가 왼쪽)
    const char *l = sync_enabled() ? "동기화 켜짐" : "동기화";
    float sw = text_w(l, 14, 0) + 32 * S, sx = bx - 10 * S - sw;
    SDL_Color wc = rgba(255, 255, 255, 220);
    rrect(sx, by, sw, bh, bh / 2, wc, wc, 0);
    TextEntry *t2 = text_tex(l, 0, (int)(14 * S + .5f));
    if (t2) text(l, sx + sw / 2, by + (bh - t2->h) / 2, 14, 0, rgba(91, 51, 191, 255), 1);
  }

  if (!ngames) {
    text("롬 파일을 끌어다 놓거나 롬 추가를 눌러요", W / 2, H / 2 - 20 * S, 17, 0, rgba(0, 0, 0, 180), 1);
    text("패미컴 · 게임보이 · 게임보이 컬러 · 게임보이 어드밴스 · 슈퍼패미컴 · 메가드라이브 · 플레이스테이션",
         W / 2, H / 2 + 14 * S, 13, 0, rgba(0, 0, 0, 120), 1);
    return;
  }
  int rows = (ngames + lib_cols - 1) / lib_cols;
  float content = rows * (card_h + gap) - gap, view = H - lib_y0 - 20 * S;
  if (scroll_y > content - view) scroll_y = content - view; if (scroll_y < 0) scroll_y = 0;
  SDL_Rect clip = {0, (int)(lib_y0 - 8 * S), W, H - (int)(lib_y0 - 8 * S)};
  SDL_RenderSetClipRect(ren, &clip);
  for (int i = 0; i < ngames; i++) {
    Game *g = &games[i]; const System *s = &SYS[g->sys];
    float x = lib_x0 + (i % lib_cols) * (card_w + gap), y = lib_y0 + (i / lib_cols) * (card_h + gap) - scroll_y;
    if (y + card_h < clip.y || y > H) continue;
    int over = hit(x, y, card_w, card_h) && mouse_y >= clip.y;
    float lift = (over || i == sel) ? 3 * S : 0;
    y -= lift;
    shadow(x, y, card_w, card_h, 20 * S, 40);
    rrect(x, y, card_w, card_h, 20 * S, s->c1, s->c2, 2);
    if (i == sel) {
      SDL_Color wc = rgba(255, 255, 255, 255);
      rrect(x - 3 * S, y - 3 * S, card_w + 6 * S, card_h + 6 * S, 23 * S, wc, wc, 0);
      rrect(x, y, card_w, card_h, 20 * S, s->c1, s->c2, 2);
    }
    float tx = x + 14 * S, maxw = card_w - 28 * S;
    // 제목은 두 줄까지, 아래에서 위로 쌓는다
    float lh = 17 * 1.25f * S;
    int two = text_w(g->title, 17, 1) > maxw;
    float ty = y + card_h - 14 * S - (two ? 2 : 1) * lh;
    if (photo[g->sys]) {  // 사진은 오른쪽 위, 기종 글자 위까지만
      float pw = card_w * (s->tall ? 0.40f : 0.70f), ph = (s->tall ? 92 : 78) * S;
      float room = ty - 17 * S - 6 * S - (y + 12 * S); if (ph > room) ph = room;
      draw_fit(photo[g->sys], photo_w[g->sys], photo_h[g->sys], x + card_w - 14 * S - pw, y + 12 * S, pw, ph, 1);
    }
    if (g->locked) {   // 흐리게 + 왼쪽 위 "잠김"
      SDL_Color fog = rgba(150, 150, 170, 120);
      rrect(x, y, card_w, card_h, 20 * S, fog, fog, 0);
      float lw = text_w("잠김", 12, 1) + 34 * S, lh2 = 24 * S, lx = x + 12 * S, ly = y + 12 * S;
      SDL_Color dk = rgba(0, 0, 0, 90);
      rrect(lx, ly, lw, lh2, lh2 / 2, dk, dk, 0);
      // 자물쇠: 고리 + 몸통
      SDL_SetRenderDrawColor(ren, 255, 255, 255, 255);
      SDL_FRect lb = {lx + 10 * S, ly + 10 * S, 10 * S, 8 * S};
      SDL_FRect sh[3] = {{lx + 11.5f * S, ly + 5 * S, 1.8f * S, 6 * S}, {lx + 16.7f * S, ly + 5 * S, 1.8f * S, 6 * S}, {lx + 11.5f * S, ly + 5 * S, 7 * S, 1.8f * S}};
      SDL_RenderFillRectF(ren, &lb); SDL_RenderFillRectsF(ren, sh, 3);
      TextEntry *tl = text_tex("잠김", 1, (int)(12 * S + .5f));
      if (tl) text("잠김", lx + 25 * S, ly + (lh2 - tl->h) / 2, 12, 1, rgba(255, 255, 255, 255), 0);
    }
    text(s->title, tx, ty - 17 * S, 12, 0, rgba(255, 255, 255, 230), 0);
    text_wrap(g->title, tx, ty, maxw, 17, 1, rgba(255, 255, 255, 255), 2);
  }
  SDL_RenderSetClipRect(ren, NULL);
}

static void fmt_time(time_t t, char *out, size_t n) {
  struct tm *tm = localtime(&t);
  int h = tm->tm_hour % 12; if (!h) h = 12;
  snprintf(out, n, "%d월 %d일 %s %d:%02d", tm->tm_mon + 1, tm->tm_mday, tm->tm_hour < 12 ? "오전" : "오후", h, tm->tm_min);
}

// 저장 목록. 클릭은 mouse_click 이 1일 때 처리
static int mouse_click, mouse_rclick;
static void draw_panel(void) {
  SDL_SetRenderDrawColor(ren, 0, 0, 0, 140); SDL_RenderFillRect(ren, NULL);
  float pw = fminf(W - 56 * S, 860 * S), cols = 4;
  float cg = 12 * S, cw = (pw - 44 * S - cg * (cols - 1)) / cols;
  float thumb_h = cw * 0.75f, ch = thumb_h + 96 * S;
  float ph = 22 * S + 40 * S + 2 * ch + cg + 22 * S;
  if (ph > H - 40 * S) ph = H - 40 * S;
  float px = (W - pw) / 2, py = (H - ph) / 2;
  shadow(px, py, pw, ph, 24 * S, 90);
  SDL_Color a = {255, 238, 247, 255}, b = {230, 244, 255, 255};
  rrect(px, py, pw, ph, 24 * S, a, b, 2);
  text_accent("저장 / 불러오기", px + 22 * S, py + 20 * S, 22);
  text("Enter 불러오기 · S 저장 · Del 지우기 · Esc 닫기", px + pw - 22 * S, py + 27 * S, 12, 0, rgba(0, 0, 0, 115), 2);
  for (int i = 0; i < NSLOT; i++) {
    float x = px + 22 * S + (i % 4) * (cw + cg), y = py + 62 * S + (i / 4) * (ch + cg);
    SDL_Color wc = rgba(255, 255, 255, 220);
    if (i == panel.sel) rrect(x - 3 * S, y - 3 * S, cw + 6 * S, ch + 6 * S, 19 * S, PINK, SKY, 3);
    rrect(x, y, cw, ch, 16 * S, wc, wc, 0);
    float ix = x + 10 * S, iy = y + 10 * S, iw = cw - 20 * S;
    SDL_Color blk = rgba(0, 0, 0, 255);
    rrect(ix, iy, iw, thumb_h - 20 * S + 10 * S, 10 * S, blk, blk, 0);
    float ih = thumb_h - 10 * S;
    if (panel.thumb[i]) draw_fit(panel.thumb[i], panel.tw[i], panel.th[i], ix, iy, iw, ih, 0);
    else text(panel.when[i] ? "화면 없음" : "비어 있음", ix + iw / 2, iy + ih / 2 - 8 * S, 13, 0, rgba(255, 255, 255, 130), 1);
    char nm[32]; slot_name(i, nm);
    text(nm, ix, iy + ih + 8 * S, 14, 1, rgba(0, 0, 0, 205), 0);
    if (panel.when[i]) { char t[64]; fmt_time(panel.when[i], t, sizeof t); text(t, ix + iw, iy + ih + 11 * S, 11, 0, rgba(0, 0, 0, 125), 2); }
    float by = y + ch - 40 * S, bh = 30 * S;
    if (i == 0) {
      if (btn("불러오기", ix, by, iw, bh, 1, panel.when[i] != 0) && mouse_click) { panel.sel = i; panel_try_load(i); mouse_click = 0; }
    } else {
      float hw = (iw - 8 * S) / 2;
      if (btn("저장", ix, by, hw, bh, 0, frames_run > 0) && mouse_click) { panel.sel = i; panel_try_save(i); mouse_click = 0; }
      if (btn("불러오기", ix + hw + 8 * S, by, hw, bh, 1, panel.when[i] != 0) && mouse_click) { panel.sel = i; panel_try_load(i); mouse_click = 0; }
    }
    if (mouse_click && hit(x, y, cw, ch)) { panel.sel = i; mouse_click = 0; }
    if (mouse_rclick && hit(x, y, cw, ch) && !confirm.open) { panel.sel = i; mouse_rclick = 0; panel_nav(NAV_DEL); }
  }
  if (mouse_click && !hit(px, py, pw, ph)) { panel_open(0); mouse_click = 0; }
}

static void draw_pmenu(void) {
  SDL_SetRenderDrawColor(ren, 0, 0, 0, 140); SDL_RenderFillRect(ren, NULL);
  float w = 340 * S, bh = 46 * S, gap = 12 * S, h = 22 * S + 40 * S + NPMENU * bh + (NPMENU - 1) * gap + 22 * S;
  float x = (W - w) / 2, y = (H - h) / 2;
  shadow(x, y, w, h, 24 * S, 90);
  SDL_Color a = {255, 238, 247, 255}, b = {230, 244, 255, 255};
  rrect(x, y, w, h, 24 * S, a, b, 2);
  text_accent("일시정지", x + 22 * S, y + 20 * S, 22);
  text("Esc 이어하기", x + w - 22 * S, y + 27 * S, 12, 0, rgba(0, 0, 0, 115), 2);
  for (int i = 0; i < NPMENU; i++) {
    float by = y + 62 * S + i * (bh + gap), bx = x + 22 * S, bw = w - 44 * S;
    if (i == pmenu.sel) rrect(bx - 3 * S, by - 3 * S, bw + 6 * S, bh + 6 * S, bh / 2 + 3 * S, PINK, SKY, 3);
    int over = btn(PMENU[i], bx, by, bw, bh, i == pmenu.sel, 1);
    if (over) pmenu.sel = i;
    if (over && mouse_click) { mouse_click = 0; pmenu_pick(i); return; }
  }
  if (mouse_click && !hit(x, y, w, h)) { mouse_click = 0; pmenu_open(0); }
}

static void draw_ctl(void) {
  SDL_SetRenderDrawColor(ren, 0, 0, 0, 140); SDL_RenderFillRect(ren, NULL);
  int rows = (ctl.n + 1) / 2;
  float pw = fminf(W - 56 * S, 680 * S), gap = 8 * S, rh = 40 * S;
  float avail = H - 40 * S - (22 + 44 + 56) * S;
  if (rows * (rh + gap) > avail) rh = avail / rows - gap;
  if (rh < 22 * S) rh = 22 * S;
  float ph = 22 * S + 44 * S + rows * (rh + gap) + 56 * S;
  float px = (W - pw) / 2, py = (H - ph) / 2;
  shadow(px, py, pw, ph, 24 * S, 90);
  SDL_Color a = {255, 238, 247, 255}, b = {230, 244, 255, 255};
  rrect(px, py, pw, ph, 24 * S, a, b, 2);
  text_accent("조작법", px + 22 * S, py + 20 * S, 22);
  text(SYS[cur->sys].title, px + 22 * S + text_w("조작법", 22, 1) + 26 * S, py + 27 * S, 15, 1, rgba(0, 0, 0, 140), 0);
  text(ctl.capture >= 0 ? "새 키를 누르세요 · Esc 취소" : "줄을 누르고 새 키 · Del 기본값 · Esc 뒤로", px + pw - 22 * S, py + 27 * S, 12, 0, rgba(0, 0, 0, 115), 2);
  float cw = (pw - 44 * S - 10 * S) / 2;
  for (int i = 0; i < ctl.n; i++) {
    float x = px + 22 * S + (i % 2) * (cw + 10 * S), y = py + 66 * S + (i / 2) * (rh + gap);
    SDL_Color wc = rgba(255, 255, 255, 220);
    if (i == ctl.sel) rrect(x - 3 * S, y - 3 * S, cw + 6 * S, rh + 6 * S, 15 * S, PINK, SKY, 3);
    rrect(x, y, cw, rh, 12 * S, wc, wc, 0);
    int a = ctl.act[i], waiting = ctl.capture == a;
    const char *kn = waiting ? "누르세요" : keymap[cur->sys][a] ? SDL_GetScancodeName(keymap[cur->sys][a]) : "없음";
    float kw = fmaxf(64 * S, text_w(kn, 13, 1) + 24 * S), kh = rh - 12 * S, kx = x + cw - 8 * S - kw, ky = y + 6 * S;
    SDL_Color soft = rgba(237, 232, 255, 255);
    if (waiting) rrect(kx, ky, kw, kh, kh / 2, PINK, SKY, 3); else rrect(kx, ky, kw, kh, kh / 2, soft, soft, 0);
    TextEntry *te = text_tex(kn, 1, (int)(13 * S + .5f));
    if (te) text(kn, kx + kw / 2, ky + (kh - te->h) / 2, 13, 1,
                 waiting ? rgba(255, 255, 255, 255) : keymap[cur->sys][a] ? rgba(91, 51, 191, 255) : rgba(0, 0, 0, 90), 1);
    // 이름이 길면 칸 안에서 자른다
    SDL_Rect clip = {(int)(x + 10 * S), (int)y, (int)(kx - x - 16 * S), (int)rh};
    SDL_RenderSetClipRect(ren, &clip);
    TextEntry *tn = text_tex(ctl.name[i], 0, (int)(13 * S + .5f));
    if (tn) text(ctl.name[i], x + 12 * S, y + (rh - tn->h) / 2, 13, 0, rgba(0, 0, 0, 205), 0);
    SDL_RenderSetClipRect(ren, NULL);
    if (mouse_click && hit(x, y, cw, rh)) { ctl.sel = i; ctl.capture = a; mouse_click = 0; }
  }
  float fy = py + ph - 22 * S - 34 * S;
  text("Esc 메뉴 · F1 저장 · F2 불러오기 · F3 저장 목록 · F11 전체 화면은 고정", px + 22 * S, fy + 9 * S, 12, 0, rgba(0, 0, 0, 115), 0);
  float bw = 120 * S;
  if (btn("기본값으로", px + pw - 22 * S - bw, fy, bw, 34 * S, 0, 1) && mouse_click) { ctl_reset(); mouse_click = 0; }
  if (mouse_click && !hit(px, py, pw, ph)) { mouse_click = 0; ctl_close(); }
}

static void draw_cmenu(void) {
  if (cmenu.i < 0 || cmenu.i >= ngames) { cmenu.open = 0; return; }
  float w = 170 * S, bh = 34 * S, h = 12 * S * 2 + bh * 2 + 8 * S;
  float x = fminf(cmenu.x, W - w - 8 * S), y = fminf(cmenu.y, H - h - 8 * S);
  shadow(x, y, w, h, 16 * S, 80);
  SDL_Color wc = rgba(255, 255, 255, 250);
  rrect(x, y, w, h, 16 * S, wc, wc, 0);
  int i = cmenu.i;
  if (btn(games[i].locked ? "잠금 풀기" : "잠그기", x + 12 * S, y + 12 * S, w - 24 * S, bh, 1, 1) && mouse_click) {
    mouse_click = 0; cmenu.open = 0; toggle_lock(i); return;
  }
  if (btn("목록에서 지우기", x + 12 * S, y + 12 * S + bh + 8 * S, w - 24 * S, bh, 0, 1) && mouse_click) {
    mouse_click = 0; cmenu.open = 0; sel = i; lib_nav(NAV_DEL); return;
  }
  if (mouse_click && !hit(x, y, w, h)) { mouse_click = 0; cmenu.open = 0; }
}

static void draw_confirm(void) {
  SDL_SetRenderDrawColor(ren, 0, 0, 0, 120); SDL_RenderFillRect(ren, NULL);
  float w = 400 * S, h = 190 * S, x = (W - w) / 2, y = (H - h) / 2;
  shadow(x, y, w, h, 20 * S, 90);
  SDL_Color wc = rgba(255, 255, 255, 255);
  rrect(x, y, w, h, 20 * S, wc, wc, 0);
  text(confirm.title, x + 22 * S, y + 20 * S, 18, 1, rgba(0, 0, 0, 220), 0);
  text_wrap(confirm.body, x + 22 * S, y + 54 * S, w - 44 * S, 14, 0, rgba(0, 0, 0, 150), 3);
  float bw = 110 * S, bh = 36 * S, by = y + h - bh - 18 * S;
  float cx = x + w - 22 * S - bw * 2 - 10 * S, ox = x + w - 22 * S - bw;
  if (!confirm.focus) rrect(cx - 3 * S, by - 3 * S, bw + 6 * S, bh + 6 * S, bh / 2 + 3 * S, PINK, SKY, 3);
  if (confirm.focus) rrect(ox - 3 * S, by - 3 * S, bw + 6 * S, bh + 6 * S, bh / 2 + 3 * S, PINK, SKY, 3);
  if (btn("취소", cx, by, bw, bh, 0, 1) && mouse_click) { confirm.open = 0; mouse_click = 0; }
  if (btn(confirm.ok, ox, by, bw, bh, 1, 1) && mouse_click) { confirm.open = 0; mouse_click = 0; confirm.fn(confirm.arg); }
  mouse_click = 0;
}

// ---------------------------------------------------------------- 동기화 설정 창

static struct { int open; char code[32]; char server[300]; int focus, fresh; char status[160]; } syncui;   // focus 0=코드 1=서버, fresh=칸을 막 골라서 전체 선택된 상태
static void sync_focus(int f) { syncui.focus = f; syncui.fresh = 1; }
static void sync_open(int open) {
  syncui.open = open;
  if (open) {
    snprintf(syncui.code, sizeof syncui.code, "%s", sync_code());
    snprintf(syncui.server, sizeof syncui.server, "%s", sync_server());
    syncui.status[0] = 0; syncui.focus = 0; syncui.fresh = 0; SDL_StartTextInput();
  } else SDL_StopTextInput();
}
static void sync_type(const char *t) {   // 코드: 영문·숫자·- 만 대문자로 / 서버: 보이는 글자 아무거나
  if (syncui.fresh) { (syncui.focus ? syncui.server : syncui.code)[0] = 0; syncui.fresh = 0; }   // 고른 칸에 처음 치면 새로 씀
  if (syncui.focus == 1) {
    for (; *t; t++) { size_t n = strlen(syncui.server); if (*t > ' ' && *t < 127 && n + 1 < sizeof syncui.server) { syncui.server[n] = *t; syncui.server[n + 1] = 0; } }
    return;
  }
  for (; *t; t++) {
    char c = *t; if (c >= 'a' && c <= 'z') c -= 32;
    if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-')) continue;
    size_t n = strlen(syncui.code); if (n + 1 < 20) { syncui.code[n] = c; syncui.code[n + 1] = 0; }
  }
}
static void sync_now(void) {
  snprintf(syncui.status, sizeof syncui.status, "서버와 맞추는 중");
  if (!sync_check()) { snprintf(syncui.status, sizeof syncui.status, "동기화 서버에 못 닿았어요"); return; }
  int total = 0, bad = 0;
  for (int i = 0; i < ngames; i++) { int c = sync_game(saves_dir, games[i].name, 1); if (c < 0) bad = 1; else total += c; }
  if (bad) snprintf(syncui.status, sizeof syncui.status, "동기화 서버에 못 닿았어요");
  else if (!total) snprintf(syncui.status, sizeof syncui.status, "이미 맞춰져 있어요");
  else snprintf(syncui.status, sizeof syncui.status, "파일 %d개를 주고받았어요", total);
}
static void sync_apply(void) {
  if (strlen(syncui.code) < 4) return;
  sync_set_code(syncui.code);
  sync_set_server(syncui.server);
  snprintf(syncui.server, sizeof syncui.server, "%s", sync_server());
  sync_now();
}
static void fmt_time(time_t t, char *out, size_t n);
static void draw_sync(void) {
  SDL_SetRenderDrawColor(ren, 0, 0, 0, 120); SDL_RenderFillRect(ren, NULL);
  float w = fminf(W - 40 * S, 540 * S), h = 392 * S, x = (W - w) / 2, y = (H - h) / 2;
  shadow(x, y, w, h, 22 * S, 90);
  SDL_Color a = {255, 238, 247, 255}, b = {230, 244, 255, 255};
  rrect(x, y, w, h, 22 * S, a, b, 2);
  text_accent("기기 간 저장 동기화", x + 22 * S, y + 20 * S, 21);
  if (btn("닫기", x + w - 22 * S - 76 * S, y + 18 * S, 76 * S, 32 * S, 1, 1) && mouse_click) { sync_open(0); mouse_click = 0; return; }
  float ty = y + 62 * S;
  ty = text_wrap("같은 코드를 넣은 기기끼리 슬롯 저장, 자동 저장, 게임 안 세이브를 주고받아요. 게임을 켤 때 받고, 저장하거나 나갈 때 올려요. 롬 파일 이름이 기기마다 같아야 해요.",
                 x + 22 * S, ty, w - 44 * S, 13, 0, rgba(0, 0, 0, 165), 3);
  ty += 12 * S;
  text("동기화 코드", x + 22 * S, ty, 13, 1, rgba(0, 0, 0, 180), 0);
  ty += 22 * S;
  float fw = w - 44 * S - 98 * S, fh = 40 * S;
  SDL_Color wc = rgba(255, 255, 255, 235);
  rrect(x + 22 * S, ty, fw, fh, 10 * S, wc, wc, 0);
  if (syncui.focus == 0) rrect(x + 22 * S, ty + fh - 3 * S, fw, 3 * S, 1.5f * S, PINK, SKY, 1);
  if (mouse_click && hit(x + 22 * S, ty, fw, fh)) { sync_focus(0); mouse_click = 0; }
  const char *caret = (SDL_GetTicks() / 500) % 2 ? "|" : " ";
  char shown[40]; snprintf(shown, sizeof shown, "%s%s", syncui.code, syncui.focus == 0 && !syncui.fresh ? caret : "");
  if (syncui.focus == 0 && syncui.fresh && syncui.code[0]) { SDL_Color sel = rgba(155, 92, 255, 60); rrect(x + 30 * S, ty + 7 * S, text_w(syncui.code, 18, 1) + 8 * S, fh - 14 * S, 6 * S, sel, sel, 0); }
  if (syncui.code[0]) text(shown, x + 34 * S, ty + 9 * S, 18, 1, rgba(0, 0, 0, 220), 0);
  else text("예: K7PM-2QXA", x + 34 * S, ty + 10 * S, 16, 0, rgba(0, 0, 0, 90), 0);
  if (btn("새 코드", x + w - 22 * S - 88 * S, ty + 4 * S, 88 * S, 32 * S, 0, 1) && mouse_click) { char c[10]; sync_new_code(c); snprintf(syncui.code, sizeof syncui.code, "%s", c); mouse_click = 0; }
  ty += fh + 8 * S;
  ty = text_wrap("처음 쓰는 기기에서 새 코드를 만들고, 다른 기기엔 그 코드를 그대로 넣어요. 코드를 아는 사람은 저장을 볼 수 있으니 남한테 알려주지 마세요. Ctrl+V 로 붙여 넣을 수 있어요.",
                 x + 22 * S, ty, w - 44 * S, 11, 0, rgba(0, 0, 0, 120), 2);
  ty += 10 * S;
  // 서버 주소
  text("서버 주소", x + 22 * S, ty, 13, 1, rgba(0, 0, 0, 180), 0);
  ty += 22 * S;
  float sh = 34 * S, sw2 = w - 44 * S - 132 * S;
  rrect(x + 22 * S, ty, sw2, sh, 10 * S, wc, wc, 0);
  if (syncui.focus == 1) rrect(x + 22 * S, ty + sh - 3 * S, sw2, 3 * S, 1.5f * S, PINK, SKY, 1);
  if (mouse_click && hit(x + 22 * S, ty, sw2, sh)) { sync_focus(1); mouse_click = 0; }
  {  // 길면 뒤쪽이 보이게
    char sv[340]; snprintf(sv, sizeof sv, "%s%s", syncui.server, syncui.focus == 1 && !syncui.fresh ? caret : "");
    if (syncui.focus == 1 && syncui.fresh && syncui.server[0]) { SDL_Color sel = rgba(155, 92, 255, 60); rrect(x + 28 * S, ty + 6 * S, fminf(text_w(syncui.server, 13, 0), sw2 - 20 * S) + 8 * S, sh - 12 * S, 5 * S, sel, sel, 0); }
    const char *p = sv; while (*p && text_w(p, 13, 0) > sw2 - 20 * S) { p++; while (((unsigned char)*p & 0xC0) == 0x80) p++; }
    text(p, x + 32 * S, ty + 8 * S, 13, 0, rgba(0, 0, 0, 210), 0);
  }
  if (btn("파이(테일스케일)", x + w - 22 * S - 124 * S, ty + 1 * S, 124 * S, 32 * S, 0, 1) && mouse_click) {
    snprintf(syncui.server, sizeof syncui.server, "%s", SYNC_DEFAULT_SERVER); mouse_click = 0;
  }
  ty += sh + 6 * S;
  ty = text_wrap("같은 와이파이면 서버를 깐 맥의 주소(맥 앱 동기화 창에 나와요)를, 테일스케일을 쓰면 파이 주소를 넣어요. Tab 으로 칸을 바꿔요.",
                 x + 22 * S, ty, w - 44 * S, 11, 0, rgba(0, 0, 0, 120), 2);
  ty += 10 * S;
  int same = sync_enabled() && !strcmp(sync_code(), syncui.code) && !strcmp(sync_server(), syncui.server);
  float bx = x + 22 * S, bh = 34 * S;
  if (btn(same ? "저장됨" : "저장", bx, ty, 104 * S, bh, 1, strlen(syncui.code) >= 4) && mouse_click) { mouse_click = 0; sync_apply(); }
  bx += 112 * S;
  if (btn("지금 맞추기", bx, ty, 112 * S, bh, 0, sync_enabled()) && mouse_click) { mouse_click = 0; sync_now(); }
  bx += 120 * S;
  if (sync_enabled() && btn("끄기", bx, ty, 70 * S, bh, 0, 1) && mouse_click) {
    mouse_click = 0; sync_set_code(""); syncui.code[0] = 0; snprintf(syncui.status, sizeof syncui.status, "동기화를 껐어요");
  }
  ty += bh + 14 * S;
  char st[200];
  if (syncui.status[0]) snprintf(st, sizeof st, "%s", syncui.status);
  else if (sync_last()) { char t[64]; fmt_time(sync_last(), t, sizeof t); snprintf(st, sizeof st, "마지막 동기화: %s", t); }
  else snprintf(st, sizeof st, "아직 동기화한 적 없어요");
  text(st, x + 22 * S, ty, 12, 1, rgba(0, 0, 0, 140), 0);
  if (mouse_click && !hit(x, y, w, h)) sync_open(0);
  mouse_click = 0;
}

static void draw_game(void) {
  SDL_SetRenderDrawColor(ren, 0, 0, 0, 255); SDL_RenderClear(ren);
  if (game_tex) {
    double a = rh_aspect(); if (a <= 0) a = (double)game_w / game_h;
    float w = W, h = W / a; if (h > H) { h = H; w = H * a; }
    SDL_FRect d = {(W - w) / 2, (H - h) / 2, w, h};
    game_rect = d;
    SDL_RenderCopyF(ren, game_tex, NULL, &d);
  }
  if (panel.open) draw_panel();
  if (pmenu.open) draw_pmenu();
  if (ctl.open) draw_ctl();
}

static void draw_toast(void) {
  if (SDL_GetTicks() >= toast_until || !toast_msg[0]) return;
  float tw = text_w(toast_msg, 14, 0), w = tw + 32 * S, h = 34 * S, x = (W - w) / 2, y = 16 * S;
  Uint32 left = toast_until - SDL_GetTicks(); int alpha = left < 300 ? left * 255 / 300 : 255;
  SDL_Color p = PINK, s = SKY; p.a = s.a = alpha;
  rrect(x, y, w, h, h / 2, p, s, 1);
  TextEntry *e = text_tex(toast_msg, 0, (int)(14 * S + .5f));
  if (e) text(toast_msg, W / 2, y + (h - e->h) / 2, 14, 0, rgba(255, 255, 255, alpha), 1);
}

// ---------------------------------------------------------------- 시험용 스크립트(--script "wait:60,key:Return,shot:a.png,quit")

static char *script; static int script_wait;
static void save_shot(const char *path) {
  SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, W, H, 32, SDL_PIXELFORMAT_RGBA32);
  SDL_RenderReadPixels(ren, NULL, SDL_PIXELFORMAT_RGBA32, s->pixels, s->pitch);
  size_t n = 0; void *ctx[2] = {NULL, &n};
  stbi_write_png_to_func(png_cb, ctx, W, H, 4, s->pixels, s->pitch);
  if (ctx[0]) { write_all(path, ctx[0], n); free(ctx[0]); }
  SDL_FreeSurface(s);
}
static int script_step(int *quit) {
  if (!script || !*script) return 0;
  if (script_wait > 0) { script_wait--; return 0; }
  char *end = strchr(script, ','); if (end) *end = 0;
  char *cmd = script; script = end ? end + 1 : script + strlen(script);
  if (!strncmp(cmd, "wait:", 5)) script_wait = atoi(cmd + 5);
  else if (!strncmp(cmd, "shot:", 5)) save_shot(cmd + 5);
  else if (!strcmp(cmd, "quit")) *quit = 1;
  else if (!strncmp(cmd, "type:", 5)) { if (syncui.open) sync_type(cmd + 5); }   // 글자 입력 이벤트를 흉내 내면 sdl2-compat 에서 죽어서 바로 넣음
  else if (!strncmp(cmd, "key:", 4) || !strncmp(cmd, "down:", 5) || !strncmp(cmd, "up:", 3)) {
    int down = cmd[0] != 'u', up = cmd[0] != 'd';
    SDL_Scancode sc = SDL_GetScancodeFromName(strchr(cmd, ':') + 1);
    SDL_Event e = {0}; e.type = SDL_KEYDOWN; e.key.keysym.scancode = sc; e.key.keysym.sym = SDL_GetKeyFromScancode(sc);
    if (down) SDL_PushEvent(&e);
    if (up) { e.type = SDL_KEYUP; SDL_PushEvent(&e); }
  } else if (!strncmp(cmd, "click:", 6)) {   // 논리 좌표(배율 전)
    float x, y; sscanf(cmd + 6, "%f:%f", &x, &y);
    SDL_Event e = {0}; e.type = SDL_MOUSEBUTTONDOWN; e.button.button = SDL_BUTTON_LEFT; e.button.x = (int)x; e.button.y = (int)y;
    SDL_PushEvent(&e);
  }
  return 1;
}

// ---------------------------------------------------------------- main

static void join(char *out, size_t n, const char *a, const char *b) { snprintf(out, n, "%s%s", a, b); }

int main(int argc, char **argv) {
  SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
  SDL_SetHint(SDL_HINT_VIDEO_ALLOW_SCREENSAVER, "0");
#ifdef _WIN32
  SetProcessDPIAware();
#endif
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0) { fprintf(stderr, "%s\n", SDL_GetError()); return 1; }
  TTF_Init();

  char *bp = SDL_GetBasePath(); snprintf(base_dir, sizeof base_dir, "%s", bp ? bp : "./"); SDL_free(bp);
#ifdef _WIN32
  char *pp = SDL_GetPrefPath(NULL, "RetroEmu");
#else
  char *pp = SDL_GetPrefPath(NULL, "RetroEmuSDL");   // 맥 시험용: 맥 앱과 따로
#endif
  snprintf(data_dir, sizeof data_dir, "%s", pp); SDL_free(pp);
  join(roms_dir, sizeof roms_dir, data_dir, "roms"); join(saves_dir, sizeof saves_dir, data_dir, "saves");
  join(sys_dir, sizeof sys_dir, data_dir, "system"); join(cores_dir, sizeof cores_dir, base_dir, "cores");
  mkdir_u8(roms_dir); mkdir_u8(saves_dir); mkdir_u8(sys_dir);
  sync_init(data_dir);
  rh_set_gl_provider(&sdl_gl);

  char *start_rom = NULL; char *add_list[256]; int nadd = 0; const char *shot_size = NULL;
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--cores") && i + 1 < argc) snprintf(cores_dir, sizeof cores_dir, "%s", argv[++i]);
    else if (!strcmp(argv[i], "--rom") && i + 1 < argc) start_rom = argv[++i];
    else if (!strcmp(argv[i], "--script") && i + 1 < argc) script = strdup(argv[++i]);
    else if (!strcmp(argv[i], "--size") && i + 1 < argc) shot_size = argv[++i];
    else if (argv[i][0] != '-' && nadd < 256) add_list[nadd++] = argv[i];   // 롬을 exe 에 끌어다 놓기
  }

  int ww = 1100, wh = 760; if (shot_size) sscanf(shot_size, "%dx%d", &ww, &wh);
  win = SDL_CreateWindow("레트로 에뮬레이터", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, ww, wh,
                         SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
  SDL_SetWindowMinimumSize(win, 640, 480);
  ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
  if (!ren) ren = SDL_CreateRenderer(win, -1, 0);
  SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
  {  // 창 아이콘
    char p[2100]; snprintf(p, sizeof p, "%sicon.png", base_dir);
    size_t n; void *b = read_all(p, &n);
    if (b) { int w, h, c; unsigned char *px = stbi_load_from_memory(b, (int)n, &w, &h, &c, 4);
      if (px) { SDL_Surface *s = SDL_CreateRGBSurfaceWithFormatFrom(px, w, h, 32, w * 4, SDL_PIXELFORMAT_RGBA32); SDL_SetWindowIcon(win, s); SDL_FreeSurface(s); stbi_image_free(px); }
      free(b); }
  }
  for (int i = 0; i < NSYS; i++) {
    char p[2100]; snprintf(p, sizeof p, "%sconsoles/%s.png", base_dir, SYS[i].id);
    photo[i] = load_png(p, &photo_w[i], &photo_h[i]);
  }

  if (nadd) add_files(add_list, nadd);
  keys_load();
  load_library();
  if (start_rom) {
    add_rom(start_rom); load_library();
    for (int i = 0; i < ngames; i++) if (!strcmp(games[i].name, base_name(start_rom))) { sel = i; start_game(&games[i]); }
  }

  int quit = 0, lw = 0, lh = 0;
  Uint32 nav_repeat_at = 0; int nav_held = NAV_NONE;
  while (!quit) {
    SDL_GetRendererOutputSize(ren, &W, &H);
    SDL_GetWindowSize(win, &lw, &lh);
    S = lw ? (float)W / lw : 1;
    float ui = fminf(1.0f, fmaxf(0.75f, lh / 760.0f));   // 작은 창이면 조금 줄임
    S *= ui;
    float mscale = lw ? (float)W / lw : 1;

    SDL_Event e;
    mouse_click = 0; mouse_rclick = 0;
    while (SDL_PollEvent(&e)) {
      switch (e.type) {
      case SDL_QUIT: quit = 1; break;
      case SDL_DROPFILE: { char *p = e.drop.file; add_files(&p, 1); SDL_free(p); break; }
      case SDL_MOUSEMOTION:
        mouse_x = e.motion.x * mscale; mouse_y = e.motion.y * mscale;
        if (screen == PLAY && cur && SYS[cur->sys].touch && (e.motion.state & SDL_BUTTON_LMASK) && !panel.open && !pmenu.open && !ctl.open && game_rect.w > 0)
          rh_set_pointer((mouse_x - game_rect.x) / game_rect.w, (mouse_y - game_rect.y) / game_rect.h, 1);
        break;
      case SDL_MOUSEBUTTONUP:
        if (e.button.button == SDL_BUTTON_LEFT && screen == PLAY) rh_set_pointer(0, 0, 0);
        break;
      case SDL_MOUSEBUTTONDOWN:
        mouse_x = e.button.x * mscale; mouse_y = e.button.y * mscale;
        if (e.button.button == SDL_BUTTON_LEFT) mouse_click = 1;
        // DS 아래 화면: 게임 그림 위를 누르면 터치
        if (e.button.button == SDL_BUTTON_LEFT && screen == PLAY && cur && SYS[cur->sys].touch && !panel.open && !pmenu.open && !ctl.open && !confirm.open && game_rect.w > 0 &&
            mouse_x >= game_rect.x && mouse_x < game_rect.x + game_rect.w && mouse_y >= game_rect.y && mouse_y < game_rect.y + game_rect.h) {
          rh_set_pointer((mouse_x - game_rect.x) / game_rect.w, (mouse_y - game_rect.y) / game_rect.h, 1);
          mouse_click = 0;
        }
        if (e.button.button == SDL_BUTTON_RIGHT && screen == LIB && !confirm.open && !syncui.open) {
          lib_layout();
          for (int i = 0; i < ngames; i++) {
            float x = lib_x0 + (i % lib_cols) * (card_w + gap), y = lib_y0 + (i / lib_cols) * (card_h + gap) - scroll_y;
            if (hit(x, y, card_w, card_h)) { sel = i; cmenu.open = 1; cmenu.i = i; cmenu.x = mouse_x; cmenu.y = mouse_y; break; }
          }
        }
        if (e.button.button == SDL_BUTTON_RIGHT && screen == PLAY && panel.open) mouse_rclick = 1;  // 저장 목록 칸 지우기
        break;
      case SDL_MOUSEWHEEL: if (screen == LIB) scroll_y -= e.wheel.preciseY * 60 * S; break;
      case SDL_CONTROLLERDEVICEADDED: if (!pad) { pad = SDL_GameControllerOpen(e.cdevice.which); if (pad) toast("패드 연결됨 · 셀렉트+스타트 = 메뉴"); } break;
      case SDL_CONTROLLERDEVICEREMOVED:
        if (pad && e.cdevice.which == SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pad))) { SDL_GameControllerClose(pad); pad = NULL; pad_mask = 0; toast("패드 끊김"); }
        break;
      case SDL_CONTROLLERBUTTONDOWN: case SDL_CONTROLLERBUTTONUP: {
        int down = e.type == SDL_CONTROLLERBUTTONDOWN, b = e.cbutton.button;
        int in_game = screen == PLAY && !panel.open && !pmenu.open && !ctl.open && !confirm.open;
        if (in_game) {
          int bit = pad_bit(b);
          if (bit >= 0) { if (down) pad_mask |= 1u << bit; else pad_mask &= ~(1u << bit); }
          uint32_t combo = (1u << RH_SELECT) | (1u << RH_START);
          if ((down && (pad_mask & combo) == combo) || (down && b == SDL_CONTROLLER_BUTTON_GUIDE)) { pad_mask = 0; pmenu_open(1); }
        } else if (down) {
          int n = NAV_NONE;
          if (b == SDL_CONTROLLER_BUTTON_DPAD_UP) n = NAV_UP; if (b == SDL_CONTROLLER_BUTTON_DPAD_DOWN) n = NAV_DOWN;
          if (b == SDL_CONTROLLER_BUTTON_DPAD_LEFT) n = NAV_LEFT; if (b == SDL_CONTROLLER_BUTTON_DPAD_RIGHT) n = NAV_RIGHT;
          if (b == SDL_CONTROLLER_BUTTON_A) n = NAV_OK; if (b == SDL_CONTROLLER_BUTTON_B) n = NAV_BACK;
          if (b == SDL_CONTROLLER_BUTTON_X) n = NAV_ALT; if (b == SDL_CONTROLLER_BUTTON_Y) n = NAV_DEL;
          if (b == SDL_CONTROLLER_BUTTON_GUIDE && screen == PLAY) n = NAV_BACK;
          if (n && !syncui.open) nav(n);
        }
        break;
      }
      case SDL_TEXTINPUT: if (syncui.open) sync_type(e.text.text); break;
      case SDL_KEYDOWN: case SDL_KEYUP: {
        int down = e.type == SDL_KEYDOWN; SDL_Scancode k = e.key.keysym.scancode;
        if (syncui.open) {
          if (!down) break;
          char *fld = syncui.focus ? syncui.server : syncui.code;
          size_t n = strlen(fld);
          if (k == SDL_SCANCODE_ESCAPE) sync_open(0);
          else if (k == SDL_SCANCODE_TAB) sync_focus(!syncui.focus);
          else if (k == SDL_SCANCODE_BACKSPACE && syncui.fresh) { fld[0] = 0; syncui.fresh = 0; }
          else if (k == SDL_SCANCODE_BACKSPACE && n) fld[n - 1] = 0;
          else if (k == SDL_SCANCODE_RETURN || k == SDL_SCANCODE_KP_ENTER) { syncui.fresh = 0; sync_apply(); }
          else if (k == SDL_SCANCODE_A && (e.key.keysym.mod & (KMOD_CTRL | KMOD_GUI))) syncui.fresh = 1;
          else if (k == SDL_SCANCODE_V && (e.key.keysym.mod & (KMOD_CTRL | KMOD_GUI))) { char *c = SDL_GetClipboardText(); if (c) { sync_type(c); SDL_free(c); } }
          break;
        }
        if (ctl.open && ctl.capture >= 0) { if (down && !e.key.repeat) ctl_key(k); break; }   // 조작법: 새 키 받기
        if (down && (k == SDL_SCANCODE_F11 || (k == SDL_SCANCODE_RETURN && (e.key.keysym.mod & KMOD_ALT)))) { toggle_fullscreen(); break; }
        if (screen == PLAY && !panel.open && !pmenu.open && !ctl.open && !confirm.open) {
          int act = key_action(k);
          if (act >= 16) { int rb = 1 << (act - 16); if (down) rstick_keys |= rb; else rstick_keys &= ~rb; break; }
          if (act >= 0) { if (down) key_mask |= 1u << act; else key_mask &= ~(1u << act); }
          if (down && !e.key.repeat) {
            if (k == SDL_SCANCODE_ESCAPE) pmenu_open(1);
            if (k == SDL_SCANCODE_F1) do_save(quick_slot);
            if (k == SDL_SCANCODE_F2) do_load(quick_slot);
            if (k == SDL_SCANCODE_F3) panel_open(1);
          }
        } else if (down) {
          int n = NAV_NONE;
          if (k == SDL_SCANCODE_UP) n = NAV_UP; if (k == SDL_SCANCODE_DOWN) n = NAV_DOWN;
          if (k == SDL_SCANCODE_LEFT) n = NAV_LEFT; if (k == SDL_SCANCODE_RIGHT) n = NAV_RIGHT;
          if (k == SDL_SCANCODE_RETURN || k == SDL_SCANCODE_KP_ENTER || k == SDL_SCANCODE_SPACE) n = NAV_OK;
          if (k == SDL_SCANCODE_ESCAPE || k == SDL_SCANCODE_F3) n = NAV_BACK;
          if (k == SDL_SCANCODE_S || k == SDL_SCANCODE_F1) n = NAV_ALT;
          if (k == SDL_SCANCODE_DELETE) n = NAV_DEL;
          if (k == SDL_SCANCODE_L && screen == LIB) n = NAV_LOCK;
          if (k == SDL_SCANCODE_O && (e.key.keysym.mod & KMOD_CTRL) && screen == LIB) pick_files();
          if (n) nav(n);
        }
        break;
      }
      case SDL_WINDOWEVENT:
        if (e.window.event == SDL_WINDOWEVENT_MINIMIZED && screen == PLAY && audio) SDL_PauseAudioDevice(audio, 1);
        if (e.window.event == SDL_WINDOWEVENT_RESTORED && screen == PLAY && audio && !panel.open && !pmenu.open && !ctl.open) { SDL_PauseAudioDevice(audio, 0); last_counter = SDL_GetPerformanceCounter(); }
        break;
      }
    }
    // 메뉴 화면에서 패드 스틱으로도 움직이기(꾹 누르면 반복)
    if (pad) {
      update_pad_axes();
      if (!(screen == PLAY && !panel.open && !pmenu.open && !ctl.open && !confirm.open) && !syncui.open) {
        int x = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTX), y = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTY);
        int n = x < -20000 ? NAV_LEFT : x > 20000 ? NAV_RIGHT : y < -20000 ? NAV_UP : y > 20000 ? NAV_DOWN : NAV_NONE;
        Uint32 now = SDL_GetTicks();
        if (n != nav_held) { nav_held = n; if (n) { nav(n); nav_repeat_at = now + 350; } }
        else if (n && now >= nav_repeat_at) { nav(n); nav_repeat_at = now + 120; }
      }
    }

    if (screen == PLAY) run_frames();

    if (screen == LIB) {
      draw_library();
      if (cmenu.open && !confirm.open) draw_cmenu();
      // 왼쪽 클릭: 롬 추가 버튼 / 카드
      if (syncui.open) draw_sync();
      if (mouse_click && !confirm.open && !syncui.open && !cmenu.open) {
        float bw = 112 * S, bh = 38 * S, bx = W - 28 * S - bw, by = 30 * S;
        const char *l = sync_enabled() ? "동기화 켜짐" : "동기화";
        float sw = text_w(l, 14, 0) + 32 * S, sx = bx - 10 * S - sw;
        if (hit(bx, by, bw, bh)) pick_files();
        else if (hit(sx, by, sw, bh)) sync_open(1);
        else if (mouse_y >= lib_y0 - 8 * S)
          for (int i = 0; i < ngames; i++) {
            float x = lib_x0 + (i % lib_cols) * (card_w + gap), y = lib_y0 + (i / lib_cols) * (card_h + gap) - scroll_y;
            if (hit(x, y, card_w, card_h)) { sel = i; lib_open(i); break; }
          }
        mouse_click = 0;
      }
    } else {
      draw_game();
    }
    if (confirm.open) draw_confirm();
    draw_toast();
    script_step(&quit);
    SDL_RenderPresent(ren);
    if (screen == PLAY && (SDL_GetWindowFlags(win) & SDL_WINDOW_MINIMIZED)) SDL_Delay(30);
  }
  stop_game();
  sync_wait(8000);   // 나갈 때 올리던 저장이 끝날 때까지 조금 기다린다
  SDL_Quit();
  return 0;
}
