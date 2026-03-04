// ============================================================
// ppm.h — Single-header PPM image library
//
// Zero stdlib dependencies. All I/O goes through raw Linux
// syscalls; memory is managed via mmap/munmap. Drop this file
// into any project and it will compile with no libc at all.
//
// QUICK START
// -----------
//   // In exactly ONE .c / .cpp file:
//   #define PPM_IMPLEMENTATION
//   #include "ppm.h"
//
//   // In every other file that needs the API:
//   #include "ppm.h"
//
// BASIC USAGE
// -----------
//   // Create a 256x256 image, paint it red, write it to disk:
//
//   ppm_Image_t *img = ppm_create_image(256, 256);
//
//   for (ppm_u64 y = 0; y < img->height; y++) {
//     for (ppm_u64 x = 0; x < img->width; x++) {
//       ppm_Pixel_t *px = ppm_get_pixel(img, x, y);
//       px->red = 255; px->green = 0; px->blue = 0;
//     }
//   }
//
//   PPM_FILE f = ppm_open("out.ppm",
//                         PPM_O_WRONLY | PPM_O_CREAT | PPM_O_TRUNC,
//                         PPM_MODE_644);
//   ppm_write(img, f);
//   ppm_close(f);
//   ppm_free_image(img);
//
// PORTABILITY
// -----------
//   Tested on x86-64 Linux with GCC and Clang.
//   For other architectures override the syscall numbers and/or
//   shims -- see the "PORTING" section below.
//
// PORTING -- syscall numbers
// --------------------------
//   The defaults match x86-64 Linux. For other targets define
//   any or all of the following before including this header:
//
//     #define PPM_SYS_READ   <number>
//     #define PPM_SYS_WRITE  <number>
//     #define PPM_SYS_OPEN   <number>
//     #define PPM_SYS_CLOSE  <number>
//     #define PPM_SYS_MMAP   <number>
//     #define PPM_SYS_MUNMAP <number>
//
//   To replace the inline-asm syscall shims entirely (e.g. for a
//   non-GCC toolchain or a non-x86 ISA) define both of:
//
//     #define PPM_SYSCALL3(n,a,b,c)         your_3arg_syscall(n,a,b,c)
//     #define PPM_SYSCALL6(n,a,b,c,d,e,f)  your_6arg_syscall(n,a,b,c,d,e,f)
//
// OVERRIDING THE ALLOCATOR
// ------------------------
//   Replace both macros together -- they must be a matched pair:
//
//     #define PPM_ALLOC(sz)          my_malloc(sz)
//     #define PPM_DEALLOC(ptr, sz)   my_free(ptr)   // sz is still passed
//
//   The default allocator uses mmap(MAP_PRIVATE|MAP_ANONYMOUS).
//   The `sz` argument to PPM_DEALLOC is the original allocation size,
//   which is required by munmap(2).
//
// OVERRIDING THE ERROR HANDLER
// -----------------------------
//   By default errors are printed to stderr (fd 2) and execution
//   continues. To hook your own handler:
//
//     #define PPM_ERROR(msg)   my_fatal(msg)
//
//   `msg` is a string literal describing the failure. The handler may
//   longjmp, abort, or do anything you like.
//
// OPTIONAL PREFIX STRIPPING
// -------------------------
//   Define PPM_STRIP_PREFIX before including to expose shorter aliases:
//
//     #define PPM_STRIP_PREFIX
//     #include "ppm.h"
//     // Now you can write: Image_t, Pixel_t, create_image(), ...
//
//   Avoid in headers or library code -- pollutes the global namespace.
//
// FILE FORMAT
// -----------
//   Only P6 (binary PPM) with 8-bit channels (maxval 1-255) is
//   supported for both reading and writing. P3 (ASCII PPM) and
//   16-bit variants are not supported.
// ============================================================

// ============================================================
// Primitive Types
//
// Self-contained integer types -- no <stdint.h> or <limits.h>
// required. Width detection is done via preprocessor predefined
// macros for LP64 (Linux/macOS) and LLP64 (Win64) ABIs.
// ============================================================

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned char ppm_u8;   /* 8-bit unsigned                */
typedef unsigned short ppm_u16; /* 16-bit unsigned               */
typedef unsigned int ppm_u32;   /* 32-bit unsigned               */

