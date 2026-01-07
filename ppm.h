#define PPM_STRIP_PREFIX
#define PPM_IMPLEMENTATION

// File handle type default (user can override)
#ifndef PPM_FILE
#include <stdio.h>
typedef FILE PPM_FILE;
#endif

// Integer type defaults (user can override)
#ifndef PPM_UINT64
#include <stdint.h>
#define PPM_UINT64 uint64_t
#endif

#ifndef PPM_UINT8
#include <stdint.h>
#define PPM_UINT8 uint8_t
#endif

// Memory allocation defaults (user can override)
#ifndef PPM_MALLOC
#include <stdlib.h>
#define PPM_MALLOC(size) malloc(size)
#endif

#ifndef PPM_FREE
#include <stdlib.h>
#define PPM_FREE(ptr) free(ptr)
#endif

#ifndef PPM_NULL
#include <stdio.h>
#define PPM_NULL NULL
#endif

// I/O function defaults (user can override)
#ifndef PPM_FPRINTF
#include <stdio.h>
#define PPM_FPRINTF fprintf
#endif

#ifndef PPM_FSCANF
#include <stdio.h>
#define PPM_FSCANF fscanf
#endif

#ifndef PPM_FWRITE
#include <stdio.h>
#define PPM_FWRITE fwrite
#endif

#ifndef PPM_FREAD
#include <stdio.h>
#define PPM_FREAD fread
#endif

#ifndef PPM_FGETC
#include <stdio.h>
#define PPM_FGETC fgetc
#endif

#ifndef PPM_UNGETC
#include <stdio.h>
#define PPM_UNGETC ungetc
#endif

#ifndef PPM_ERROR
#include <stdio.h>
#define PPM_ERROR(str) fprintf(stderr, "[ERROR] %s\n", str)
#endif

// String Function defaults
#ifndef PPM_STRCMP
#include <string.h>
#define PPM_STRCMP strcmp
#endif

#if defined(PPM_STRIP_PREFIX)
#define Pixel_t ppm_Pixel_t
#define Pixels_t ppm_Pixels_t
#define Image_t ppm_Image_t
#define write ppm_write
#define read ppm_read
#define create_image ppm_create_image
#define get_pixel ppm_get_pixel
#define free_image ppm_free_image
#endif

typedef struct {
  PPM_UINT8 red;
  PPM_UINT8 green;
  PPM_UINT8 blue;
} ppm_Pixel_t;

typedef struct {
  ppm_Pixel_t *data;
  PPM_UINT64 width;
  PPM_UINT64 height;
} ppm_Image_t;

// Function declarations
int ppm_write(const ppm_Image_t *restrict image, PPM_FILE *file);
ppm_Image_t *ppm_read(PPM_FILE *file);

// Helper functions
ppm_Image_t *ppm_create_image(PPM_UINT64 width, PPM_UINT64 height);
ppm_Pixel_t *ppm_get_pixel(ppm_Image_t *img, PPM_UINT64 x, PPM_UINT64 y);
void ppm_free_image(ppm_Image_t *img);

#if defined(PPM_IMPLEMENTATION)

ppm_Image_t *ppm_create_image(uint64_t width, uint64_t height) {
  ppm_Image_t *img = PPM_MALLOC(sizeof(ppm_Image_t));
  if (img == PPM_NULL) {
    PPM_ERROR("Image allocation failed.");
    return PPM_NULL;
  }

  img->width = width;
  img->height = height;
  img->data = PPM_MALLOC(sizeof(ppm_Pixel_t) * width * height);

  if (img->data == PPM_NULL) {
    PPM_ERROR("Image data allocation failed.");
    PPM_FREE(img);
    return PPM_NULL;
  }

  return img;
}

ppm_Pixel_t *ppm_get_pixel(ppm_Image_t *img, uint64_t x, uint64_t y) {
  if (img == PPM_NULL || x >= img->width || y >= img->height) {
    PPM_ERROR("Bad params passed to ppm_get_pixel");
    return PPM_NULL;
  }
  return &img->data[y * img->width + x];
}

void ppm_free_image(ppm_Image_t *img) {
  if (img != PPM_NULL) {
    PPM_FREE(img->data);
    PPM_FREE(img);
  }
}

int ppm_write(const ppm_Image_t *restrict image, PPM_FILE *file) {
  if (image == PPM_NULL || file == PPM_NULL) {
    PPM_ERROR("Invalid parameters passed to ppm_write");
    return -1;
  }

  if (image->data == PPM_NULL) {
    PPM_ERROR("Image data is NULL");
    return -1;
  }

  // Write PPM header (P6 format - binary)
  PPM_FPRINTF(file, "P6\n%lu %lu\n255\n", image->width, image->height);

  // Write pixel data
  PPM_UINT64 total_pixels = image->width * image->height;
  PPM_UINT64 written =
      PPM_FWRITE(image->data, sizeof(ppm_Pixel_t), total_pixels, file);

  if (written != total_pixels) {
    PPM_ERROR("Failed to write all pixel data");
    return -1;
  }

  return 0;
}

ppm_Image_t *ppm_read(PPM_FILE *file) {
  if (file == PPM_NULL) {
    PPM_ERROR("File pointer is NULL");
    return PPM_NULL;
  }

  char magic[3];
  PPM_UINT64 width, height, max_val;

  // Read magic number
  if (PPM_FSCANF(file, "%2s", magic) != 1) {
    PPM_ERROR("Failed to read magic number");
    return PPM_NULL;
  }

  if (PPM_STRCMP(magic, "P6") != 0) {
    PPM_ERROR("Unsupported PPM format (only P6 supported)");
    return PPM_NULL;
  }

  // Skip whitespace and comments
  int c;
  while ((c = PPM_FGETC(file)) != EOF) {
    if (c == '#') {
      while ((c = PPM_FGETC(file)) != EOF && c != '\n')
        ;
    } else if (c != ' ' && c != '\t' && c != '\n' && c != '\r') {
      PPM_UNGETC(c, file);
      break;
    }
  }

  if (PPM_FSCANF(file, "%lu %lu", &width, &height) != 2) {
    PPM_ERROR("Failed to read image dimensions");
    return PPM_NULL;
  }

  if (PPM_FSCANF(file, "%lu", &max_val) != 1) {
    PPM_ERROR("Failed to read max color value");
    return PPM_NULL;
  }

  if (max_val != 255) {
    PPM_ERROR("Only 8-bit color depth supported (max value must be 255)");
    return PPM_NULL;
  }

  PPM_FGETC(file); // Skip single whitespace character

  ppm_Image_t *img = ppm_create_image(width, height);
  if (img == PPM_NULL)
    return PPM_NULL;

  PPM_UINT64 total_pixels = width * height;
  PPM_UINT64 read_pixels =
      PPM_FREAD(img->data, sizeof(ppm_Pixel_t), total_pixels, file);

  if (read_pixels != total_pixels) {
    PPM_ERROR("Failed to read all pixel data");
    ppm_free_image(img);
    return PPM_NULL;
  }

  return img;
}

#endif // PPM_IMPLEMENTATION
