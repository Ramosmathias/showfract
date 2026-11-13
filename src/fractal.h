#pragma once

#include <cstdint>
#include <vector>

namespace showfract {

using Pixel = std::uint32_t;

struct Frame {
    Frame(int width, int height);

    Pixel* row(int y);

    int width;
    int height;
    std::vector<Pixel> pixels;
};

struct Grid {
    float x0 = -1.0f;
    float y0 = -1.0f;
    float dx = 0.001f;
    float dy = 0.001f;
};

enum class Mode {
    sequential, bands, dynamic, openmp, ppl, vector, buffer, iterator, async, simd
};

bool supportsAvx2();
void render(Frame& frame, const Grid& grid, Mode mode);

void PixFract(Pixel* pixel, float xc, float yc);
void renderSimdRow(Frame& frame, const Grid& grid, int y);

} // namespace showfract
