#include "starfield_renderer.hpp"

#include <d3dcompiler.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <mutex>
#include <limits>
#include <string>
#include <system_error>
#include <unordered_set>
#include <vector>

#include <spdlog/spdlog.h>

#include <windows.h>

#pragma comment(lib, "d3dcompiler.lib")

namespace
{
    using Microsoft::WRL::ComPtr;

    std::once_flag g_logInitOnce;
    std::unordered_set<std::string> g_loggedMissingNodes;
    std::mutex g_missingNodesMutex;
    std::atomic<int> g_routeLogCounter{0};
    std::atomic<int> g_constantsLogCounter{0};
    std::atomic<int> g_projectionFailCounter{0};

    constexpr float kOrbitYawSensitivity = 0.0028f;
    constexpr float kOrbitPitchSensitivity = 0.0024f;
    constexpr float kOrbitPitchMin = -DirectX::XM_PIDIV2 + 0.05f;
    constexpr float kOrbitPitchMax = DirectX::XM_PIDIV2 - 0.05f;
    constexpr float kPanSensitivity = 1.35f;
    constexpr float kPanLimit = 6.0f;
    constexpr float kDistanceRouteFocus = 2.6f;
    constexpr float kDistanceGlobal = 1.8f;
    constexpr float kSingleNodeFocusRadius = 20.0f;
    constexpr float kDefaultLocalFocusRadius = 35.0f;
    constexpr float kLocalViewMaxRadius = 300.0f;

    std::filesystem::path moduleDirectory()
    {
        wchar_t buffer[MAX_PATH] = {};
        HMODULE module = nullptr;
        if (::GetModuleHandleExW(
                GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCWSTR>(&moduleDirectory),
                &module))
        {
            const DWORD length = ::GetModuleFileNameW(module, buffer, static_cast<DWORD>(std::size(buffer)));
            if (length > 0)
            {
                std::error_code ec;
                auto path = std::filesystem::path(buffer, buffer + length).parent_path();
                return std::filesystem::weakly_canonical(path, ec);
            }
        }
        return {};
    }

    ComPtr<ID3DBlob> compileShader(const std::string& source, const char* entryPoint, const char* target)
    {
        UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#if defined(_DEBUG)
        flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
        ComPtr<ID3DBlob> bytecode;
        ComPtr<ID3DBlob> errors;
        const HRESULT hr = D3DCompile(
            source.c_str(),
            source.size(),
            nullptr,
            nullptr,
            nullptr,
            entryPoint,
            target,
            flags,
            0,
            &bytecode,
            &errors);
        if (FAILED(hr))
        {
            if (errors)
            {
                spdlog::error("Starfield shader compile failed: {}", static_cast<const char*>(errors->GetBufferPointer()));
            }
            else
            {
                spdlog::error("Starfield shader compile failed (hr=0x{:08X})", static_cast<unsigned>(hr));
            }
            return nullptr;
        }
        return bytecode;
    }

    std::uint32_t parseSystemId(const std::string& id)
    {
        try
        {
            if (id.empty())
            {
                return 0;
            }
            size_t processed = 0;
            const std::uint64_t parsed = std::stoull(id, &processed, 10);
            if (processed != id.size() || parsed > std::numeric_limits<std::uint32_t>::max())
            {
                return 0;
            }
            return static_cast<std::uint32_t>(parsed);
        }
        catch (...)
        {
            return 0;
        }
    }

    D3D12_HEAP_PROPERTIES uploadHeapProps()
    {
        D3D12_HEAP_PROPERTIES props{};
        props.Type = D3D12_HEAP_TYPE_UPLOAD;
        props.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        props.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
        props.CreationNodeMask = 1;
        props.VisibleNodeMask = 1;
        return props;
    }

    D3D12_RESOURCE_DESC bufferDesc(UINT64 size)
    {
        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Alignment = 0;
        desc.Width = size;
        desc.Height = 1;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = DXGI_FORMAT_UNKNOWN;
        desc.SampleDesc.Count = 1;
        desc.SampleDesc.Quality = 0;
        desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        desc.Flags = D3D12_RESOURCE_FLAG_NONE;
        return desc;
    }
}

StarfieldRenderer& StarfieldRenderer::instance()
{
    static StarfieldRenderer instance;
    return instance;
}

void StarfieldRenderer::setViewportRect(float x, float y, float width, float height)
{
    if (width <= 1.0f || height <= 1.0f)
    {
        clearViewportRect();
        return;
    }

    viewportX_ = x;
    viewportY_ = y;
    viewportWidth_ = width;
    viewportHeight_ = height;
    viewportValid_ = true;
}

void StarfieldRenderer::clearViewportRect()
{
    viewportValid_ = false;
    viewportX_ = 0.0f;
    viewportY_ = 0.0f;
    viewportWidth_ = 0.0f;
    viewportHeight_ = 0.0f;
}

bool StarfieldRenderer::initialize(ID3D12Device* device, DXGI_FORMAT targetFormat)
{
    if (ready_)
    {
        return true;
    }

    if (!device)
    {
        return false;
    }

    if (!ensureCatalogLoaded())
    {
        spdlog::warn("StarfieldRenderer: catalog unavailable; skipping renderer init");
        return false;
    }

    device_ = device;

    if (!createPipeline(device, targetFormat))
    {
        return false;
    }

    if (!createVertexBuffer(device))
    {
        return false;
    }

    if (!createConstantBuffer(device))
    {
        return false;
    }

    manualZoom_ = 1.0f;
    manualYaw_ = 0.0f;
    manualPitch_ = 0.35f;
    manualPanX_ = 0.0f;
    manualPanY_ = 0.0f;
    externalCameraActive_ = false;
    routeFocusValid_ = false;
    hasFrameConstants_ = false;
    lastViewportWidth_ = 0;
    lastViewportHeight_ = 0;

    ready_ = true;
    spdlog::info("StarfieldRenderer initialized (stars={})", starVertexCount_);
    return true;
}

void StarfieldRenderer::shutdown()
{
    ready_ = false;

    if (constantBuffer_ && mappedConstants_)
    {
        constantBuffer_->Unmap(0, nullptr);
    }
    mappedConstants_ = nullptr;

    if (routeVertexBuffer_ && mappedRouteVertices_)
    {
        routeVertexBuffer_->Unmap(0, nullptr);
    }
    mappedRouteVertices_ = nullptr;

    starVertexBuffer_.Reset();
    routeVertexBuffer_.Reset();
    constantBuffer_.Reset();
    starfieldPipeline_.Reset();
    routePipeline_.Reset();
    rootSignature_.Reset();
    device_.Reset();

    starVertexView_ = {};
    routeVertexView_ = {};
    starVertexCount_ = 0;
    routeVertexCount_ = 0;
    routeVertexCapacity_ = 0;

    lastRouteTimestamp_ = 0;
    lastRouteCount_ = 0;
    lastActiveNodeId_.clear();
    hasFrameConstants_ = false;
    manualZoom_ = 1.0f;
    manualYaw_ = 0.0f;
    manualPitch_ = 0.35f;
    manualPanX_ = 0.0f;
    manualPanY_ = 0.0f;
    externalCameraActive_ = false;
    routeFocusValid_ = false;
    routeFocusRadius_ = 1.0f;
    routeFocusCenter_ = DirectX::XMFLOAT3{0.0f, 0.0f, 0.0f};

    clearViewportRect();

    catalog_.reset();
    catalogPath_.clear();
}