/* 64-bit and pointer-width types, detected from ABI macros. */
#if defined(_WIN64) || defined(__x86_64__) || defined(__aarch64__) ||          \
    defined(__LP64__) || defined(_LP64)
typedef unsigned long long ppm_u64;   /* 64-bit unsigned           */
typedef long long ppm_i64;            /* 64-bit signed             */
typedef unsigned long long ppm_usize; /* pointer-width unsigned    */
#else
typedef unsigned long ppm_u64;
typedef long ppm_i64;
typedef unsigned long ppm_usize;
#endif

// ============================================================
// PPM_FILE -- raw OS file descriptor wrapper
//
// A thin struct around a bare `int` fd so that the public API
// does not expose raw integers, making call-site mistakes
// (e.g. passing an fd where an image pointer is expected)
// a compile-time type error rather than a silent bug.
//
// Obtain with ppm_open(). Release with ppm_close().
// Check f.fd >= 0 to detect a failed open.
// ============================================================

typedef struct {
  int fd; /* underlying OS file descriptor; negative means invalid */
} PPM_FILE;

/* ---- open(2) flags (POSIX / Linux ABI values) ---- */

/* Open for reading only. */
#define PPM_O_RDONLY 0
/* Open for writing only. */
#define PPM_O_WRONLY 1
/* Open for both reading and writing. */
#define PPM_O_RDWR 2
/* Create the file if it does not exist. Requires a mode argument. */
#define PPM_O_CREAT 0100 /* octal */
/* Truncate an existing file to zero length on open. */
#define PPM_O_TRUNC 01000 /* octal */
/* Default creation mode: owner rw, group r, other r (unix 0644). */
#define PPM_MODE_644 0644

// ============================================================
// ppm_Pixel_t -- a single RGB pixel stored as 3 packed bytes
//
// Fields are in R-G-B order, matching the P6 on-disk layout.
// The pixel array can therefore be written to / read from disk
// with a single bulk copy -- no byte-swapping or reordering needed.
// ============================================================

typedef struct {
  ppm_u8 red;   /* red channel,   0-255 */
  ppm_u8 green; /* green channel, 0-255 */
  ppm_u8 blue;  /* blue channel,  0-255 */
} ppm_Pixel_t;

// ============================================================
// ppm_Image_t -- an in-memory P6 image
//
// `data` is a flat, row-major pixel array with (width * height)
// elements. The pixel at column x, row y lives at index
// (y * width + x). Use ppm_get_pixel() for bounds-checked access
// rather than indexing `data` directly.
//
// Always allocate via ppm_create_image() and release with
// ppm_free_image(). Never manually free `data` -- the allocator
// may not be malloc/free.
// ============================================================

typedef struct {
  ppm_u64 width;     /* image width  in pixels                          */
  ppm_u64 height;    /* image height in pixels                          */
  ppm_Pixel_t *data; /* row-major pixel buffer; non-NULL if valid image */
} ppm_Image_t;

// ============================================================
// Public API
// ============================================================

/*
 * ppm_open -- open or create a file
 *
 *   path   Null-terminated filesystem path.
 *   flags  Bitwise OR of PPM_O_* constants. Common patterns:
 *            Read existing:     PPM_O_RDONLY
 *            Write / create:    PPM_O_WRONLY | PPM_O_CREAT | PPM_O_TRUNC
 *   mode   Unix permission bits applied when PPM_O_CREAT is set
 *          (e.g. PPM_MODE_644). Ignored when not creating a file.
 *
 *   Returns a PPM_FILE. A negative .fd indicates failure (the
 *   underlying open(2) syscall returned an error).
 *
 *   Example:
 *     PPM_FILE f = ppm_open("img.ppm", PPM_O_RDONLY, 0);
 *     if (f.fd < 0) { /* handle error *\/ }
 */
PPM_FILE ppm_open(const char *path, int flags, int mode);

