#include "fractal.h"

#include <immintrin.h>

namespace showfract {
namespace {

// Original eight-pixel iteration and color calculation from the project.
void PixFractSIMD(Pixel* ppix, const __m256& xC, const __m256& yC)
{
  const int IDX_MAX = 512;
  const __m256 minus_one = { -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f };
  const __m256 two = { 2.0f, 2.0f, 2.0f, 2.0f, 2.0f, 2.0f, 2.0f, 2.0f };
  const __m256 four = { 4.0f, 4.0f, 4.0f, 4.0f, 4.0f, 4.0f, 4.0f, 4.0f };

  __m256 xi = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
  __m256 yi = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
  __m256i idx = _mm256_setzero_si256();
  __m256i active = _mm256_set1_epi32(-1);
  const __m256i minus_one_i = _mm256_set1_epi32(-1);

  for (int i = 0; i < IDX_MAX; i++)
  {
    __m256 xi2 = _mm256_mul_ps(xi, xi);
    __m256 yi2 = _mm256_mul_ps(yi, yi);
    __m256 neg_yi2 = _mm256_mul_ps(yi2, minus_one);
    __m256 sum = _mm256_sub_ps(xi2, neg_yi2); // xi2 + yi2

    __m256 mask_ps = _mm256_cmp_ps(sum, four, _CMP_LT_OQ);
    __m256i mask_i = _mm256_castps_si256(mask_ps);
    active = _mm256_and_si256(active, mask_i);

    if (_mm256_testz_si256(active, active))
      break;

    __m256i inc = _mm256_and_si256(active, minus_one_i);
    idx = _mm256_sub_epi32(idx, inc);

    __m256 x = xi;
    __m256 y = yi;

    __m256 x2 = _mm256_mul_ps(x, x);
    __m256 y2 = _mm256_mul_ps(y, y);
    __m256 x2_minus_y2 = _mm256_sub_ps(x2, y2);
    __m256 neg_xC = _mm256_mul_ps(xC, minus_one);
    xi = _mm256_sub_ps(x2_minus_y2, neg_xC); // x2 - y2 + xC

    __m256 xy = _mm256_mul_ps(x, y);
    __m256 two_xy = _mm256_mul_ps(xy, two);
    __m256 neg_yC = _mm256_mul_ps(yC, minus_one);
    yi = _mm256_sub_ps(two_xy, neg_yC); // 2*x*y + yC
  }

  const __m256i maskFF = _mm256_set1_epi32(0xFF);
  __m256i red = _mm256_and_si256(_mm256_mullo_epi32(idx, _mm256_set1_epi32(3)), maskFF);
  __m256i green = _mm256_and_si256(_mm256_mullo_epi32(idx, _mm256_set1_epi32(5)), maskFF);
  __m256i blue = _mm256_and_si256(_mm256_mullo_epi32(idx, _mm256_set1_epi32(7)), maskFF);

  __m256i red_shift = _mm256_slli_epi32(red, 16);
  __m256i green_shift = _mm256_slli_epi32(green, 8);
  __m256i pix = _mm256_or_si256(_mm256_or_si256(red_shift, green_shift), blue);

  _mm256_storeu_si256((__m256i*)ppix, pix);
}


} // namespace

void renderSimdRow(Frame& frame, const Grid& grid, int y)
{
    const float yc = grid.y0 + grid.dy*y;
    const __m256 imaginary = _mm256_set1_ps(yc);
    Pixel* output = frame.row(y);
    int x = 0;

    for (; x + 8 <= frame.width; x += 8) {
        // Compute the same coordinate for each lane as the scalar mode.
        const __m256 real = _mm256_setr_ps(
            grid.x0 + grid.dx*x,       grid.x0 + grid.dx*(x + 1),
            grid.x0 + grid.dx*(x + 2), grid.x0 + grid.dx*(x + 3),
            grid.x0 + grid.dx*(x + 4), grid.x0 + grid.dx*(x + 5),
            grid.x0 + grid.dx*(x + 6), grid.x0 + grid.dx*(x + 7));
        PixFractSIMD(output + x, real, imaginary);
    }
    for (; x < frame.width; ++x)
        PixFract(output + x, grid.x0 + grid.dx*x, yc);
}

} // namespace showfract