void StarfieldRenderer::render(
    ID3D12GraphicsCommandList* commandList,
    UINT width,
    UINT height,
    const overlay::OverlayState* state)
{
    if (!ready_ || !commandList || starVertexCount_ == 0 || width == 0 || height == 0)
    {
        return;
    }

    updateRouteBuffer(state);

    const float viewportWidth = viewportValid_ ? viewportWidth_ : static_cast<float>(width);
    const float viewportHeight = viewportValid_ ? viewportHeight_ : static_cast<float>(height);
    const UINT constantsWidth = static_cast<UINT>(std::max(1.0f, std::round(viewportWidth)));
    const UINT constantsHeight = static_cast<UINT>(std::max(1.0f, std::round(viewportHeight)));

    if (!updateConstants(state, constantsWidth, constantsHeight))
    {
        spdlog::warn("StarfieldRenderer: updateConstants failed (ready={}, stars={}, routeCount={})", ready_, starVertexCount_, routeVertexCount_);
        return;
    }

    std::call_once(g_logInitOnce, [this]() {
        spdlog::info(
            "StarfieldRenderer: first render (stars={}, catalog='{}')",
            starVertexCount_,
            catalogPath_.empty() ? std::string{"<unresolved>"} : catalogPath_.string());
    });

    const float viewportX = viewportValid_ ? viewportX_ : 0.0f;
    const float viewportY = viewportValid_ ? viewportY_ : 0.0f;

    D3D12_VIEWPORT viewport{};
    viewport.TopLeftX = viewportX;
    viewport.TopLeftY = viewportY;
    viewport.Width = viewportWidth;
    viewport.Height = viewportHeight;
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;

    const LONG scissorLeft = static_cast<LONG>(std::floor(viewportX));
    const LONG scissorTop = static_cast<LONG>(std::floor(viewportY));
    const LONG scissorRight = static_cast<LONG>(std::ceil(viewportX + viewportWidth));
    const LONG scissorBottom = static_cast<LONG>(std::ceil(viewportY + viewportHeight));

    const LONG targetWidth = static_cast<LONG>(width);
    const LONG targetHeight = static_cast<LONG>(height);

    D3D12_RECT scissor{
        std::clamp<LONG>(scissorLeft, 0, targetWidth),
        std::clamp<LONG>(scissorTop, 0, targetHeight),
        std::clamp<LONG>(scissorRight, 0, targetWidth),
        std::clamp<LONG>(scissorBottom, 0, targetHeight)};

    commandList->SetGraphicsRootSignature(rootSignature_.Get());
    commandList->SetGraphicsRootConstantBufferView(0, constantBuffer_->GetGPUVirtualAddress());
    commandList->RSSetViewports(1, &viewport);
    commandList->RSSetScissorRects(1, &scissor);

    commandList->SetPipelineState(starfieldPipeline_.Get());
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_POINTLIST);
    commandList->IASetVertexBuffers(0, 1, &starVertexView_);
    commandList->DrawInstanced(starVertexCount_, 1, 0, 0);

    if (routePipeline_ && routeVertexCount_ >= 2)
    {
        commandList->SetPipelineState(routePipeline_.Get());
        commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINESTRIP);
        commandList->IASetVertexBuffers(0, 1, &routeVertexView_);
        commandList->DrawInstanced(routeVertexCount_, 1, 0, 0);
    }
}

bool StarfieldRenderer::ensureCatalogLoaded()
{
    if (catalog_)
    {
        return true;
    }

    const auto path = resolveCatalogPath();
    if (path.empty())
    {
        spdlog::warn("StarfieldRenderer: catalog path not found");
        return false;
    }

    try
    {
        catalog_ = overlay::load_star_catalog_from_file(path);
        catalogPath_ = path;

        const auto& bboxMin = catalog_->bbox_min;
        const auto& bboxMax = catalog_->bbox_max;
        boundsCenter_ = DirectX::XMFLOAT3(
            (bboxMin.x + bboxMax.x) * 0.5f,
            (bboxMin.y + bboxMax.y) * 0.5f,
            (bboxMin.z + bboxMax.z) * 0.5f);

        const DirectX::XMFLOAT3 extents{
            (bboxMax.x - bboxMin.x) * 0.5f,
            (bboxMax.y - bboxMin.y) * 0.5f,
            (bboxMax.z - bboxMin.z) * 0.5f};

        boundsRadius_ = std::max({extents.x, extents.y, extents.z, 1.0f});

        spdlog::info(
            "StarfieldRenderer: loaded catalog from {} (stars={})",
            path.string(),
            catalog_->records.size());
        return true;
    }
    catch (const std::exception& ex)
    {
        spdlog::error("StarfieldRenderer: failed to load catalog: {}", ex.what());
        return false;
    }
}

std::filesystem::path StarfieldRenderer::resolveCatalogPath() const
{
    const auto base = moduleDirectory();
    if (base.empty())
    {
        return {};
    }

    const std::array<std::filesystem::path, 4> candidates{
        base / L"star_catalog_v1.bin",
        base / L".." / L"star_catalog_v1.bin",
        base / L".." / L".." / L"data" / L"star_catalog_v1.bin",
        base / L".." / L".." / L".." / L"data" / L"star_catalog_v1.bin"};

    for (const auto& candidate : candidates)
    {
        std::error_code ec;
        if (!candidate.empty() && std::filesystem::exists(candidate, ec) && !ec)
        {
            return std::filesystem::weakly_canonical(candidate, ec);
        }
    }

    return {};
}

