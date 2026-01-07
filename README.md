# PPM - Portable PixMap Image Library

A single-header C library for reading and writing PPM (Portable PixMap) image files. Supports the P6 binary format with 8-bit color depth.

## Features

- **Single-header library** - Just include `ppm.h` to get started
- **Customizable allocators** - Override malloc, free, and I/O functions
- **Flexible type system** - Customize integer and file types
- **Memory efficient** - Direct pixel buffer access
- **Error handling** - Comprehensive error reporting

## Installation

Simply copy `ppm.h` to your project and include it in your source file.

## Usage

To use the library, define `PPM_IMPLEMENTATION` before including the header in one of your source files:

```c
#define PPM_IMPLEMENTATION
#include "ppm.h"
```

## API Reference

### Types

**`ppm_Pixel_t`** - Represents a single pixel

```c
typedef struct {
  PPM_UINT8 red;
  PPM_UINT8 green;
  PPM_UINT8 blue;
} ppm_Pixel_t;
```

**`ppm_Image_t`** - Represents an image

```c
typedef struct {
  ppm_Pixel_t *data;  // Pixel buffer (width * height elements)
  PPM_UINT64 width;   // Image width in pixels
  PPM_UINT64 height;  // Image height in pixels
} ppm_Image_t;
```

### Functions

**`ppm_Image_t *ppm_create_image(PPM_UINT64 width, PPM_UINT64 height)`**

- Creates a new image with the specified dimensions
- Returns NULL on failure

**`void ppm_free_image(ppm_Image_t *img)`**

- Frees all memory associated with an image

**`ppm_Pixel_t *ppm_get_pixel(ppm_Image_t *img, PPM_UINT64 x, PPM_UINT64 y)`**

- Gets a pointer to the pixel at position (x, y)
- Returns NULL if coordinates are out of bounds

**`int ppm_write(const ppm_Image_t *image, PPM_FILE *file)`**

- Writes an image to a file in P6 format
- Returns 0 on success, -1 on failure

**`ppm_Image_t *ppm_read(PPM_FILE *file)`**

- Reads a P6 format PPM image from a file
- Returns NULL on failure

## Customization

The library allows customization through preprocessor defines:

- `PPM_FILE` - File handle type (default: `FILE`)
- `PPM_UINT64` - 64-bit unsigned integer type
- `PPM_UINT8` - 8-bit unsigned integer type
- `PPM_MALLOC` - Memory allocation function
- `PPM_FREE` - Memory deallocation function
- `PPM_FPRINTF`, `PPM_FSCANF`, `PPM_FWRITE`, `PPM_FREAD` - I/O functions
- `PPM_STRIP_PREFIX` - Remove `ppm_` prefix from function and type names

## Example

Create a simple red square image:

```c
#define PPM_IMPLEMENTATION
#include "ppm.h"
#include <stdio.h>

int main() {
    // Create a 256x256 image
    ppm_Image_t *img = ppm_create_image(256, 256);
    if (!img) {
        fprintf(stderr, "Failed to create image\n");
        return 1;
    }

    // Fill with red color
    for (int y = 0; y < 256; y++) {
        for (int x = 0; x < 256; x++) {
            ppm_Pixel_t *pixel = ppm_get_pixel(img, x, y);
            pixel->red = 255;
            pixel->green = 0;
            pixel->blue = 0;
        }
    }

    // Write to file
    FILE *output = fopen("red_square.ppm", "wb");
    if (!output) {
        fprintf(stderr, "Failed to open output file\n");
        ppm_free_image(img);
        return 1;
    }

    if (ppm_write(img, output) != 0) {
        fprintf(stderr, "Failed to write image\n");
        fclose(output);
        ppm_free_image(img);
        return 1;
    }

    fclose(output);
    ppm_free_image(img);

    printf("Image written to red_square.ppm\n");
    return 0;
}
```

Compile with:

```bash
gcc -o example example.c
./example
```

This creates a `red_square.ppm` file that can be opened with any image viewer that supports PPM format.

## License

Refer to the project's license file for details.
