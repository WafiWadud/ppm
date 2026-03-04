// ============================================================
// ppm.h — Single-header PPM library, zero stdlib dependencies
//
// Usage:
//   #define PPM_IMPLEMENTATION
//   #include "ppm.h"
//
// All I/O is done through raw Linux syscalls (no libc, no headers needed).
// Memory is managed via mmap/munmap syscalls.
//
// Override platform syscall numbers for non-x86-64 targets:
//   #define PPM_SYS_READ   <number>
//   #define PPM_SYS_WRITE  <number>
//   #define PPM_SYS_OPEN   <number>
//   #define PPM_SYS_CLOSE  <number>
//   #define PPM_SYS_MMAP   <number>
//   #define PPM_SYS_MUNMAP <number>
//
// Override the entire memory/IO layer:
//   #define PPM_ALLOC(sz)          your_alloc(sz)
//   #define PPM_DEALLOC(ptr, sz)   your_dealloc(ptr, sz)
//   #define PPM_ERROR(msg)         your_error_handler(msg)
// ============================================================

// ============================================================
// Primitive Types (no stdint.h / limits.h needed)
// ============================================================

typedef unsigned char ppm_u8;
typedef unsigned short ppm_u16;
typedef unsigned int ppm_u32;

// ppm_u64 / ppm_usize — LP64 (Linux/macOS 64-bit) or LLP64 (Win64)
#if defined(_WIN64) || defined(__x86_64__) || defined(__aarch64__) ||          \
    defined(__LP64__) || defined(_LP64)
typedef unsigned long long ppm_u64;
typedef long long ppm_i64;
typedef unsigned long long ppm_usize;
#else
typedef unsigned long ppm_u64;
typedef long ppm_i64;
typedef unsigned long ppm_usize;
#endif

// ============================================================
// File Handle — wraps a raw OS file descriptor (no FILE*)
// ============================================================

typedef struct {
  int fd;
} PPM_FILE;

// Flags for ppm_open (POSIX / Linux values)
#define PPM_O_RDONLY 0
#define PPM_O_WRONLY 1
#define PPM_O_RDWR 2
#define PPM_O_CREAT 0100  /* octal */
#define PPM_O_TRUNC 01000 /* octal */
#define PPM_MODE_644 0644 /* default creation mode */

// ============================================================
// Core Types
// ============================================================

typedef struct {
  ppm_u8 red;
  ppm_u8 green;
  ppm_u8 blue;
} ppm_Pixel_t;

typedef struct {
  ppm_u64 width;
  ppm_u64 height;
  ppm_Pixel_t *data;
} ppm_Image_t;

// ============================================================
// Public API
// ============================================================

PPM_FILE ppm_open(const char *path, int flags, int mode);
void ppm_close(PPM_FILE f);

int ppm_write(const ppm_Image_t *image, PPM_FILE file);
ppm_Image_t *ppm_read(PPM_FILE file);
ppm_Image_t *ppm_create_image(ppm_u64 width, ppm_u64 height);
ppm_Pixel_t *ppm_get_pixel(ppm_Image_t *img, ppm_u64 x, ppm_u64 y);
void ppm_free_image(ppm_Image_t *img);

// ============================================================
// Optional Prefix Stripping
// ============================================================

#if defined(PPM_STRIP_PREFIX)
#define Pixel_t ppm_Pixel_t
#define Image_t ppm_Image_t
#define open_ppm ppm_open
#define close_ppm ppm_close
#define write ppm_write
#define read ppm_read
#define create_image ppm_create_image
#define get_pixel ppm_get_pixel
#define free_image ppm_free_image
#endif

// ============================================================
// IMPLEMENTATION
// ============================================================

#ifdef PPM_IMPLEMENTATION

// ------------------------------------------------------------
// Syscall Numbers (x86-64 Linux defaults)
// ------------------------------------------------------------

