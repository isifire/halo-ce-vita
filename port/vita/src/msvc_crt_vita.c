/* MSVC C-runtime emulation layer for PlayStation Vita.
 * Implements MSVC CRT functions and Xbox-path aware stdio wrappers.
 */
#define HALO_LINUX_PLATFORM_LAYER 1
#include <xtl.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>

#undef fopen
#undef fprintf
#undef snprintf
#undef sprintf
#undef vsnprintf
#undef vsprintf
#undef printf
#undef vfprintf

#define VITA_PATH_MAX 512

static void translate_vita_path(const char *guest_path, char *vita, unsigned int size) {
    if (!guest_path) { if (size) vita[0] = 0; return; }
    if (strncmp(guest_path, "ux0:", 4) == 0 || strncmp(guest_path, "app0:", 5) == 0 ||
        strncmp(guest_path, "ur0:", 4) == 0 || strncmp(guest_path, "uma0:", 5) == 0) {
        snprintf(vita, size, "%s", guest_path);
        for (char *p = vita; *p; p++) if (*p == '\\') *p = '/';
        return;
    }
    char save_root[48];
    const char *tail = guest_path;
    const char *root = "ux0:data/halo";
    if (guest_path[0] && guest_path[1] == ':') {
        char drive = (char)(guest_path[0] | 32);
        tail = guest_path + 2;
        if (drive != 'd') {
            snprintf(save_root, sizeof(save_root), "ux0:data/halo-vita/%c", drive);
            sceIoMkdir("ux0:data/halo-vita", 0777);
            sceIoMkdir(save_root, 0777);
            root = save_root;
        }
    }
    while (*tail == '/' || *tail == '\\') tail++;
    snprintf(vita, size, "%s/%s", root, tail);
    for (char *p = vita; *p; p++) {
        if (*p == '\\') *p = '/';
    }
    size_t len = strlen(vita);
    while (len > 13 && vita[len-1] == '/') { vita[len-1] = '\0'; len--; }
}

/* ---------- strings */

int _stricmp(const char *s1, const char *s2) {
    for (;; s1++, s2++) {
        int c1 = tolower((unsigned char)*s1);
        int c2 = tolower((unsigned char)*s2);
        if (c1 != c2 || !c1) return c1 - c2;
    }
}

int _strnicmp(const char *s1, const char *s2, size_t n) {
    for (; n; n--, s1++, s2++) {
        int c1 = tolower((unsigned char)*s1);
        int c2 = tolower((unsigned char)*s2);
        if (c1 != c2 || !c1) return c1 - c2;
    }
    return 0;
}

char *_strdup(const char *s) {
    size_t len = strlen(s) + 1;
    char *copy = malloc(len);
    if (copy) memcpy(copy, s, len);
    return copy;
}

char *_strlwr(char *s) {
    for (char *p = s; *p; p++) *p = (char)tolower((unsigned char)*p);
    return s;
}

char *_strupr(char *s) {
    for (char *p = s; *p; p++) *p = (char)toupper((unsigned char)*p);
    return s;
}

static char *u2s(unsigned long val, char *s, int radix, int neg) {
    char buf[36]; int cnt = 0; char *c = s;
    if (radix < 2 || radix > 36) { *s = 0; return s; }
    do {
        int d = (int)(val % (unsigned long)radix);
        buf[cnt++] = (char)(d < 10 ? '0' + d : 'a' + d - 10);
        val /= (unsigned long)radix;
    } while (val);
    if (neg) *c++ = '-';
    while (cnt) *c++ = buf[--cnt];
    *c = 0;
    return s;
}

char *_ltoa(long v, char *s, int r) {
    if (r == 10 && v < 0) return u2s(0UL - (unsigned long)v, s, r, 1);
    return u2s((unsigned long)v, s, r, 0);
}

char *_itoa(int v, char *s, int r) { return _ltoa(v, s, r); }
char *_ultoa(unsigned long v, char *s, int r) { return u2s(v, s, r, 0); }