/*
 * ppm_close -- close a file opened with ppm_open
 *
 *   f  The PPM_FILE to close. After this call the fd is invalid;
 *      do not use f again. Passing an already-closed or invalid
 *      PPM_FILE is undefined behaviour (mirrors close(2) semantics).
 */
void ppm_close(PPM_FILE f);

/*
 * ppm_create_image -- allocate a blank image of the given dimensions
 *
 *   width, height  Pixel dimensions. Both must be > 0.
 *
 *   Returns a heap-allocated ppm_Image_t. The pixel buffer is
 *   allocated but NOT zeroed -- pixels hold indeterminate values.
 *   Initialize every pixel before writing or displaying the image.
 *
 *   Returns NULL on failure (zero dimensions, integer overflow in the
 *   size calculation, or allocator failure). Errors are reported via
 *   PPM_ERROR before returning NULL.
 *
 *   The caller must eventually pass the result to ppm_free_image().
 *
 *   Example:
 *     ppm_Image_t *img = ppm_create_image(1920, 1080);
 *     if (!img) { /* allocation failed *\/ }
 */
ppm_Image_t *ppm_create_image(ppm_u64 width, ppm_u64 height);

/*
 * ppm_free_image -- release all memory owned by an image
 *
 *   img  Image to free. NULL is silently ignored (safe no-op).
 *        After this call `img` is a dangling pointer -- do not use it.
 *
 *   Both the pixel buffer and the image struct are released via
 *   PPM_DEALLOC. Do NOT call free() on `img` directly.
 */
void ppm_free_image(ppm_Image_t *img);

/*
 * ppm_get_pixel -- bounds-checked pixel accessor
 *
 *   img  Image to access. Must not be NULL.
 *   x    Column index, 0-based (valid range: 0 ... width-1).
 *   y    Row index,    0-based (valid range: 0 ... height-1).
 *
 *   Returns a pointer to the ppm_Pixel_t at (x, y), or NULL if img
 *   is NULL or the coordinates are out of bounds. The pointer remains
 *   valid until ppm_free_image() is called on img.
 *
 *   Modifying the returned pixel modifies the image in-place:
 *     ppm_Pixel_t *px = ppm_get_pixel(img, 10, 20);
 *     if (px) { px->red = 255; px->green = 128; px->blue = 0; }
 */
ppm_Pixel_t *ppm_get_pixel(ppm_Image_t *img, ppm_u64 x, ppm_u64 y);

/*
 * ppm_write -- encode and write a P6 PPM image to an open file
 *
 *   image  Source image. Must not be NULL; image->data must not be NULL.
 *   file   An open PPM_FILE with write permission.
 *
 *   Writes the ASCII P6 header ("P6\n<W> <H>\n255\n") followed by
 *   the raw binary pixel data. Uses a 64 KiB internal write buffer
 *   to minimise syscall count for large images.
 *
 *   Returns 0 on success, -1 on any I/O failure. On failure the file
 *   may contain a partial write and should be discarded. Errors are
 *   also reported via PPM_ERROR.
 *
 *   Example:
 *     PPM_FILE f = ppm_open("out.ppm",
 *                           PPM_O_WRONLY | PPM_O_CREAT | PPM_O_TRUNC,
 *                           PPM_MODE_644);
 *     if (ppm_write(img, f) != 0) { /* write failed *\/ }
 *     ppm_close(f);
 */
int ppm_write(const ppm_Image_t *image, PPM_FILE file);

/*
 * ppm_read -- decode a P6 PPM image from an open file
 *
 *   file  An open PPM_FILE positioned at the start of a P6 PPM stream.
 *
 *   Parses the ASCII header (magic "P6", width, height, maxval;
 *   '#'-prefixed comment lines are skipped per the PPM spec), then
 *   bulk-reads the binary pixel data into a newly allocated ppm_Image_t.
 *
 *   Limitations:
 *     - Only P6 (binary) PPM is accepted. P3 (ASCII) is not supported.
 *     - maxval must be in the range 1-255 (8-bit channels only).
 *
 *   Returns a heap-allocated ppm_Image_t on success.
 *   Returns NULL on parse or I/O error; errors are reported via
 *   PPM_ERROR before returning NULL.
 *   The caller must eventually pass the result to ppm_free_image().
 *
 *   Example:
 *     PPM_FILE f  = ppm_open("in.ppm", PPM_O_RDONLY, 0);
 *     ppm_Image_t *img = ppm_read(f);
 *     ppm_close(f);
 *     if (!img) { /* parse failed *\/ }
 */