#ifndef PPM_SYS_READ
#define PPM_SYS_READ 0
#endif
#ifndef PPM_SYS_WRITE
#define PPM_SYS_WRITE 1
#endif
#ifndef PPM_SYS_OPEN
#define PPM_SYS_OPEN 2
#endif
#ifndef PPM_SYS_CLOSE
#define PPM_SYS_CLOSE 3
#endif
#ifndef PPM_SYS_MMAP
#define PPM_SYS_MMAP 9
#endif
#ifndef PPM_SYS_MUNMAP
#define PPM_SYS_MUNMAP 11
#endif

// ------------------------------------------------------------
// Inline Syscall Shims (x86-64 GCC/Clang inline asm)
// Replace PPM_SYSCALL3 / PPM_SYSCALL6 for other arches.
// ------------------------------------------------------------

#ifndef PPM_SYSCALL3
static ppm_i64 ppm__sc3(ppm_i64 n, ppm_i64 a, ppm_i64 b, ppm_i64 c) {
  ppm_i64 ret;
  __asm__ __volatile__("syscall"
                       : "=a"(ret)
                       : "0"(n), "D"(a), "S"(b), "d"(c)
                       : "rcx", "r11", "memory");
  return ret;
}
#define PPM_SYSCALL3(n, a, b, c)                                               \
  ppm__sc3((ppm_i64)(n), (ppm_i64)(a), (ppm_i64)(b), (ppm_i64)(c))
#endif

#ifndef PPM_SYSCALL6
static ppm_i64 ppm__sc6(ppm_i64 n, ppm_i64 a, ppm_i64 b, ppm_i64 c, ppm_i64 d,
                        ppm_i64 e, ppm_i64 f) {
  ppm_i64 ret;
  register ppm_i64 r10 __asm__("r10") = d;
  register ppm_i64 r8 __asm__("r8") = e;
  register ppm_i64 r9 __asm__("r9") = f;
  __asm__ __volatile__("syscall"
                       : "=a"(ret)
                       : "0"(n), "D"(a), "S"(b), "d"(c), "r"(r10), "r"(r8),
                         "r"(r9)
                       : "rcx", "r11", "memory");
  return ret;
}
#define PPM_SYSCALL6(n, a, b, c, d, e, f)                                      \
  ppm__sc6((ppm_i64)(n), (ppm_i64)(a), (ppm_i64)(b), (ppm_i64)(c),             \
           (ppm_i64)(d), (ppm_i64)(e), (ppm_i64)(f))
#endif

// ------------------------------------------------------------
// mmap flags (Linux)
// ------------------------------------------------------------

#define PPM__PROT_RW 3          /* PROT_READ | PROT_WRITE */
#define PPM__MAP_PRIV_ANON 0x22 /* MAP_PRIVATE | MAP_ANONYMOUS */
#define PPM__MAP_FAILED ((void *)(-1))

// ------------------------------------------------------------
// Memory
// ------------------------------------------------------------

#ifndef PPM_ALLOC
static void *ppm__alloc(ppm_usize bytes) {
  void *p = (void *)PPM_SYSCALL6(PPM_SYS_MMAP, 0, (ppm_i64)bytes, PPM__PROT_RW,
                                 PPM__MAP_PRIV_ANON, -1, 0);
  return (p == PPM__MAP_FAILED) ? (void *)0 : p;
}
#define PPM_ALLOC(sz) ppm__alloc((ppm_usize)(sz))
#endif

#ifndef PPM_DEALLOC
static void ppm__dealloc(void *ptr, ppm_usize bytes) {
  if (ptr)
    PPM_SYSCALL3(PPM_SYS_MUNMAP, (ppm_i64)ptr, (ppm_i64)bytes, 0);
}
#define PPM_DEALLOC(ptr, sz) ppm__dealloc(ptr, (ppm_usize)(sz))
#endif

// ------------------------------------------------------------
// Limits
// ------------------------------------------------------------

#define PPM_U64_MAX ((ppm_u64)(~(ppm_u64)0))
#define PPM_USIZE_MAX ((ppm_usize)(~(ppm_usize)0))

// ------------------------------------------------------------
// Raw I/O
// ------------------------------------------------------------

PPM_FILE ppm_open(const char *path, int flags, int mode) {
  PPM_FILE f;
  f.fd = (int)PPM_SYSCALL3(PPM_SYS_OPEN, (ppm_i64)path, flags, mode);
  return f;
}