bool StarfieldRenderer::createPipeline(ID3D12Device* device, DXGI_FORMAT targetFormat)
{
    static const char* kStarfieldVS = R"HLSL(
        cbuffer FrameConstants : register(b0)
        {
            float4x4 ViewProj;
            float4 CameraPosition;
            float4 ClusterCenterRadius;
            float4 Params;
        };

        struct VSInput {
            float3 position : POSITION;
            float brightness : TEXCOORD0;
            float security : TEXCOORD1;
        };

        struct VSOutput {
            float4 position : SV_POSITION;
            float brightness : TEXCOORD0;
            float security : TEXCOORD1;
            float pointSize : PSIZE;
        };

        VSOutput main(VSInput input) {
            VSOutput output;
            float3 worldPos = input.position;
            output.position = mul(ViewProj, float4(worldPos, 1.0f));

            float3 toCamera = CameraPosition.xyz - worldPos;
            float distance = max(length(toCamera), 0.5f);
            float baseSize = Params.x;
            float minSize = Params.z;
            float falloff = Params.w;
            float scale = baseSize / max(distance * falloff, 1.0f);
            output.pointSize = clamp(scale, minSize, baseSize);

            output.brightness = input.brightness;
            output.security = input.security;
            return output;
        }
    )HLSL";

    static const char* kStarfieldPS = R"HLSL(
        float clamp01(float v) { return saturate(v); }

        struct PSInput {
            float4 position : SV_POSITION;
            float brightness : TEXCOORD0;
            float security : TEXCOORD1;
        };

        float4 main(PSInput input) : SV_TARGET {
            float intensity = clamp01(input.brightness);
            float securityT = clamp01((input.security + 1.0f) * 0.5f);
            float warm = saturate(securityT);
            float cool = 1.0f - warm;
            float3 baseColor = float3(0.42f + 0.58f * warm, 0.36f + 0.52f * cool, 0.92f);
            float boost = clamp01(intensity * 1.35f + 0.25f);
            float alpha = clamp01(0.35f + intensity * 0.55f);
            return float4(baseColor * boost, alpha);
        }
    )HLSL";

    static const char* kRouteVS = R"HLSL(
        cbuffer FrameConstants : register(b0)
        {
            float4x4 ViewProj;
            float4 CameraPosition;
            float4 ClusterCenterRadius;
            float4 Params;
        };

        struct VSInput {
            float3 position : POSITION;
            float progress : TEXCOORD0;
            float state : TEXCOORD1;
        };

        struct VSOutput {
            float4 position : SV_POSITION;
            float progress : TEXCOORD0;
            float state : TEXCOORD1;
        };

        VSOutput main(VSInput input) {
            VSOutput output;
            output.position = mul(ViewProj, float4(input.position, 1.0f));
            output.progress = input.progress;
            output.state = input.state;
            return output;
        }
    )HLSL";

    static const char* kRoutePS = R"HLSL(
        float4 main(float4 position : SV_POSITION, float progress : TEXCOORD0, float state : TEXCOORD1) : SV_TARGET {
            float3 cold = float3(0.2f, 0.8f, 1.0f);
            float3 warm = float3(1.0f, 0.5f, 0.2f);
            float3 baseColor = lerp(cold, warm, saturate(progress));
            if (state > 0.5f) {
                baseColor = float3(1.0f, 1.0f, 0.4f);
            }
            return float4(baseColor, 0.85f);
        }
    )HLSL";

    auto starVS = compileShader(kStarfieldVS, "main", "vs_5_0");
    auto starPS = compileShader(kStarfieldPS, "main", "ps_5_0");
    auto routeVS = compileShader(kRouteVS, "main", "vs_5_0");
    auto routePS = compileShader(kRoutePS, "main", "ps_5_0");

    if (!starVS || !starPS || !routeVS || !routePS)
    {
        return false;
    }

    D3D12_ROOT_PARAMETER rootParam{};
    rootParam.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParam.Descriptor.ShaderRegister = 0;
    rootParam.Descriptor.RegisterSpace = 0;
    rootParam.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_ROOT_SIGNATURE_DESC rootDesc{};
    rootDesc.NumParameters = 1;
    rootDesc.pParameters = &rootParam;
    rootDesc.NumStaticSamplers = 0;
    rootDesc.pStaticSamplers = nullptr;
    rootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> signature;
    ComPtr<ID3DBlob> error;
    HRESULT hr = D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error);
    if (FAILED(hr))
    {
        if (error)
        {
            spdlog::error(
                "StarfieldRenderer: root signature serialization failed: {}",
                static_cast<const char*>(error->GetBufferPointer()));
        }
        else
        {
            spdlog::error(
                "StarfieldRenderer: root signature serialization failed (hr=0x{:08X})",
                static_cast<unsigned>(hr));
        }
        return false;
    }

    hr = device->CreateRootSignature(
        0,
        signature->GetBufferPointer(),
        signature->GetBufferSize(),
        IID_PPV_ARGS(&rootSignature_));
    if (FAILED(hr))
    {
        spdlog::error("StarfieldRenderer: CreateRootSignature failed (hr=0x{:08X})", static_cast<unsigned>(hr));
        return false;
    }

    D3D12_INPUT_ELEMENT_DESC starInput[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(StarVertex, position), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32_FLOAT, 0, offsetof(StarVertex, brightness), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 1, DXGI_FORMAT_R32_FLOAT, 0, offsetof(StarVertex, security), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}};

    D3D12_INPUT_ELEMENT_DESC routeInput[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(RouteVertex, position), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32_FLOAT, 0, offsetof(RouteVertex, progress), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 1, DXGI_FORMAT_R32_FLOAT, 0, offsetof(RouteVertex, state), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}};

    D3D12_BLEND_DESC additiveBlend{};
    additiveBlend.AlphaToCoverageEnable = FALSE;
    additiveBlend.IndependentBlendEnable = FALSE;
    auto& additiveRT = additiveBlend.RenderTarget[0];
    additiveRT.BlendEnable = TRUE;
    additiveRT.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    additiveRT.SrcBlend = D3D12_BLEND_SRC_ALPHA;
    additiveRT.DestBlend = D3D12_BLEND_ONE;
    additiveRT.BlendOp = D3D12_BLEND_OP_ADD;
    additiveRT.SrcBlendAlpha = D3D12_BLEND_ONE;
    additiveRT.DestBlendAlpha = D3D12_BLEND_ONE;
    additiveRT.BlendOpAlpha = D3D12_BLEND_OP_ADD;

    D3D12_BLEND_DESC routeBlend{};
    routeBlend.AlphaToCoverageEnable = FALSE;
    routeBlend.IndependentBlendEnable = FALSE;
    auto& routeRT = routeBlend.RenderTarget[0];
    routeRT.BlendEnable = TRUE;
    routeRT.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    routeRT.SrcBlend = D3D12_BLEND_SRC_ALPHA;
    routeRT.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    routeRT.BlendOp = D3D12_BLEND_OP_ADD;
    routeRT.SrcBlendAlpha = D3D12_BLEND_ONE;
    routeRT.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
    routeRT.BlendOpAlpha = D3D12_BLEND_OP_ADD;

    D3D12_RASTERIZER_DESC rasterDesc{};
    rasterDesc.FillMode = D3D12_FILL_MODE_SOLID;
    rasterDesc.CullMode = D3D12_CULL_MODE_NONE;
    rasterDesc.FrontCounterClockwise = FALSE;
    rasterDesc.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
    rasterDesc.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
    rasterDesc.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
    rasterDesc.DepthClipEnable = TRUE;
    rasterDesc.MultisampleEnable = FALSE;
    rasterDesc.AntialiasedLineEnable = FALSE;
    rasterDesc.ForcedSampleCount = 0;
    rasterDesc.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;

    D3D12_DEPTH_STENCIL_DESC depthDesc{};
    depthDesc.DepthEnable = FALSE;
    depthDesc.StencilEnable = FALSE;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC starPso{};
    starPso.InputLayout = {starInput, static_cast<UINT>(std::size(starInput))};
    starPso.pRootSignature = rootSignature_.Get();
    starPso.VS = {starVS->GetBufferPointer(), starVS->GetBufferSize()};
    starPso.PS = {starPS->GetBufferPointer(), starPS->GetBufferSize()};
    starPso.BlendState = additiveBlend;
    starPso.RasterizerState = rasterDesc;
    starPso.DepthStencilState = depthDesc;
    starPso.SampleMask = UINT_MAX;
    starPso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
    starPso.NumRenderTargets = 1;
    starPso.RTVFormats[0] = targetFormat;
    starPso.SampleDesc.Count = 1;

    hr = device->CreateGraphicsPipelineState(&starPso, IID_PPV_ARGS(&starfieldPipeline_));
    if (FAILED(hr))
    {
        spdlog::error("StarfieldRenderer: CreateGraphicsPipelineState (starfield) failed (hr=0x{:08X})", static_cast<unsigned>(hr));
        return false;
    }

    D3D12_GRAPHICS_PIPELINE_STATE_DESC routePso{};
    routePso.InputLayout = {routeInput, static_cast<UINT>(std::size(routeInput))};
    routePso.pRootSignature = rootSignature_.Get();
    routePso.VS = {routeVS->GetBufferPointer(), routeVS->GetBufferSize()};
    routePso.PS = {routePS->GetBufferPointer(), routePS->GetBufferSize()};
    routePso.BlendState = routeBlend;
    routePso.RasterizerState = rasterDesc;
    routePso.DepthStencilState = depthDesc;
    routePso.SampleMask = UINT_MAX;
    routePso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
    routePso.NumRenderTargets = 1;
    routePso.RTVFormats[0] = targetFormat;
    routePso.SampleDesc.Count = 1;

    hr = device->CreateGraphicsPipelineState(&routePso, IID_PPV_ARGS(&routePipeline_));
    if (FAILED(hr))
    {
        spdlog::error("StarfieldRenderer: CreateGraphicsPipelineState (route) failed (hr=0x{:08X})", static_cast<unsigned>(hr));
        return false;
    }

    return true;
}

