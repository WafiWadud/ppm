# PPM - Portable PixMap Image Library

A single-header C library for reading and writing PPM (Portable PixMap) image files in P6 binary format with 8-bit color depth. **Zero stdlib dependencies** — no libc, no system headers required.

## Features

- **Single-header library** — just `#define PPM_IMPLEMENTATION` and include `ppm.h`
- **Zero stdlib dependencies** — uses raw Linux syscalls for I/O and `mmap`/`munmap` for memory; no libc, no system headers pulled in
- **Fully overridable** — swap out syscall numbers, allocator, or error handler via `#define` before including
- **Buffered I/O** — 4 KiB read buffer and 64 KiB write buffer to keep syscall overhead low
- **Overflow-safe** — dimension and allocation size checked before any `mmap` call
- **Error reporting** — writes descriptive messages to `stderr` (fd 2) without `fprintf`

## Installation

Copy `ppm.h` into your project. No other files needed.

## Usage

Define `PPM_IMPLEMENTATION` in exactly one translation unit before including the header:

```c
#define PPM_IMPLEMENTATION
#include "ppm.h"
```

Other translation units that only use the API can include without the define:

```c
#include "ppm.h"
```

## API Reference

### Types

**`ppm_Pixel_t`** — a single RGB pixel

```c
typedef struct {
    ppm_u8 red;
    ppm_u8 green;
    ppm_u8 blue;
} ppm_Pixel_t;
```

**`ppm_Image_t`** — an image

```c
typedef struct {
    ppm_u64      width;
    ppm_u64      height;
    ppm_Pixel_t *data;   // row-major pixel buffer (width * height elements)
} ppm_Image_t;
```

**`PPM_FILE`** — a thin wrapper around a raw OS file descriptor

```c
typedef struct { int fd; } PPM_FILE;
```

### Functions

**`PPM_FILE ppm_open(const char *path, int flags, int mode)`**

Opens a file. Use the provided flag constants:

```c
PPM_O_RDONLY   // open for reading
PPM_O_WRONLY   // open for writing
PPM_O_CREAT    // create if it doesn't exist (combine with PPM_O_WRONLY)
PPM_O_TRUNC    // truncate to zero length on open
PPM_MODE_644   // permission bits for newly created files (rw-r--r--)
```

**`void ppm_close(PPM_FILE f)`**

Closes a file handle.

**`ppm_Image_t *ppm_create_image(ppm_u64 width, ppm_u64 height)`**

Allocates a new image with the given dimensions. Returns `NULL` on invalid dimensions or allocation failure.

**`void ppm_free_image(ppm_Image_t *img)`**

Frees all memory associated with an image. Safe to call with `NULL`.

**`ppm_Pixel_t *ppm_get_pixel(ppm_Image_t *img, ppm_u64 x, ppm_u64 y)`**

Returns a pointer to the pixel at `(x, y)`. Returns `NULL` if out of bounds or `img` is `NULL`.

**`int ppm_write(const ppm_Image_t *image, PPM_FILE file)`**

Writes the image to `file` in P6 binary PPM format. Returns `0` on success, `-1` on failure.

**`ppm_Image_t *ppm_read(PPM_FILE file)`**

Reads a P6 binary PPM image from `file`. Returns `NULL` on failure. Only 8-bit images (maxval ≤ 255) are supported.

## Customization

All platform-specific behaviour can be overridden before `#define PPM_IMPLEMENTATION`:

### Syscall numbers (default: x86-64 Linux)

```c
#define PPM_SYS_READ    0
#define PPM_SYS_WRITE   1
#define PPM_SYS_OPEN    2
#define PPM_SYS_CLOSE   3
#define PPM_SYS_MMAP    9
#define PPM_SYS_MUNMAP  11
```

### Syscall shims

Replace the inline assembly entirely for non-x86-64 targets:

```c
#define PPM_SYSCALL3(n, a, b, c)          your_syscall3(n, a, b, c)
#define PPM_SYSCALL6(n, a, b, c, d, e, f) your_syscall6(n, a, b, c, d, e, f)
```

### Memory

```c
#define PPM_ALLOC(size)        your_alloc(size)
#define PPM_DEALLOC(ptr, size) your_dealloc(ptr, size)
```

Note that `PPM_DEALLOC` receives the allocation size, since the default `munmap` implementation needs it. If you override with a `malloc`/`free`-style allocator you can simply ignore the size argument.

### Error reporting

```c
#define PPM_ERROR(msg) your_error_handler(msg)
```

### Prefix stripping

Define `PPM_STRIP_PREFIX` to expose the API without the `ppm_` prefix (`Pixel_t`, `Image_t`, `get_pixel`, etc.).

## Example

Create a 256×256 gradient image and save it to disk:

```c
#define PPM_IMPLEMENTATION
#include "ppm.h"

void _start(void) {
    ppm_Image_t *img = ppm_create_image(256, 256);

    for (ppm_u64 y = 0; y < 256; y++) {
        for (ppm_u64 x = 0; x < 256; x++) {
            ppm_Pixel_t *px = ppm_get_pixel(img, x, y);
            px->red   = (ppm_u8)x;
            px->green = (ppm_u8)y;
            px->blue  = 128;
        }
    }

    PPM_FILE out = ppm_open("gradient.ppm",
                            PPM_O_WRONLY | PPM_O_CREAT | PPM_O_TRUNC,
                            PPM_MODE_644);
    ppm_write(img, out);
    ppm_close(out);
    ppm_free_image(img);
}
```

Compile with no stdlib:

```bash
gcc -O2 -nostdlib -nodefaultlibs -nostartfiles -ffreestanding -o example example.c
./example
```

Or alongside libc if you prefer a normal `main()`:

```bash
gcc -O2 -o example example.c
./example
```

Both produce a valid `gradient.ppm` viewable in any image viewer that supports PPM.

## Platform Support

The default implementation targets **x86-64 Linux**. Porting to other targets requires overriding the syscall numbers and shims (see Customization above). The type system works on both LP64 (Linux/macOS 64-bit) and LLP64 (Windows 64-bit) targets.

## License

Refer to the project's license file for details.