void ppm_close(PPM_FILE f) { PPM_SYSCALL3(PPM_SYS_CLOSE, f.fd, 0, 0); }

static ppm_i64 ppm__fd_read(int fd, void *buf, ppm_usize n) {
  return PPM_SYSCALL3(PPM_SYS_READ, fd, (ppm_i64)buf, (ppm_i64)n);
}

// Keeps writing until all bytes are out (handles short writes).
static int ppm__fd_write_all(int fd, const void *buf, ppm_usize n) {
  const ppm_u8 *src = (const ppm_u8 *)buf;
  ppm_usize written = 0;
  while (written < n) {
    ppm_i64 r = PPM_SYSCALL3(PPM_SYS_WRITE, fd, (ppm_i64)(src + written),
                             (ppm_i64)(n - written));
    if (r <= 0)
      return -1;
    written += (ppm_usize)r;
  }
  return 0;
}

// ------------------------------------------------------------
// Error reporting (writes to fd 2 = stderr)
// ------------------------------------------------------------

#ifndef PPM_ERROR
static void ppm__error(const char *msg) {
  static const char prefix[] = "[PPM ERROR] ";
  ppm_usize plen = sizeof(prefix) - 1;
  ppm__fd_write_all(2, prefix, plen);
  // hand-rolled strlen
  ppm_usize mlen = 0;
  while (msg[mlen])
    mlen++;
  ppm__fd_write_all(2, msg, mlen);
  ppm__fd_write_all(2, "\n", 1);
}
#define PPM_ERROR(msg) ppm__error(msg)
#endif

// ------------------------------------------------------------
// Character helpers
// ------------------------------------------------------------

static int ppm__isspace(int c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' ||
         c == '\v';
}

static int ppm__isdigit(int c) { return c >= '0' && c <= '9'; }

static int ppm__strcmp(const char *a, const char *b) {
  while (*a && *a == *b) {
    a++;
    b++;
  }
  return (unsigned char)*a - (unsigned char)*b;
}

// ------------------------------------------------------------
// u64 → decimal string (no sprintf).  buf must be >= 21 bytes.
// Returns number of characters written (not including NUL).
// ------------------------------------------------------------

static int ppm__u64_to_str(ppm_u64 v, char *buf) {
  if (v == 0) {
    buf[0] = '0';
    buf[1] = '\0';
    return 1;
  }
  char tmp[20];
  int i = 0;
  while (v) {
    tmp[i++] = (char)('0' + (v % 10));
    v /= 10;
  }
  for (int j = 0; j < i; j++)
    buf[j] = tmp[i - 1 - j];
  buf[i] = '\0';
  return i;
}

// ------------------------------------------------------------
// Buffered reader (avoids one syscall per byte)
// ------------------------------------------------------------

#define PPM__IBUF 4096

typedef struct {
  int fd;
  ppm_u8 buf[PPM__IBUF];
  int pos;
  int len;
  int eof;
} ppm_Rdr;

static void ppm_rdr_init(ppm_Rdr *r, PPM_FILE f) {
  r->fd = f.fd;
  r->pos = r->len = r->eof = 0;
}

static int ppm_rdr_getc(ppm_Rdr *r) {
  if (r->pos >= r->len) {
    if (r->eof)
      return -1;
    ppm_i64 n = ppm__fd_read(r->fd, r->buf, PPM__IBUF);
    if (n <= 0) {
      r->eof = 1;
      return -1;
    }
    r->len = (int)n;
    r->pos = 0;
  }
  return (int)(ppm_u8)r->buf[r->pos++];
}

static void ppm_rdr_unget(ppm_Rdr *r) {
  if (r->pos > 0)
    r->pos--;
}

// Skip whitespace + '#' comments. Returns 0 on success, -1 on EOF.
static int ppm_rdr_skip_ws(ppm_Rdr *r) {
  int c;
  while ((c = ppm_rdr_getc(r)) != -1) {
    if (ppm__isspace(c))
      continue;
    if (c == '#') {
      while ((c = ppm_rdr_getc(r)) != -1 && c != '\n')
        ;
      continue;
    }
    ppm_rdr_unget(r);
    return 0;
  }
  return -1;
}

