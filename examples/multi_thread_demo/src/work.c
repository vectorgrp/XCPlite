// multi_thread_demo - work implementation
//
// Mandelbrot set, rendered line by line, the line number is the work item index
// Ported from the rayon_demo of XCP-Lite Rust, inspired by the mandelbrot rayon example in the book "Programming Rust" by Jim Blandy and Jason Orendorff
//
// - The area of the complex plane to render is defined by the calibration segment 'mandelbrot' (center x, y and width)
// - A new image is rendered when the area was changed or on explicit request
// - The image is written to the file mandelbrot.png when all lines are rendered
//
// See the interface contract in work.h

#include "work.h"

#include <assert.h> // for assert
#include <math.h>   // for powf
#include <stdio.h>  // for printf
#include <stdlib.h> // for malloc, free

#include "a2l.h"    // for A2l generation
#include "xcplib.h" // for application programming interface

#include "platform.h" // for clockGetMonotonicNs

// PNG writer, single header library, public domain or MIT, see https://github.com/nothings/stb
#if defined(_MSC_VER)
#define STBI_MSC_SECURE_CRT
#endif
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

//-----------------------------------------------------------------------------------------------------
// Image

#define IMAGE_FILE_NAME "mandelbrot.png"
#define X_RES 2048   // Image width in pixels
#define Y_RES 1536   // Image height in pixels, number of work items in a batch
#define MAX_ITER 254 // Iteration limit, pixel value 255 marks a point in the set

//-----------------------------------------------------------------------------------------------------
// Calibration parameters

typedef struct mandelbrot {
    double x;     // Center of the area to render, real part
    double y;     // Center of the area to render, imaginary part
    double width; // Width of the area to render, the height is given by the image aspect ratio
} mandelbrot_t;

// Default parameters
// static const mandelbrot_t mandelbrot = {.x = -0.5, .y = 0.0, .width = 3.0}; // Complete set
static const mandelbrot_t mandelbrot = {.x = -0.8015, .y = 0.1561, .width = 0.0055};

// Calibration segment handle
static tXcpCalSegIndex mandelbrot_calseg = XCP_UNDEFINED_CALSEG;

//-----------------------------------------------------------------------------------------------------
// Work state

// Parameters of the current batch, a copy taken in work_begin_batch, so all lines of an image are rendered with the same parameters
static bool gValid = false; // gBatchParams contains the parameters of a rendered image
static mandelbrot_t gBatchParams;
static double gUpperLeftRe; // Upper left corner of the area to render
static double gUpperLeftIm;
static double gPixelSize; // Size of a pixel on the complex plane

// Iteration count per pixel, line by line, each line is owned by the worker which renders it
static uint8_t *gPixels = NULL;

// RGB image and color map for the PNG file
static uint8_t *gImage = NULL;
static uint8_t gColorMap[256][3];

//-----------------------------------------------------------------------------------------------------
// Coloring

// Normalize a color intensity value to the RGB range
static uint8_t normalize(float color, float factor) { return (uint8_t)(powf(color * factor, 0.8f) * 255.0f); }

// Convert a light wavelength in nm to RGB
static void wavelength_to_rgb(uint32_t i, uint8_t rgb[3]) {

    float wave = (float)i;
    float r = 0.0f, g = 0.0f, b = 0.0f;
    if (i >= 380 && i <= 439) {
        r = (440.0f - wave) / (440.0f - 380.0f);
        b = 1.0f;
    } else if (i >= 440 && i <= 489) {
        g = (wave - 440.0f) / (490.0f - 440.0f);
        b = 1.0f;
    } else if (i >= 490 && i <= 509) {
        g = 1.0f;
        b = (510.0f - wave) / (510.0f - 490.0f);
    } else if (i >= 510 && i <= 579) {
        r = (wave - 510.0f) / (580.0f - 510.0f);
        g = 1.0f;
    } else if (i >= 580 && i <= 644) {
        r = 1.0f;
        g = (645.0f - wave) / (645.0f - 580.0f);
    } else if (i >= 645 && i <= 780) {
        r = 1.0f;
    }

    float factor = 1.0f;
    if (i >= 380 && i <= 419) {
        factor = 0.3f + 0.7f * (wave - 380.0f) / (420.0f - 380.0f);
    } else if (i >= 701 && i <= 780) {
        factor = 0.3f + 0.7f * (780.0f - wave) / (780.0f - 700.0f);
    }

    rgb[0] = normalize(r, factor);
    rgb[1] = normalize(g, factor);
    rgb[2] = normalize(b, factor);
}

