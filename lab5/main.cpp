#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>
#include <vector>
#include <algorithm>
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
ID3D11DepthStencilView* g_DepthStencilView = nullptr;

ID3D11VertexShader* g_CubeVS = nullptr;
ID3D11PixelShader* g_CubePS = nullptr;
ID3D11PixelShader* g_TransparentPS = nullptr;
ID3D11InputLayout* g_CubeInputLayout = nullptr;
ID3D11Buffer* g_CubeVertexBuffer = nullptr;
ID3D11Buffer* g_CubeIndexBuffer = nullptr;
ID3D11Buffer* g_CubeModelBuffer = nullptr;
ID3D11ShaderResourceView* g_CubeTextureSRV = nullptr;
ID3D11SamplerState* g_CubeSampler = nullptr;

ID3D11DepthStencilState* g_OpaqueDepthState = nullptr;
ID3D11DepthStencilState* g_TransparentDepthState = nullptr;
ID3D11BlendState* g_TransparentBlendState = nullptr;

ID3D11VertexShader* g_SkyboxVS = nullptr;
ID3D11PixelShader* g_SkyboxPS = nullptr;
ID3D11InputLayout* g_SkyboxInputLayout = nullptr;
ID3D11Buffer* g_SkyboxVertexBuffer = nullptr;
ID3D11Buffer* g_SkyboxIndexBuffer = nullptr;
ID3D11ShaderResourceView* g_SkyboxTextureSRV = nullptr;
ID3D11SamplerState* g_SkyboxSampler = nullptr;
ID3D11DepthStencilState* g_SkyboxDepthState = nullptr;

ID3D11Buffer* g_ViewProjBuffer = nullptr;
ID3D11Buffer* g_TransparentColorBuffer = nullptr;

constexpr int WINDOW_WIDTH = 800;
constexpr int WINDOW_HEIGHT = 600;

float g_CamPhi = 0.0f;
float g_CamTheta = XM_PIDIV2;
float g_CamDist = 5.0f;

XMVECTOR g_CamPosition = XMVectorZero();

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

#ifndef MAKEFOURCC
#define MAKEFOURCC(ch0, ch1, ch2, ch3) \
    ((DWORD)(BYTE)(ch0) | ((DWORD)(BYTE)(ch1) << 8) | \
    ((DWORD)(BYTE)(ch2) << 16) | ((DWORD)(BYTE)(ch3) << 24))
#endif

struct DDS_PIXELFORMAT {
    DWORD dwSize;
    DWORD dwFlags;
    DWORD dwFourCC;
    DWORD dwRGBBitCount;
    DWORD dwRBitMask;
    DWORD dwGBitMask;
    DWORD dwBBitMask;
    DWORD dwABitMask;
};

struct DDS_HEADER {
    DWORD dwSize;
    DWORD dwHeaderFlags;
    DWORD dwHeight;
    DWORD dwWidth;
    DWORD dwPitchOrLinearSize;
    DWORD dwDepth;
    DWORD dwMipMapCount;
    DWORD dwReserved1[11];
    DDS_PIXELFORMAT ddspf;
    DWORD dwSurfaceFlags;
    DWORD dwCubemapFlags;
    DWORD dwReserved2[3];
};

#define DDS_MAGIC 0x20534444
#define DDS_SURFACE_FLAGS_MIPMAP 0x00400000
#define DDS_FOURCC 0x00000004
#define DDS_RGB 0x00000040

#define FOURCC_DXT1 MAKEFOURCC('D','X','T','1')
#define FOURCC_DXT3 MAKEFOURCC('D','X','T','3')
#define FOURCC_DXT5 MAKEFOURCC('D','X','T','5')
#define FOURCC_DXT1_REV MAKEFOURCC('1','X','D','D')

static UINT32 DivUp(UINT32 a, UINT32 b) {
    return (a + b - 1) / b;
}

static UINT32 GetBytesPerBlock(DXGI_FORMAT fmt) {
    switch (fmt) {
    case DXGI_FORMAT_BC1_UNORM:
    case DXGI_FORMAT_BC4_UNORM: return 8;
    case DXGI_FORMAT_BC2_UNORM:
    case DXGI_FORMAT_BC3_UNORM:
    case DXGI_FORMAT_BC5_UNORM: return 16;
    default: return 0;
    }
}

struct TextureDesc {
    UINT32 pitch = 0;
    UINT32 mipmapsCount = 0;
    DXGI_FORMAT fmt = DXGI_FORMAT_UNKNOWN;
    UINT32 width = 0;
    UINT32 height = 0;
    void* pData = nullptr;
};