ppm_Image_t *ppm_read(PPM_FILE file);

// ============================================================
// Optional Prefix Stripping
//
// Define PPM_STRIP_PREFIX before including this header to bring
// shorter, unprefixed aliases into scope. Handy for quick programs,
// but pollutes the global namespace -- avoid in headers or libraries.
//
//   #define PPM_STRIP_PREFIX
//   #include "ppm.h"
//   Image_t *img = create_image(320, 240);  // works
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
//
// Everything below is compiled only in the ONE translation unit
// that defines PPM_IMPLEMENTATION before including this header.
// ============================================================

#ifdef PPM_IMPLEMENTATION

// ------------------------------------------------------------
// Syscall Numbers (x86-64 Linux defaults)
//
// Override any of these before including the header to port to
// a different architecture or kernel ABI.
// ------------------------------------------------------------

#ifndef PPM_SYS_READ
#define PPM_SYS_READ 0 /* read(2)   */
#endif
#ifndef PPM_SYS_WRITE
#define PPM_SYS_WRITE 1 /* write(2)  */
#endif
#ifndef PPM_SYS_OPEN
#define PPM_SYS_OPEN 2 /* open(2)   */
#endif
#ifndef PPM_SYS_CLOSE
#define PPM_SYS_CLOSE 3 /* close(2)  */
#endif
#ifndef PPM_SYS_MMAP
#define PPM_SYS_MMAP 9 /* mmap(2)   */
#endif
#ifndef PPM_SYS_MUNMAP
#define PPM_SYS_MUNMAP 11 /* munmap(2) */
#endif

// ------------------------------------------------------------
// Inline Syscall Shims -- x86-64 GCC/Clang inline assembly
//
// ppm__sc3: issues a syscall with up to 3 arguments
//           (passed in rdi, rsi, rdx).
// ppm__sc6: issues a syscall with up to 6 arguments
//           (adds r10, r8, r9 for arguments 4-6).
//
// Both shims are static inline: the compiler folds them into
// their single macro call-site with zero function-call overhead.
//
// rcx and r11 are clobbered by the syscall instruction itself
// (the CPU uses them to save rip and rflags respectively).
// They are listed in the clobber list so the compiler knows
// not to keep live values there across the syscall.
//
// To target a different ISA or toolchain, define both macros
// before including this header to supply your own shims:
//
//   #define PPM_SYSCALL3(n,a,b,c)        my_sc3(n,a,b,c)
//   #define PPM_SYSCALL6(n,a,b,c,d,e,f) my_sc6(n,a,b,c,d,e,f)
// ------------------------------------------------------------

