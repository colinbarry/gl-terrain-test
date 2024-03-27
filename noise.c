#include "noise.h"
#include <math.h>

static const int STRIDE = 1999;

static float noise(int x)
{
    x = (x << 13) ^ x;
    return (1.0f - ((x * (x * x * 15731) + 1376312589) & 0x7fffffff) / 1073741824.0f);
}

static float cos_interp(const float y0, const float y1, const float mu)
{
    float mu2 = (1 - cos(mu * M_PI)) / 2;
    return y0 * (1 - mu2) + y1 * mu2;
}

float noise2d(float x, float y, int seed, float frequency)
{
    const float sx = x * frequency;
    const float sy = y * frequency;
    const int ix = sx > 0.0 ? (int)sx : (int)(sx - 1);
    const int iy = sy > 0.0 ? (int)sy : (int)(sy - 1);
    const float rx = sx - ix;
    const float ry = sy - iy;

    const float a0 = noise(ix + iy * STRIDE + seed);
    const float a1 = noise(ix + iy * STRIDE + 1 + seed);
    const float a2 = noise(ix + (iy + 1) * STRIDE + seed);
    const float a3 = noise(ix + (iy + 1) * STRIDE + 1 + seed);

    const float b0 = cos_interp(a0, a1, rx);
    const float b1 = cos_interp(a2, a3, rx);
    return cos_interp(b0, b1, ry);
}
