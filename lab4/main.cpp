#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>
#include <cstdio>
#include <cstdlib>
#include <string>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "dxguid.lib")

using namespace DirectX;

#ifndef MAKEFOURCC
#define MAKEFOURCC(ch0, ch1, ch2, ch3) \
    ((DWORD)(BYTE)(ch0) | ((DWORD)(BYTE)(ch1) << 8) | \
    ((DWORD)(BYTE)(ch2) << 16) | ((DWORD)(BYTE)(ch3) << 24))
#endif

#define DDS_MAGIC 0x20534444
#define FOURCC_DXT1 MAKEFOURCC('D','X','T','1')
#define FOURCC_DXT3 MAKEFOURCC('D','X','T','3')
#define FOURCC_DXT5 MAKEFOURCC('D','X','T','5')
#define DDS_SURFACE_FLAGS_MIPMAP 0x00400000

struct DDS_HEADER {
    DWORD dwSize;
    DWORD dwHeaderFlags;
    DWORD dwHeight;
    DWORD dwWidth;
    DWORD dwPitchOrLinearSize;
    DWORD dwDepth;
    DWORD dwMipMapCount;
    DWORD dwReserved1[11];
    DWORD ddspf_dwSize;
    DWORD ddspf_dwFlags;
    DWORD ddspf_dwFourCC;
    DWORD ddspf_dwRGBBitCount;
    DWORD ddspf_dwRBitMask;
    DWORD ddspf_dwGBitMask;
    DWORD ddspf_dwBBitMask;
    DWORD ddspf_dwABitMask;
    DWORD dwSurfaceFlags;
    DWORD dwCubemapFlags;
    DWORD dwReserved2[3];
};

struct TextureDesc {
    UINT32 width = 0;
    UINT32 height = 0;
    UINT32 mipmapsCount = 0;
    DXGI_FORMAT fmt = DXGI_FORMAT_UNKNOWN;
    void* pData = nullptr;
};

UINT32 DivUp(UINT32 a, UINT32 b) { return (a + b - 1) / b; }

UINT32 GetBytesPerBlock(DXGI_FORMAT fmt) {
    switch (fmt) {
    case DXGI_FORMAT_BC1_UNORM: return 8;
    case DXGI_FORMAT_BC2_UNORM:
    case DXGI_FORMAT_BC3_UNORM: return 16;
    default: return 0;
    }
}

bool LoadDDS(const wchar_t* filename, TextureDesc& desc) {
    HANDLE hFile = CreateFileW(filename, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        OutputDebugStringA("Failed to open DDS file\n");
        return false;
    }

    DWORD dwMagic, dwBytesRead;
    ReadFile(hFile, &dwMagic, sizeof(DWORD), &dwBytesRead, NULL);
    if (dwMagic != DDS_MAGIC) {
        CloseHandle(hFile);
        OutputDebugStringA("Invalid DDS magic\n");
        return false;
    }

    DDS_HEADER header;
    ReadFile(hFile, &header, sizeof(DDS_HEADER), &dwBytesRead, NULL);

    desc.width = header.dwWidth;
    desc.height = header.dwHeight;
    desc.mipmapsCount = (header.dwSurfaceFlags & DDS_SURFACE_FLAGS_MIPMAP) ? header.dwMipMapCount : 1;

    if (header.ddspf_dwFourCC == FOURCC_DXT1) desc.fmt = DXGI_FORMAT_BC1_UNORM;
    else if (header.ddspf_dwFourCC == FOURCC_DXT3) desc.fmt = DXGI_FORMAT_BC2_UNORM;
    else if (header.ddspf_dwFourCC == FOURCC_DXT5) desc.fmt = DXGI_FORMAT_BC3_UNORM;
    else {
        CloseHandle(hFile);
        OutputDebugStringA("Unsupported DDS format\n");
        return false;
    }

    UINT32 blockWidth = DivUp(desc.width, 4u);
    UINT32 blockHeight = DivUp(desc.height, 4u);
    UINT32 pitch = blockWidth * GetBytesPerBlock(desc.fmt);
    UINT32 dataSize = pitch * blockHeight;

    desc.pData = malloc(dataSize);
    if (!desc.pData) { CloseHandle(hFile); return false; }
    ReadFile(hFile, desc.pData, dataSize, &dwBytesRead, NULL);
    CloseHandle(hFile);
    return true;
}