static bool LoadDDS(const wchar_t* filename, TextureDesc& desc) {
    HANDLE hFile = CreateFileW(filename, GENERIC_READ, FILE_SHARE_READ, NULL,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        OutputDebugStringA("Failed to open DDS file\n");
        return false;
    }

    DWORD dwMagic;
    DWORD dwBytesRead;
    ReadFile(hFile, &dwMagic, sizeof(DWORD), &dwBytesRead, NULL);
    if (dwMagic != DDS_MAGIC) {
        CloseHandle(hFile);
        OutputDebugStringA("Invalid DDS file\n");
        return false;
    }

    DDS_HEADER header;
    ReadFile(hFile, &header, sizeof(DDS_HEADER), &dwBytesRead, NULL);

    desc.width = header.dwWidth;
    desc.height = header.dwHeight;
    desc.mipmapsCount = (header.dwSurfaceFlags & DDS_SURFACE_FLAGS_MIPMAP) ? header.dwMipMapCount : 1;

    if (header.ddspf.dwFlags & DDS_FOURCC) {
        DWORD fcc = header.ddspf.dwFourCC;
        char fccStr[8] = {};
        *(DWORD*)&fccStr[0] = fcc & 0xFF ? ((fcc & 0xFF) << 24) | ((fcc & 0xFF00) >> 8) | ((fcc & 0xFF0000) >> 16) | ((fcc & 0xFF000000) >> 24) : fcc;
        char buf[256];
        sprintf_s(buf, "DDS FourCC=0x%08X chars='%c%c%c%c'", fcc,
            (fcc & 0xFF), (fcc >> 8) & 0xFF, (fcc >> 16) & 0xFF, (fcc >> 24) & 0xFF);
        OutputDebugStringA(buf);
        if (fcc == FOURCC_DXT1 || fcc == FOURCC_DXT1_REV) {
            desc.fmt = DXGI_FORMAT_BC1_UNORM;
        } else if (fcc == FOURCC_DXT3 || fcc == MAKEFOURCC('1','X','D','3')) {
            desc.fmt = DXGI_FORMAT_BC2_UNORM;
        } else if (fcc == FOURCC_DXT5 || fcc == MAKEFOURCC('1','X','D','5')) {
            desc.fmt = DXGI_FORMAT_BC3_UNORM;
        } else {
            desc.fmt = DXGI_FORMAT_UNKNOWN;
        }
    } else {
        desc.fmt = DXGI_FORMAT_UNKNOWN;
    }

    if (desc.fmt == DXGI_FORMAT_UNKNOWN) {
        CloseHandle(hFile);
        OutputDebugStringA("Unsupported DDS format\n");
        return false;
    }

    UINT32 blockWidth = DivUp(desc.width, 4u);
    UINT32 blockHeight = DivUp(desc.height, 4u);
    UINT32 pitch = blockWidth * GetBytesPerBlock(desc.fmt);
    UINT32 dataSize = pitch * blockHeight;

    desc.pData = malloc(dataSize);
    if (!desc.pData) {
        CloseHandle(hFile);
        return false;
    }
    ReadFile(hFile, desc.pData, dataSize, &dwBytesRead, NULL);
    CloseHandle(hFile);
    return true;
}

static const char* g_CubeVSSource = R"(
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
    float2 uv : TEXCOORD;
};
struct VS_OUTPUT {
    float4 pos : SV_Position;
    float2 uv : TEXCOORD;
};
VS_OUTPUT main(VS_INPUT input) {
    VS_OUTPUT output;
    float4 worldPos = mul(float4(input.pos, 1.0), model);
    output.pos = mul(worldPos, viewProj);
    output.uv = input.uv;
    return output;
}
)";

static const char* g_CubePSSource = R"(
Texture2D colorTexture : register(t0);
SamplerState colorSampler : register(s0);
struct PS_INPUT {
    float4 pos : SV_Position;
    float2 uv : TEXCOORD;
};
float4 main(PS_INPUT input) : SV_Target {
    return colorTexture.Sample(colorSampler, input.uv);
}
)";

static const char* g_TransparentPSSource = R"(
cbuffer ColorBuffer : register(b2)
{
    float4 color;
};
struct PS_INPUT {
    float4 pos : SV_Position;
    float2 uv : TEXCOORD;
};
float4 main(PS_INPUT input) : SV_Target {
    return color;
}
)";

static const char* g_SkyboxVSSource = R"(
cbuffer ViewProjBuffer : register(b1)
{
    row_major float4x4 viewProj;
};
struct VS_INPUT {
    float3 pos : POSITION;
};
struct VS_OUTPUT {
    float4 pos : SV_Position;
    float3 localPos : TEXCOORD;
};
VS_OUTPUT main(VS_INPUT input) {
    VS_OUTPUT output;
    output.pos = mul(float4(input.pos, 1.0), viewProj);
    output.localPos = input.pos;
    return output;
}
)";

static const char* g_SkyboxPSSource = R"(
TextureCube skyboxTexture : register(t1);
SamplerState skyboxSampler : register(s1);
struct PS_INPUT {
    float4 pos : SV_Position;
    float3 localPos : TEXCOORD;
};
float4 main(PS_INPUT input) : SV_Target {
    return skyboxTexture.Sample(skyboxSampler, input.localPos);
}
)";

struct TexturedVertex {
    float position[3];
    float uv[2];
};

struct VertexPos {
    float x, y, z;
};

static const TexturedVertex g_CubeVertices[] = {
    { { -0.5f, -0.5f, -0.5f }, { 0.0f, 1.0f } },
    { {  0.5f, -0.5f, -0.5f }, { 1.0f, 1.0f } },
    { {  0.5f,  0.5f, -0.5f }, { 1.0f, 0.0f } },
    { { -0.5f,  0.5f, -0.5f }, { 0.0f, 0.0f } },
    { { -0.5f, -0.5f,  0.5f }, { 0.0f, 1.0f } },
    { {  0.5f, -0.5f,  0.5f }, { 1.0f, 1.0f } },
    { {  0.5f,  0.5f,  0.5f }, { 1.0f, 0.0f } },
    { { -0.5f,  0.5f,  0.5f }, { 0.0f, 0.0f } },
    { { -0.5f, -0.5f,  0.5f }, { 0.0f, 1.0f } },
    { { -0.5f, -0.5f, -0.5f }, { 1.0f, 1.0f } },
    { { -0.5f,  0.5f, -0.5f }, { 1.0f, 0.0f } },
    { { -0.5f,  0.5f,  0.5f }, { 0.0f, 0.0f } },
    { {  0.5f, -0.5f, -0.5f }, { 0.0f, 1.0f } },
    { {  0.5f, -0.5f,  0.5f }, { 1.0f, 1.0f } },
    { {  0.5f,  0.5f,  0.5f }, { 1.0f, 0.0f } },
    { {  0.5f,  0.5f, -0.5f }, { 0.0f, 0.0f } },
    { { -0.5f,  0.5f, -0.5f }, { 0.0f, 1.0f } },
    { {  0.5f,  0.5f, -0.5f }, { 1.0f, 1.0f } },
    { {  0.5f,  0.5f,  0.5f }, { 1.0f, 0.0f } },
    { { -0.5f,  0.5f,  0.5f }, { 0.0f, 0.0f } },
    { { -0.5f, -0.5f,  0.5f }, { 0.0f, 1.0f } },
    { {  0.5f, -0.5f,  0.5f }, { 1.0f, 1.0f } },
    { {  0.5f, -0.5f, -0.5f }, { 1.0f, 0.0f } },
    { { -0.5f, -0.5f, -0.5f }, { 0.0f, 0.0f } }
};