bool StarfieldRenderer::createVertexBuffer(ID3D12Device* device)
{
    if (!catalog_ || catalog_->records.empty())
    {
        spdlog::warn("StarfieldRenderer: catalog has no records");
        return false;
    }

    std::vector<StarVertex> vertices;
    vertices.reserve(catalog_->records.size());
    sampleCatalogPositions_.clear();
    sampleCatalogPositions_.reserve(8);

    std::size_t sampleLogCount = 0;
    for (const auto& record : catalog_->records)
    {
        StarVertex vertex{};
        vertex.position = DirectX::XMFLOAT3(record.position.x, record.position.y, record.position.z);

        float security = record.security;
        if (!std::isfinite(security))
        {
            security = 0.0f;
        }
        security = std::clamp(security, -1.0f, 1.0f);

        const float brightness = 0.35f + 0.65f * std::clamp(1.0f - std::fabs(security), 0.0f, 1.0f);

        vertex.brightness = brightness;
        vertex.security = security;
        vertices.push_back(vertex);

        if (sampleLogCount < 8)
        {
            sampleCatalogPositions_.push_back(vertex.position);
            spdlog::info(
                "StarfieldRenderer: catalog sample idx={} id={} pos=({:.1f},{:.1f},{:.1f}) security={:.2f}",
                sampleLogCount,
                record.system_id,
                vertex.position.x,
                vertex.position.y,
                vertex.position.z,
                vertex.security);
            ++sampleLogCount;
        }
    }

    const UINT bufferSize = static_cast<UINT>(vertices.size() * sizeof(StarVertex));

    const auto heapProps = uploadHeapProps();
    const auto resourceDesc = bufferDesc(bufferSize);

    HRESULT hr = device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &resourceDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&starVertexBuffer_));
    if (FAILED(hr))
    {
        spdlog::error("StarfieldRenderer: failed to create vertex buffer (hr=0x{:08X})", static_cast<unsigned>(hr));
        return false;
    }

    void* mapped = nullptr;
    D3D12_RANGE readRange{0, 0};
    hr = starVertexBuffer_->Map(0, &readRange, &mapped);
    if (FAILED(hr) || !mapped)
    {
        spdlog::error("StarfieldRenderer: failed to map vertex buffer (hr=0x{:08X})", static_cast<unsigned>(hr));
        return false;
    }

    std::memcpy(mapped, vertices.data(), bufferSize);
    starVertexBuffer_->Unmap(0, nullptr);

    starVertexCount_ = static_cast<UINT>(vertices.size());
    spdlog::info("StarfieldRenderer: vertex buffer uploaded (stars={})", starVertexCount_);
    starVertexView_.BufferLocation = starVertexBuffer_->GetGPUVirtualAddress();
    starVertexView_.SizeInBytes = bufferSize;
    starVertexView_.StrideInBytes = sizeof(StarVertex);

    return true;
}

bool StarfieldRenderer::createConstantBuffer(ID3D12Device* device)
{
    const UINT64 alignedSize = (sizeof(FrameConstants) + 255u) & ~255u;

    const auto heapProps = uploadHeapProps();
    const auto desc = bufferDesc(alignedSize);

    HRESULT hr = device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &desc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&constantBuffer_));
    if (FAILED(hr))
    {
        spdlog::error("StarfieldRenderer: failed to create constant buffer (hr=0x{:08X})", static_cast<unsigned>(hr));
        return false;
    }

    D3D12_RANGE readRange{0, 0};
    void* mapped = nullptr;
    hr = constantBuffer_->Map(0, &readRange, &mapped);
    if (FAILED(hr) || !mapped)
    {
        spdlog::error("StarfieldRenderer: failed to map constant buffer (hr=0x{:08X})", static_cast<unsigned>(hr));
        return false;
    }

    mappedConstants_ = static_cast<FrameConstants*>(mapped);
    std::memset(mappedConstants_, 0, sizeof(FrameConstants));

    return true;
}

