#ifndef BELLWIN_RENDERING_H
#define BELLWIN_RENDERING_H

#include <stdint.h>

enum {
    BELLWIN_AA_SAMPLE_GRID = 4,
    BELLWIN_AA_SAMPLE_COUNT = BELLWIN_AA_SAMPLE_GRID * BELLWIN_AA_SAMPLE_GRID,
    BELLWIN_AA_SUBPIXEL_SCALE = BELLWIN_AA_SAMPLE_GRID * 2,
};

typedef struct BellwinCircleCoverage {
    uint8_t fill;
    uint8_t border;
} BellwinCircleCoverage;

/*
 * Estimate pixel coverage on a 4x4 subpixel grid. Coordinates and radii are
 * physical pixels; the returned counts can be used as alpha weights out of
 * BELLWIN_AA_SAMPLE_COUNT.
 */
static BellwinCircleCoverage bellwin_circle_coverage(
    int pixelX,
    int pixelY,
    int centerX,
    int centerY,
    int radius,
    int borderWidth
) {
    BellwinCircleCoverage coverage = {0, 0};
    const int subpixelScale = BELLWIN_AA_SUBPIXEL_SCALE;
    const int centerXSubpixels = centerX * subpixelScale;
    const int centerYSubpixels = centerY * subpixelScale;
    const int outerRadiusSubpixels = radius * subpixelScale;
    int innerRadius = radius - borderWidth;
    if (innerRadius < 0) innerRadius = 0;
    const int innerRadiusSubpixels = innerRadius * subpixelScale;
    const int64_t outerRadiusSquared = (int64_t)outerRadiusSubpixels * outerRadiusSubpixels;
    const int64_t innerRadiusSquared = (int64_t)innerRadiusSubpixels * innerRadiusSubpixels;

    for (int sampleY = 0; sampleY < BELLWIN_AA_SAMPLE_GRID; ++sampleY) {
        int y = pixelY * subpixelScale + sampleY * 2 + 1 - centerYSubpixels;
        for (int sampleX = 0; sampleX < BELLWIN_AA_SAMPLE_GRID; ++sampleX) {
            int x = pixelX * subpixelScale + sampleX * 2 + 1 - centerXSubpixels;
            int64_t distanceSquared = (int64_t)x * x + (int64_t)y * y;
            if (distanceSquared <= innerRadiusSquared) ++coverage.fill;
            else if (distanceSquared <= outerRadiusSquared) ++coverage.border;
        }
    }
    return coverage;
}

static uint8_t bellwin_horizontal_capsule_coverage(
    int pixelX,
    int pixelY,
    int left,
    int top,
    int right,
    int bottom
) {
    if (right <= left || bottom <= top) return 0;

    const int subpixelScale = BELLWIN_AA_SUBPIXEL_SCALE;
    const int radius = (bottom - top) * subpixelScale / 2;
    const int centerY = (top + bottom) * subpixelScale / 2;
    int leftCenterX = left * subpixelScale + radius;
    int rightCenterX = right * subpixelScale - radius;
    if (leftCenterX > rightCenterX) {
        leftCenterX = rightCenterX = (left + right) * subpixelScale / 2;
    }
    const int64_t radiusSquared = (int64_t)radius * radius;
    uint8_t coverage = 0;

    for (int sampleY = 0; sampleY < BELLWIN_AA_SAMPLE_GRID; ++sampleY) {
        int y = pixelY * subpixelScale + sampleY * 2 + 1 - centerY;
        for (int sampleX = 0; sampleX < BELLWIN_AA_SAMPLE_GRID; ++sampleX) {
            int x = pixelX * subpixelScale + sampleX * 2 + 1;
            int nearestX = x;
            if (nearestX < leftCenterX) nearestX = leftCenterX;
            if (nearestX > rightCenterX) nearestX = rightCenterX;
            int distanceX = x - nearestX;
            int64_t distanceSquared = (int64_t)distanceX * distanceX + (int64_t)y * y;
            if (distanceSquared <= radiusSquared) ++coverage;
        }
    }
    return coverage;
}

#endif