ID3D11Device* g_D3DDevice = nullptr;
ID3D11DeviceContext* g_ImmediateContext = nullptr;
IDXGISwapChain* g_SwapChain = nullptr;
ID3D11RenderTargetView* g_RenderTarget = nullptr;
ID3D11DepthStencilView* g_DepthStencilView = nullptr;

ID3D11VertexShader* g_VS = nullptr;
ID3D11PixelShader* g_PS = nullptr;
ID3D11InputLayout* g_InputLayout = nullptr;
ID3D11Buffer* g_VertexBuffer = nullptr;
ID3D11Buffer* g_IndexBuffer = nullptr;
ID3D11Buffer* g_ModelBuffer = nullptr;
ID3D11Buffer* g_ViewProjBuffer = nullptr;

ID3D11VertexShader* g_SkyboxVS = nullptr;
ID3D11PixelShader* g_SkyboxPS = nullptr;
ID3D11InputLayout* g_SkyboxInputLayout = nullptr;
ID3D11Buffer* g_SkyboxVertexBuffer = nullptr;
ID3D11Buffer* g_SkyboxIndexBuffer = nullptr;

ID3D11Texture2D* g_CubeTexture = nullptr;
ID3D11ShaderResourceView* g_CubeTextureView = nullptr;
ID3D11SamplerState* g_Sampler = nullptr;

ID3D11Texture2D* g_CubemapTexture = nullptr;
ID3D11ShaderResourceView* g_CubemapView = nullptr;

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
        if (ptr) { ptr->Release(); ptr = nullptr; }
    }
}

static const char* g_VS_Source = R"(
struct VS_INPUT {
    float3 pos : POSITION;
    float2 uv : TEXCOORD;
};
struct VS_OUTPUT {
    float4 pos : SV_Position;
    float2 uv : TEXCOORD;
};
cbuffer ModelBuffer : register(b0) {
    row_major float4x4 model;
}
cbuffer ViewProjBuffer : register(b1) {
    row_major float4x4 viewProj;
}
VS_OUTPUT main(VS_INPUT input) {
    VS_OUTPUT output;
    float4 worldPos = mul(float4(input.pos, 1.0), model);
    output.pos = mul(worldPos, viewProj);
    output.uv = input.uv;
    return output;
}
)";

static const char* g_PS_Source = R"(
struct PS_INPUT {
    float4 pos : SV_Position;
    float2 uv : TEXCOORD;
};
Texture2D colorTexture : register(t0);
SamplerState colorSampler : register(s0);
float4 main(PS_INPUT input) : SV_Target {
    return colorTexture.Sample(colorSampler, input.uv);
}
)";

static const char* g_SkyboxVS_Source = R"(
struct VS_INPUT {
    float3 pos : POSITION;
    float2 uv : TEXCOORD;
};
struct VS_OUTPUT {
    float4 pos : SV_Position;
    float3 localPos : TEXCOORD;
};
cbuffer ViewProjBuffer : register(b1) {
    row_major float4x4 viewProj;
}
VS_OUTPUT main(VS_INPUT input) {
    VS_OUTPUT output;
    output.pos = mul(float4(input.pos, 1.0), viewProj);
    output.localPos = input.pos;
    return output;
}
)";

static const char* g_SkyboxPS_Source = R"(
struct PS_INPUT {
    float4 pos : SV_Position;
    float3 localPos : TEXCOORD;
};
TextureCube skyboxTexture : register(t1);
SamplerState skyboxSampler : register(s1);
float4 main(PS_INPUT input) : SV_Target {
    return skyboxTexture.Sample(skyboxSampler, input.localPos);
};
)";

struct TextureVertex {
    float x, y, z;
    float u, v;
};