/* ---------- path splitting */

void _splitpath(const char *path, char *drive, char *dir, char *name, char *ext) {
    const char *cur = path, *last_sep = NULL, *last_dot = NULL, *scan;
    if (path[0] && path[1] == ':') {
        if (drive) { drive[0] = path[0]; drive[1] = ':'; drive[2] = 0; }
        cur += 2;
    } else if (drive) { drive[0] = 0; }
    for (scan = cur; *scan; scan++) {
        if (*scan == '\\' || *scan == '/') last_sep = scan;
        else if (*scan == '.') last_dot = scan;
    }
    if (last_dot && last_sep && last_dot < last_sep) last_dot = NULL;
    if (dir) {
        size_t len = last_sep ? (size_t)(last_sep - cur + 1) : 0;
        if (len >= 256) len = 255;
        memcpy(dir, cur, len); dir[len] = 0;
    }
    cur = last_sep ? last_sep + 1 : cur;
    if (name) {
        size_t len = last_dot ? (size_t)(last_dot - cur) : strlen(cur);
        if (len >= 256) len = 255;
        memcpy(name, cur, len); name[len] = 0;
    }
    if (ext) {
        strncpy(ext, last_dot ? last_dot : "", 255);
        ext[255] = 0;
    }
}

void _makepath(char *path, const char *drive, const char *dir, const char *name, const char *ext) {
    path[0] = 0;
    if (drive && *drive) { strncat(path, drive, 1); strcat(path, ":"); }
    if (dir && *dir) {
        strcat(path, dir);
        size_t len = strlen(path);
        if (path[len - 1] != '\\' && path[len - 1] != '/') strcat(path, "/");
    }
    if (name) strcat(path, name);
    if (ext && *ext) {
        if (*ext != '.') strcat(path, ".");
        strcat(path, ext);
    }
}

char *_fullpath(char *abs, const char *rel, size_t max_len) {
    if (!abs) {
        max_len = 512;
        abs = malloc(max_len);
        if (!abs) return NULL;
    }
    if (strlen(rel) + 1 > max_len) { errno = ERANGE; return NULL; }
    strcpy(abs, rel);
    return abs;
}

/* ---------- floating point control & status */

unsigned int _control87(unsigned int new_val, unsigned int mask) { (void)new_val; (void)mask; return 0; }
unsigned int _controlfp(unsigned int new_val, unsigned int mask) { (void)new_val; (void)mask; return 0; }
unsigned int _statusfp(void) { return 0; }
unsigned int _clearfp(void) { return 0; }

int _isnan(double v) { return isnan(v); }
int _finite(double v) { return isfinite(v); }
double _hypot(double x, double y) { return hypot(x, y); }
double _copysign(double x, double y) { return copysign(x, y); }

long fast_ftol_C(float f) { return (long)f; }

int *_errno(void) { return &errno; }

/* ---------- file descriptors */

int _close(int fd) { return sceIoClose(fd); }
int _read(int fd, void *buf, unsigned int count) { return sceIoRead(fd, buf, count); }
int _write(int fd, const void *buf, unsigned int count) { return sceIoWrite(fd, buf, count); }
int _fstat(int handle, struct _stat *buffer) {
    SceIoStat s;
    if (!buffer) return -1;
    memset(buffer, 0, sizeof(*buffer));
    if (sceIoGetstatByFd(handle, &s) < 0) return -1;
    buffer->st_size = (long)s.st_size;
    buffer->st_mode = SCE_S_ISDIR(s.st_mode) ? _S_IFDIR : _S_IFREG;
    return 0;
}
int _stat(const char *path, struct _stat *buffer) {
    char p[VITA_PATH_MAX];
    SceIoStat s;
    if (!path || !buffer) return -1;
    translate_vita_path(path, p, sizeof(p));
    memset(buffer, 0, sizeof(*buffer));
    if (sceIoGetstat(p, &s) < 0) return -1;
    buffer->st_size = (long)s.st_size;
    buffer->st_mode = SCE_S_ISDIR(s.st_mode) ? _S_IFDIR : _S_IFREG;
    return 0;
}