static const uint16_t g_CubeIndices[] = {
    0, 2, 1,  0, 3, 2,
    4, 5, 6,  4, 6, 7,
    8, 10, 9,  8, 11, 10,
    12, 14, 13,  12, 15, 14,
    16, 18, 17,  16, 19, 18,
    20, 22, 21,  20, 23, 22
};

static const VertexPos g_SkyboxVertices[] = {
    { -10.0f, -10.0f, -10.0f },
    {  10.0f, -10.0f, -10.0f },
    {  10.0f,  10.0f, -10.0f },
    { -10.0f,  10.0f, -10.0f },
    { -10.0f, -10.0f,  10.0f },
    {  10.0f, -10.0f,  10.0f },
    {  10.0f,  10.0f,  10.0f },
    { -10.0f,  10.0f,  10.0f }
};

static const uint16_t g_SkyboxIndices[] = {
    0, 2, 1,  0, 3, 2,
    4, 5, 6,  4, 6, 7,
    0, 7, 3,  0, 4, 7,
    1, 2, 6,  1, 6, 5,
    3, 7, 6,  3, 6, 2,
    0, 1, 5,  0, 5, 4
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

static HRESULT CreateDepthStencil() noexcept {
    D3D11_TEXTURE2D_DESC descDepth = {};
    descDepth.Width = WINDOW_WIDTH;
    descDepth.Height = WINDOW_HEIGHT;
    descDepth.MipLevels = 1;
    descDepth.ArraySize = 1;
    descDepth.Format = DXGI_FORMAT_D32_FLOAT;
    descDepth.SampleDesc.Count = 1;
    descDepth.SampleDesc.Quality = 0;
    descDepth.Usage = D3D11_USAGE_DEFAULT;
    descDepth.BindFlags = D3D11_BIND_DEPTH_STENCIL;

    ID3D11Texture2D* depthTexture = nullptr;
    HRESULT hr = g_D3DDevice->CreateTexture2D(&descDepth, nullptr, &depthTexture);
    if (FAILED(hr)) return hr;

    hr = g_D3DDevice->CreateDepthStencilView(depthTexture, nullptr, &g_DepthStencilView);
    depthTexture->Release();
    return hr;
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

    hr = CreateDepthStencil();
    if (FAILED(hr))
        return hr;

    g_ImmediateContext->OMSetRenderTargets(1, &g_RenderTarget, g_DepthStencilView);

    return S_OK;
}

static HRESULT CreateProceduralCubeTexture() noexcept {
    constexpr int size = 64;
    uint32_t data[size * size];

    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            int cx = x - size / 2;
            int cy = y - size / 2;
            float dist = sqrtf(float(cx * cx + cy * cy)) / (size / 2);

            uint8_t r = uint8_t(255 * (1.0f - dist) * 0.8f);
            uint8_t g = uint8_t(128 + 127 * sinf(x * 0.3f));
            uint8_t b = uint8_t(128 + 127 * cosf(y * 0.3f));

            int checker = ((x / 8) + (y / 8)) % 2;
            if (checker) {
                r = uint8_t(r * 0.7f);
                g = uint8_t(g * 0.7f);
                b = uint8_t(b * 0.7f);
            }

            data[y * size + x] = 0xFF000000 | (b << 16) | (g << 8) | r;
        }
    }

    D3D11_TEXTURE2D_DESC texDesc = {};
    texDesc.Width = size;
    texDesc.Height = size;
    texDesc.MipLevels = 1;
    texDesc.ArraySize = 1;
    texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    texDesc.SampleDesc.Count = 1;
    texDesc.SampleDesc.Quality = 0;
    texDesc.Usage = D3D11_USAGE_DEFAULT;
    texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA initData = { data, size * 4, 0 };

    ID3D11Texture2D* tex = nullptr;
    HRESULT hr = g_D3DDevice->CreateTexture2D(&texDesc, &initData, &tex);
    if (FAILED(hr)) return hr;

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = texDesc.Format;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;
    srvDesc.Texture2D.MostDetailedMip = 0;
    hr = g_D3DDevice->CreateShaderResourceView(tex, &srvDesc, &g_CubeTextureSRV);
    tex->Release();
    if (FAILED(hr)) return hr;

    D3D11_SAMPLER_DESC sampDesc = {};
    sampDesc.Filter = D3D11_FILTER_ANISOTROPIC;
    sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
    sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
    sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    sampDesc.MaxAnisotropy = 16;
    sampDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    sampDesc.MinLOD = 0;
    sampDesc.MaxLOD = FLT_MAX;
    hr = g_D3DDevice->CreateSamplerState(&sampDesc, &g_CubeSampler);
    return hr;
}

