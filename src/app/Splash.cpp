#include "Splash.h"

#include <atomic>
#include <cstring>
#include <memory>

#include <windows.h>

namespace {
std::atomic<bool> g_stop{false};

struct Job {
    QImage image;
    int ms = 0;
};

DWORD WINAPI splashThread(LPVOID param) {
    const std::unique_ptr<Job> job(static_cast<Job*>(param));
    const int w = job->image.width(), h = job->image.height();
    const HINSTANCE inst = GetModuleHandleW(nullptr);
    WNDCLASSW wc{};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = inst;
    wc.lpszClassName = L"IsoRecorderSplash";
    RegisterClassW(&wc);
    const int x = (GetSystemMetrics(SM_CXSCREEN) - w) / 2, y = (GetSystemMetrics(SM_CYSCREEN) - h) / 2;
    // okno warstwowe: obraz z przezroczystoscia, nie zabiera fokusu, nie ma przycisku na pasku zadan
    const HWND hwnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, wc.lpszClassName, L"",
                                      WS_POPUP, x, y, w, h, nullptr, nullptr, inst, nullptr);
    if (!hwnd) return 0;
    const HDC screen = GetDC(nullptr);
    const HDC mem = CreateCompatibleDC(screen);
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;         // wiersze od gory
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    const HBITMAP bmp = CreateDIBSection(mem, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (bmp && bits) {
        for (int row = 0; row < h; ++row)
            std::memcpy(static_cast<uchar*>(bits) + size_t(row) * size_t(w) * 4, job->image.constScanLine(row), size_t(w) * 4);
        const HGDIOBJ old = SelectObject(mem, bmp);
        POINT src{0, 0}, pos{x, y};
        SIZE size{w, h};
        BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
        UpdateLayeredWindow(hwnd, screen, &pos, &size, mem, &src, 0, &blend, ULW_ALPHA);
        ShowWindow(hwnd, SW_SHOWNOACTIVATE);
        const ULONGLONG end = GetTickCount64() + ULONGLONG(job->ms);
        MSG msg;
        while (GetTickCount64() < end && !g_stop.load()) {
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
            Sleep(15);
        }
        SelectObject(mem, old);
    }
    if (bmp) DeleteObject(bmp);
    DeleteDC(mem);
    ReleaseDC(nullptr, screen);
    DestroyWindow(hwnd);
    return 0;
}
}  // namespace

void Splash::show(const QImage& image, int milliseconds) {
    if (image.isNull() || milliseconds <= 0) return;
    g_stop = false;
    auto* job = new Job{image.convertToFormat(QImage::Format_ARGB32_Premultiplied), milliseconds};
    if (const HANDLE thread = CreateThread(nullptr, 0, &splashThread, job, 0, nullptr)) CloseHandle(thread);
    else delete job;
}

void Splash::close() { g_stop = true; }