bool StarfieldRenderer::ensureRouteCapacity(ID3D12Device* device, UINT vertexCount)
{
    if (vertexCount == 0)
    {
        return true;
    }

    if (routeVertexCapacity_ >= vertexCount && routeVertexBuffer_ && mappedRouteVertices_)
    {
        return true;
    }

    if (routeVertexBuffer_ && mappedRouteVertices_)
    {
        routeVertexBuffer_->Unmap(0, nullptr);
        mappedRouteVertices_ = nullptr;
    }

    const auto heapProps = uploadHeapProps();
    const UINT64 bufferSize = static_cast<UINT64>(vertexCount) * sizeof(RouteVertex);
    const auto desc = bufferDesc(bufferSize);

    HRESULT hr = device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &desc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&routeVertexBuffer_));
    if (FAILED(hr))
    {
        spdlog::error("StarfieldRenderer: failed to create route buffer (hr=0x{:08X})", static_cast<unsigned>(hr));
        routeVertexBuffer_.Reset();
        routeVertexCapacity_ = 0;
        return false;
    }

    D3D12_RANGE readRange{0, 0};
    void* mapped = nullptr;
    hr = routeVertexBuffer_->Map(0, &readRange, &mapped);
    if (FAILED(hr) || !mapped)
    {
        spdlog::error("StarfieldRenderer: failed to map route buffer (hr=0x{:08X})", static_cast<unsigned>(hr));
        routeVertexBuffer_.Reset();
        routeVertexCapacity_ = 0;
        return false;
    }

    mappedRouteVertices_ = static_cast<RouteVertex*>(mapped);
    routeVertexCapacity_ = vertexCount;

    routeVertexView_.BufferLocation = routeVertexBuffer_->GetGPUVirtualAddress();
    routeVertexView_.SizeInBytes = static_cast<UINT>(bufferSize);
    routeVertexView_.StrideInBytes = sizeof(RouteVertex);

    return true;
}