static HRESULT CreateCubeTexture() noexcept {
    TextureDesc texDesc;
    if (!LoadDDS(L"Textures/brick.dds", texDesc)) {
        return CreateProceduralCubeTexture();
    }

    D3D11_TEXTURE2D_DESC tex2DDesc = {};
    tex2DDesc.Width = texDesc.width;
    tex2DDesc.Height = texDesc.height;
    tex2DDesc.MipLevels = 1;
    tex2DDesc.ArraySize = 1;
    tex2DDesc.Format = texDesc.fmt;
    tex2DDesc.SampleDesc.Count = 1;
    tex2DDesc.SampleDesc.Quality = 0;
    tex2DDesc.Usage = D3D11_USAGE_IMMUTABLE;
    tex2DDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    UINT32 blockWidth = DivUp(texDesc.width, 4u);
    UINT32 blockHeight = DivUp(texDesc.height, 4u);
    UINT32 pitch = blockWidth * GetBytesPerBlock(texDesc.fmt);

    D3D11_SUBRESOURCE_DATA texData = {};
    texData.pSysMem = texDesc.pData;
    texData.SysMemPitch = pitch;

    ID3D11Texture2D* tex = nullptr;
    HRESULT hr = g_D3DDevice->CreateTexture2D(&tex2DDesc, &texData, &tex);
    free(texDesc.pData);
    if (FAILED(hr)) return CreateProceduralCubeTexture();

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = texDesc.fmt;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;
    srvDesc.Texture2D.MostDetailedMip = 0;
    hr = g_D3DDevice->CreateShaderResourceView(tex, &srvDesc, &g_CubeTextureSRV);
    tex->Release();
    if (FAILED(hr)) return CreateProceduralCubeTexture();

    D3D11_SAMPLER_DESC sampDesc = {};
    sampDesc.Filter = D3D11_FILTER_ANISOTROPIC;
    sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
    sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
    sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    sampDesc.MaxAnisotropy = 16;
    sampDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    sampDesc.MinLOD = 0;
    sampDesc.MaxLOD = FLT_MAX;
    hr = g_D3DDevice->CreateSamplerState(&sampDesc, &g_CubeSampler);
    return hr;
}

static HRESULT CreateProceduralSkyboxTexture() noexcept {
    constexpr int size = 64;
    uint32_t faces[6][size * size];

    for (int face = 0; face < 6; ++face) {
        for (int y = 0; y < size; ++y) {
            for (int x = 0; x < size; ++x) {
                float u = float(x) / size;
                float v = float(y) / size;

                float r, g, b;

                if (face == 2) {
                    r = 0.6f + 0.4f * (1.0f - v);
                    g = 0.7f + 0.3f * (1.0f - v);
                    b = 0.9f + 0.1f * (1.0f - v);
                } else if (face == 3) {
                    r = 0.2f;
                    g = 0.15f;
                    b = 0.1f;
                } else {
                    float horizon = 0.3f;
                    float t = (v < horizon) ? (horizon - v) / horizon : 0;
                    r = 0.4f + 0.2f * t + (v < horizon ? 0.3f * (1.0f - v / horizon) : 0);
                    g = 0.5f + 0.3f * t;
                    b = 0.7f + 0.3f * t;
                }

                uint8_t ri = uint8_t(r * 255);
                uint8_t gi = uint8_t(g * 255);
                uint8_t bi = uint8_t(b * 255);
                faces[face][y * size + x] = 0xFF000000 | (bi << 16) | (gi << 8) | ri;
            }
        }
    }

    D3D11_TEXTURE2D_DESC texDesc = {};
    texDesc.Width = size;
    texDesc.Height = size;
    texDesc.MipLevels = 1;
    texDesc.ArraySize = 6;
    texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    texDesc.SampleDesc.Count = 1;
    texDesc.SampleDesc.Quality = 0;
    texDesc.Usage = D3D11_USAGE_DEFAULT;
    texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    texDesc.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE;

    D3D11_SUBRESOURCE_DATA initData[6];
    for (int i = 0; i < 6; ++i) {
        initData[i].pSysMem = faces[i];
        initData[i].SysMemPitch = size * 4;
        initData[i].SysMemSlicePitch = 0;
    }

    ID3D11Texture2D* tex = nullptr;
    HRESULT hr = g_D3DDevice->CreateTexture2D(&texDesc, initData, &tex);
    if (FAILED(hr)) return hr;

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = texDesc.Format;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
    srvDesc.TextureCube.MipLevels = 1;
    srvDesc.TextureCube.MostDetailedMip = 0;
    hr = g_D3DDevice->CreateShaderResourceView(tex, &srvDesc, &g_SkyboxTextureSRV);
    tex->Release();
    if (FAILED(hr)) return hr;

    D3D11_SAMPLER_DESC sampDesc = {};
    sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    sampDesc.MinLOD = 0;
    sampDesc.MaxLOD = FLT_MAX;
    hr = g_D3DDevice->CreateSamplerState(&sampDesc, &g_SkyboxSampler);
    return hr;
}