// Parse a decimal u64. Returns 0 on success.
static int ppm_rdr_read_u64(ppm_Rdr *r, ppm_u64 *out) {
  if (ppm_rdr_skip_ws(r) != 0)
    return -1;
  int c = ppm_rdr_getc(r);
  if (c == -1 || !ppm__isdigit(c))
    return -1;
  ppm_u64 val = 0;
  while (c != -1 && ppm__isdigit(c)) {
    ppm_u64 d = (ppm_u64)(c - '0');
    if (val > (PPM_U64_MAX - d) / 10)
      return -1; // overflow
    val = val * 10 + d;
    c = ppm_rdr_getc(r);
  }
  if (c != -1)
    ppm_rdr_unget(r);
  *out = val;
  return 0;
}

// Read exactly `n` bytes, draining the internal buffer first.
static int ppm_rdr_read_exact(ppm_Rdr *r, void *dst_v, ppm_usize n) {
  ppm_u8 *dst = (ppm_u8 *)dst_v;
  ppm_usize done = 0;

  // Drain buffered bytes first
  while (done < n && r->pos < r->len)
    dst[done++] = r->buf[r->pos++];

  // Direct syscall for the rest (avoids double-buffering large pixel blobs)
  while (done < n) {
    ppm_i64 got = ppm__fd_read(r->fd, dst + done, n - done);
    if (got <= 0)
      return -1;
    done += (ppm_usize)got;
  }
  return 0;
}

// ------------------------------------------------------------
// Buffered writer
// ------------------------------------------------------------

#define PPM__OBUF 65536

typedef struct {
  int fd;
  ppm_u8 buf[PPM__OBUF];
  ppm_usize pos;
} ppm_Wtr;

static void ppm_wtr_init(ppm_Wtr *w, PPM_FILE f) {
  w->fd = f.fd;
  w->pos = 0;
}

static int ppm_wtr_flush(ppm_Wtr *w) {
  if (w->pos == 0)
    return 0;
  int r = ppm__fd_write_all(w->fd, w->buf, w->pos);
  w->pos = 0;
  return r;
}

static int ppm_wtr_write(ppm_Wtr *w, const void *data, ppm_usize n) {
  const ppm_u8 *src = (const ppm_u8 *)data;
  while (n > 0) {
    ppm_usize space = PPM__OBUF - w->pos;
    ppm_usize chunk = (n < space) ? n : space;
    for (ppm_usize i = 0; i < chunk; i++)
      w->buf[w->pos + i] = src[i];
    w->pos += chunk;
    src += chunk;
    n -= chunk;
    if (w->pos == PPM__OBUF && ppm_wtr_flush(w) != 0)
      return -1;
  }
  return 0;
}

static int ppm_wtr_str(ppm_Wtr *w, const char *s) {
  ppm_usize len = 0;
  while (s[len])
    len++;
  return ppm_wtr_write(w, s, len);
}

// ============================================================
// Image Creation
// ============================================================

ppm_Image_t *ppm_create_image(ppm_u64 width, ppm_u64 height) {
  if (width == 0 || height == 0) {
    PPM_ERROR("Invalid dimensions");
    return (void *)0;
  }
  if (width > PPM_U64_MAX / height) {
    PPM_ERROR("Dimension overflow");
    return (void *)0;
  }

  ppm_u64 total = width * height;
  ppm_usize px_size = (ppm_usize)total * sizeof(ppm_Pixel_t);

  if (total > PPM_USIZE_MAX / sizeof(ppm_Pixel_t)) {
    PPM_ERROR("Allocation size overflow");
    return (void *)0;
  }

  ppm_Image_t *img = (ppm_Image_t *)PPM_ALLOC(sizeof(ppm_Image_t));
  if (!img) {
    PPM_ERROR("Image struct allocation failed");
    return (void *)0;
  }

  img->width = width;
  img->height = height;
  img->data = (ppm_Pixel_t *)PPM_ALLOC(px_size);

  if (!img->data) {
    PPM_ERROR("Pixel buffer allocation failed");
    PPM_DEALLOC(img, sizeof(ppm_Image_t));
    return (void *)0;
  }

  return img;
}