bool StarfieldRenderer::updateConstants(const overlay::OverlayState* state, UINT width, UINT height)
{
    if (!mappedConstants_)
    {
        return false;
    }

    auto resolveRecord = [this](const std::string& systemId, const std::string& displayName) -> const overlay::StarCatalogRecord*
    {
        if (!catalog_)
        {
            return nullptr;
        }

        const overlay::StarCatalogRecord* record = nullptr;
        const std::uint32_t parsedSystemId = parseSystemId(systemId);
        if (parsedSystemId != 0)
        {
            record = catalog_->find_by_system_id(parsedSystemId);
        }
        if (!record && !systemId.empty())
        {
            record = catalog_->find_by_name(systemId);
        }
        if (!record && !displayName.empty())
        {
            const std::uint32_t parsedDisplayId = parseSystemId(displayName);
            if (parsedDisplayId != 0)
            {
                record = catalog_->find_by_system_id(parsedDisplayId);
            }
            if (!record)
            {
                record = catalog_->find_by_name(displayName);
            }
        }
        return record;
    };

    const float aspect = height > 0 ? static_cast<float>(width) / static_cast<float>(height) : 1.0f;
    float fovRadians = DirectX::XMConvertToRadians(60.0f);

    DirectX::XMFLOAT3 focusCenter = boundsCenter_;
    float focusRadius = boundsRadius_;
    bool focusAssigned = false;
    bool localFocusOverride = false;
    if (routeFocusValid_)
    {
        focusCenter = routeFocusCenter_;
        focusRadius = std::max(routeFocusRadius_, 1.0f);
        focusAssigned = true;
    }
    else if (state)
    {
        if (state->player_marker.has_value())
        {
            const auto& marker = *state->player_marker;
            if (const auto* record = resolveRecord(marker.system_id, marker.display_name))
            {
                focusCenter = DirectX::XMFLOAT3(record->position.x, record->position.y, record->position.z);
                focusRadius = kDefaultLocalFocusRadius;
                focusAssigned = true;
                localFocusOverride = true;
            }
        }

        if (!focusAssigned && !state->route.empty())
        {
            focusCenter = routeFocusCenter_;
            focusRadius = std::max({routeFocusRadius_, kDefaultLocalFocusRadius, 1.0f});
            focusAssigned = true;
            localFocusOverride = true;
        }

        if (!focusAssigned && !state->highlighted_systems.empty())
        {
            const auto& highlight = state->highlighted_systems.front();
            if (const auto* record = resolveRecord(highlight.system_id, highlight.display_name))
            {
                focusCenter = DirectX::XMFLOAT3(record->position.x, record->position.y, record->position.z);
                focusRadius = kDefaultLocalFocusRadius;
                focusAssigned = true;
                localFocusOverride = true;
            }
        }
    }

    float coverageRadius = focusRadius;
    if (localFocusOverride)
    {
        const auto focusV = DirectX::XMLoadFloat3(&focusCenter);
        const auto boundsCenterV = DirectX::XMLoadFloat3(&boundsCenter_);
        const float focusToBounds = DirectX::XMVectorGetX(DirectX::XMVector3Length(DirectX::XMVectorSubtract(focusV, boundsCenterV)));
        const float expanded = boundsRadius_ + focusToBounds;
        coverageRadius = std::max(coverageRadius, expanded);
    }

    externalCameraActive_ = state && state->camera_pose.has_value();

    DirectX::XMVECTOR targetV = DirectX::XMLoadFloat3(&focusCenter);
    DirectX::XMVECTOR eyeV = targetV;
    DirectX::XMVECTOR upV = DirectX::XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

    if (externalCameraActive_)
    {
        const auto& cam = *state->camera_pose;
        eyeV = DirectX::XMVectorSet(cam.position.x, cam.position.y, cam.position.z, 1.0f);
        targetV = DirectX::XMVectorSet(cam.look_at.x, cam.look_at.y, cam.look_at.z, 1.0f);
        upV = DirectX::XMVectorSet(cam.up.x, cam.up.y, cam.up.z, 0.0f);
        fovRadians = DirectX::XMConvertToRadians(std::clamp(cam.fov_degrees, 15.0f, 120.0f));
    }
    else
    {
        const bool treatAsLocalFocus = routeFocusValid_ || localFocusOverride;
        const float radiusForDistance = treatAsLocalFocus
            ? std::min(std::max(coverageRadius, 1.0f), kLocalViewMaxRadius)
            : std::max(coverageRadius, 1.0f);
        DirectX::XMVECTOR forward = DirectX::XMVectorSet(0.0f, 0.0f, -1.0f, 0.0f);
        DirectX::XMVECTOR right = DirectX::XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f);
        const DirectX::XMVECTOR baseUp = upV;
        DirectX::XMVECTOR rotation = DirectX::XMQuaternionRotationRollPitchYaw(manualPitch_, manualYaw_, 0.0f);
        forward = DirectX::XMVector3Rotate(forward, rotation);
        right = DirectX::XMVector3Rotate(right, rotation);
        upV = DirectX::XMVector3Rotate(baseUp, rotation);

        const float distanceFactor = treatAsLocalFocus ? kDistanceRouteFocus : kDistanceGlobal;
        const float distance = radiusForDistance * distanceFactor;
        DirectX::XMVECTOR offset = DirectX::XMVectorScale(forward, distance);
        eyeV = DirectX::XMVectorSubtract(targetV, offset);

        const float panScaleBase = std::max(radiusForDistance, 1.0f);
        DirectX::XMVECTOR panOffset = DirectX::XMVectorAdd(
            DirectX::XMVectorScale(right, manualPanX_ * panScaleBase),
            DirectX::XMVectorScale(upV, manualPanY_ * panScaleBase));
        eyeV = DirectX::XMVectorAdd(eyeV, panOffset);
        targetV = DirectX::XMVectorAdd(targetV, panOffset);
    }

    if (DirectX::XMVectorGetX(DirectX::XMVector3LengthSq(DirectX::XMVectorSubtract(targetV, eyeV))) < 1e-6f)
    {
        targetV = DirectX::XMVectorAdd(targetV, DirectX::XMVectorSet(0.0f, 0.0f, -1.0f, 0.0f));
    }

    if (DirectX::XMVectorGetX(DirectX::XMVector3LengthSq(upV)) < 1e-6f)
    {
        upV = DirectX::XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    }

    DirectX::XMVECTOR eyeOffset = DirectX::XMVectorSubtract(eyeV, targetV);
    if (DirectX::XMVectorGetX(DirectX::XMVector3LengthSq(eyeOffset)) < 1e-6f)
    {
        eyeOffset = DirectX::XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);
    }

    const float zoomScale = std::clamp(manualZoom_, minZoom_, maxZoom_);
    eyeOffset = DirectX::XMVectorScale(eyeOffset, zoomScale);
    eyeV = DirectX::XMVectorAdd(targetV, eyeOffset);
    DirectX::XMFLOAT3 eye;
    DirectX::XMFLOAT3 target;
    DirectX::XMFLOAT3 up;
    DirectX::XMStoreFloat3(&eye, eyeV);
    DirectX::XMStoreFloat3(&target, targetV);
    DirectX::XMStoreFloat3(&up, DirectX::XMVector3Normalize(upV));

    const bool treatAsLocalFocus = routeFocusValid_ || localFocusOverride;
    const float focusForPlanes = treatAsLocalFocus ? std::max(coverageRadius, 1.0f) : std::max(boundsRadius_, 1.0f);
    const float radiusForDistance = treatAsLocalFocus
        ? std::min(std::max(coverageRadius, 1.0f), kLocalViewMaxRadius)
        : std::max(coverageRadius, 1.0f);
    const float nearBasis = treatAsLocalFocus ? std::max(radiusForDistance, 1.0f) : focusForPlanes;
    const float nearScale = treatAsLocalFocus ? 0.001f : 0.01f;
    const float farScale = treatAsLocalFocus ? 6.0f : 25.0f;
    const float nearPlane = std::max(nearBasis * nearScale, 0.1f);
    const float farPlane = std::max(focusForPlanes * farScale, nearPlane + 250.0f);

    auto view = DirectX::XMMatrixLookAtRH(eyeV, targetV, DirectX::XMVector3Normalize(upV));
    auto proj = DirectX::XMMatrixPerspectiveFovRH(fovRadians, aspect, nearPlane, farPlane);
    auto viewProj = view * proj;

    DirectX::XMStoreFloat4x4(&lastViewProjUntransposed_, viewProj);
    DirectX::XMStoreFloat4x4(&mappedConstants_->viewProj, DirectX::XMMatrixTranspose(viewProj));
    mappedConstants_->cameraPosition = DirectX::XMFLOAT4(eye.x, eye.y, eye.z, 1.0f);

    const DirectX::XMFLOAT3 clusterCenter = treatAsLocalFocus ? focusCenter : boundsCenter_;
    const float clusterRadius = treatAsLocalFocus ? focusForPlanes : boundsRadius_;
    mappedConstants_->clusterCenterRadius = DirectX::XMFLOAT4(clusterCenter.x, clusterCenter.y, clusterCenter.z, clusterRadius);

    const float baseSizePx = std::clamp(static_cast<float>(height) * 0.012f * zoomScale, 3.0f, 28.0f);
    const float minSizePx = std::clamp(baseSizePx * 0.35f, 1.75f, baseSizePx);
    const float falloff = treatAsLocalFocus ? 0.0022f : 0.0014f;
    mappedConstants_->params = DirectX::XMFLOAT4(baseSizePx, aspect, minSizePx, falloff);

    hasFrameConstants_ = true;
    lastViewportWidth_ = width;
    lastViewportHeight_ = height;

    const bool hasCameraPose = state && state->camera_pose.has_value();
    const int constantsLogIndex = g_constantsLogCounter.fetch_add(1, std::memory_order_relaxed);
    if (constantsLogIndex < 60 || hasCameraPose)
    {
        spdlog::info(
            "StarfieldRenderer: updateConstants camera=({:.2f},{:.2f},{:.2f}) target=({:.2f},{:.2f},{:.2f}) zoomScale={:.3f} manualZoom={:.3f} focusRadius={:.2f} treatAsLocal={} routeFocus={} viewport={}x{} near={:.2f} far={:.2f} cameraPose={} focusCenter=({:.2f},{:.2f},{:.2f})",
            eye.x,
            eye.y,
            eye.z,
            target.x,
            target.y,
            target.z,
            zoomScale,
            manualZoom_,
            focusForPlanes,
            treatAsLocalFocus,
            routeFocusValid_,
            width,
            height,
            nearPlane,
            farPlane,
            hasCameraPose,
            focusCenter.x,
            focusCenter.y,
            focusCenter.z);
    }

    if (!sampleCatalogPositions_.empty() && constantsLogIndex < 20)
    {
        auto viewProjMatrix = DirectX::XMLoadFloat4x4(&lastViewProjUntransposed_);
        std::size_t maxSamples = std::min<std::size_t>(sampleCatalogPositions_.size(), 4);
        for (std::size_t i = 0; i < maxSamples; ++i)
        {
            const auto& sample = sampleCatalogPositions_[i];
            auto worldV = DirectX::XMVectorSet(sample.x, sample.y, sample.z, 1.0f);
            auto clipV = DirectX::XMVector4Transform(worldV, viewProjMatrix);
            DirectX::XMFLOAT4 clipF;
            DirectX::XMStoreFloat4(&clipF, clipV);
            float ndcX = 0.0f;
            float ndcY = 0.0f;
            bool valid = std::abs(clipF.w) > 1e-5f;
            if (valid)
            {
                ndcX = clipF.x / clipF.w;
                ndcY = clipF.y / clipF.w;
            }
            spdlog::info(
                "StarfieldRenderer: sample proj idx={} world=({:.1f},{:.1f},{:.1f}) clip=({:.2f},{:.2f},{:.2f},{:.2f}) ndc=({:.2f},{:.2f}) valid={} zoom={:.3f}",
                i,
                sample.x,
                sample.y,
                sample.z,
                clipF.x,
                clipF.y,
                clipF.z,
                clipF.w,
                ndcX,
                ndcY,
                valid,
                manualZoom_);
        }
    }

    return true;
}