static HRESULT CreateSkyboxTexture() noexcept {
    const wchar_t* faceNames[6] = {
        L"Textures/Skybox/posx.dds",
        L"Textures/Skybox/negx.dds",
        L"Textures/Skybox/posy.dds",
        L"Textures/Skybox/negy.dds",
        L"Textures/Skybox/posz.dds",
        L"Textures/Skybox/negz.dds"
    };

    TextureDesc faceDescs[6];
    bool allLoaded = true;
    for (int i = 0; i < 6; ++i) {
        if (!LoadDDS(faceNames[i], faceDescs[i])) {
            char buf[256];
            sprintf_s(buf, "Failed to load: %S", faceNames[i]);
            OutputDebugStringA(buf);
            allLoaded = false;
            break;
        } else {
            char buf[256];
            sprintf_s(buf, "Loaded %S: %dx%d fmt=%d", faceNames[i],
                faceDescs[i].width, faceDescs[i].height, faceDescs[i].fmt);
            OutputDebugStringA(buf);
        }
    }

    if (!allLoaded) {
        for (int i = 0; i < 6; ++i) {
            if (faceDescs[i].pData) {
                free(faceDescs[i].pData);
                faceDescs[i].pData = nullptr;
            }
        }
        OutputDebugStringA("Using procedural skybox");
        return CreateProceduralSkyboxTexture();
    }

    for (int i = 1; i < 6; ++i) {
        if (faceDescs[i].fmt != faceDescs[0].fmt ||
            faceDescs[i].width != faceDescs[0].width ||
            faceDescs[i].height != faceDescs[0].height) {
            for (int j = 0; j < 6; ++j) free(faceDescs[j].pData);
            return CreateProceduralSkyboxTexture();
        }
    }

    D3D11_TEXTURE2D_DESC cubeDesc = {};
    cubeDesc.Width = faceDescs[0].width;
    cubeDesc.Height = faceDescs[0].height;
    cubeDesc.MipLevels = 1;
    cubeDesc.ArraySize = 6;
    cubeDesc.Format = faceDescs[0].fmt;
    cubeDesc.SampleDesc.Count = 1;
    cubeDesc.SampleDesc.Quality = 0;
    cubeDesc.Usage = D3D11_USAGE_IMMUTABLE;
    cubeDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    cubeDesc.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE;

    UINT32 blockWidth = DivUp(cubeDesc.Width, 4u);
    UINT32 blockHeight = DivUp(cubeDesc.Height, 4u);
    UINT32 pitch = blockWidth * GetBytesPerBlock(cubeDesc.Format);

    D3D11_SUBRESOURCE_DATA initData[6];
    for (int i = 0; i < 6; ++i) {
        initData[i].pSysMem = faceDescs[i].pData;
        initData[i].SysMemPitch = pitch;
        initData[i].SysMemSlicePitch = 0;
    }

    ID3D11Texture2D* tex = nullptr;
    HRESULT hr = g_D3DDevice->CreateTexture2D(&cubeDesc, initData, &tex);
    for (int i = 0; i < 6; ++i)
        free(faceDescs[i].pData);
    if (FAILED(hr)) return CreateProceduralSkyboxTexture();

    D3D11_SHADER_RESOURCE_VIEW_DESC cubeSRVDesc = {};
    cubeSRVDesc.Format = cubeDesc.Format;
    cubeSRVDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
    cubeSRVDesc.TextureCube.MipLevels = 1;
    cubeSRVDesc.TextureCube.MostDetailedMip = 0;
    hr = g_D3DDevice->CreateShaderResourceView(tex, &cubeSRVDesc, &g_SkyboxTextureSRV);
    tex->Release();
    if (FAILED(hr)) return CreateProceduralSkyboxTexture();

    D3D11_SAMPLER_DESC sampDesc = {};
    sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    sampDesc.MinLOD = 0;
    sampDesc.MaxLOD = FLT_MAX;
    hr = g_D3DDevice->CreateSamplerState(&sampDesc, &g_SkyboxSampler);
    return hr;
}

static HRESULT CreateDepthStates() noexcept {
    D3D11_DEPTH_STENCIL_DESC opaqueDesc = {};
    opaqueDesc.DepthEnable = TRUE;
    opaqueDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    opaqueDesc.DepthFunc = D3D11_COMPARISON_LESS;
    opaqueDesc.StencilEnable = FALSE;
    HRESULT hr = g_D3DDevice->CreateDepthStencilState(&opaqueDesc, &g_OpaqueDepthState);
    if (FAILED(hr)) return hr;

    D3D11_DEPTH_STENCIL_DESC transDesc = {};
    transDesc.DepthEnable = TRUE;
    transDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    transDesc.DepthFunc = D3D11_COMPARISON_LESS;
    transDesc.StencilEnable = FALSE;
    hr = g_D3DDevice->CreateDepthStencilState(&transDesc, &g_TransparentDepthState);
    if (FAILED(hr)) return hr;

    D3D11_DEPTH_STENCIL_DESC skyDesc = {};
    skyDesc.DepthEnable = TRUE;
    skyDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    skyDesc.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
    skyDesc.StencilEnable = FALSE;
    hr = g_D3DDevice->CreateDepthStencilState(&skyDesc, &g_SkyboxDepthState);
    if (FAILED(hr)) return hr;

    return S_OK;
}

static HRESULT CreateBlendState() noexcept {
    D3D11_BLEND_DESC desc = {};
    desc.AlphaToCoverageEnable = FALSE;
    desc.IndependentBlendEnable = FALSE;
    desc.RenderTarget[0].BlendEnable = TRUE;
    desc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    desc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    desc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    desc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_RED |
        D3D11_COLOR_WRITE_ENABLE_GREEN | D3D11_COLOR_WRITE_ENABLE_BLUE;
    desc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    desc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
    desc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;

    return g_D3DDevice->CreateBlendState(&desc, &g_TransparentBlendState);
}

struct OpaqueObject {
    XMMATRIX model;
};

struct TransparentObject {
    XMMATRIX model;
    XMFLOAT4 color;
    float distance;
};

static std::vector<OpaqueObject> g_OpaqueObjects;
static std::vector<TransparentObject> g_TransparentObjects;