static const TextureVertex g_Vertices[] = {
    { -0.5f, -0.5f, -0.5f, 0.0f, 1.0f },
    {  0.5f, -0.5f, -0.5f, 1.0f, 1.0f },
    {  0.5f,  0.5f, -0.5f, 1.0f, 0.0f },
    { -0.5f,  0.5f, -0.5f, 0.0f, 0.0f },
    { -0.5f, -0.5f,  0.5f, 0.0f, 1.0f },
    {  0.5f, -0.5f,  0.5f, 1.0f, 1.0f },
    {  0.5f,  0.5f,  0.5f, 1.0f, 0.0f },
    { -0.5f,  0.5f,  0.5f, 0.0f, 0.0f },
    { -0.5f, -0.5f,  0.5f, 0.0f, 1.0f },
    { -0.5f, -0.5f, -0.5f, 1.0f, 1.0f },
    { -0.5f,  0.5f, -0.5f, 1.0f, 0.0f },
    { -0.5f,  0.5f,  0.5f, 0.0f, 0.0f },
    {  0.5f, -0.5f, -0.5f, 0.0f, 1.0f },
    {  0.5f, -0.5f,  0.5f, 1.0f, 1.0f },
    {  0.5f,  0.5f,  0.5f, 1.0f, 0.0f },
    {  0.5f,  0.5f, -0.5f, 0.0f, 0.0f },
    { -0.5f,  0.5f, -0.5f, 0.0f, 1.0f },
    {  0.5f,  0.5f, -0.5f, 1.0f, 1.0f },
    {  0.5f,  0.5f,  0.5f, 1.0f, 0.0f },
    { -0.5f,  0.5f,  0.5f, 0.0f, 0.0f },
    { -0.5f, -0.5f,  0.5f, 0.0f, 1.0f },
    {  0.5f, -0.5f,  0.5f, 1.0f, 1.0f },
    {  0.5f, -0.5f, -0.5f, 1.0f, 0.0f },
    { -0.5f, -0.5f, -0.5f, 0.0f, 0.0f }
};

static const uint16_t g_Indices[] = {
    0, 2, 1, 0, 3, 2,
    4, 5, 6, 4, 6, 7,
    8, 10, 9, 8, 11, 10,
    12, 14, 13, 12, 15, 14,
    16, 18, 17, 16, 19, 18,
    20, 22, 21, 20, 23, 22
};

static const TextureVertex g_SkyboxVertices[] = {
    { -10, -10, -10, 0, 0 }, {  10, -10, -10, 0, 0 }, {  10,  10, -10, 0, 0 }, { -10,  10, -10, 0, 0 },
    { -10, -10,  10, 0, 0 }, {  10, -10,  10, 0, 0 }, {  10,  10,  10, 0, 0 }, { -10,  10,  10, 0, 0 }
};

static const uint16_t g_SkyboxIndices[] = {
    0, 2, 1, 0, 3, 2,
    4, 5, 6, 4, 6, 7,
    0, 7, 3, 0, 4, 7,
    1, 2, 6, 1, 6, 5,
    3, 7, 6, 3, 6, 2,
    0, 1, 5, 0, 5, 4
};

static ID3DBlob* CompileShaderFromString(const char* source, const char* entryPoint, const char* profile, const char* debugName) {
    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
    ID3DBlob* code = nullptr;
    ID3DBlob* errors = nullptr;

    HRESULT hr = D3DCompile(source, strlen(source), debugName, nullptr, nullptr, entryPoint, profile, flags, 0, &code, &errors);
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

static HRESULT CreateD3DResources(HWND hTargetWindow) {
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

    hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createFlags, &requestedLevel, 1, D3D11_SDK_VERSION, &scDesc, &g_SwapChain, &g_D3DDevice, &obtainedLevel, &g_ImmediateContext);
    if (FAILED(hr)) return hr;

    ID3D11Texture2D* backBuffer = nullptr;
    hr = g_SwapChain->GetBuffer(0, IID_ID3D11Texture2D, reinterpret_cast<void**>(&backBuffer));
    if (FAILED(hr)) return hr;

    hr = g_D3DDevice->CreateRenderTargetView(backBuffer, nullptr, &g_RenderTarget);
    utils::SafeRelease(backBuffer);
    if (FAILED(hr)) return hr;

    D3D11_TEXTURE2D_DESC depthDesc = {};
    depthDesc.Width = WINDOW_WIDTH;
    depthDesc.Height = WINDOW_HEIGHT;
    depthDesc.MipLevels = 1;
    depthDesc.ArraySize = 1;
    depthDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    depthDesc.SampleDesc.Count = 1;
    depthDesc.SampleDesc.Quality = 0;
    depthDesc.Usage = D3D11_USAGE_DEFAULT;
    depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;

    ID3D11Texture2D* depthStencil = nullptr;
    hr = g_D3DDevice->CreateTexture2D(&depthDesc, nullptr, &depthStencil);
    if (FAILED(hr)) return hr;

    hr = g_D3DDevice->CreateDepthStencilView(depthStencil, nullptr, &g_DepthStencilView);
    depthStencil->Release();
    if (FAILED(hr)) return hr;

    g_ImmediateContext->OMSetRenderTargets(1, &g_RenderTarget, g_DepthStencilView);

    return S_OK;
}