bool StarfieldRenderer::updateRouteBuffer(const overlay::OverlayState* state)
{
    if (!state)
    {
        if (routeVertexCount_ != 0)
        {
            spdlog::debug("StarfieldRenderer: route buffer cleared (null state)");
        }
        routeVertexCount_ = 0;
        routeFocusValid_ = false;
        return true;
    }

    if (!device_)
    {
        spdlog::warn("StarfieldRenderer: updateRouteBuffer invoked without device");
        routeVertexCount_ = 0;
        routeFocusValid_ = false;
        return false;
    }

    if (state->route.empty())
    {
        if (routeVertexCount_ != 0)
        {
            spdlog::info("StarfieldRenderer: route buffer cleared (empty route)");
        }
        routeVertexCount_ = 0;
        routeFocusValid_ = false;
        return true;
    }

    const auto previousTimestamp = lastRouteTimestamp_;
    const auto previousCount = lastRouteCount_;

    std::vector<DirectX::XMFLOAT3> positions;
    std::vector<float> activeMask;
    positions.reserve(state->route.size());
    activeMask.reserve(state->route.size());

    const std::string activeId = state->active_route_node_id.value_or(std::string{});
    const bool routeChanged = state->generated_at_ms != previousTimestamp || state->route.size() != previousCount || lastActiveNodeId_ != activeId;

    if (routeChanged)
    {
        const int logIndex = g_routeLogCounter.fetch_add(1, std::memory_order_relaxed);
        if (logIndex < 24)
        {
            spdlog::info(
                "StarfieldRenderer: processing route update (nodes={}, active='{}', generated_ms={}, focusValid={})",
                state->route.size(),
                activeId,
                state->generated_at_ms,
                routeFocusValid_);
        }
    }

    for (const auto& node : state->route)
    {
        const overlay::StarCatalogRecord* record = nullptr;
        std::uint32_t resolvedId = 0;

        if (!node.system_id.empty())
        {
            resolvedId = parseSystemId(node.system_id);
            if (resolvedId != 0)
            {
                record = catalog_->find_by_system_id(resolvedId);
            }
            if (!record)
            {
                record = catalog_->find_by_name(node.system_id);
                if (record)
                {
                    resolvedId = record->system_id;
                }
            }
        }

        if (!record && !node.display_name.empty())
        {
            if (resolvedId == 0)
            {
                resolvedId = parseSystemId(node.display_name);
                if (resolvedId != 0)
                {
                    record = catalog_->find_by_system_id(resolvedId);
                }
            }
            if (!record)
            {
                record = catalog_->find_by_name(node.display_name);
                if (record)
                {
                    resolvedId = record->system_id;
                }
            }
        }

        if (!record)
        {
            if (state->generated_at_ms != previousTimestamp || state->route.size() != previousCount)
            {
                const std::string identifier = !node.system_id.empty() ? node.system_id : node.display_name;
                std::scoped_lock lock(g_missingNodesMutex);
                if (g_loggedMissingNodes.insert(identifier).second)
                {
                    spdlog::warn("StarfieldRenderer: catalog lookup failed for route node '{}' (display='{}')", node.system_id, node.display_name);
                }
            }
            continue;
        }

        if (resolvedId == 0)
        {
            resolvedId = record->system_id;
        }

        positions.emplace_back(record->position.x, record->position.y, record->position.z);

        const bool isActive = !activeId.empty()
            && (activeId == node.system_id
                || (!node.display_name.empty() && activeId == node.display_name)
                || activeId == std::to_string(resolvedId));
        activeMask.push_back(isActive ? 1.0f : 0.0f);

        if (routeChanged)
        {
            spdlog::debug(
                "StarfieldRenderer:   node '{}'/'{}' resolved -> id={} active={}",
                node.system_id,
                node.display_name,
                resolvedId,
                isActive);
        }
    }

    if (positions.empty())
    {
        routeVertexCount_ = 0;
        routeFocusValid_ = false;
        if (state->generated_at_ms != previousTimestamp || state->route.size() != previousCount)
        {
            spdlog::warn("StarfieldRenderer: route buffer empty after processing {} nodes", state->route.size());
        }
        return true;
    }

    DirectX::XMFLOAT3 bboxMin = positions.front();
    DirectX::XMFLOAT3 bboxMax = positions.front();
    for (const auto& pos : positions)
    {
        bboxMin.x = std::min(bboxMin.x, pos.x);
        bboxMin.y = std::min(bboxMin.y, pos.y);
        bboxMin.z = std::min(bboxMin.z, pos.z);
        bboxMax.x = std::max(bboxMax.x, pos.x);
        bboxMax.y = std::max(bboxMax.y, pos.y);
        bboxMax.z = std::max(bboxMax.z, pos.z);
    }

    DirectX::XMFLOAT3 focusCenter{
        (bboxMin.x + bboxMax.x) * 0.5f,
        (bboxMin.y + bboxMax.y) * 0.5f,
        (bboxMin.z + bboxMax.z) * 0.5f};

    float focusRadius = 1.0f;
    for (const auto& pos : positions)
    {
        const float dx = pos.x - focusCenter.x;
        const float dy = pos.y - focusCenter.y;
        const float dz = pos.z - focusCenter.z;
        focusRadius = std::max(focusRadius, std::sqrt(dx * dx + dy * dy + dz * dz));
    }

    routeFocusCenter_ = focusCenter;
    routeFocusRadius_ = std::max(focusRadius, 1.0f);
    routeFocusValid_ = positions.size() >= 2;

    if (!ensureRouteCapacity(device_.Get(), static_cast<UINT>(positions.size())))
    {
        return false;
    }

    if (positions.size() == 1)
    {
        routeFocusCenter_ = positions[0];
        routeFocusRadius_ = kSingleNodeFocusRadius;
        routeFocusValid_ = false;
        mappedRouteVertices_[0] = RouteVertex{positions[0], 0.0f, activeMask[0], 0.0f};
        routeVertexCount_ = 1;
        lastRouteTimestamp_ = state->generated_at_ms;
        lastRouteCount_ = static_cast<std::uint32_t>(state->route.size());
        lastActiveNodeId_ = state->active_route_node_id.value_or(std::string{});
        if (state->generated_at_ms != previousTimestamp || state->route.size() != previousCount)
        {
            spdlog::info("StarfieldRenderer: route buffer updated (1 node, active='{}')", lastActiveNodeId_);
        }
        return true;
    }

    std::vector<float> cumulative(positions.size(), 0.0f);
    double totalDistance = 0.0;
    for (std::size_t i = 1; i < positions.size(); ++i)
    {
        const auto& prev = positions[i - 1];
        const auto& curr = positions[i];
        const double dx = static_cast<double>(curr.x) - static_cast<double>(prev.x);
        const double dy = static_cast<double>(curr.y) - static_cast<double>(prev.y);
        const double dz = static_cast<double>(curr.z) - static_cast<double>(prev.z);
        totalDistance += std::sqrt(dx * dx + dy * dy + dz * dz);
        cumulative[i] = static_cast<float>(totalDistance);
    }

    const float invTotal = totalDistance > 1e-3 ? static_cast<float>(1.0 / totalDistance) : 0.0f;

    for (std::size_t i = 0; i < positions.size(); ++i)
    {
        const float fallbackProgress = positions.size() > 1 ? static_cast<float>(i) / static_cast<float>(positions.size() - 1) : 0.0f;
        const float progress = invTotal > 0.0f ? cumulative[i] * invTotal : fallbackProgress;
        mappedRouteVertices_[i] = RouteVertex{positions[i], progress, activeMask[i], 0.0f};
    }

    routeVertexCount_ = static_cast<UINT>(positions.size());
    lastRouteTimestamp_ = state->generated_at_ms;
    lastRouteCount_ = static_cast<std::uint32_t>(state->route.size());
    lastActiveNodeId_ = state->active_route_node_id.value_or(std::string{});

    if (routeChanged)
    {
        spdlog::info(
            "StarfieldRenderer: route buffer updated (nodes={}, radius={:.2f}, active='{}')",
            routeVertexCount_,
            routeFocusRadius_,
            lastActiveNodeId_);
        spdlog::debug(
            "StarfieldRenderer:   focus center=({:.2f},{:.2f},{:.2f}) radius={:.2f}",
            routeFocusCenter_.x,
            routeFocusCenter_.y,
            routeFocusCenter_.z,
            routeFocusRadius_);
    }

    return true;
}

