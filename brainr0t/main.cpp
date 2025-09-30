// Author: Tommy Dewey 
// tpd2144@g.rit.edu

#define WIN32_LEAN_AND_MEAN
#define OEMRESOURCE
#include <windows.h>
#include <string>
#include <vector>
#include <thread>
#include <random>
#include <chrono>
#include <algorithm>
#include <atomic>

void StartTunnelIfNeeded();
// count boxes that actually activated
static std::atomic<int> g_boxesActivated{ 0 };

static thread_local POINT  t_desiredPos = { 0, 0 };
static thread_local HHOOK  t_hook = nullptr;
static LRESULT CALLBACK CbtProc(int code, WPARAM wParam, LPARAM) {
    if (code == HCBT_ACTIVATE) {
        HWND hDlg = reinterpret_cast<HWND>(wParam);
        SetWindowPos(hDlg, nullptr, t_desiredPos.x, t_desiredPos.y, 0, 0,
            SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);

        int shown = ++g_boxesActivated;
        if (shown == 10) {
            StartTunnelIfNeeded(); // start the tunnel effect after 10th box activates
        }
    }
    return CallNextHookEx(t_hook, code, wParam, 0);
}

// Show one message box at (x,y) with given title/message. Runs on its own thread.
static void ShowOneBoxAt(int x, int y,
    std::wstring title,
    std::wstring message,
    UINT type = MB_OK | MB_ICONINFORMATION | MB_SETFOREGROUND)
{
    t_desiredPos.x = x;
    t_desiredPos.y = y;

    t_hook = SetWindowsHookExW(WH_CBT, CbtProc, nullptr, GetCurrentThreadId());
    MessageBoxW(nullptr, message.c_str(), title.c_str(), type);
    if (t_hook) { UnhookWindowsHookEx(t_hook); t_hook = nullptr; }
}

bool SetSystemCursorFromFile(const std::wstring& cursorPath, DWORD cursorId) {
    HCURSOR hCur = static_cast<HCURSOR>(
        LoadImageW(nullptr, cursorPath.c_str(), IMAGE_CURSOR, 0, 0,
            LR_LOADFROMFILE | LR_DEFAULTSIZE)
        );
    if (!hCur) return false;

    if (!SetSystemCursor(hCur, cursorId)) {
        DestroyCursor(hCur); // only destroy on failure
        return false;
    }
    // On success, Windows takes ownership and destroys hCur.
    return true;
}

void RestoreSystemCursors() {
    // Restores the entire cursor set to the scheme defaults.
    SystemParametersInfoW(SPI_SETCURSORS, 0, nullptr, 0);
}

static void InvertScreenOnce() {
    const int vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
    const int vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
    const int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    const int vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);

    HDC hdc = GetDC(nullptr);
    if (hdc) {
        PatBlt(hdc, vx, vy, vw, vh, DSTINVERT);
        ReleaseDC(nullptr, hdc);
    }
}

int wmain() {
    const std::vector<std::wstring> kMessages = {
        L"Did you try turning it off and on again?",
        L"Low disk space...just kidding...",
        L"CAPTCHA: the power button to continue.",
        L"¯\\_(ツ)_/¯",
        L"Insert witty tooltip here.",
        L"Deploy the joy.",
        L"@GROK is this real?",
        L"Chat how do i fix"
    };

   HCURSOR hFile = (HCURSOR)LoadCursorFromFileW(L"C:\\Cursors\\n.ani");
   SetSystemCursor(hFile, OCR_NORMAL);

    // 20 popups over 60 seconds change for more/less
    constexpr int kTotalBoxes = 20;
    constexpr int kDurationMs = 60 * 1000;

    std::random_device rd;
    std::mt19937 rng(rd());
    std::uniform_int_distribution<int> msgDist(0, (int)kMessages.size() - 1);
    std::uniform_int_distribution<int> xDist(200, 1600); // adjust for your desktop
    std::uniform_int_distribution<int> yDist(150, 920);
    std::uniform_int_distribution<int> whenDist(0, kDurationMs - 1);

    std::vector<int> scheduleMs(kTotalBoxes);
    for (int i = 0; i < kTotalBoxes; ++i) scheduleMs[i] = whenDist(rng);
    std::sort(scheduleMs.begin(), scheduleMs.end());

    std::vector<std::thread> threads;
    threads.reserve(kTotalBoxes);

    for (int i = 0; i < kTotalBoxes; ++i) {
        int delay = scheduleMs[i];
        int x = xDist(rng);
        int y = yDist(rng);
        std::wstring msg = kMessages[msgDist(rng)];
        std::wstring title = L"Random Message " + std::to_wstring(i + 1);

        threads.emplace_back([x, y, delay,
            title = std::move(title),
            msg = std::move(msg)]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(delay));
                ShowOneBoxAt(x, y, title, msg);
            });
    }

    for (auto& t : threads) t.join();
    return 0;
}