// ============================================================
// Pixel Access
// ============================================================

ppm_Pixel_t *ppm_get_pixel(ppm_Image_t *img, ppm_u64 x, ppm_u64 y) {
  if (!img || x >= img->width || y >= img->height)
    return (void *)0;
  return &img->data[y * img->width + x];
}

// ============================================================
// Free
// ============================================================

void ppm_free_image(ppm_Image_t *img) {
  if (!img)
    return;
  ppm_usize px_size =
      (ppm_usize)(img->width * img->height) * sizeof(ppm_Pixel_t);
  PPM_DEALLOC(img->data, px_size);
  PPM_DEALLOC(img, sizeof(ppm_Image_t));
}

// ============================================================
// Write (P6, 8-bit)
// ============================================================

int ppm_write(const ppm_Image_t *image, PPM_FILE file) {
  if (!image || !image->data) {
    PPM_ERROR("Invalid parameters to ppm_write");
    return -1;
  }

  ppm_Wtr w;
  ppm_wtr_init(&w, file);

  char num[21];

  // Build header: "P6\n<W> <H>\n255\n"
  if (ppm_wtr_str(&w, "P6\n") != 0)
    goto fail;
  ppm__u64_to_str(image->width, num);
  if (ppm_wtr_str(&w, num) != 0)
    goto fail;
  if (ppm_wtr_str(&w, " ") != 0)
    goto fail;
  ppm__u64_to_str(image->height, num);
  if (ppm_wtr_str(&w, num) != 0)
    goto fail;
  if (ppm_wtr_str(&w, "\n255\n") != 0)
    goto fail;

  {
    ppm_usize px_size =
        (ppm_usize)(image->width * image->height) * sizeof(ppm_Pixel_t);
    if (ppm_wtr_write(&w, image->data, px_size) != 0)
      goto fail;
  }

  if (ppm_wtr_flush(&w) != 0)
    goto fail;
  return 0;

fail:
  PPM_ERROR("Failed to write pixel data");
  ppm_wtr_flush(&w); /* best-effort flush on error */
  return -1;
}

// ============================================================
// Read (P6, 8-bit)
// ============================================================

ppm_Image_t *ppm_read(PPM_FILE file) {
  ppm_Rdr r;
  ppm_rdr_init(&r, file);

  // Magic bytes ("P6")
  char magic[3] = {0};
  {
    int c0 = ppm_rdr_getc(&r);
    int c1 = ppm_rdr_getc(&r);
    if (c0 < 0 || c1 < 0) {
      PPM_ERROR("Failed to read magic");
      return (void *)0;
    }
    magic[0] = (char)c0;
    magic[1] = (char)c1;
  }

  if (ppm__strcmp(magic, "P6") != 0) {
    PPM_ERROR("Only P6 (binary PPM) is supported");
    return (void *)0;
  }

  ppm_u64 width, height, maxval;

  if (ppm_rdr_read_u64(&r, &width) != 0 || ppm_rdr_read_u64(&r, &height) != 0 ||
      ppm_rdr_read_u64(&r, &maxval) != 0) {
    PPM_ERROR("Invalid or missing header values");
    return (void *)0;
  }

  if (maxval == 0 || maxval > 255) {
    PPM_ERROR("Only 8-bit PPM (maxval <= 255) is supported");
    return (void *)0;
  }

  // PPM spec: exactly one whitespace byte follows maxval before pixel data
  ppm_rdr_getc(&r);

  ppm_Image_t *img = ppm_create_image(width, height);
  if (!img)
    return (void *)0;

  ppm_usize px_size = (ppm_usize)(width * height) * sizeof(ppm_Pixel_t);

  if (ppm_rdr_read_exact(&r, img->data, px_size) != 0) {
    PPM_ERROR("Failed to read pixel data");
    ppm_free_image(img);
    return (void *)0;
  }

  return img;
}

#endif /* PPM_IMPLEMENTATION */