// Map iteration counts 0..255 to the visible spectrum, 255 (in the set) is black
static void init_color_map(void) {
    for (uint32_t i = 0; i < 256; i++) {
        wavelength_to_rgb(379 + (i * (781 - 379)) / 255, gColorMap[i]);
    }
}

//-----------------------------------------------------------------------------------------------------
// Mandelbrot set

// Number of iterations until c = (cr, ci) leaves the circle of radius 2, MAX_ITER+1 if c seems to be a member of the set
static uint8_t mandelbrot_iterations(double cr, double ci) {
    double zr = 0.0, zi = 0.0;
    for (uint32_t i = 0; i < MAX_ITER; i++) {
        double zr2 = zr * zr;
        double zi2 = zi * zi;
        if (zr2 + zi2 > 4.0) {
            return (uint8_t)i;
        }
        zi = 2.0 * zr * zi + ci;
        zr = zr2 - zi2 + cr;
    }
    return MAX_ITER + 1;
}

//-----------------------------------------------------------------------------------------------------
// Work interface

bool work_init(void) {

    // Create a calibration segment for the area to render
    mandelbrot_calseg = XcpCreateCalSeg("mandelbrot", &mandelbrot, sizeof(mandelbrot));
    if (mandelbrot_calseg == XCP_UNDEFINED_CALSEG) {
        return false;
    }
    A2lSetSegmentAddrMode(mandelbrot_calseg, mandelbrot);
    A2lCreateParameter(mandelbrot.x, "Center of the area to render, real part", "", -2.0, 2.0);
    A2lCreateParameter(mandelbrot.y, "Center of the area to render, imaginary part", "", -2.0, 2.0);
    A2lCreateParameter(mandelbrot.width, "Width of the area to render", "", 0.0, 4.0);

    // Allocate the pixel buffer and the RGB image
    gPixels = (uint8_t *)malloc((size_t)X_RES * Y_RES);
    gImage = (uint8_t *)malloc((size_t)X_RES * Y_RES * 3);
    if (gPixels == NULL || gImage == NULL) {
        return false;
    }

    init_color_map();
    return true;
}

uint32_t work_begin_batch(bool requested) {

    // Take a copy of the parameters for this batch
    const mandelbrot_t *params = (const mandelbrot_t *)XcpLockCalSeg(mandelbrot_calseg);
    mandelbrot_t p = *params;
    XcpUnlockCalSeg(mandelbrot_calseg);

    // Render a new image only if requested, or for the first image, or if the area was changed
    if (!requested && gValid && p.x == gBatchParams.x && p.y == gBatchParams.y && p.width == gBatchParams.width) {
        return 0;
    }
    gBatchParams = p;
    gValid = true;

    // Pixel size and upper left corner of the area on the complex plane
    gPixelSize = p.width / X_RES;
    gUpperLeftRe = p.x - p.width / 2.0;
    gUpperLeftIm = p.y + gPixelSize * Y_RES / 2.0;

    printf("Render mandelbrot x=%g, y=%g, width=%g\n", p.x, p.y, p.width);
    return Y_RES;
}

void work_execute(uint32_t line) {

    assert(line < Y_RES);

    // Render one line of the image
    uint8_t *pixels = gPixels + (size_t)line * X_RES;
    double ci = gUpperLeftIm - (double)line * gPixelSize;
    for (uint32_t column = 0; column < X_RES; column++) {
        double cr = gUpperLeftRe + (double)column * gPixelSize;
        pixels[column] = mandelbrot_iterations(cr, ci);
    }
}

bool work_complete(uint32_t lines) {

    assert(lines == Y_RES);
    (void)lines;

    // Convert iteration counts to RGB and write the PNG file
    uint64_t t1 = clockGetMonotonicNs();
    for (size_t i = 0; i < (size_t)X_RES * Y_RES; i++) {
        const uint8_t *rgb = gColorMap[gPixels[i]];
        gImage[i * 3 + 0] = rgb[0];
        gImage[i * 3 + 1] = rgb[1];
        gImage[i * 3 + 2] = rgb[2];
    }
    if (!stbi_write_png(IMAGE_FILE_NAME, X_RES, Y_RES, 3, gImage, X_RES * 3)) {
        printf("Failed to write %s\n", IMAGE_FILE_NAME);
        return false;
    }
    printf("Image written to %s, resolution %dx%d, %.1f ms\n", IMAGE_FILE_NAME, X_RES, Y_RES, (double)(clockGetMonotonicNs() - t1) / 1000000.0);
    return true;
}

void work_shutdown(void) {
    free(gPixels);
    gPixels = NULL;
    free(gImage);
    gImage = NULL;
}
