#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>
#include <vector>
#include <cassert>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "dxguid.lib")

using namespace DirectX;

ID3D11Device* g_D3DDevice = nullptr;
ID3D11DeviceContext* g_ImmediateContext = nullptr;
IDXGISwapChain* g_SwapChain = nullptr;
ID3D11RenderTargetView* g_RenderTarget = nullptr;

ID3D11VertexShader* g_VS = nullptr;
ID3D11PixelShader* g_PS = nullptr;
ID3D11InputLayout* g_InputLayout = nullptr;
ID3D11Buffer* g_VertexBuffer = nullptr;
ID3D11Buffer* g_IndexBuffer = nullptr;
ID3D11Buffer* g_ModelBuffer = nullptr;
ID3D11Buffer* g_ViewProjBuffer = nullptr;

constexpr int WINDOW_WIDTH = 800;
constexpr int WINDOW_HEIGHT = 600;

float g_CamPhi = 0.0f;
float g_CamTheta = XM_PIDIV2;
float g_CamDist = 3.0f;

LARGE_INTEGER g_StartTime, g_Freq;
float g_TotalTime = 0.0f;

LRESULT CALLBACK WindowProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace utils {
    template<typename T>
    void SafeRelease(T*& ptr) noexcept {
        if (ptr) {
            ptr->Release();
            ptr = nullptr;
        }
    }
}

static const char* g_VS_Source = R"(
cbuffer ModelBuffer : register(b0)
{
    row_major float4x4 model;
};
cbuffer ViewProjBuffer : register(b1)
{
    row_major float4x4 viewProj;
};
struct VS_INPUT {
    float3 pos : POSITION;
    float4 color : COLOR;
};
struct VS_OUTPUT {
    float4 pos : SV_Position;
    float4 color : COLOR;
};
VS_OUTPUT main(VS_INPUT input) {
    VS_OUTPUT output;
    float4 worldPos = mul(float4(input.pos, 1.0), model);
    output.pos = mul(worldPos, viewProj);
    output.color = input.color;
    return output;
}
)";

static const char* g_PS_Source = R"(
struct PS_INPUT {
    float4 pos : SV_Position;
    float4 color : COLOR;
};
float4 main(PS_INPUT input) : SV_Target {
    return input.color;
}
)";

struct ColoredVertex {
    float position[3];
    uint32_t rgba;
};

static const ColoredVertex g_Vertices[] = {
    { { -0.5f, -0.5f, -0.5f }, 0x00FF0000 }, // red
    { {  0.5f, -0.5f, -0.5f }, 0x0000FF00 }, // green
    { {  0.5f,  0.5f, -0.5f }, 0x000000FF }, // blue
    { { -0.5f,  0.5f, -0.5f }, 0x00FFFF00 }, // yellow
    { { -0.5f, -0.5f,  0.5f }, 0x00FF00FF }, // magenta
    { {  0.5f, -0.5f,  0.5f }, 0x0000FFFF }, // cyan
    { {  0.5f,  0.5f,  0.5f }, 0x00FFFFFF }, // white
    { { -0.5f,  0.5f,  0.5f }, 0x00000000 }  // black
};

static const uint16_t g_Indices[] = {
    // front face (z = -0.5)
    0, 1, 2,  0, 2, 3,
    // back face (z = 0.5)
    4, 6, 5,  4, 7, 6,
    // left face (x = -0.5)
    0, 3, 7,  0, 7, 4,
    // right face (x = 0.5)
    1, 5, 6,  1, 6, 2,
    // top face (y = 0.5)
    3, 2, 6,  3, 6, 7,
    // bottom face (y = -0.5)
    0, 4, 5,  0, 5, 1
};

static ID3DBlob* CompileShaderFromString(
    const char* source,
    const char* entryPoint,
    const char* profile,
    const char* debugName) noexcept
{
    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;

    ID3DBlob* code = nullptr;
    ID3DBlob* errors = nullptr;

    HRESULT hr = D3DCompile(
        source,
        strlen(source),
        debugName,
        nullptr,
        nullptr,
        entryPoint,
        profile,
        flags,
        0,
        &code,
        &errors
    );

    if (FAILED(hr)) {
        if (errors) {
            const char* msg = static_cast<const char*>(errors->GetBufferPointer());
            OutputDebugStringA(msg);
            MessageBoxA(nullptr, msg, "Shader Compilation Error", MB_ICONERROR);
            utils::SafeRelease(errors);
        }
        return nullptr;
    }

    utils::SafeRelease(errors);
    return code;
}

