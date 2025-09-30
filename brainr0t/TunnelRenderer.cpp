#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <d3dcompiler.h>
#include <thread>
#include <chrono>
#include <atomic>
#include <mutex>
#include <atomic>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

void StartTunnelIfNeeded();
static std::atomic<bool> g_tunnelStarted{ false };

namespace {
    // D3D globals (render thread only)
    HWND                     g_hwnd = nullptr;
    ID3D11Device* g_dev = nullptr;
    ID3D11DeviceContext* g_ctx = nullptr;
    IDXGISwapChain* g_swap = nullptr;
    ID3D11RenderTargetView* g_rtv = nullptr;
    ID3D11VertexShader* g_vs = nullptr;
    ID3D11PixelShader* g_ps = nullptr;
    ID3D11Buffer* g_cbuf = nullptr;

    // start ocne
    std::once_flag g_once;

    struct CBuf { float time; float w; float h; float pad; };

    const char* g_HLSL = R"(
cbuffer CBuf : register(b0) { float time; float w; float h; float pad; }

struct VSOut { float4 pos : SV_Position; };

VSOut VSMain(uint vid : SV_VertexID) {
    float2 verts[3] = { float2(-1,-1), float2(-1,3), float2(3,-1) };
    VSOut o; o.pos = float4(verts[vid], 0.0, 1.0); return o;
}

float3 palette(float t) {
    float3 a = float3(0.5, 0.5, 0.5);
    float3 b = float3(0.5, 0.5, 0.5);
    float3 c = float3(1.0, 1.0, 1.0);
    float3 d = float3(0.00, 0.10, 0.20);
    return a + b*cos(6.28318*(c*t + d));
}
float bands(float x) { return 0.5 + 0.5*sin(x); }

float4 PSMain(VSOut i) : SV_Target {
    float2 uv = i.pos.xy / float2(w, h);
    float2 p = (uv - 0.5) * float2(w/h, 1.0) * 2.0;

    float r = length(p) + 1e-5;
    float a = atan2(p.y, p.x);

    float speed = 0.8;
    float tunnel = 8.0 / r + time * 2.0 * speed;
    float swirl = a + 0.5*sin(time*0.7) * r;

    float shade = bands(tunnel + 6.0*swirl);

    float3 base = palette(frac(0.1*tunnel + 0.05*time + 0.3*swirl));
    float3 col = lerp(base*0.2, base, shade);

    float vig = smoothstep(1.3, 0.4, length(p));
    col *= vig;

    return float4(col, 1.0);
}
)";

    static void SetViewport(UINT w, UINT h) {
        if (!g_ctx) return;
        D3D11_VIEWPORT vp{};
        vp.TopLeftX = 0.0f; vp.TopLeftY = 0.0f;
        vp.Width = (FLOAT)w; vp.Height = (FLOAT)h;
        vp.MinDepth = 0.0f; vp.MaxDepth = 1.0f;
        g_ctx->RSSetViewports(1, &vp);
    }

    LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM wParam, LPARAM lParam) {
        switch (m) {
        case WM_DESTROY: PostQuitMessage(0); return 0;
        case WM_SIZE:
        case WM_DISPLAYCHANGE:
        case WM_SETTINGCHANGE:
        {
            if (!g_ctx || !g_swap) break;
            if (g_rtv) { g_rtv->Release(); g_rtv = nullptr; }
            RECT rc; GetClientRect(h, &rc);
            UINT ww = rc.right - rc.left, hh = rc.bottom - rc.top;
            g_swap->ResizeBuffers(0, ww, hh, DXGI_FORMAT_UNKNOWN, 0);
            ID3D11Texture2D* back = nullptr;
            if (SUCCEEDED(g_swap->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&back))) {
                g_dev->CreateRenderTargetView(back, nullptr, &g_rtv);
                back->Release();
            }
            SetViewport(ww, hh);
            return 0;
        }
        }
        return DefWindowProc(h, m, wParam, lParam);
    }

    bool CreateD3D(HWND hwnd, UINT w, UINT h) {
        DXGI_SWAP_CHAIN_DESC sd{};
        sd.BufferCount = 2;
        sd.BufferDesc.Width = w;
        sd.BufferDesc.Height = h;
        sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.OutputWindow = hwnd;
        sd.SampleDesc.Count = 1;
        sd.Windowed = TRUE;
        sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

        UINT flags = 0;
#if defined(_DEBUG)
        flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
        D3D_FEATURE_LEVEL fl;
        if (FAILED(D3D11CreateDeviceAndSwapChain(
            nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, nullptr, 0,
            D3D11_SDK_VERSION, &sd, &g_swap, &g_dev, &fl, &g_ctx))) return false;

        ID3D11Texture2D* back = nullptr;
        g_swap->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&back);
        g_dev->CreateRenderTargetView(back, nullptr, &g_rtv);
        back->Release();

        ID3DBlob* vsb = nullptr; ID3DBlob* psb = nullptr; ID3DBlob* err = nullptr;

        if (FAILED(D3DCompile(g_HLSL, (UINT)strlen(g_HLSL), nullptr, nullptr, nullptr, "VSMain", "vs_5_0", 0, 0, &vsb, &err))) {
            if (err) { MessageBoxA(nullptr, (char*)err->GetBufferPointer(), "VS compile error", MB_OK); err->Release(); }
            return false;
        }
        if (FAILED(D3DCompile(g_HLSL, (UINT)strlen(g_HLSL), nullptr, nullptr, nullptr, "PSMain", "ps_5_0", 0, 0, &psb, &err))) {
            if (err) { MessageBoxA(nullptr, (char*)err->GetBufferPointer(), "PS compile error", MB_OK); err->Release(); }
            if (vsb) vsb->Release();
            return false;
        }
        g_dev->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, &g_vs);
        g_dev->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), nullptr, &g_ps);
        if (vsb) vsb->Release();
        if (psb) psb->Release();

        D3D11_BUFFER_DESC cbd{};
        cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        cbd.ByteWidth = sizeof(CBuf);
        cbd.Usage = D3D11_USAGE_DYNAMIC;
        cbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(g_dev->CreateBuffer(&cbd, nullptr, &g_cbuf))) return false;

        SetViewport(w, h);
        return true;
    }

    void DestroyD3D() {
        if (g_cbuf) g_cbuf->Release();
        if (g_ps) g_ps->Release();
        if (g_vs) g_vs->Release();
        if (g_rtv) g_rtv->Release();
        if (g_swap) g_swap->Release();
        if (g_ctx) g_ctx->Release();
        if (g_dev) g_dev->Release();
        g_cbuf = nullptr; g_ps = nullptr; g_vs = nullptr;
        g_rtv = nullptr; g_swap = nullptr; g_ctx = nullptr; g_dev = nullptr;
    }

    void RenderLoop() {
        // cover the whole virtual-screen (all monitors)
        const int vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
        const int vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
        const int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
        const int vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);

        // class
        WNDCLASSEXW wc{ sizeof(WNDCLASSEXW) };
        wc.lpfnWndProc = WndProc;
        wc.hInstance = GetModuleHandle(nullptr);
        wc.lpszClassName = L"TunnelBGWindow";
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
        RegisterClassExW(&wc);

        DWORD ex = WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW; // no taskbar, no focus
        DWORD st = WS_POPUP;

        g_hwnd = CreateWindowExW(ex, wc.lpszClassName, L"", st, vx, vy, vw, vh, nullptr, nullptr, wc.hInstance, nullptr);
        if (!g_hwnd) return;

        ShowWindow(g_hwnd, SW_SHOW);
        SetWindowPos(g_hwnd, HWND_BOTTOM, vx, vy, vw, vh, SWP_SHOWWINDOW | SWP_NOACTIVATE);

        if (!CreateD3D(g_hwnd, (UINT)vw, (UINT)vh)) return;

        auto t0 = std::chrono::high_resolution_clock::now();
        MSG msg{};
        for (;;) {
            while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
                if (msg.message == WM_QUIT) {
                    DestroyD3D();
                    return;
                }
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }

            // time
            float timeSec = std::chrono::duration<float>(std::chrono::high_resolution_clock::now() - t0).count();

            // constants
            D3D11_MAPPED_SUBRESOURCE map;
            if (SUCCEEDED(g_ctx->Map(g_cbuf, 0, D3D11_MAP_WRITE_DISCARD, 0, &map))) {
                CBuf* cb = (CBuf*)map.pData;
                cb->time = timeSec;
                cb->w = (float)vw;
                cb->h = (float)vh;
                cb->pad = 0.0f;
                g_ctx->Unmap(g_cbuf, 0);
            }

            // draw
            float clear[4] = { 0.02f, 0.02f, 0.03f, 1.0f };
            g_ctx->OMSetRenderTargets(1, &g_rtv, nullptr);
            g_ctx->ClearRenderTargetView(g_rtv, clear);
            g_ctx->VSSetShader(g_vs, nullptr, 0);
            g_ctx->PSSetShader(g_ps, nullptr, 0);
            g_ctx->VSSetConstantBuffers(0, 1, &g_cbuf);
            g_ctx->PSSetConstantBuffers(0, 1, &g_cbuf);
            g_ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            g_ctx->Draw(3, 0);

            g_swap->Present(1, 0); // vsync
        }
    }
} // namespace

