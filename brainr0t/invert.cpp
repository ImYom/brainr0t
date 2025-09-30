#define UNICODE
#define _UNICODE
#define _WIN32_WINNT 0x0A00
#include <windows.h>

COLORREF gColors[] = {
    RGB(255,0,0), RGB(0,255,0), RGB(0,0,255),
    RGB(255,0,255), RGB(0,255,255), RGB(255,255,0),
    RGB(255,0,170), RGB(0,255,170), RGB(255,255,255), RGB(0,0,0)
};
int gIdx = 0;
BYTE gAlpha = 160;
HWND gHost = nullptr;
bool gVisible = true;

void PaintLayer() {
    int x = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int y = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int cx = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int cy = GetSystemMetrics(SM_CYVIRTUALSCREEN);

    HDC hdcScreen = GetDC(nullptr);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);

    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = cx;
    bi.bmiHeader.biHeight = -cy; // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HBITMAP hbmp = CreateDIBSection(hdcScreen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    HGDIOBJ old = SelectObject(hdcMem, hbmp);

    // Fill with current color
    COLORREF c = gColors[gIdx];
    BYTE r = GetRValue(c), g = GetGValue(c), b = GetBValue(c);
    DWORD* p = (DWORD*)bits;
    size_t total = (size_t)cx * (size_t)cy;
    DWORD px = (gAlpha << 24) | (b << 16) | (g << 8) | (r);
    for (size_t i = 0; i < total; ++i) p[i] = px;

    POINT ptSrc = { 0,0 };
    SIZE sizeWnd = { cx, cy };
    POINT ptDest = { x,y };
    BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    UpdateLayeredWindow(gHost, hdcScreen, &ptDest, &sizeWnd, hdcMem, &ptSrc, 0, &bf, ULW_ALPHA);

    SelectObject(hdcMem, old);
    DeleteObject(hbmp);
    DeleteDC(hdcMem);
    ReleaseDC(nullptr, hdcScreen);
}

void ResizeAndPaint() {
    int x = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int y = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int cx = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int cy = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    SetWindowPos(gHost, HWND_TOPMOST, x, y, cx, cy, SWP_SHOWWINDOW | SWP_NOACTIVATE);
    PaintLayer();
}

LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_CREATE:
        ResizeAndPaint();
        return 0;
    case WM_DISPLAYCHANGE:
    case WM_SETTINGCHANGE:
    case WM_SIZE:
    case WM_DPICHANGED:
        ResizeAndPaint();
        return 0;
    case WM_HOTKEY:
        switch ((int)w) {
        case 1: // toggle
            gVisible = !gVisible;
            ShowWindow(gHost, gVisible ? SW_SHOW : SW_HIDE);
            break;
        case 2: // opacity down
            gAlpha = (BYTE)(gAlpha > 20 ? gAlpha - 15 : 20);
            PaintLayer();
            break;
        case 3: // opacity up
            gAlpha = (BYTE)(gAlpha < 240 ? gAlpha + 15 : 240);
            PaintLayer();
            break;
        case 4: // cycle color
            gIdx = (gIdx + 1) % (int)(sizeof(gColors) / sizeof(gColors[0]));
            PaintLayer();
            break;
        case 5: // quit
            PostQuitMessage(0);
            break;
        }
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(h, m, w, l);
}

int WINAPI wWinMain(HINSTANCE hI, HINSTANCE, PWSTR, int) {
    SetProcessDPIAware();
    WNDCLASS wc{}; wc.lpfnWndProc = WndProc; wc.hInstance = hI; wc.lpszClassName = L"TintGlass";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClass(&wc);

    gHost = CreateWindowEx(
        WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW,
        wc.lpszClassName, L"", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, hI, nullptr);

    if (!gHost) return 1;

    // Global hotkeys
    RegisterHotKey(nullptr, 1, MOD_CONTROL | MOD_ALT, 'T'); // toggle
    RegisterHotKey(nullptr, 2, MOD_CONTROL | MOD_ALT, VK_OEM_4); // [
    RegisterHotKey(nullptr, 3, MOD_CONTROL | MOD_ALT, VK_OEM_6); // ]
    RegisterHotKey(nullptr, 4, MOD_CONTROL | MOD_ALT, VK_OEM_5); // \
        RegisterHotKey(nullptr, 5, MOD_CONTROL|MOD_ALT, 'Q'); // quit

    ShowWindow(gHost, SW_SHOW);
    ResizeAndPaint();

    MSG msg; while (GetMessage(&msg, nullptr, 0, 0)) { TranslateMessage(&msg); DispatchMessage(&msg); }

    UnregisterHotKey(nullptr, 1); UnregisterHotKey(nullptr, 2);
    UnregisterHotKey(nullptr, 3); UnregisterHotKey(nullptr, 4); UnregisterHotKey(nullptr, 5);
    DestroyWindow(gHost);
    return 0;
}