static HRESULT CreateD3DResources(HWND hTargetWindow) noexcept {
    HRESULT hr = S_OK;

    DXGI_SWAP_CHAIN_DESC scDesc = {};
    scDesc.BufferCount = 1;
    scDesc.BufferDesc.Width = WINDOW_WIDTH;
    scDesc.BufferDesc.Height = WINDOW_HEIGHT;
    scDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scDesc.BufferDesc.RefreshRate.Numerator = 60;
    scDesc.BufferDesc.RefreshRate.Denominator = 1;
    scDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scDesc.OutputWindow = hTargetWindow;
    scDesc.SampleDesc.Count = 1;
    scDesc.SampleDesc.Quality = 0;
    scDesc.Windowed = TRUE;
    scDesc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

    UINT createFlags = 0;

    D3D_FEATURE_LEVEL requestedLevel = D3D_FEATURE_LEVEL_11_0;
    D3D_FEATURE_LEVEL obtainedLevel;

    hr = D3D11CreateDeviceAndSwapChain(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        createFlags,
        &requestedLevel,
        1,
        D3D11_SDK_VERSION,
        &scDesc,
        &g_SwapChain,
        &g_D3DDevice,
        &obtainedLevel,
        &g_ImmediateContext
    );

    if (FAILED(hr))
        return hr;

    ID3D11Texture2D* backBuffer = nullptr;
    hr = g_SwapChain->GetBuffer(0, IID_ID3D11Texture2D, reinterpret_cast<void**>(&backBuffer));
    if (FAILED(hr))
        return hr;

    hr = g_D3DDevice->CreateRenderTargetView(backBuffer, nullptr, &g_RenderTarget);
    utils::SafeRelease(backBuffer);
    if (FAILED(hr))
        return hr;

    g_ImmediateContext->OMSetRenderTargets(1, &g_RenderTarget, nullptr);

    return S_OK;
}

static HRESULT CreateSceneAssets() noexcept {
    HRESULT hr = S_OK;

    D3D11_BUFFER_DESC vbDesc = {};
    vbDesc.ByteWidth = sizeof(g_Vertices);
    vbDesc.Usage = D3D11_USAGE_IMMUTABLE;
    vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;

    D3D11_SUBRESOURCE_DATA vbInitData = {};
    vbInitData.pSysMem = g_Vertices;

    hr = g_D3DDevice->CreateBuffer(&vbDesc, &vbInitData, &g_VertexBuffer);
    if (FAILED(hr)) return hr;

    D3D11_BUFFER_DESC ibDesc = {};
    ibDesc.ByteWidth = sizeof(g_Indices);
    ibDesc.Usage = D3D11_USAGE_IMMUTABLE;
    ibDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;

    D3D11_SUBRESOURCE_DATA ibInitData = {};
    ibInitData.pSysMem = g_Indices;

    hr = g_D3DDevice->CreateBuffer(&ibDesc, &ibInitData, &g_IndexBuffer);
    if (FAILED(hr)) return hr;

    ID3DBlob* vsBlob = CompileShaderFromString(g_VS_Source, "main", "vs_5_0", "cube_vs.hlsl");
    if (!vsBlob) return E_FAIL;

    hr = g_D3DDevice->CreateVertexShader(
        vsBlob->GetBufferPointer(),
        vsBlob->GetBufferSize(),
        nullptr,
        &g_VS
    );
    if (FAILED(hr)) {
        utils::SafeRelease(vsBlob);
        return hr;
    }

    ID3DBlob* psBlob = CompileShaderFromString(g_PS_Source, "main", "ps_5_0", "cube_ps.hlsl");
    if (!psBlob) {
        utils::SafeRelease(vsBlob);
        return E_FAIL;
    }

    hr = g_D3DDevice->CreatePixelShader(
        psBlob->GetBufferPointer(),
        psBlob->GetBufferSize(),
        nullptr,
        &g_PS
    );
    if (FAILED(hr)) {
        utils::SafeRelease(vsBlob);
        utils::SafeRelease(psBlob);
        return hr;
    }

    D3D11_INPUT_ELEMENT_DESC layout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,
          offsetof(ColoredVertex, position), D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "COLOR",    0, DXGI_FORMAT_R8G8B8A8_UNORM,   0,
          offsetof(ColoredVertex, rgba),      D3D11_INPUT_PER_VERTEX_DATA, 0 }
    };

    hr = g_D3DDevice->CreateInputLayout(
        layout,
        _countof(layout),
        vsBlob->GetBufferPointer(),
        vsBlob->GetBufferSize(),
        &g_InputLayout
    );

    utils::SafeRelease(vsBlob);
    utils::SafeRelease(psBlob);

    if (FAILED(hr)) return hr;

    D3D11_BUFFER_DESC modelDesc = {};
    modelDesc.ByteWidth = sizeof(XMFLOAT4X4);
    modelDesc.Usage = D3D11_USAGE_DEFAULT;
    modelDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    modelDesc.CPUAccessFlags = 0;

    hr = g_D3DDevice->CreateBuffer(&modelDesc, nullptr, &g_ModelBuffer);
    if (FAILED(hr)) return hr;

    D3D11_BUFFER_DESC vpDesc = {};
    vpDesc.ByteWidth = sizeof(XMFLOAT4X4);
    vpDesc.Usage = D3D11_USAGE_DYNAMIC;
    vpDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    vpDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    hr = g_D3DDevice->CreateBuffer(&vpDesc, nullptr, &g_ViewProjBuffer);
    if (FAILED(hr)) return hr;

    return S_OK;
}

