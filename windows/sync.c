// 저장 동기화(윈도우판). 설명은 sync.h.
#include "sync.h"
#include <SDL.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#endif

static char cfg_path[2100], code_[64], server_[512];
static time_t last_;
static SDL_mutex *lock_;
static SDL_atomic_t active_;

// ---------------------------------------------------------------- SHA-256 (동기화 칸 이름 만들기용)

static const uint32_t K256[64] = {
  0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
  0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
  0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
  0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
  0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
  0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
#define ROR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))
static void sha256_block(uint32_t h[8], const uint8_t *p) {
  uint32_t w[64];
  for (int i = 0; i < 16; i++) w[i] = (uint32_t)p[i * 4] << 24 | p[i * 4 + 1] << 16 | p[i * 4 + 2] << 8 | p[i * 4 + 3];
  for (int i = 16; i < 64; i++) {
    uint32_t s0 = ROR(w[i - 15], 7) ^ ROR(w[i - 15], 18) ^ (w[i - 15] >> 3), s1 = ROR(w[i - 2], 17) ^ ROR(w[i - 2], 19) ^ (w[i - 2] >> 10);
    w[i] = w[i - 16] + s0 + w[i - 7] + s1;
  }
  uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
  for (int i = 0; i < 64; i++) {
    uint32_t t1 = hh + (ROR(e, 6) ^ ROR(e, 11) ^ ROR(e, 25)) + ((e & f) ^ (~e & g)) + K256[i] + w[i];
    uint32_t t2 = (ROR(a, 2) ^ ROR(a, 13) ^ ROR(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
    hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
  }
  h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
}
static void sha256(const void *data, size_t n, uint8_t out[32]) {
  uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
  const uint8_t *p = data; size_t left = n;
  for (; left >= 64; left -= 64, p += 64) sha256_block(h, p);
  uint8_t buf[128] = {0}; memcpy(buf, p, left); buf[left] = 0x80;
  size_t total = left + 9 <= 64 ? 64 : 128;
  uint64_t bits = (uint64_t)n * 8;
  for (int i = 0; i < 8; i++) buf[total - 1 - i] = (uint8_t)(bits >> (i * 8));
  sha256_block(h, buf); if (total == 128) sha256_block(h, buf + 64);
  for (int i = 0; i < 8; i++) { out[i * 4] = h[i] >> 24; out[i * 4 + 1] = h[i] >> 16; out[i * 4 + 2] = h[i] >> 8; out[i * 4 + 3] = h[i]; }
}

void sync_key(const char *code, char out[33]) {
  char s[128]; snprintf(s, sizeof s, "retro-emu:%s", code);
  uint8_t d[32]; sha256(s, strlen(s), d);
  for (int i = 0; i < 16; i++) sprintf(out + i * 2, "%02x", d[i]);
  out[32] = 0;
}

// ---------------------------------------------------------------- 설정

// data_dir 은 UTF-8 이라 윈도우에선 넓은 문자 경로로 연다(사용자 이름이 한글이어도)
static FILE *cfg_open(int write) {
#ifdef _WIN32
  wchar_t w[2100]; MultiByteToWideChar(CP_UTF8, 0, cfg_path, -1, w, 2100);
  return _wfopen(w, write ? L"w" : L"r");
#else
  return fopen(cfg_path, write ? "w" : "r");
#endif
}

static void save_cfg(void) {
  FILE *f = cfg_open(1);
  if (!f) return;
  fprintf(f, "code=%s\nlast=%lld\n", code_, (long long)last_);
  fclose(f);
}

void sync_init(const char *data_dir) {
  if (!lock_) lock_ = SDL_CreateMutex();
  snprintf(cfg_path, sizeof cfg_path, "%ssync.txt", data_dir);
  const char *env = getenv("RETROEMU_SYNC");
  snprintf(server_, sizeof server_, "%s", env && *env ? env : SYNC_DEFAULT_SERVER);
  FILE *f = cfg_open(0);
  if (!f) return;
  char line[256];
  while (fgets(line, sizeof line, f)) {
    line[strcspn(line, "\r\n")] = 0;
    if (!strncmp(line, "code=", 5)) snprintf(code_, sizeof code_, "%s", line + 5);
    if (!strncmp(line, "last=", 5)) last_ = (time_t)atoll(line + 5);
  }
  fclose(f);
}

const char *sync_code(void) { return code_; }
int sync_enabled(void) { return strlen(code_) >= 4; }
time_t sync_last(void) { return last_; }

void sync_set_code(const char *c) {
  while (*c == ' ') c++;
  int n = 0;
  for (; *c && n < (int)sizeof code_ - 1; c++) code_[n++] = (char)toupper((unsigned char)*c);
  while (n && code_[n - 1] == ' ') n--;
  code_[n] = 0;
  save_cfg();
}

void sync_new_code(char out[10]) {
  static const char a[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
  srand((unsigned)time(NULL) ^ (unsigned)SDL_GetPerformanceCounter());
  for (int i = 0; i < 9; i++) out[i] = i == 4 ? '-' : a[rand() % 32];
  out[9] = 0;
}

// ---------------------------------------------------------------- 파일(UTF-8 경로, 수정 시각 ms)

#ifdef _WIN32
static wchar_t *W8(const char *s) { int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0); wchar_t *w = malloc(n * sizeof *w); MultiByteToWideChar(CP_UTF8, 0, s, -1, w, n); return w; }
static long long ft_ms(FILETIME ft) { ULARGE_INTEGER u; u.LowPart = ft.dwLowDateTime; u.HighPart = ft.dwHighDateTime; return (long long)(u.QuadPart / 10000ULL) - 11644473600000LL; }
static void set_mtime(const char *p, long long ms) {
  wchar_t *w = W8(p); HANDLE h = CreateFileW(w, FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL); free(w);
  if (h == INVALID_HANDLE_VALUE) return;
  ULARGE_INTEGER u; u.QuadPart = (unsigned long long)(ms + 11644473600000LL) * 10000ULL;
  FILETIME ft; ft.dwLowDateTime = u.LowPart; ft.dwHighDateTime = u.HighPart;
  SetFileTime(h, NULL, NULL, &ft); CloseHandle(h);
}
static FILE *fopen8(const char *p, const wchar_t *m) { wchar_t *w = W8(p); FILE *f = _wfopen(w, m); free(w); return f; }
static int remove8(const char *p) { wchar_t *w = W8(p); int ok = DeleteFileW(w); free(w); return ok ? 0 : -1; }
static int rename8(const char *a, const char *b) { wchar_t *wa = W8(a), *wb = W8(b); int ok = MoveFileExW(wa, wb, MOVEFILE_REPLACE_EXISTING); free(wa); free(wb); return ok ? 0 : -1; }
#define RB L"rb"
#define WB L"wb"
#else
static long long file_mtime(const char *p) {
  struct stat st; if (stat(p, &st)) return -1;
  return (long long)st.st_mtimespec.tv_sec * 1000 + st.st_mtimespec.tv_nsec / 1000000;
}
static void set_mtime(const char *p, long long ms) {
  struct timeval tv[2]; tv[0].tv_sec = tv[1].tv_sec = ms / 1000; tv[0].tv_usec = tv[1].tv_usec = (ms % 1000) * 1000; utimes(p, tv);
}
static FILE *fopen8(const char *p, const char *m) { return fopen(p, m); }
static int remove8(const char *p) { return remove(p); }
static int rename8(const char *a, const char *b) { return rename(a, b); }
#define RB "rb"
#define WB "wb"
#endif

static void *read_file(const char *p, size_t *n) {
  FILE *f = fopen8(p, RB); if (!f) return NULL;
  fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
  void *b = malloc(sz > 0 ? sz : 1); *n = fread(b, 1, sz, f); fclose(f); return b;
}

// saves 안에서 이 게임 파일(<롬>.*.state|.png, <롬>.srm)만 고른다
typedef struct { char name[300]; long long mtime; } LocalFile;
static int sendable(const char *n, const char *prefix) {
  size_t l = strlen(n);
  if (strncmp(n, prefix, strlen(prefix))) return 0;
  return (l > 6 && !strcmp(n + l - 6, ".state")) || (l > 4 && !strcmp(n + l - 4, ".png")) || (l > 4 && !strcmp(n + l - 4, ".srm"));
}
static int list_local(const char *dir, const char *prefix, LocalFile *out, int max) {
  int n = 0;
#ifdef _WIN32
  char pat[2200]; snprintf(pat, sizeof pat, "%s/*", dir);
  wchar_t *w = W8(pat); WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileW(w, &fd); free(w);
  if (h == INVALID_HANDLE_VALUE) return 0;
  do {
    char nm[600]; WideCharToMultiByte(CP_UTF8, 0, fd.cFileName, -1, nm, sizeof nm, NULL, NULL);
    if (n < max && strlen(nm) < 300 && sendable(nm, prefix)) { strcpy(out[n].name, nm); out[n].mtime = ft_ms(fd.ftLastWriteTime); n++; }
  } while (FindNextFileW(h, &fd));
  FindClose(h);
#else
  DIR *d = opendir(dir); if (!d) return 0;
  struct dirent *e;
  while ((e = readdir(d))) {
    if (n < max && strlen(e->d_name) < 300 && sendable(e->d_name, prefix)) {
      char p[2400]; snprintf(p, sizeof p, "%s/%s", dir, e->d_name);
      strcpy(out[n].name, e->d_name); out[n].mtime = file_mtime(p); n++;
    }
  }
  closedir(d);
#endif
  return n;
}

// ---------------------------------------------------------------- HTTP

typedef struct { char *data; size_t n; } Buf;

static void esc(const char *s, char *out, size_t cap) {
  size_t o = 0;
  for (const unsigned char *p = (const unsigned char *)s; *p && o + 4 < cap; p++) {
    if (isalnum(*p) || *p == '-' || *p == '.' || *p == '_' || *p == '~') out[o++] = *p;
    else o += sprintf(out + o, "%%%02X", *p);
  }
  out[o] = 0;
}

#ifdef _WIN32
// 상태 코드, 실패하면 -1
static int http(const char *method, const char *url, const void *body, size_t blen, long long mtime, Buf *resp, int timeout_ms) {
  int status = -1;
  wchar_t *wurl = W8(url), *wm = W8(method);
  URL_COMPONENTS uc; memset(&uc, 0, sizeof uc); uc.dwStructSize = sizeof uc;
  wchar_t host[256], path[2048];
  uc.lpszHostName = host; uc.dwHostNameLength = 256; uc.lpszUrlPath = path; uc.dwUrlPathLength = 2048;
  wchar_t extra[1024]; uc.lpszExtraInfo = extra; uc.dwExtraInfoLength = 1024;
  HINTERNET s = NULL, c = NULL, r = NULL;
  if (!WinHttpCrackUrl(wurl, 0, 0, &uc)) goto done;
  wcscat(path, extra);
  s = WinHttpOpen(L"RetroEmu/1", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
  if (!s) s = WinHttpOpen(L"RetroEmu/1", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
  if (!s) goto done;
  WinHttpSetTimeouts(s, timeout_ms, timeout_ms, timeout_ms, timeout_ms);
  c = WinHttpConnect(s, host, uc.nPort, 0); if (!c) goto done;
  r = WinHttpOpenRequest(c, wm, path, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0);
  if (!r) goto done;
  wchar_t hdr[64] = L"";
  if (mtime >= 0) swprintf(hdr, 64, L"X-Mtime: %lld\r\n", mtime);
  if (!WinHttpSendRequest(r, hdr[0] ? hdr : WINHTTP_NO_ADDITIONAL_HEADERS, hdr[0] ? (DWORD)-1L : 0, (void *)body, (DWORD)blen, (DWORD)blen, 0)) goto done;
  if (!WinHttpReceiveResponse(r, NULL)) goto done;
  DWORD code = 0, sz = sizeof code;
  WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &code, &sz, WINHTTP_NO_HEADER_INDEX);
  if (resp) {
    DWORD avail;
    while (WinHttpQueryDataAvailable(r, &avail) && avail) {
      resp->data = realloc(resp->data, resp->n + avail + 1);
      DWORD got = 0; if (!WinHttpReadData(r, resp->data + resp->n, avail, &got) || !got) break;
      resp->n += got;
    }
    if (resp->data) resp->data[resp->n] = 0;
  }
  status = (int)code;
done:
  if (r) WinHttpCloseHandle(r); if (c) WinHttpCloseHandle(c); if (s) WinHttpCloseHandle(s);
  free(wurl); free(wm);
  return status;
}
#else
// 맥 점검용 빌드: curl 명령으로
static int http(const char *method, const char *url, const void *body, size_t blen, long long mtime, Buf *resp, int timeout_ms) {
  char tin[64] = "", tout[64], cmd[3000];
  static int seq; int id; SDL_LockMutex(lock_); id = ++seq; SDL_UnlockMutex(lock_);
  snprintf(tout, sizeof tout, "/tmp/retro-sync-%d-%d.out", (int)getpid(), id);
  if (body) {
    snprintf(tin, sizeof tin, "/tmp/retro-sync-%d-%d.in", (int)getpid(), id);
    FILE *f = fopen(tin, "wb"); if (!f) return -1; fwrite(body, 1, blen, f); fclose(f);
  }
  char mh[64] = ""; if (mtime >= 0) snprintf(mh, sizeof mh, "-H 'X-Mtime: %lld'", mtime);
  snprintf(cmd, sizeof cmd, "curl -s -m %d -X %s %s %s%s -o '%s' -w '%%{http_code}' '%s'", timeout_ms / 1000 + 1, method, mh,
           body ? "--data-binary @" : "", body ? tin : "", tout, url);
  FILE *p = popen(cmd, "r"); if (!p) return -1;
  char out[16] = ""; if (!fgets(out, sizeof out, p)) out[0] = 0; pclose(p);
  if (tin[0]) remove(tin);
  int code = atoi(out);
  if (resp) { size_t n; void *d = read_file(tout, &n); if (d) { resp->data = realloc(d, n + 1); resp->data[n] = 0; resp->n = n; } }
  remove(tout);
  return code > 0 ? code : -1;
}
#endif

// ---------------------------------------------------------------- 목록 JSON 읽기 {"files": {"이름": {"mtime": 1, "size": 2, "deleted": false}}}

typedef struct { char name[300]; long long mtime; int deleted; } RemoteFile;

static const char *json_str(const char *p, char *out, size_t cap) {   // p 는 따옴표 다음
  size_t o = 0;
  while (*p && *p != '"') {
    char ch = *p++;
    if (ch == '\\') {
      ch = *p++;
      if (ch == 'u') {   // \uXXXX -> UTF-8 (서버는 한글을 그대로 보내서 거의 안 씀)
        unsigned v = 0; for (int i = 0; i < 4 && *p; i++, p++) v = v * 16 + (isdigit((unsigned char)*p) ? *p - '0' : (tolower((unsigned char)*p) - 'a' + 10));
        if (v < 0x80) { if (o + 1 < cap) out[o++] = (char)v; }
        else if (v < 0x800) { if (o + 2 < cap) { out[o++] = 0xC0 | (v >> 6); out[o++] = 0x80 | (v & 63); } }
        else if (o + 3 < cap) { out[o++] = 0xE0 | (v >> 12); out[o++] = 0x80 | ((v >> 6) & 63); out[o++] = 0x80 | (v & 63); }
        continue;
      }
      if (ch == 'n') ch = '\n'; else if (ch == 't') ch = '\t';
    }
    if (o + 1 < cap) out[o++] = ch;
  }
  out[o] = 0;
  return *p ? p + 1 : p;
}

static int parse_list(const char *js, RemoteFile *out, int max) {
  const char *p = strstr(js, "\"files\""); if (!p) return -1;
  p = strchr(p + 7, '{'); if (!p) return -1; p++;
  int n = 0;
  while (*p) {
    while (*p && *p != '"' && *p != '}') p++;
    if (*p != '"') break;
    RemoteFile rf = {0};
    p = json_str(p + 1, rf.name, sizeof rf.name);
    const char *o = strchr(p, '{'), *e = o ? strchr(o, '}') : NULL;
    if (!o || !e) break;
    const char *m = strstr(o, "\"mtime\""); if (m && m < e) { m = strchr(m, ':'); rf.mtime = atoll(m + 1); }
    const char *d = strstr(o, "\"deleted\""); if (d && d < e) { d = strchr(d, ':') + 1; while (*d == ' ') d++; rf.deleted = *d == 't'; }
    if (n < max) out[n++] = rf;
    p = e + 1;
  }
  return n;
}

// ---------------------------------------------------------------- 맞추기

#define MAXF 64
int sync_game(const char *saves_dir, const char *rom, int pull) {
  if (!sync_enabled()) return 0;
  char key[33], prefix[320], eprefix[1000], url[2400];
  sync_key(code_, key);
  snprintf(prefix, sizeof prefix, "%s.", rom);
  esc(prefix, eprefix, sizeof eprefix);
  snprintf(url, sizeof url, "%s/v1/%s/list?prefix=%s", server_, key, eprefix);
  Buf b = {0};
  int st = http("GET", url, NULL, 0, -1, &b, 6000);
  if (st != 200 || !b.data) { free(b.data); return -1; }
  static RemoteFile rem[MAXF]; static LocalFile loc[MAXF];   // 큰 배열은 정적으로(락 안에서만 씀)
  SDL_LockMutex(lock_);
  int nr = parse_list(b.data, rem, MAXF); free(b.data);
  if (nr < 0) { SDL_UnlockMutex(lock_); return -1; }
  int nl = list_local(saves_dir, prefix, loc, MAXF);
  int changed = 0;
  // 서버 쪽 기준으로 훑고, 서버에 없는 이 기기 파일은 따로
  for (int i = 0; i < nr; i++) {
    LocalFile *l = NULL; for (int j = 0; j < nl; j++) if (!strcmp(loc[j].name, rem[i].name)) l = &loc[j];
    char path[2400], en[1000]; snprintf(path, sizeof path, "%s/%s", saves_dir, rem[i].name); esc(rem[i].name, en, sizeof en);
    snprintf(url, sizeof url, "%s/v1/%s/f/%s", server_, key, en);
    if (pull && (!l || rem[i].mtime > l->mtime + 1000)) {
      if (rem[i].deleted) { if (l && !remove8(path)) changed++; }
      else {
        Buf f = {0};
        if (http("GET", url, NULL, 0, -1, &f, 30000) == 200) {
          char tmp[2420]; snprintf(tmp, sizeof tmp, "%s.sync", path);
          FILE *o = fopen8(tmp, WB);
          if (o) {
            size_t w = f.n ? fwrite(f.data, 1, f.n, o) : 0; fclose(o);
            if (w == f.n) { set_mtime(tmp, rem[i].mtime); if (!rename8(tmp, path)) changed++; } else remove8(tmp);
          }
        }
        free(f.data);
      }
    } else if (l && l->mtime > rem[i].mtime + 1000) {
      size_t n; void *d = read_file(path, &n);
      if (d) { if (http("PUT", url, d, n, l->mtime, NULL, 30000) == 200) changed++; free(d); }
    }
  }
  for (int j = 0; j < nl; j++) {
    int found = 0; for (int i = 0; i < nr; i++) if (!strcmp(loc[j].name, rem[i].name)) found = 1;
    if (found) continue;
    char path[2400], en[1000]; snprintf(path, sizeof path, "%s/%s", saves_dir, loc[j].name); esc(loc[j].name, en, sizeof en);
    snprintf(url, sizeof url, "%s/v1/%s/f/%s", server_, key, en);
    size_t n; void *d = read_file(path, &n);
    if (d) { if (http("PUT", url, d, n, loc[j].mtime, NULL, 30000) == 200) changed++; free(d); }
  }
  last_ = time(NULL); save_cfg();
  SDL_UnlockMutex(lock_);
  return changed;
}

int sync_check(void) {
  char url[700]; snprintf(url, sizeof url, "%s/health", server_);
  return http("GET", url, NULL, 0, -1, NULL, 5000) == 200;
}

typedef struct { char dir[2100], rom[300], slot[16]; int del; } Job;
static int job_run(void *arg) {
  Job *j = arg;
  if (j->del) {
    char key[33], url[2400], name[340], en[1000]; sync_key(code_, key);
    long long now = (long long)time(NULL) * 1000;
    const char *ext[2] = {"state", "png"};
    for (int i = 0; i < 2; i++) {
      snprintf(name, sizeof name, "%s.%s.%s", j->rom, j->slot, ext[i]); esc(name, en, sizeof en);
      snprintf(url, sizeof url, "%s/v1/%s/f/%s", server_, key, en);
      http("DELETE", url, NULL, 0, now, NULL, 6000);
    }
  } else sync_game(j->dir, j->rom, 0);
  free(j);
  SDL_AtomicDecRef(&active_);
  return 0;
}
static void spawn(Job *j) {
  SDL_AtomicIncRef(&active_);
  SDL_Thread *t = SDL_CreateThread(job_run, "sync", j);
  if (t) SDL_DetachThread(t); else { free(j); SDL_AtomicDecRef(&active_); }
}
void sync_push_async(const char *dir, const char *rom) {
  if (!sync_enabled()) return;
  Job *j = calloc(1, sizeof *j); snprintf(j->dir, sizeof j->dir, "%s", dir); snprintf(j->rom, sizeof j->rom, "%s", rom);
  spawn(j);
}
void sync_deleted_async(const char *rom, const char *slot) {
  if (!sync_enabled()) return;
  Job *j = calloc(1, sizeof *j); j->del = 1; snprintf(j->rom, sizeof j->rom, "%s", rom); snprintf(j->slot, sizeof j->slot, "%s", slot);
  spawn(j);
}
void sync_wait(int ms) {
  Uint32 until = SDL_GetTicks() + ms;
  while (SDL_AtomicGet(&active_) > 0 && SDL_GetTicks() < until) SDL_Delay(20);
}
