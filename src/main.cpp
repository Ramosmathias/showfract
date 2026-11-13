#include "fractal.h"

#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cwchar>
#include <exception>
#include <fstream>
#include <iomanip>
#include <string>
#include <utility>

namespace {

using showfract::Mode;

struct Choice {
    Mode mode;
    const wchar_t* label;
    const char* name;
};

constexpr std::array choices{
    Choice{Mode::sequential, L"Monothread", "scalar"},
    Choice{Mode::bands, L"Multithread - bandes fixes", "bands"},
    Choice{Mode::dynamic, L"Multithread - lignes dynamiques", "dynamic"},
    Choice{Mode::openmp, L"OpenMP", "openmp"},
    Choice{Mode::ppl, L"PPL - parallel_for", "ppl"},
    Choice{Mode::vector, L"C++17 - vecteur", "vector"},
    Choice{Mode::buffer, L"C++17 - tableau", "buffer"},
    Choice{Mode::iterator, L"C++17 - itérateur", "iterator"},
    Choice{Mode::async, L"C++17 - std::async", "async"},
    Choice{Mode::simd, L"SIMD - AVX2", "avx2"}
};

constexpr UINT firstMode = 100;
constexpr int frameWidth = 1920;
constexpr int frameHeight = 1080;
constexpr wchar_t windowClass[] = L"ShowFractWindow";

struct Demo {
    showfract::Frame frame{frameWidth, frameHeight};
    showfract::Grid grid;
};

void drawFrame(HWND window, const showfract::Frame& frame)
{
    PAINTSTRUCT paint{};
    HDC context = BeginPaint(window, &paint);
    RECT area{};
    GetClientRect(window, &area);
    FillRect(context, &area, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));

    BITMAPINFO bitmap{};
    bitmap.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmap.bmiHeader.biWidth = frame.width;
    bitmap.bmiHeader.biHeight = -frame.height;
    bitmap.bmiHeader.biPlanes = 1;
    bitmap.bmiHeader.biBitCount = 32;
    bitmap.bmiHeader.biCompression = BI_RGB;

    const LONG width = area.right;
    const LONG height = area.bottom;
    const LONG scaledWidth = std::min(width, height * frame.width / frame.height);
    const LONG scaledHeight = scaledWidth * frame.height / frame.width;
    StretchDIBits(context, (width - scaledWidth) / 2, (height - scaledHeight) / 2,
        scaledWidth, scaledHeight, 0, 0, frame.width, frame.height,
        frame.pixels.data(), &bitmap, DIB_RGB_COLORS, SRCCOPY);
    EndPaint(window, &paint);
}

void calculate(HWND window, Demo& demo, const Choice& choice)
{
    constexpr int repetitions = 10;
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < repetitions; ++i)
        showfract::render(demo.frame, demo.grid, choice.mode);
    const auto elapsed = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - start).count() / repetitions;

    wchar_t title[160]{};
    swprintf_s(title, L"ShowFract | %ls | %.2f ms / image", choice.label, elapsed);
    SetWindowTextW(window, title);
    InvalidateRect(window, nullptr, FALSE);
}

LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
    }
    auto* demo = reinterpret_cast<Demo*>(GetWindowLongPtrW(window, GWLP_USERDATA));

    switch (message) {
    case WM_COMMAND: {
        const UINT id = LOWORD(wParam);
        if (id >= firstMode && id < firstMode + choices.size()) {
            try {
                calculate(window, *demo, choices[id - firstMode]);
            } catch (const std::exception&) {
                MessageBoxW(window, L"Le calcul a échoué.", L"ShowFract", MB_OK | MB_ICONERROR);
            }
            return 0;
        }
        break;
    }
    case WM_PAINT:
        if (demo) {
            drawFrame(window, demo->frame);
            return 0;
        }
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

int verifyRendering(const wchar_t* reportPath)
{
    std::ofstream report(reportPath);
    if (!report)
        return 2;
    report << std::fixed << std::setprecision(2);

    bool allMatch = true;
    for (const auto& size : {std::pair{frameWidth, frameHeight}, std::pair{257, 127}}) {
        showfract::Frame reference(size.first, size.second);
        showfract::Frame result(size.first, size.second);
        const showfract::Grid grid;
        showfract::render(reference, grid, Mode::sequential);
        report << size.first << "x" << size.second << '\n';
        for (const auto& choice : choices) {
            if (choice.mode == Mode::simd && !showfract::supportsAvx2()) {
                report << choice.name << ": skipped (AVX2 unavailable)\n";
                continue;
            }
            std::fill(result.pixels.begin(), result.pixels.end(), 0xffffffffu);
            const auto start = std::chrono::steady_clock::now();
            showfract::render(result, grid, choice.mode);
            const auto elapsed = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - start).count();
            const bool same = result.pixels == reference.pixels;
            report << choice.name << ": " << (same ? "OK" : "DIFFERENT")
                   << " (" << elapsed << " ms)\n";
            allMatch = allMatch && same;
        }
    }
    report.flush();
    return report ? (allMatch ? 0 : 1) : 2;
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show)
{
    int argumentCount = 0;
    auto arguments = CommandLineToArgvW(GetCommandLineW(), &argumentCount);
    if (!arguments)
        return 2;
    if (argumentCount == 3 && std::wcscmp(arguments[1], L"--verify") == 0) {
        const std::wstring reportPath(arguments[2]);
        LocalFree(arguments);
        return verifyRendering(reportPath.c_str());
    }
    LocalFree(arguments);

    Demo demo;
    const Choice& initial = showfract::supportsAvx2() ? choices.back() : choices[2];

    WNDCLASSW windowDefinition{};
    windowDefinition.style = CS_HREDRAW | CS_VREDRAW;
    windowDefinition.lpfnWndProc = windowProc;
    windowDefinition.hInstance = instance;
    windowDefinition.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowDefinition.lpszClassName = windowClass;
    if (!RegisterClassW(&windowDefinition))
        return 2;

    HMENU menu = CreateMenu();
    HMENU calculateMenu = CreatePopupMenu();
    if (!menu || !calculateMenu)
        return 2;
    for (UINT i = 0; i < choices.size(); ++i) {
        const bool enabled = choices[i].mode != Mode::simd || showfract::supportsAvx2();
        AppendMenuW(calculateMenu, MF_STRING | (enabled ? MF_ENABLED : MF_GRAYED),
            firstMode + i, choices[i].label);
    }
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(calculateMenu), L"&Calculer");

    HWND window = CreateWindowExW(0, windowClass, L"ShowFract", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 1180, 720, nullptr, menu, instance, &demo);
    if (!window) {
        DestroyMenu(menu);
        return 2;
    }
    calculate(window, demo, initial);
    ShowWindow(window, show);
    UpdateWindow(window);

    MSG event{};
    BOOL status;
    while ((status = GetMessageW(&event, nullptr, 0, 0)) > 0) {
        TranslateMessage(&event);
        DispatchMessageW(&event);
    }
    return status == -1 ? 2 : static_cast<int>(event.wParam);
}
