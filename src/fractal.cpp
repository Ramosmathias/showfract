#include "fractal.h"

#include <windows.h>
#include <omp.h>
#include <ppl.h>

#include <algorithm>
#include <atomic>
#include <execution>
#include <future>
#include <iterator>
#include <memory>
#include <numeric>

namespace showfract {

Frame::Frame(int width, int height)
    : width(width), height(height), pixels(static_cast<std::size_t>(width) * height) {}

Pixel* Frame::row(int y)
{
    return pixels.data() + static_cast<std::size_t>(y) * width;
}

void PixFract(Pixel* pixel, float xc, float yc)
{
    const int IDX_MAX = 512;
    int idx = 0;
    float xi = 0;
    float yi = 0;

    while (idx < IDX_MAX && (xi*xi + yi*yi) < 4.0f) {
        const float x = xi;
        const float y = yi;
        xi = x*x - y*y + xc;
        yi = 2.0f*x*y + yc;
        idx++;
    }

    const Pixel red = (3*idx) % 256;
    const Pixel green = (5*idx) % 256;
    const Pixel blue = (7*idx) % 256;
    *pixel = (red << 16) | (green << 8) | blue;
}

namespace {

void renderScalarRow(Frame& frame, const Grid& grid, int y)
{
    Pixel* pixel = frame.row(y);
    const float yc = grid.y0 + grid.dy*y;
    for (int x = 0; x < frame.width; ++x)
        PixFract(pixel++, grid.x0 + grid.dx*x, yc);
}

using RowRenderer = void (*)(Frame&, const Grid&, int);

struct Band {
    Frame* frame;
    const Grid* grid;
    int begin;
    int end;
};

DWORD WINAPI renderBand(void* argument)
{
    const auto& band = *static_cast<const Band*>(argument);
    for (int y = band.begin; y < band.end; ++y)
        renderScalarRow(*band.frame, *band.grid, y);
    return 0;
}

struct RowQueue {
    Frame& frame;
    const Grid& grid;
    RowRenderer renderRow;
    std::atomic<int> next{0};
};

DWORD WINAPI renderFromQueue(void* argument)
{
    auto& queue = *static_cast<RowQueue*>(argument);
    for (int y = queue.next.fetch_add(1, std::memory_order_relaxed);
         y < queue.frame.height;
         y = queue.next.fetch_add(1, std::memory_order_relaxed))
        queue.renderRow(queue.frame, queue.grid, y);
    return 0;
}

int workerCount(int height)
{
    SYSTEM_INFO system{};
    GetSystemInfo(&system);
    return std::min(height, static_cast<int>(system.dwNumberOfProcessors));
}

void waitForWorkers(const std::vector<HANDLE>& workers)
{
    for (HANDLE worker : workers) {
        WaitForSingleObject(worker, INFINITE);
        CloseHandle(worker);
    }
}

void renderBands(Frame& frame, const Grid& grid)
{
    const int count = workerCount(frame.height);
    std::vector<Band> bands(count);
    std::vector<HANDLE> workers;
    workers.reserve(count);
    for (int i = 0; i < count; ++i) {
        auto& band = bands[i];
        band = {&frame, &grid, i*frame.height/count, (i + 1)*frame.height/count};
        if (HANDLE worker = CreateThread(nullptr, 0, renderBand, &band, 0, nullptr))
            workers.push_back(worker);
        else
            renderBand(&band);
    }
    waitForWorkers(workers);
}

void renderDynamic(Frame& frame, const Grid& grid, RowRenderer renderRow)
{
    RowQueue queue{frame, grid, renderRow};
    const int count = workerCount(frame.height);
    std::vector<HANDLE> workers;
    workers.reserve(count);
    for (int i = 1; i < count; ++i) {
        if (HANDLE worker = CreateThread(nullptr, 0, renderFromQueue, &queue, 0, nullptr))
            workers.push_back(worker);
    }
    renderFromQueue(&queue);
    waitForWorkers(workers);
}

void renderAsync(Frame& frame, const Grid& grid)
{
    RowQueue queue{frame, grid, renderScalarRow};
    std::vector<std::future<DWORD>> workers;
    const int count = workerCount(frame.height);
    workers.reserve(count);
    for (int i = 1; i < count; ++i)
        workers.push_back(std::async(std::launch::async, renderFromQueue, &queue));
    renderFromQueue(&queue);
    for (auto& worker : workers)
        worker.get();
}

class RowIndex {
public:
    using iterator_category = std::forward_iterator_tag;
    using value_type = int;
    using difference_type = std::ptrdiff_t;
    using pointer = void;
    using reference = int;

    explicit RowIndex(int y = 0) : y(y) {}

    int operator*() const { return y; }
    RowIndex& operator++() { ++y; return *this; }
    RowIndex operator++(int) { const auto previous = *this; ++*this; return previous; }
    bool operator==(RowIndex other) const { return y == other.y; }
    bool operator!=(RowIndex other) const { return y != other.y; }

private:
    int y;
};

} // namespace

bool supportsAvx2()
{
    return IsProcessorFeaturePresent(PF_AVX2_INSTRUCTIONS_AVAILABLE) != FALSE;
}

void render(Frame& frame, const Grid& grid, Mode mode)
{
    const auto scalar = [&](int y) { renderScalarRow(frame, grid, y); };
    switch (mode) {
    case Mode::sequential:
        for (int y = 0; y < frame.height; ++y)
            scalar(y);
        break;
    case Mode::bands:
        renderBands(frame, grid);
        break;
    case Mode::dynamic:
        renderDynamic(frame, grid, renderScalarRow);
        break;
    case Mode::openmp:
        #pragma omp parallel for
        for (int y = 0; y < frame.height; ++y)
            scalar(y);
        break;
    case Mode::ppl:
        concurrency::parallel_for(0, frame.height, scalar);
        break;
    case Mode::vector: {
        std::vector<int> rows(frame.height);
        std::iota(rows.begin(), rows.end(), 0);
        std::for_each(std::execution::par, rows.begin(), rows.end(), scalar);
        break;
    }
    case Mode::buffer: {
        auto rows = std::make_unique<int[]>(frame.height);
        auto* end = rows.get() + frame.height;
        std::iota(rows.get(), end, 0);
        std::for_each(std::execution::par, rows.get(), end, scalar);
        break;
    }
    case Mode::iterator:
        std::for_each(std::execution::par, RowIndex(0), RowIndex(frame.height), scalar);
        break;
    case Mode::async:
        renderAsync(frame, grid);
        break;
    case Mode::simd:
        renderDynamic(frame, grid, supportsAvx2() ? renderSimdRow : renderScalarRow);
        break;
    }
}

} // namespace showfract