bool StarfieldRenderer::projectWorldToScreen(const DirectX::XMFLOAT3& world, float viewportWidth, float viewportHeight, DirectX::XMFLOAT2& out) const
{
    if (!hasFrameConstants_)
    {
        return false;
    }

    const float width = viewportWidth > 0.0f ? viewportWidth : static_cast<float>(lastViewportWidth_);
    const float height = viewportHeight > 0.0f ? viewportHeight : static_cast<float>(lastViewportHeight_);
    if (width <= 0.0f || height <= 0.0f)
    {
        return false;
    }

    const DirectX::XMMATRIX viewProj = DirectX::XMLoadFloat4x4(&lastViewProjUntransposed_);
    const DirectX::XMVECTOR pos = DirectX::XMVectorSet(world.x, world.y, world.z, 1.0f);
    const DirectX::XMVECTOR clip = DirectX::XMVector4Transform(pos, viewProj);
    const float w = DirectX::XMVectorGetW(clip);
    if (w <= 1e-4f)
    {
        return false;
    }

    const DirectX::XMVECTOR ndc = DirectX::XMVectorDivide(clip, DirectX::XMVectorReplicate(w));
    const float x = DirectX::XMVectorGetX(ndc);
    const float y = DirectX::XMVectorGetY(ndc);
    const float z = DirectX::XMVectorGetZ(ndc);
    if (z < 0.0f || z > 1.0f || x < -1.2f || x > 1.2f || y < -1.2f || y > 1.2f)
    {
        return false;
    }

    out.x = (x * 0.5f + 0.5f) * width;
    out.y = (1.0f - (y * 0.5f + 0.5f)) * height;
    return true;
}

bool StarfieldRenderer::projectSystemToScreen(const std::string& systemId, float viewportWidth, float viewportHeight, DirectX::XMFLOAT2& out) const
{
    if (!catalog_)
    {
        return false;
    }

    const overlay::StarCatalogRecord* record = nullptr;
    std::uint32_t resolvedId = parseSystemId(systemId);
    if (resolvedId != 0)
    {
        record = catalog_->find_by_system_id(resolvedId);
    }
    if (!record && !systemId.empty())
    {
        record = catalog_->find_by_name(systemId);
    }

    if (!record)
    {
        return false;
    }

    const DirectX::XMFLOAT3 world{
        record->position.x,
        record->position.y,
        record->position.z};
    const bool projected = projectWorldToScreen(world, viewportWidth, viewportHeight, out);
    if (!projected)
    {
        const auto name = catalog_->name_for(*record);
        const int failIndex = g_projectionFailCounter.fetch_add(1, std::memory_order_relaxed);
        if (failIndex < 40)
        {
            spdlog::debug(
                "StarfieldRenderer: projection culled system='{}' resolved={} name='{}'",
                systemId,
                resolvedId,
                std::string{name});
        }
    }
    return projected;
}

void StarfieldRenderer::adjustZoom(float wheelDelta)
{
    if (std::abs(wheelDelta) < 1e-3f)
    {
        return;
    }

    const float scale = std::exp(-wheelDelta * zoomStep_);
    const float previous = manualZoom_;
    manualZoom_ = std::clamp(manualZoom_ * scale, minZoom_, maxZoom_);
    spdlog::info(
        "StarfieldRenderer: manual zoom adjusted {} -> {} (wheel={:.3f})",
        previous,
        manualZoom_,
        wheelDelta);
}

void StarfieldRenderer::resetZoom()
{
    manualZoom_ = 1.0f;
    manualYaw_ = 0.0f;
    manualPitch_ = 0.35f;
    manualPanX_ = 0.0f;
    manualPanY_ = 0.0f;
    spdlog::info("StarfieldRenderer: manual view reset");
}

void StarfieldRenderer::orbitDrag(float deltaX, float deltaY)
{
    if (externalCameraActive_ || !std::isfinite(deltaX) || !std::isfinite(deltaY))
    {
        return;
    }

    manualYaw_ += deltaX * kOrbitYawSensitivity;
    manualPitch_ = std::clamp(manualPitch_ + deltaY * kOrbitPitchSensitivity, kOrbitPitchMin, kOrbitPitchMax);
}

void StarfieldRenderer::panDrag(float deltaX, float deltaY)
{
    if (externalCameraActive_)
    {
        return;
    }

    if (!std::isfinite(deltaX) || !std::isfinite(deltaY))
    {
        return;
    }

    const float referenceWidth = static_cast<float>(std::max<UINT>(1, lastViewportWidth_));
    const float referenceHeight = static_cast<float>(std::max<UINT>(1, lastViewportHeight_));
    const float reference = std::max(referenceWidth, referenceHeight);
    if (reference <= 0.0f)
    {
        return;
    }

    const float scale = (kPanSensitivity * manualZoom_) / reference;
    manualPanX_ = std::clamp(manualPanX_ - deltaX * scale, -kPanLimit, kPanLimit);
    manualPanY_ = std::clamp(manualPanY_ + deltaY * scale, -kPanLimit, kPanLimit);
}