static HRESULT CreateSceneAssets() noexcept {
    HRESULT hr = S_OK;

    D3D11_BUFFER_DESC vbDesc = {};
    vbDesc.ByteWidth = sizeof(g_CubeVertices);
    vbDesc.Usage = D3D11_USAGE_IMMUTABLE;
    vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;

    D3D11_SUBRESOURCE_DATA vbInitData = {};
    vbInitData.pSysMem = g_CubeVertices;

    hr = g_D3DDevice->CreateBuffer(&vbDesc, &vbInitData, &g_CubeVertexBuffer);
    if (FAILED(hr)) return hr;

    D3D11_BUFFER_DESC ibDesc = {};
    ibDesc.ByteWidth = sizeof(g_CubeIndices);
    ibDesc.Usage = D3D11_USAGE_IMMUTABLE;
    ibDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;

    D3D11_SUBRESOURCE_DATA ibInitData = {};
    ibInitData.pSysMem = g_CubeIndices;

    hr = g_D3DDevice->CreateBuffer(&ibDesc, &ibInitData, &g_CubeIndexBuffer);
    if (FAILED(hr)) return hr;

    ID3DBlob* vsBlob = CompileShaderFromString(g_CubeVSSource, "main", "vs_5_0", "cube_vs.hlsl");
    if (!vsBlob) return E_FAIL;

    hr = g_D3DDevice->CreateVertexShader(
        vsBlob->GetBufferPointer(),
        vsBlob->GetBufferSize(),
        nullptr,
        &g_CubeVS
    );
    if (FAILED(hr)) {
        utils::SafeRelease(vsBlob);
        return hr;
    }

    ID3DBlob* psBlob = CompileShaderFromString(g_CubePSSource, "main", "ps_5_0", "cube_ps.hlsl");
    if (!psBlob) {
        utils::SafeRelease(vsBlob);
        return E_FAIL;
    }

    hr = g_D3DDevice->CreatePixelShader(
        psBlob->GetBufferPointer(),
        psBlob->GetBufferSize(),
        nullptr,
        &g_CubePS
    );
    if (FAILED(hr)) {
        utils::SafeRelease(vsBlob);
        utils::SafeRelease(psBlob);
        return hr;
    }

    ID3DBlob* transPsBlob = CompileShaderFromString(g_TransparentPSSource, "main", "ps_5_0", "transparent_ps.hlsl");
    if (!transPsBlob) {
        utils::SafeRelease(vsBlob);
        return E_FAIL;
    }

    hr = g_D3DDevice->CreatePixelShader(
        transPsBlob->GetBufferPointer(),
        transPsBlob->GetBufferSize(),
        nullptr,
        &g_TransparentPS
    );
    utils::SafeRelease(transPsBlob);
    if (FAILED(hr)) {
        utils::SafeRelease(vsBlob);
        return hr;
    }

    D3D11_INPUT_ELEMENT_DESC layout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,
          offsetof(TexturedVertex, position), D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0,
          offsetof(TexturedVertex, uv), D3D11_INPUT_PER_VERTEX_DATA, 0 }
    };

    hr = g_D3DDevice->CreateInputLayout(
        layout,
        _countof(layout),
        vsBlob->GetBufferPointer(),
        vsBlob->GetBufferSize(),
        &g_CubeInputLayout
    );

    utils::SafeRelease(vsBlob);
    utils::SafeRelease(psBlob);

    if (FAILED(hr)) return hr;

    D3D11_BUFFER_DESC modelDesc = {};
    modelDesc.ByteWidth = sizeof(XMFLOAT4X4);
    modelDesc.Usage = D3D11_USAGE_DEFAULT;
    modelDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    modelDesc.CPUAccessFlags = 0;

    hr = g_D3DDevice->CreateBuffer(&modelDesc, nullptr, &g_CubeModelBuffer);
    if (FAILED(hr)) return hr;

    D3D11_BUFFER_DESC colorDesc = {};
    colorDesc.ByteWidth = sizeof(XMFLOAT4);
    colorDesc.Usage = D3D11_USAGE_DEFAULT;
    colorDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    colorDesc.CPUAccessFlags = 0;

    hr = g_D3DDevice->CreateBuffer(&colorDesc, nullptr, &g_TransparentColorBuffer);
    if (FAILED(hr)) return hr;

    hr = CreateCubeTexture();
    if (FAILED(hr)) return hr;

    D3D11_BUFFER_DESC vbSkyDesc = {};
    vbSkyDesc.ByteWidth = sizeof(g_SkyboxVertices);
    vbSkyDesc.Usage = D3D11_USAGE_IMMUTABLE;
    vbSkyDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;

    D3D11_SUBRESOURCE_DATA vbSkyInitData = {};
    vbSkyInitData.pSysMem = g_SkyboxVertices;

    hr = g_D3DDevice->CreateBuffer(&vbSkyDesc, &vbSkyInitData, &g_SkyboxVertexBuffer);
    if (FAILED(hr)) return hr;

    D3D11_BUFFER_DESC ibSkyDesc = {};
    ibSkyDesc.ByteWidth = sizeof(g_SkyboxIndices);
    ibSkyDesc.Usage = D3D11_USAGE_IMMUTABLE;
    ibSkyDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;

    D3D11_SUBRESOURCE_DATA ibSkyInitData = {};
    ibSkyInitData.pSysMem = g_SkyboxIndices;

    hr = g_D3DDevice->CreateBuffer(&ibSkyDesc, &ibSkyInitData, &g_SkyboxIndexBuffer);
    if (FAILED(hr)) return hr;

    ID3DBlob* skyVsBlob = CompileShaderFromString(g_SkyboxVSSource, "main", "vs_5_0", "skybox_vs.hlsl");
    if (!skyVsBlob) return E_FAIL;

    hr = g_D3DDevice->CreateVertexShader(
        skyVsBlob->GetBufferPointer(),
        skyVsBlob->GetBufferSize(),
        nullptr,
        &g_SkyboxVS
    );
    if (FAILED(hr)) {
        utils::SafeRelease(skyVsBlob);
        return hr;
    }

    ID3DBlob* skyPsBlob = CompileShaderFromString(g_SkyboxPSSource, "main", "ps_5_0", "skybox_ps.hlsl");
    if (!skyPsBlob) {
        utils::SafeRelease(skyVsBlob);
        return E_FAIL;
    }

    hr = g_D3DDevice->CreatePixelShader(
        skyPsBlob->GetBufferPointer(),
        skyPsBlob->GetBufferSize(),
        nullptr,
        &g_SkyboxPS
    );
    if (FAILED(hr)) {
        utils::SafeRelease(skyVsBlob);
        utils::SafeRelease(skyPsBlob);
        return hr;
    }

    D3D11_INPUT_ELEMENT_DESC skyLayout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,
          0, D3D11_INPUT_PER_VERTEX_DATA, 0 }
    };

    hr = g_D3DDevice->CreateInputLayout(
        skyLayout,
        _countof(skyLayout),
        skyVsBlob->GetBufferPointer(),
        skyVsBlob->GetBufferSize(),
        &g_SkyboxInputLayout
    );

    utils::SafeRelease(skyVsBlob);
    utils::SafeRelease(skyPsBlob);

    if (FAILED(hr)) return hr;

    hr = CreateSkyboxTexture();
    if (FAILED(hr)) return hr;

    hr = CreateDepthStates();
    if (FAILED(hr)) return hr;

    hr = CreateBlendState();
    if (FAILED(hr)) return hr;

    D3D11_BUFFER_DESC vpDesc = {};
    vpDesc.ByteWidth = sizeof(XMFLOAT4X4);
    vpDesc.Usage = D3D11_USAGE_DYNAMIC;
    vpDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    vpDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    hr = g_D3DDevice->CreateBuffer(&vpDesc, nullptr, &g_ViewProjBuffer);
    if (FAILED(hr)) return hr;

    g_OpaqueObjects.push_back({ XMMatrixTranslation(-1.0f, 0.0f, 0.0f) });
    g_OpaqueObjects.push_back({ XMMatrixTranslation(1.0f, 0.0f, 0.0f) });

    g_TransparentObjects.push_back({
        XMMatrixTranslation(0.0f, 0.5f, 0.5f),
        XMFLOAT4(1.0f, 0.0f, 0.0f, 0.5f),
        0.0f
    });
    g_TransparentObjects.push_back({
        XMMatrixTranslation(0.0f, 0.0f, -0.5f),
        XMFLOAT4(0.0f, 1.0f, 0.0f, 0.5f),
        0.0f
    });
    g_TransparentObjects.push_back({
        XMMatrixTranslation(0.5f, -0.5f, 0.0f),
        XMFLOAT4(0.0f, 0.0f, 1.0f, 0.5f),
        0.0f
    });

    return S_OK;
}

