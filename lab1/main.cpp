#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>

HWND g_hWnd = nullptr;
ID3D11Device* g_pDevice = nullptr;
ID3D11DeviceContext* g_pContext = nullptr;
IDXGISwapChain* g_pSwapChain = nullptr;
ID3D11RenderTargetView* g_pRTV = nullptr;

UINT g_Width = 1280;
UINT g_Height = 720;

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_SIZE:
        if (g_pSwapChain && wParam != SIZE_MINIMIZED)
        {
            UINT newWidth = LOWORD(lParam);
            UINT newHeight = HIWORD(lParam);

            if (newWidth > 0 && newHeight > 0)
            {
                if (g_pRTV) {
                    g_pRTV->Release();
                    g_pRTV = nullptr;
                }

                g_pSwapChain->ResizeBuffers(0, newWidth, newHeight, DXGI_FORMAT_UNKNOWN, 0);

                ID3D11Texture2D* pBackBuffer = nullptr;
                g_pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&pBackBuffer);
                g_pDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_pRTV);
                pBackBuffer->Release();

                D3D11_VIEWPORT vp = {};
                vp.Width = (float)newWidth;
                vp.Height = (float)newHeight;
                vp.MinDepth = 0.0f;
                vp.MaxDepth = 1.0f;
                g_pContext->RSSetViewports(1, &vp);

                g_Width = newWidth;
                g_Height = newHeight;
            }
        }
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

HRESULT InitDirectX(HWND hWnd)
{
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 1;
    sd.BufferDesc.Width = g_Width;
    sd.BufferDesc.Height = g_Height;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;

    UINT createDeviceFlags = 0;
#if defined(DEBUG) || defined(_DEBUG)
    createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    D3D_FEATURE_LEVEL featureLevels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0 };
    D3D_FEATURE_LEVEL featureLevel;

    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createDeviceFlags,
        featureLevels, 2, D3D11_SDK_VERSION,
        &sd, &g_pSwapChain, &g_pDevice, &featureLevel, &g_pContext);

    if (FAILED(hr)) return hr;

    ID3D11Texture2D* pBackBuffer = nullptr;
    g_pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&pBackBuffer);
    g_pDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_pRTV);
    pBackBuffer->Release();

    D3D11_VIEWPORT vp = { 0.0f, 0.0f, (float)g_Width, (float)g_Height, 0.0f, 1.0f };
    g_pContext->RSSetViewports(1, &vp);

    return S_OK;
}

void Cleanup()
{
    if (g_pRTV)        g_pRTV->Release();
    if (g_pSwapChain)  g_pSwapChain->Release();
    if (g_pContext)    g_pContext->Release();
    if (g_pDevice)     g_pDevice->Release();
}

void Render()
{
    const float color[4] = { 0.0f, 0.15f, 0.3f, 1.0f };
    g_pContext->ClearRenderTargetView(g_pRTV, color);
    g_pSwapChain->Present(1, 0);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd)
{
    WNDCLASSEX wc = { sizeof(WNDCLASSEX), CS_CLASSDC, WndProc, 0, 0, hInstance, nullptr,
                      LoadCursor(nullptr, IDC_ARROW), nullptr, nullptr, L"DX11Clear", nullptr };
    RegisterClassEx(&wc);

    RECT r = { 0, 0, (LONG)g_Width, (LONG)g_Height };
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);

    g_hWnd = CreateWindow(wc.lpszClassName, L"DirectX 11", WS_OVERLAPPEDWINDOW,
                          100, 100, r.right - r.left, r.bottom - r.top, nullptr, nullptr, hInstance, nullptr);

    ShowWindow(g_hWnd, nShowCmd);
    UpdateWindow(g_hWnd);

    if (FAILED(InitDirectX(g_hWnd)))
    {
        Cleanup();
        return 0;
    }

    MSG msg = {};
    while (msg.message != WM_QUIT)
    {
        if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        else
        {
            Render();
        }
    }

    Cleanup();
    UnregisterClass(wc.lpszClassName, hInstance);
    return (int)msg.wParam;
}