static HRESULT CreateSceneAssets() {
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

    D3D11_BUFFER_DESC skyboxVBDesc = {};
    skyboxVBDesc.ByteWidth = sizeof(g_SkyboxVertices);
    skyboxVBDesc.Usage = D3D11_USAGE_IMMUTABLE;
    skyboxVBDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA skyboxVBInitData = {};
    skyboxVBInitData.pSysMem = g_SkyboxVertices;
    hr = g_D3DDevice->CreateBuffer(&skyboxVBDesc, &skyboxVBInitData, &g_SkyboxVertexBuffer);
    if (FAILED(hr)) return hr;

    D3D11_BUFFER_DESC skyboxIBDesc = {};
    skyboxIBDesc.ByteWidth = sizeof(g_SkyboxIndices);
    skyboxIBDesc.Usage = D3D11_USAGE_IMMUTABLE;
    skyboxIBDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
    D3D11_SUBRESOURCE_DATA skyboxIBInitData = {};
    skyboxIBInitData.pSysMem = g_SkyboxIndices;
    hr = g_D3DDevice->CreateBuffer(&skyboxIBDesc, &skyboxIBInitData, &g_SkyboxIndexBuffer);
    if (FAILED(hr)) return hr;

    ID3DBlob* vsBlob = CompileShaderFromString(g_VS_Source, "main", "vs_5_0", "cube_vs.hlsl");
    if (!vsBlob) return E_FAIL;
    hr = g_D3DDevice->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &g_VS);
    if (FAILED(hr)) { utils::SafeRelease(vsBlob); return hr; }

    D3D11_INPUT_ELEMENT_DESC layout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 }
    };
    hr = g_D3DDevice->CreateInputLayout(layout, _countof(layout), vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), &g_InputLayout);
    utils::SafeRelease(vsBlob);
    if (FAILED(hr)) return hr;

    ID3DBlob* psBlob = CompileShaderFromString(g_PS_Source, "main", "ps_5_0", "cube_ps.hlsl");
    if (!psBlob) return E_FAIL;
    hr = g_D3DDevice->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &g_PS);
    utils::SafeRelease(psBlob);
    if (FAILED(hr)) return hr;

    ID3DBlob* skyboxVSBlob = CompileShaderFromString(g_SkyboxVS_Source, "main", "vs_5_0", "skybox_vs.hlsl");
    if (!skyboxVSBlob) return E_FAIL;
    hr = g_D3DDevice->CreateVertexShader(skyboxVSBlob->GetBufferPointer(), skyboxVSBlob->GetBufferSize(), nullptr, &g_SkyboxVS);
    if (FAILED(hr)) { utils::SafeRelease(skyboxVSBlob); return hr; }

    hr = g_D3DDevice->CreateInputLayout(layout, _countof(layout), skyboxVSBlob->GetBufferPointer(), skyboxVSBlob->GetBufferSize(), &g_SkyboxInputLayout);
    utils::SafeRelease(skyboxVSBlob);
    if (FAILED(hr)) return hr;

    ID3DBlob* skyboxPSBlob = CompileShaderFromString(g_SkyboxPS_Source, "main", "ps_5_0", "skybox_ps.hlsl");
    if (!skyboxPSBlob) return E_FAIL;
    hr = g_D3DDevice->CreatePixelShader(skyboxPSBlob->GetBufferPointer(), skyboxPSBlob->GetBufferSize(), nullptr, &g_SkyboxPS);
    utils::SafeRelease(skyboxPSBlob);
    if (FAILED(hr)) return hr;

    D3D11_BUFFER_DESC modelDesc = {};
    modelDesc.ByteWidth = sizeof(XMFLOAT4X4);
    modelDesc.Usage = D3D11_USAGE_DEFAULT;
    modelDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
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