static void DestroyD3DResources() noexcept {
    using utils::SafeRelease;

    SafeRelease(g_ModelBuffer);
    SafeRelease(g_ViewProjBuffer);
    SafeRelease(g_InputLayout);
    SafeRelease(g_PS);
    SafeRelease(g_VS);
    SafeRelease(g_IndexBuffer);
    SafeRelease(g_VertexBuffer);
    SafeRelease(g_RenderTarget);
    SafeRelease(g_SwapChain);
    SafeRelease(g_ImmediateContext);

    SafeRelease(g_D3DDevice);
}

static void UpdateModelBuffer(float dt) {
    static float angle = 0.0f;
    angle += dt * 0.5f;

    XMMATRIX model = XMMatrixRotationY(angle);
    XMFLOAT4X4 modelData;
    XMStoreFloat4x4(&modelData, model);

    g_ImmediateContext->UpdateSubresource(g_ModelBuffer, 0, nullptr, &modelData, 0, 0);
}

static void UpdateViewProjBuffer() {
    float x = g_CamDist * sinf(g_CamTheta) * sinf(g_CamPhi);
    float y = g_CamDist * cosf(g_CamTheta);
    float z = g_CamDist * sinf(g_CamTheta) * cosf(g_CamPhi);

    XMVECTOR eye = XMVectorSet(x, y, z, 0.0f);
    XMVECTOR target = XMVectorZero();
    XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    XMMATRIX view = XMMatrixLookAtLH(eye, target, up);

    float aspect = static_cast<float>(WINDOW_WIDTH) / static_cast<float>(WINDOW_HEIGHT);
    float fov = XM_PIDIV4;
    float nearZ = 0.1f;
    float farZ = 100.0f;
    XMMATRIX proj = XMMatrixPerspectiveFovLH(fov, aspect, nearZ, farZ);

    XMMATRIX viewProj = view * proj;
    XMFLOAT4X4 vpData;
    XMStoreFloat4x4(&vpData, viewProj);

    D3D11_MAPPED_SUBRESOURCE mapped;
    HRESULT hr = g_ImmediateContext->Map(g_ViewProjBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (SUCCEEDED(hr)) {
        memcpy(mapped.pData, &vpData, sizeof(vpData));
        g_ImmediateContext->Unmap(g_ViewProjBuffer, 0);
    }
}

static void RenderFrame() noexcept {
    const float clearColor[4] = { 0.0f, 0.15f, 0.3f, 1.0f };
    g_ImmediateContext->ClearRenderTargetView(g_RenderTarget, clearColor);

    D3D11_VIEWPORT viewport = {};
    viewport.TopLeftX = 0.0f;
    viewport.TopLeftY = 0.0f;
    viewport.Width = static_cast<float>(WINDOW_WIDTH);
    viewport.Height = static_cast<float>(WINDOW_HEIGHT);
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    g_ImmediateContext->RSSetViewports(1, &viewport);

    D3D11_RECT scissorRect = { 0, 0, WINDOW_WIDTH, WINDOW_HEIGHT };
    g_ImmediateContext->RSSetScissorRects(1, &scissorRect);

    UINT stride = sizeof(ColoredVertex);
    UINT offset = 0;
    ID3D11Buffer* vbs[] = { g_VertexBuffer };
    g_ImmediateContext->IASetVertexBuffers(0, 1, vbs, &stride, &offset);
    g_ImmediateContext->IASetIndexBuffer(g_IndexBuffer, DXGI_FORMAT_R16_UINT, 0);
    g_ImmediateContext->IASetInputLayout(g_InputLayout);
    g_ImmediateContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    ID3D11Buffer* vsConstantBuffers[] = { g_ModelBuffer, g_ViewProjBuffer };
    g_ImmediateContext->VSSetConstantBuffers(0, 2, vsConstantBuffers);
    g_ImmediateContext->VSSetShader(g_VS, nullptr, 0);
    g_ImmediateContext->PSSetShader(g_PS, nullptr, 0);

    g_ImmediateContext->DrawIndexed(36, 0, 0);

    g_SwapChain->Present(0, 0);
}

LRESULT CALLBACK WindowProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    case WM_KEYDOWN:
        {
            const float deltaPhi = 0.1f;
            const float deltaTheta = 0.1f;
            switch (wParam) {
            case VK_LEFT:
                g_CamPhi -= deltaPhi;
                break;
            case VK_RIGHT:
                g_CamPhi += deltaPhi;
                break;
            case VK_UP:
                g_CamTheta -= deltaTheta;
                if (g_CamTheta < 0.1f) g_CamTheta = 0.1f;
                break;
            case VK_DOWN:
                g_CamTheta += deltaTheta;
                if (g_CamTheta > XM_PI - 0.1f) g_CamTheta = XM_PI - 0.1f;
                break;
            }
        }
        return 0;
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

int WINAPI WinMain(
    _In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE,
    _In_ LPSTR,
    _In_ int nCmdShow)
{
    WNDCLASSEX wc = {};
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = L"D3D11_Cube";

    if (!RegisterClassEx(&wc)) {
        MessageBox(nullptr, L"Window registration failed", L"Error", MB_ICONERROR);
        return -1;
    }

    HWND hMainWindow = CreateWindowEx(
        0,
        wc.lpszClassName,
        L"Direct3D 11 - Rotating Cube",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        WINDOW_WIDTH, WINDOW_HEIGHT,
        nullptr,
        nullptr,
        hInstance,
        nullptr
    );

    if (!hMainWindow) {
        MessageBox(nullptr, L"Window creation failed", L"Error", MB_ICONERROR);
        return -1;
    }

    ShowWindow(hMainWindow, nCmdShow);
    UpdateWindow(hMainWindow);

    if (FAILED(CreateD3DResources(hMainWindow))) {
        MessageBox(nullptr, L"Direct3D initialization failed", L"Error", MB_ICONERROR);
        DestroyD3DResources();
        return -1;
    }

    if (FAILED(CreateSceneAssets())) {
        MessageBox(nullptr, L"Scene asset creation failed", L"Error", MB_ICONERROR);
        DestroyD3DResources();
        return -1;
    }

    QueryPerformanceFrequency(&g_Freq);
    QueryPerformanceCounter(&g_StartTime);

    MSG msg = {};
    while (msg.message != WM_QUIT) {
        if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        else {
            LARGE_INTEGER currentTime;
            QueryPerformanceCounter(&currentTime);
            float dt = static_cast<float>(currentTime.QuadPart - g_StartTime.QuadPart) / static_cast<float>(g_Freq.QuadPart);
            g_StartTime = currentTime;

            UpdateModelBuffer(dt);
            UpdateViewProjBuffer();
            RenderFrame();
        }
    }

    DestroyD3DResources();

    return static_cast<int>(msg.wParam);
}