static void DestroyD3DResources() noexcept {
    using utils::SafeRelease;

    SafeRelease(g_CubeModelBuffer);
    SafeRelease(g_TransparentColorBuffer);
    SafeRelease(g_ViewProjBuffer);
    SafeRelease(g_CubeTextureSRV);
    SafeRelease(g_CubeSampler);
    SafeRelease(g_CubeInputLayout);
    SafeRelease(g_TransparentPS);
    SafeRelease(g_CubePS);
    SafeRelease(g_CubeVS);
    SafeRelease(g_CubeIndexBuffer);
    SafeRelease(g_CubeVertexBuffer);

    SafeRelease(g_TransparentBlendState);
    SafeRelease(g_TransparentDepthState);
    SafeRelease(g_OpaqueDepthState);

    SafeRelease(g_SkyboxDepthState);
    SafeRelease(g_SkyboxTextureSRV);
    SafeRelease(g_SkyboxSampler);
    SafeRelease(g_SkyboxInputLayout);
    SafeRelease(g_SkyboxPS);
    SafeRelease(g_SkyboxVS);
    SafeRelease(g_SkyboxIndexBuffer);
    SafeRelease(g_SkyboxVertexBuffer);

    SafeRelease(g_DepthStencilView);
    SafeRelease(g_RenderTarget);
    SafeRelease(g_SwapChain);
    SafeRelease(g_ImmediateContext);
    SafeRelease(g_D3DDevice);
}

static void UpdateViewProjBuffer() noexcept {
    float x = g_CamDist * sinf(g_CamTheta) * sinf(g_CamPhi);
    float y = g_CamDist * cosf(g_CamTheta);
    float z = g_CamDist * sinf(g_CamTheta) * cosf(g_CamPhi);

    g_CamPosition = XMVectorSet(x, y, z, 0.0f);

    XMVECTOR eye = g_CamPosition;
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

static void UpdateSkyboxViewProjBuffer() noexcept {
    XMVECTOR eye = g_CamPosition;
    XMVECTOR target = XMVectorZero();
    XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    XMMATRIX view = XMMatrixLookAtLH(eye, target, up);

    view.r[3] = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);

    float aspect = static_cast<float>(WINDOW_WIDTH) / static_cast<float>(WINDOW_HEIGHT);
    float fov = XM_PIDIV4;
    XMMATRIX proj = XMMatrixPerspectiveFovLH(fov, aspect, 0.1f, 100.0f);

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

static void UpdateTransparentDistances() noexcept {
    for (auto& obj : g_TransparentObjects) {
        XMVECTOR pos = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);
        pos = XMVector3Transform(pos, obj.model);
        XMVECTOR diff = pos - g_CamPosition;
        obj.distance = XMVectorGetX(XMVector3Length(diff));
    }
}

static void SortTransparentObjects() noexcept {
    std::sort(g_TransparentObjects.begin(), g_TransparentObjects.end(),
        [](const TransparentObject& a, const TransparentObject& b) {
            return a.distance > b.distance;
        });
}

static void RenderSkybox() noexcept {
    g_ImmediateContext->OMSetDepthStencilState(g_SkyboxDepthState, 0);

    UINT skyStride = sizeof(VertexPos);
    UINT skyOffset = 0;
    ID3D11Buffer* skyVBs[] = { g_SkyboxVertexBuffer };
    g_ImmediateContext->IASetVertexBuffers(0, 1, skyVBs, &skyStride, &skyOffset);
    g_ImmediateContext->IASetIndexBuffer(g_SkyboxIndexBuffer, DXGI_FORMAT_R16_UINT, 0);
    g_ImmediateContext->IASetInputLayout(g_SkyboxInputLayout);
    g_ImmediateContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    g_ImmediateContext->VSSetConstantBuffers(1, 1, &g_ViewProjBuffer);
    g_ImmediateContext->VSSetShader(g_SkyboxVS, nullptr, 0);
    g_ImmediateContext->PSSetShader(g_SkyboxPS, nullptr, 0);
    g_ImmediateContext->PSSetShaderResources(1, 1, &g_SkyboxTextureSRV);
    g_ImmediateContext->PSSetSamplers(1, 1, &g_SkyboxSampler);

    g_ImmediateContext->DrawIndexed(36, 0, 0);
}