static HRESULT LoadTextures() {
    HRESULT hr = S_OK;

    TextureDesc texDesc;
    if (!LoadDDS(L"Textures/brick.dds", texDesc)) {
        MessageBoxA(nullptr, "Failed to load brick.dds", "Error", MB_OK);
        return E_FAIL;
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

    D3D11_SUBRESOURCE_DATA texData = {};
    texData.pSysMem = texDesc.pData;

    UINT32 blockWidth = DivUp(texDesc.width, 4u);
    UINT32 blockHeight = DivUp(texDesc.height, 4u);
    texData.SysMemPitch = blockWidth * GetBytesPerBlock(texDesc.fmt);

    hr = g_D3DDevice->CreateTexture2D(&tex2DDesc, &texData, &g_CubeTexture);
    if (FAILED(hr)) return hr;

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = texDesc.fmt;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;
    srvDesc.Texture2D.MostDetailedMip = 0;
    hr = g_D3DDevice->CreateShaderResourceView(g_CubeTexture, &srvDesc, &g_CubeTextureView);
    if (FAILED(hr)) return hr;

    free(texDesc.pData);

    D3D11_SAMPLER_DESC sampDesc = {};
    sampDesc.Filter = D3D11_FILTER_ANISOTROPIC;
    sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
    sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
    sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    sampDesc.MinLOD = -FLT_MAX;
    sampDesc.MaxLOD = FLT_MAX;
    sampDesc.MaxAnisotropy = 16;
    sampDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    hr = g_D3DDevice->CreateSamplerState(&sampDesc, &g_Sampler);
    if (FAILED(hr)) return hr;

    const wchar_t* faceNames[] = {
        L"Textures/Skybox/posx.dds", L"Textures/Skybox/negx.dds",
        L"Textures/Skybox/posy.dds", L"Textures/Skybox/negy.dds",
        L"Textures/Skybox/posz.dds", L"Textures/Skybox/negz.dds"
    };

    TextureDesc faceDescs[6];
    for (int i = 0; i < 6; ++i) {
        if (!LoadDDS(faceNames[i], faceDescs[i])) {
            char err[256];
            sprintf_s(err, "Failed to load: %S", faceNames[i]);
            MessageBoxA(nullptr, err, "Error", MB_OK);
            return E_FAIL;
        }
    }

    for (int i = 1; i < 6; ++i) {
        if (faceDescs[i].fmt != faceDescs[0].fmt || faceDescs[i].width != faceDescs[0].width || faceDescs[i].height != faceDescs[0].height) {
            MessageBoxA(nullptr, "Cubemap faces must have same format and size", "Error", MB_OK);
            return E_FAIL;
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

    blockWidth = DivUp(cubeDesc.Width, 4u);
    blockHeight = DivUp(cubeDesc.Height, 4u);
    UINT32 pitch = blockWidth * GetBytesPerBlock(cubeDesc.Format);

    D3D11_SUBRESOURCE_DATA initData[6];
    for (int i = 0; i < 6; ++i) {
        initData[i].pSysMem = faceDescs[i].pData;
        initData[i].SysMemPitch = pitch;
    }

    hr = g_D3DDevice->CreateTexture2D(&cubeDesc, initData, &g_CubemapTexture);
    if (FAILED(hr)) return hr;

    for (int i = 0; i < 6; ++i) free(faceDescs[i].pData);

    D3D11_SHADER_RESOURCE_VIEW_DESC cubeSRVDesc = {};
    cubeSRVDesc.Format = cubeDesc.Format;
    cubeSRVDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
    cubeSRVDesc.TextureCube.MipLevels = 1;
    cubeSRVDesc.TextureCube.MostDetailedMip = 0;
    hr = g_D3DDevice->CreateShaderResourceView(g_CubemapTexture, &cubeSRVDesc, &g_CubemapView);
    if (FAILED(hr)) return hr;

    return S_OK;
}

static void DestroyD3DResources() {
    using utils::SafeRelease;
    SafeRelease(g_Sampler);
    SafeRelease(g_CubeTextureView);
    SafeRelease(g_CubeTexture);
    SafeRelease(g_CubemapView);
    SafeRelease(g_CubemapTexture);
    SafeRelease(g_ModelBuffer);
    SafeRelease(g_ViewProjBuffer);
    SafeRelease(g_InputLayout);
    SafeRelease(g_SkyboxInputLayout);
    SafeRelease(g_PS);
    SafeRelease(g_SkyboxPS);
    SafeRelease(g_VS);
    SafeRelease(g_SkyboxVS);
    SafeRelease(g_IndexBuffer);
    SafeRelease(g_VertexBuffer);
    SafeRelease(g_SkyboxIndexBuffer);
    SafeRelease(g_SkyboxVertexBuffer);
    SafeRelease(g_RenderTarget);
    SafeRelease(g_DepthStencilView);
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

static void UpdateViewProjBuffer(XMMATRIX& viewProj) {
    XMFLOAT4X4 vpData;
    XMStoreFloat4x4(&vpData, viewProj);
    D3D11_MAPPED_SUBRESOURCE mapped;
    HRESULT hr = g_ImmediateContext->Map(g_ViewProjBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (SUCCEEDED(hr)) {
        memcpy(mapped.pData, &vpData, sizeof(vpData));
        g_ImmediateContext->Unmap(g_ViewProjBuffer, 0);
    }
}

static void RenderFrame() {
    const float clearColor[4] = { 0.25f, 0.25f, 0.25f, 1.0f };
    g_ImmediateContext->ClearRenderTargetView(g_RenderTarget, clearColor);
    g_ImmediateContext->ClearDepthStencilView(g_DepthStencilView, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

    D3D11_VIEWPORT viewport = {};
    viewport.TopLeftX = 0.0f;
    viewport.TopLeftY = 0.0f;
    viewport.Width = static_cast<float>(WINDOW_WIDTH);
    viewport.Height = static_cast<float>(WINDOW_HEIGHT);
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    g_ImmediateContext->RSSetViewports(1, &viewport);

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

    ID3D11SamplerState* samplers[] = { g_Sampler };
    g_ImmediateContext->PSSetSamplers(0, 1, samplers);
    g_ImmediateContext->PSSetSamplers(1, 1, samplers);

    XMMATRIX viewNoTranslate = view;
    viewNoTranslate.r[3] = XMVectorSet(0, 0, 0, 1);
    XMMATRIX vpSky = viewNoTranslate * proj;
    UpdateViewProjBuffer(vpSky);

    D3D11_DEPTH_STENCIL_DESC dsDesc = {};
    dsDesc.DepthEnable = TRUE;
    dsDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    dsDesc.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
    dsDesc.StencilEnable = FALSE;
    ID3D11DepthStencilState* pDSSkybox = nullptr;
    g_D3DDevice->CreateDepthStencilState(&dsDesc, &pDSSkybox);
    g_ImmediateContext->OMSetDepthStencilState(pDSSkybox, 0);

    D3D11_RASTERIZER_DESC rsDesc = {};
    rsDesc.FillMode = D3D11_FILL_SOLID;
    rsDesc.CullMode = D3D11_CULL_NONE;
    rsDesc.FrontCounterClockwise = FALSE;
    ID3D11RasterizerState* pRSCullNone = nullptr;
    g_D3DDevice->CreateRasterizerState(&rsDesc, &pRSCullNone);
    g_ImmediateContext->RSSetState(pRSCullNone);

    g_ImmediateContext->VSSetShader(g_SkyboxVS, nullptr, 0);
    g_ImmediateContext->PSSetShader(g_SkyboxPS, nullptr, 0);
    g_ImmediateContext->IASetInputLayout(g_SkyboxInputLayout);

    UINT stride = sizeof(TextureVertex);
    UINT offset = 0;
    ID3D11Buffer* vbSky[] = { g_SkyboxVertexBuffer };
    g_ImmediateContext->IASetVertexBuffers(0, 1, vbSky, &stride, &offset);
    g_ImmediateContext->IASetIndexBuffer(g_SkyboxIndexBuffer, DXGI_FORMAT_R16_UINT, 0);
    g_ImmediateContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    ID3D11ShaderResourceView* skySRV[] = { g_CubemapView };
    g_ImmediateContext->PSSetShaderResources(1, 1, skySRV);

    ID3D11Buffer* cbsSky[] = { nullptr, g_ViewProjBuffer };
    g_ImmediateContext->VSSetConstantBuffers(0, 2, cbsSky);

    g_ImmediateContext->DrawIndexed(36, 0, 0);

    pRSCullNone->Release();
    pDSSkybox->Release();

    D3D11_DEPTH_STENCIL_DESC dsDescCube = {};
    dsDescCube.DepthEnable = TRUE;
    dsDescCube.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    dsDescCube.DepthFunc = D3D11_COMPARISON_LESS;
    dsDescCube.StencilEnable = FALSE;
    ID3D11DepthStencilState* pDSCube = nullptr;
    g_D3DDevice->CreateDepthStencilState(&dsDescCube, &pDSCube);
    g_ImmediateContext->OMSetDepthStencilState(pDSCube, 0);

    D3D11_RASTERIZER_DESC rsDescCube = {};
    rsDescCube.FillMode = D3D11_FILL_SOLID;
    rsDescCube.CullMode = D3D11_CULL_BACK;
    rsDescCube.FrontCounterClockwise = FALSE;
    ID3D11RasterizerState* pRSCube = nullptr;
    g_D3DDevice->CreateRasterizerState(&rsDescCube, &pRSCube);
    g_ImmediateContext->RSSetState(pRSCube);

    UpdateViewProjBuffer(viewProj);

    g_ImmediateContext->VSSetShader(g_VS, nullptr, 0);
    g_ImmediateContext->PSSetShader(g_PS, nullptr, 0);
    g_ImmediateContext->IASetInputLayout(g_InputLayout);

    ID3D11Buffer* vbCube[] = { g_VertexBuffer };
    g_ImmediateContext->IASetVertexBuffers(0, 1, vbCube, &stride, &offset);
    g_ImmediateContext->IASetIndexBuffer(g_IndexBuffer, DXGI_FORMAT_R16_UINT, 0);

    ID3D11ShaderResourceView* cubeSRV[] = { g_CubeTextureView };
    g_ImmediateContext->PSSetShaderResources(0, 1, cubeSRV);

    ID3D11Buffer* cbsCube[] = { g_ModelBuffer, g_ViewProjBuffer };
    g_ImmediateContext->VSSetConstantBuffers(0, 2, cbsCube);

    g_ImmediateContext->DrawIndexed(36, 0, 0);

    pRSCube->Release();
    pDSCube->Release();

    g_SwapChain->Present(0, 0);
}

LRESULT CALLBACK WindowProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    case WM_KEYDOWN: {
        const float deltaPhi = 0.1f;
        const float deltaTheta = 0.1f;
        switch (wParam) {
        case VK_LEFT: g_CamPhi -= deltaPhi; break;
        case VK_RIGHT: g_CamPhi += deltaPhi; break;
        case VK_UP: g_CamTheta -= deltaTheta; if (g_CamTheta < 0.1f) g_CamTheta = 0.1f; break;
        case VK_DOWN: g_CamTheta += deltaTheta; if (g_CamTheta > XM_PI - 0.1f) g_CamTheta = XM_PI - 0.1f; break;
        }
        return 0;
    }
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

int WINAPI WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE, _In_ LPSTR, _In_ int nCmdShow) {
    WNDCLASSEX wc = {};
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = L"D3D11_Cube_Textured";

    if (!RegisterClassEx(&wc)) {
        MessageBox(nullptr, L"Window registration failed", L"Error", MB_ICONERROR);
        return -1;
    }

    HWND hMainWindow = CreateWindowEx(0, wc.lpszClassName, L"Lab4 - Texturing & Skybox", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, WINDOW_WIDTH, WINDOW_HEIGHT, nullptr, nullptr, hInstance, nullptr);
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

    if (FAILED(LoadTextures())) {
        MessageBox(nullptr, L"Texture loading failed", L"Error", MB_ICONERROR);
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
        } else {
            LARGE_INTEGER currentTime;
            QueryPerformanceCounter(&currentTime);
            float dt = static_cast<float>(currentTime.QuadPart - g_StartTime.QuadPart) / static_cast<float>(g_Freq.QuadPart);
            g_StartTime = currentTime;

            UpdateModelBuffer(dt);
            RenderFrame();
        }
    }

    DestroyD3DResources();
    return static_cast<int>(msg.wParam);
}