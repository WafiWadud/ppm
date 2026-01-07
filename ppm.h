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

#if defined(PPM_STRIP_PREFIX)
#define ppm_Pixel_t Pixel_t
#define ppm_Pixels_t Pixels_t
#define ppm_Image_t Image_t
#define ppm_write write
#define ppm_read read
#define ppm_create_image create_image
#define ppm_get_pixel get_pixel
#define ppm_free_image free_image
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
int ppm_write(const ppm_Image_t *restrict image, PPM_FILE *file, PPM_UINT8 maxlen);
ppm_Image_t *ppm_read(PPM_FILE *file, PPM_UINT8 maxlen);

// Helper functions
ppm_Image_t *ppm_create_image(PPM_UINT64 width, PPM_UINT64 height);
ppm_Pixel_t *ppm_get_pixel(ppm_Image_t *img, PPM_UINT64 x, PPM_UINT64 y);
void ppm_free_image(ppm_Image_t *img);

#if defined(PPM_IMPLEMENTATION)

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

int ppm_write(const ppm_Image_t *restrict image, PPM_FILE *file, PPM_UINT8 maxlen) {
  if (image == PPM_NULL || file == PPM_NULL) {
    PPM_ERROR("Invalid parameters passed to ppm_write");
    return -1;
  }

  if (image->data == PPM_NULL) {
    PPM_ERROR("Image data is NULL");
    return -1;
  }

  // Default to 255 if not specified
  if (maxlen == 0) {
    maxlen = 255;
  }

  // Write PPM header (P6 format - binary)
  PPM_FPRINTF(file, "P6\n%llu %llu\n%d\n", image->width, image->height, maxlen);

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

ppm_Image_t *ppm_read(PPM_FILE *file, PPM_UINT8 maxlen) {
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

  if (PPM_FSCANF(file, "%llu %llu", &width, &height) != 2) {
    PPM_ERROR("Failed to read image dimensions");
    return PPM_NULL;
  }

  if (PPM_FSCANF(file, "%llu", &max_val) != 1) {
    PPM_ERROR("Failed to read max color value");
    return PPM_NULL;
  }

  // Default to 255 if maxlen not specified, otherwise verify file matches expected max_val
  if (maxlen == 0) {
    maxlen = 255;
  }

  if (max_val != maxlen) {
    PPM_ERROR("Max color value in file does not match expected maxlen");
    return PPM_NULL;
  }

  // Skip whitespace after max value
  while ((c = PPM_FGETC(file)) != EOF &&
         (c == ' ' || c == '\t' || c == '\n' || c == '\r'))
    ;
  if (c != EOF)
    PPM_UNGETC(c, file);

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
