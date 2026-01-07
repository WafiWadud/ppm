#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PPM_STRIP_PREFIX
#define PPM_IMPLEMENTATION
#define PPM_ERROR(str) fprintf(stderr, "[ERROR] %s\n", str)

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
  uint8_t red;
  uint8_t green;
  uint8_t blue;
} ppm_Pixel_t;

typedef struct {
  ppm_Pixel_t *data;
  uint64_t width;
  uint64_t height;
} ppm_Image_t;

// File handle type - user can redefine this
#ifndef PPM_FILE
#include <stdio.h>
typedef FILE PPM_FILE;
#endif

// Function declarations
int ppm_write(const ppm_Image_t *restrict image, PPM_FILE *file);
ppm_Image_t *ppm_read(PPM_FILE *file);

// Helper functions
ppm_Image_t *ppm_create_image(uint64_t width, uint64_t height);
ppm_Pixel_t *ppm_get_pixel(ppm_Image_t *img, uint64_t x, uint64_t y);
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
  size_t total_pixels = image->width * image->height;
  size_t written = fwrite(image->data, sizeof(ppm_Pixel_t), total_pixels, file);

  if (written != total_pixels) {
    PPM_ERROR("Failed to write all pixel data");
    return -1;
  }

  return 0;
}

ppm_Image_t *ppm_read(FILE *file) {
  if (file == NULL) {
    PPM_ERROR("File pointer is NULL");
    return NULL;
  }

  char magic[3];
  uint64_t width, height, max_val;

  // Read magic number
  if (fscanf(file, "%2s", magic) != 1) {
    PPM_ERROR("Failed to read magic number");
    return NULL;
  }

  if (strcmp(magic, "P6") != 0) {
    PPM_ERROR("Unsupported PPM format (only P6 supported)");
    return NULL;
  }

  // Skip whitespace and comments
  int c;
  while ((c = fgetc(file)) != EOF) {
    if (c == '#') {
      // Skip comment line
      while ((c = fgetc(file)) != EOF && c != '\n')
        ;
    } else if (c != ' ' && c != '\t' && c != '\n' && c != '\r') {
      ungetc(c, file);
      break;
    }
  }

  // Read width and height
  if (fscanf(file, "%lu %lu", &width, &height) != 2) {
    PPM_ERROR("Failed to read image dimensions");
    return NULL;
  }

  // Read max color value
  if (fscanf(file, "%lu", &max_val) != 1) {
    PPM_ERROR("Failed to read max color value");
    return NULL;
  }

  if (max_val != 255) {
    PPM_ERROR("Only 8-bit color depth supported (max value must be 255)");
    return NULL;
  }

  // Skip single whitespace character after header
  fgetc(file);

  // Create image
  ppm_Image_t *img = ppm_create_image(width, height);
  if (img == NULL) {
    return NULL;
  }

  // Read pixel data
  size_t total_pixels = width * height;
  size_t read_pixels =
      fread(img->data, sizeof(ppm_Pixel_t), total_pixels, file);

  if (read_pixels != total_pixels) {
    PPM_ERROR("Failed to read all pixel data");
    ppm_free_image(img);
    return NULL;
  }

  return img;
}

#endif // PPM_IMPLEMENTATION