#ifndef PPM_SYSCALL3
static inline ppm_i64 ppm__sc3(ppm_i64 n, ppm_i64 a, ppm_i64 b, ppm_i64 c) {
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
static inline ppm_i64 ppm__sc6(ppm_i64 n, ppm_i64 a, ppm_i64 b, ppm_i64 c,
                               ppm_i64 d, ppm_i64 e, ppm_i64 f) {
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
// mmap(2) flag constants (Linux ABI values, not from <sys/mman.h>)
// ------------------------------------------------------------

#define PPM__PROT_RW 3          /* PROT_READ | PROT_WRITE        */
#define PPM__MAP_PRIV_ANON 0x22 /* MAP_PRIVATE | MAP_ANONYMOUS   */
#define PPM__MAP_FAILED ((void *)(-1))

// ------------------------------------------------------------
// Default Allocator -- anonymous mmap / munmap
//
// ppm__alloc  : creates an anonymous private mapping that is
//   readable and writable. Returns NULL on failure (mmap returns
//   MAP_FAILED, i.e. (void*)-1, on error).
//
// ppm__dealloc: releases a mapping previously obtained from
//   ppm__alloc. `bytes` must exactly match the size passed to
//   the corresponding alloc call, as required by munmap(2).
//
// Override both macros together before including the header to
// substitute a different allocator (e.g. malloc/free, a slab,
// or an arena). They must always be defined as a matched pair.
// ------------------------------------------------------------

#ifndef PPM_ALLOC
static inline void *ppm__alloc(ppm_usize bytes) {
  void *p = (void *)PPM_SYSCALL6(PPM_SYS_MMAP, 0, (ppm_i64)bytes, PPM__PROT_RW,
                                 PPM__MAP_PRIV_ANON, -1, 0);
  return (p == PPM__MAP_FAILED) ? (void *)0 : p;
}
#define PPM_ALLOC(sz) ppm__alloc((ppm_usize)(sz))
#endif

#ifndef PPM_DEALLOC
static inline void ppm__dealloc(void *ptr, ppm_usize bytes) {
  if (ptr)
    PPM_SYSCALL3(PPM_SYS_MUNMAP, (ppm_i64)ptr, (ppm_i64)bytes, 0);
}
#define PPM_DEALLOC(ptr, sz) ppm__dealloc(ptr, (ppm_usize)(sz))
#endif

// ------------------------------------------------------------
// Compile-time limit constants (no <limits.h> dependency)
// ------------------------------------------------------------

#define PPM_U64_MAX ((ppm_u64)(~(ppm_u64)0))
#define PPM_USIZE_MAX ((ppm_usize)(~(ppm_usize)0))

// ------------------------------------------------------------
// Raw I/O -- thin syscall wrappers
// ------------------------------------------------------------

PPM_FILE ppm_open(const char *path, int flags, int mode) {
  PPM_FILE f;
  f.fd = (int)PPM_SYSCALL3(PPM_SYS_OPEN, (ppm_i64)path, flags, mode);
  return f;
}

void ppm_close(PPM_FILE f) { PPM_SYSCALL3(PPM_SYS_CLOSE, f.fd, 0, 0); }

/* Single read(2) syscall. Returns bytes read (>0), 0 at EOF, or <0 on error. */
static inline ppm_i64 ppm__fd_read(int fd, void *buf, ppm_usize n) {
  return PPM_SYSCALL3(PPM_SYS_READ, fd, (ppm_i64)buf, (ppm_i64)n);
}

/*
 * ppm__fd_write_all -- write all `n` bytes to fd, retrying on short writes.
 *
 * POSIX write(2) is not guaranteed to write all requested bytes in a single
 * call (e.g. signal interruption, pipe buffer saturation). This wrapper loops
 * until all bytes are delivered or an unrecoverable error occurs.
 *
 * Returns 0 on success, -1 if any write(2) call fails.
 */
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
// Error Reporting
//
// Default handler: writes "[PPM ERROR] <msg>\n" to fd 2 (stderr)
// via raw syscall, then returns. Execution continues -- this
// library never calls abort() itself.
//
// To substitute your own handler, define PPM_ERROR(msg) before
// including this header. The macro receives a string-literal `msg`
// and may do anything: longjmp, __builtin_trap, a no-op for
// embedded targets that have no stderr, etc.
// ------------------------------------------------------------

#ifndef PPM_ERROR
static void ppm__error(const char *msg) {
  static const char prefix[] = "[PPM ERROR] ";
  ppm_usize plen = sizeof(prefix) - 1;
  ppm__fd_write_all(2, prefix, plen);
  ppm_usize mlen = 0;
  while (msg[mlen])
    mlen++;
  ppm__fd_write_all(2, msg, mlen);
  ppm__fd_write_all(2, "\n", 1);
}
#define PPM_ERROR(msg) ppm__error(msg)
#endif

// ------------------------------------------------------------
// Character Classification Helpers
//
// Reimplemented to avoid pulling in <ctype.h>. All three are
// static inline: they're tiny, called in tight parsing loops,
// and the compiler will typically eliminate the call entirely.
// ------------------------------------------------------------

/* Returns non-zero if c is ASCII whitespace: space, tab, newline (\n),
   carriage return (\r), form-feed (\f), or vertical tab (\v). */
static inline int ppm__isspace(int c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' ||
         c == '\v';
}

/* Returns non-zero if c is an ASCII decimal digit ('0' through '9'). */
static inline int ppm__isdigit(int c) { return c >= '0' && c <= '9'; }

/*
 * ppm__strcmp -- lexicographic string comparison (no <string.h>)
 *
 * Behaviour matches the C standard strcmp(): returns 0 if a == b,
 * a negative value if a < b, a positive value if a > b.
 */
static inline int ppm__strcmp(const char *a, const char *b) {
  while (*a && *a == *b) {
    a++;
    b++;
  }
  return (unsigned char)*a - (unsigned char)*b;
}

// ------------------------------------------------------------
// ppm__u64_to_str -- unsigned 64-bit integer to decimal C string
//
// Converts `v` to a NUL-terminated decimal string written into `buf`.
// `buf` must be at least 21 bytes (up to 20 decimal digits + '\0').
// Returns the number of characters written, not counting the NUL.
//
// Used to emit image dimensions in the PPM header without relying on
// sprintf or any other printf-family function.
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
// Buffered Reader (ppm_Rdr) -- internal, not part of public API
//
// Wraps a file descriptor with a PPM__IBUF-byte read-ahead buffer
// to avoid one read(2) syscall per byte while parsing the ASCII
// PPM header. For the large binary pixel payload, ppm_rdr_read_exact
// bypasses the buffer and issues direct syscalls to avoid copying
// the data twice.
//
// Fields:
//   fd   -- underlying file descriptor
//   buf  -- read-ahead ring; filled in PPM__IBUF-sized chunks
//   pos  -- index of the next unread byte in buf
//   len  -- number of valid bytes currently in buf
//   eof  -- set to 1 once read(2) returns 0 or an error
// ------------------------------------------------------------

#define PPM__IBUF 4096

typedef struct {
  int fd;
  ppm_u8 buf[PPM__IBUF];
  int pos;
  int len;
  int eof;
} ppm_Rdr;

/* Bind a reader to a file. Must be called before any other ppm_rdr_* call. */
static inline void ppm_rdr_init(ppm_Rdr *r, PPM_FILE f) {
  r->fd = f.fd;
  r->pos = r->len = r->eof = 0;
}

/*
 * ppm_rdr_getc -- return the next byte as an unsigned int, or -1 at EOF/error.
 *
 * Refills the internal buffer from the fd whenever it is exhausted,
 * using a single read(2) syscall per PPM__IBUF bytes. Marked inline
 * because it sits in the tight per-character header-parsing loop.
 */
static inline int ppm_rdr_getc(ppm_Rdr *r) {
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

/*
 * ppm_rdr_unget -- push the last-consumed byte back into the buffer.
 *
 * The next ppm_rdr_getc() call will return it again. Only one byte of
 * pushback is supported at a time (mirrors ungetc() semantics).
 */
static inline void ppm_rdr_unget(ppm_Rdr *r) {
  if (r->pos > 0)
    r->pos--;
}

/*
 * ppm_rdr_skip_ws -- advance past whitespace and '#' comment lines.
 *
 * The PPM spec allows comment lines beginning with '#' anywhere in
 * the header before the pixel data. This function skips all such
 * content, leaving the reader positioned at the first byte that is
 * neither whitespace nor part of a comment.
 *
 * Returns 0 on success, -1 if EOF is reached before finding a
 * non-whitespace, non-comment byte.
 */
static int ppm_rdr_skip_ws(ppm_Rdr *r) {
  int c;
  while ((c = ppm_rdr_getc(r)) != -1) {
    if (ppm__isspace(c))
      continue;
    if (c == '#') {
      /* consume the rest of the comment line */
      while ((c = ppm_rdr_getc(r)) != -1 && c != '\n')
        ;
      continue;
    }
    ppm_rdr_unget(r);
    return 0;
  }
  return -1;
}

/*
 * ppm_rdr_read_u64 -- parse a decimal unsigned 64-bit integer from the stream.
 *
 * Skips leading whitespace and comments, then accumulates decimal digits
 * until a non-digit byte is seen. That non-digit is pushed back so the
 * next call can see the delimiter (e.g. whitespace between header fields).
 *
 * Overflow is detected: if the accumulated value would exceed PPM_U64_MAX
 * the function returns -1 immediately without touching *out.
 *
 * Returns 0 and stores the result in *out on success.
 * Returns -1 on EOF, a non-digit start character, or integer overflow.
 */
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
      return -1; /* overflow */
    val = val * 10 + d;
    c = ppm_rdr_getc(r);
  }
  if (c != -1)
    ppm_rdr_unget(r);
  *out = val;
  return 0;
}

/*
 * ppm_rdr_read_exact -- read exactly `n` bytes into the buffer at `dst_v`.
 *
 * First drains any bytes already buffered in ppm_Rdr (left over from
 * header parsing), then falls through to direct read(2) syscalls for
 * the remainder. This avoids copying the (potentially very large) pixel
 * payload through the 4 KiB ring buffer a second time.
 *
 * Returns 0 on success, -1 if fewer than `n` bytes were available
 * before EOF or a read error.
 */
static int ppm_rdr_read_exact(ppm_Rdr *r, void *dst_v, ppm_usize n) {
  ppm_u8 *dst = (ppm_u8 *)dst_v;
  ppm_usize done = 0;

  /* Drain buffered remainder first */
  while (done < n && r->pos < r->len)
    dst[done++] = r->buf[r->pos++];

  /* Direct syscalls for the bulk of the pixel data */
  while (done < n) {
    ppm_i64 got = ppm__fd_read(r->fd, dst + done, n - done);
    if (got <= 0)
      return -1;
    done += (ppm_usize)got;
  }
  return 0;
}

// ------------------------------------------------------------
// Buffered Writer (ppm_Wtr) -- internal, not part of public API
//
// Wraps a file descriptor with a PPM__OBUF-byte write buffer.
// Pixels are accumulated in memory and flushed in large chunks,
// dramatically reducing write(2) syscall overhead for typical
// image sizes.
//
// Fields:
//   fd   -- underlying file descriptor
//   buf  -- pending output; flushed automatically when full
//   pos  -- number of bytes currently held in buf
// ------------------------------------------------------------

#define PPM__OBUF 65536

typedef struct {
  int fd;
  ppm_u8 buf[PPM__OBUF];
  ppm_usize pos;
} ppm_Wtr;

/* Bind a writer to a file. Must be called before any other ppm_wtr_* call. */
static inline void ppm_wtr_init(ppm_Wtr *w, PPM_FILE f) {
  w->fd = f.fd;
  w->pos = 0;
}

/*
 * ppm_wtr_flush -- write all buffered bytes to the fd and reset the buffer.
 *
 * Must be called once after all ppm_wtr_write / ppm_wtr_str calls to ensure
 * the final partial buffer is pushed to the OS. Also called automatically
 * by ppm_wtr_write whenever the buffer fills to capacity.
 *
 * Returns 0 on success, -1 on I/O error.
 */
static int ppm_wtr_flush(ppm_Wtr *w) {
  if (w->pos == 0)
    return 0;
  int r = ppm__fd_write_all(w->fd, w->buf, w->pos);
  w->pos = 0;
  return r;
}

/*
 * ppm_wtr_write -- append `n` bytes from `data` to the write buffer.
 *
 * Copies data into the internal buffer in chunks, flushing to the fd
 * automatically each time the buffer reaches PPM__OBUF bytes. Handles
 * inputs larger than the buffer size correctly.
 *
 * Returns 0 on success, -1 if a mid-write flush fails.
 */
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

/* Append a NUL-terminated string to the write buffer. */
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

  /* Header: "P6\n<width> <height>\n255\n" */
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

  /* Read and validate the two-byte magic string "P6" */
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

  /* PPM spec: exactly one whitespace byte separates maxval from pixel data */
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

#ifdef __cplusplus
}
#endif