void StartTunnelIfNeeded() {
    bool expected = false;
    if (!g_tunnelStarted.compare_exchange_strong(expected, true)) return;

    std::thread([] {
        // Make DPI aware so virtual-screen metrics are in pixels.
        if (HMODULE hUser = GetModuleHandleW(L"user32.dll")) {
            using SetDpiAwareFn = BOOL(WINAPI*)();
            if (auto fn = (SetDpiAwareFn)GetProcAddress(hUser, "SetProcessDPIAware")) {
                fn();
            }
        }

        // Entire virtual desktop (all monitors).
        const int vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
        const int vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
        const int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
        const int vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);

        HDC hdc = GetDC(nullptr);
        if (!hdc) return;

        const int insetPx = 12;   // shrink per frame in pixels
        const int sleepMs = 16;   // ~60 FPS

        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);

        for (;;) {
            // Copy the whole screen into a slightly smaller, centered rect.
            StretchBlt(
                hdc,
                vx + insetPx, vy + insetPx, vw - insetPx * 2, vh - insetPx * 2, // destination
                hdc,
                vx, vy, vw, vh,                                                  // source
                SRCCOPY
            );

            // Dark border “walls”
            HBRUSH hb = (HBRUSH)GetStockObject(BLACK_BRUSH);
            RECT r1{ vx, vy, vx + vw, vy + insetPx };
            RECT r2{ vx, vy + vh - insetPx, vx + vw, vy + vh };
            RECT r3{ vx, vy + insetPx, vx + insetPx, vy + vh - insetPx };
            RECT r4{ vx + vw - insetPx, vy + insetPx, vx + vw, vy + vh - insetPx };
            FillRect(hdc, &r1, hb); FillRect(hdc, &r2, hb);
            FillRect(hdc, &r3, hb); FillRect(hdc, &r4, hb);

            Sleep(sleepMs);
        }

        // Not reached in this forever loop, but here for completeness:
        // ReleaseDC(nullptr, hdc);
        }).detach(); 
}