FILE *_fdopen(int fd, const char *mode) { (void)fd; (void)mode; return NULL; }

/* ---------- formatted I/O */

static const char *trans_fmt(const char *fmt, char *buf, size_t sz) {
    size_t len = 0;
    if (!strstr(fmt, "I64") && !strstr(fmt, "I32")) return fmt;
    for (const char *c = fmt; *c && len + 3 < sz; c++) {
        buf[len++] = *c;
        if (*c != '%' || c[1] == '%') continue;
        if (c[1] == 'I' && c[2] == '6' && c[3] == '4') {
            buf[len++] = 'l'; buf[len++] = 'l'; c += 3;
        } else if (c[1] == 'I' && c[2] == '3' && c[3] == '2') {
            c += 3;
        }
    }
    buf[len] = 0;
    return buf;
}

int halo_linux_sprintf(char *b, const char *f, ...) {
    char tf[1024]; va_list ap; va_start(ap, f);
    int r = vsprintf(b, trans_fmt(f, tf, sizeof(tf)), ap);
    va_end(ap); return r;
}

int halo_linux_snprintf(char *b, size_t n, const char *f, ...) {
    char tf[1024]; va_list ap; va_start(ap, f);
    int r = vsnprintf(b, n, trans_fmt(f, tf, sizeof(tf)), ap);
    va_end(ap); return r;
}

int halo_linux_vsprintf(char *b, const char *f, va_list ap) {
    char tf[1024]; return vsprintf(b, trans_fmt(f, tf, sizeof(tf)), ap);
}

int halo_linux_vsnprintf(char *b, size_t n, const char *f, va_list ap) {
    char tf[1024]; return vsnprintf(b, n, trans_fmt(f, tf, sizeof(tf)), ap);
}

int halo_linux_printf(const char *f, ...) {
    char tf[1024]; va_list ap; va_start(ap, f);
    int r = vprintf(trans_fmt(f, tf, sizeof(tf)), ap);
    va_end(ap); return r;
}

int halo_linux_fprintf(FILE *s, const char *f, ...) {
    char tf[1024]; va_list ap; va_start(ap, f);
    int r = vfprintf(s, trans_fmt(f, tf, sizeof(tf)), ap);
    va_end(ap); return r;
}

int halo_linux_vfprintf(FILE *s, const char *f, va_list ap) {
    char tf[1024]; return vfprintf(s, trans_fmt(f, tf, sizeof(tf)), ap);
}

FILE *halo_linux_fopen(const char *path, const char *mode) {
    char p[VITA_PATH_MAX];
    translate_vita_path(path, p, sizeof(p));
    return fopen(p, mode);
}

FILE *halo_linux_freopen(const char *path, const char *mode, FILE *stream) {
    if (!path) return stream;
    char p[VITA_PATH_MAX];
    translate_vita_path(path, p, sizeof(p));
    return freopen(p, mode, stream);
}

int halo_linux_open(const char *path, int flags, ...) {
    char p[VITA_PATH_MAX];
    int sce_flags = SCE_O_RDONLY;
    translate_vita_path(path, p, sizeof(p));
    if ((flags & 3) == 1) sce_flags = SCE_O_WRONLY;
    else if ((flags & 3) == 2) sce_flags = SCE_O_RDWR;
    if (flags & 0x0100) sce_flags |= SCE_O_CREAT;
    if (flags & 0x0200) sce_flags |= SCE_O_TRUNC;
    return sceIoOpen(p, sce_flags, 0777);
}

int halo_linux_remove(const char *path) {
    char p[VITA_PATH_MAX];
    translate_vita_path(path, p, sizeof(p));
    return sceIoRemove(p);
}