static void RenderOpaqueObjects() noexcept {
    g_ImmediateContext->OMSetDepthStencilState(g_OpaqueDepthState, 0);
    g_ImmediateContext->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);

    UINT cubeStride = sizeof(TexturedVertex);
    UINT cubeOffset = 0;
    ID3D11Buffer* cubeVBs[] = { g_CubeVertexBuffer };
    g_ImmediateContext->IASetVertexBuffers(0, 1, cubeVBs, &cubeStride, &cubeOffset);
    g_ImmediateContext->IASetIndexBuffer(g_CubeIndexBuffer, DXGI_FORMAT_R16_UINT, 0);
    g_ImmediateContext->IASetInputLayout(g_CubeInputLayout);
    g_ImmediateContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    g_ImmediateContext->VSSetShader(g_CubeVS, nullptr, 0);
    g_ImmediateContext->PSSetShader(g_CubePS, nullptr, 0);
    g_ImmediateContext->PSSetShaderResources(0, 1, &g_CubeTextureSRV);
    g_ImmediateContext->PSSetSamplers(0, 1, &g_CubeSampler);

    for (const auto& obj : g_OpaqueObjects) {
        XMFLOAT4X4 modelData;
        XMStoreFloat4x4(&modelData, obj.model);
        g_ImmediateContext->UpdateSubresource(g_CubeModelBuffer, 0, nullptr, &modelData, 0, 0);

        ID3D11Buffer* vsCbs[] = { g_CubeModelBuffer, g_ViewProjBuffer };
        g_ImmediateContext->VSSetConstantBuffers(0, 2, vsCbs);

        g_ImmediateContext->DrawIndexed(36, 0, 0);
    }
}

static void RenderTransparentObjects() noexcept {
    g_ImmediateContext->OMSetDepthStencilState(g_TransparentDepthState, 0);
    g_ImmediateContext->OMSetBlendState(g_TransparentBlendState, nullptr, 0xFFFFFFFF);

    UINT cubeStride = sizeof(TexturedVertex);
    UINT cubeOffset = 0;
    ID3D11Buffer* cubeVBs[] = { g_CubeVertexBuffer };
    g_ImmediateContext->IASetVertexBuffers(0, 1, cubeVBs, &cubeStride, &cubeOffset);
    g_ImmediateContext->IASetIndexBuffer(g_CubeIndexBuffer, DXGI_FORMAT_R16_UINT, 0);
    g_ImmediateContext->IASetInputLayout(g_CubeInputLayout);
    g_ImmediateContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    g_ImmediateContext->VSSetShader(g_CubeVS, nullptr, 0);
    g_ImmediateContext->PSSetShader(g_TransparentPS, nullptr, 0);

    for (const auto& obj : g_TransparentObjects) {
        XMFLOAT4X4 modelData;
        XMStoreFloat4x4(&modelData, obj.model);
        g_ImmediateContext->UpdateSubresource(g_CubeModelBuffer, 0, nullptr, &modelData, 0, 0);

        g_ImmediateContext->UpdateSubresource(g_TransparentColorBuffer, 0, nullptr, &obj.color, 0, 0);

        ID3D11Buffer* vsCbs[] = { g_CubeModelBuffer, g_ViewProjBuffer };
        g_ImmediateContext->VSSetConstantBuffers(0, 2, vsCbs);
        g_ImmediateContext->PSSetConstantBuffers(2, 1, &g_TransparentColorBuffer);

        g_ImmediateContext->DrawIndexed(36, 0, 0);
    }
}

static void RenderFrame() noexcept {
    const float clearColor[4] = { 0.0f, 0.15f, 0.3f, 1.0f };
    g_ImmediateContext->ClearRenderTargetView(g_RenderTarget, clearColor);
    g_ImmediateContext->ClearDepthStencilView(g_DepthStencilView, D3D11_CLEAR_DEPTH, 1.0f, 0);

    D3D11_VIEWPORT viewport = {};
    viewport.TopLeftX = 0.0f;
    viewport.TopLeftY = 0.0f;
    viewport.Width = static_cast<float>(WINDOW_WIDTH);
    viewport.Height = static_cast<float>(WINDOW_HEIGHT);
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    g_ImmediateContext->RSSetViewports(1, &viewport);

    UpdateViewProjBuffer();
    UpdateSkyboxViewProjBuffer();
    RenderSkybox();

    UpdateViewProjBuffer();
    RenderOpaqueObjects();

    UpdateTransparentDistances();
    SortTransparentObjects();
    RenderTransparentObjects();

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
    wc.lpszClassName = L"D3D11_Lab5";

    if (!RegisterClassEx(&wc)) {
        MessageBox(nullptr, L"Window registration failed", L"Error", MB_ICONERROR);
        return -1;
    }

    HWND hMainWindow = CreateWindowEx(
        0,
        wc.lpszClassName,
        L"Direct3D 11 - Lab5: Depth Buffer & Transparency",
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
    LARGE_INTEGER lastTime;
    QueryPerformanceCounter(&lastTime);

    MSG msg = {};
    while (msg.message != WM_QUIT) {
        if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        else {
            LARGE_INTEGER currentTime;
            QueryPerformanceCounter(&currentTime);
            float dt = static_cast<float>(currentTime.QuadPart - lastTime.QuadPart) / static_cast<float>(g_Freq.QuadPart);
            lastTime = currentTime;

            RenderFrame();
        }
    }

    DestroyD3DResources();

    return static_cast<int>(msg.wParam);
}
