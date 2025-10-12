#include "starfield_renderer.hpp"

#include <d3dcompiler.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <string>
#include <system_error>
#include <vector>

#include <spdlog/spdlog.h>

#include <windows.h>

#pragma comment(lib, "d3dcompiler.lib")

namespace
{
    using Microsoft::WRL::ComPtr;

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

    if (!updateConstants(state, width, height))
    {
        return;
    }

    updateRouteBuffer(state);

    D3D12_VIEWPORT viewport{};
    viewport.TopLeftX = 0.0f;
    viewport.TopLeftY = 0.0f;
    viewport.Width = static_cast<float>(width);
    viewport.Height = static_cast<float>(height);
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;

    D3D12_RECT scissor{0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};

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
            output.pointSize = saturate(baseSize / distance);

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
            float3 baseColor = float3(0.35f + 0.65f * warm, 0.3f + 0.6f * cool, 0.85f);
            float alpha = clamp01(0.2f + intensity * 0.75f);
            return float4(baseColor * intensity, alpha);
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

    const float aspect = height > 0 ? static_cast<float>(width) / static_cast<float>(height) : 1.0f;

    DirectX::XMFLOAT3 eye = boundsCenter_;
    DirectX::XMFLOAT3 target = boundsCenter_;
    DirectX::XMFLOAT3 up{0.0f, 1.0f, 0.0f};
    float fovRadians = DirectX::XMConvertToRadians(60.0f);

    if (state && state->camera_pose)
    {
        const auto& cam = *state->camera_pose;
        eye = DirectX::XMFLOAT3(cam.position.x, cam.position.y, cam.position.z);
        target = DirectX::XMFLOAT3(cam.look_at.x, cam.look_at.y, cam.look_at.z);
        up = DirectX::XMFLOAT3(cam.up.x, cam.up.y, cam.up.z);
        fovRadians = DirectX::XMConvertToRadians(std::clamp(cam.fov_degrees, 15.0f, 120.0f));
    }
    else
    {
        const float radius = std::max(boundsRadius_, 1.0f);
        eye = DirectX::XMFLOAT3(boundsCenter_.x, boundsCenter_.y + radius * 0.4f, boundsCenter_.z + radius * 1.6f);
    }

    DirectX::XMVECTOR eyeV = DirectX::XMLoadFloat3(&eye);
    DirectX::XMVECTOR targetV = DirectX::XMLoadFloat3(&target);
    DirectX::XMVECTOR upV = DirectX::XMLoadFloat3(&up);

    const float eyeTargetLenSq = DirectX::XMVectorGetX(DirectX::XMVector3LengthSq(DirectX::XMVectorSubtract(targetV, eyeV)));
    if (eyeTargetLenSq < 1e-6f)
    {
        targetV = DirectX::XMVectorAdd(targetV, DirectX::XMVectorSet(0.0f, 0.0f, -1.0f, 0.0f));
    }

    const float upLenSq = DirectX::XMVectorGetX(DirectX::XMVector3LengthSq(upV));
    if (upLenSq < 1e-6f)
    {
        upV = DirectX::XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    }

    const float nearPlane = std::max(boundsRadius_ * 0.01f, 0.1f);
    const float farPlane = std::max(boundsRadius_ * 20.0f, nearPlane + 100.0f);

    auto view = DirectX::XMMatrixLookAtRH(eyeV, targetV, DirectX::XMVector3Normalize(upV));
    auto proj = DirectX::XMMatrixPerspectiveFovRH(fovRadians, aspect, nearPlane, farPlane);

    DirectX::XMStoreFloat4x4(&mappedConstants_->viewProj, DirectX::XMMatrixTranspose(view * proj));
    mappedConstants_->cameraPosition = DirectX::XMFLOAT4(eye.x, eye.y, eye.z, 1.0f);
    mappedConstants_->clusterCenterRadius = DirectX::XMFLOAT4(boundsCenter_.x, boundsCenter_.y, boundsCenter_.z, boundsRadius_);

    const float baseSizePx = std::clamp(static_cast<float>(height) * 0.012f, 3.0f, 18.0f);
    mappedConstants_->params = DirectX::XMFLOAT4(baseSizePx, aspect, 0.0f, 0.0f);

    return true;
}

bool StarfieldRenderer::updateRouteBuffer(const overlay::OverlayState* state)
{
    if (!state || state->route.empty() || !device_)
    {
        routeVertexCount_ = 0;
        return true;
    }

    std::vector<DirectX::XMFLOAT3> positions;
    std::vector<float> activeMask;
    positions.reserve(state->route.size());
    activeMask.reserve(state->route.size());

    const std::string activeId = state->active_route_node_id.value_or(std::string{});

    for (const auto& node : state->route)
    {
        const std::uint32_t systemId = parseSystemId(node.system_id);
        if (systemId == 0)
        {
            continue;
        }

        const auto* record = catalog_->find_by_system_id(systemId);
        if (!record)
        {
            continue;
        }

        positions.emplace_back(record->position.x, record->position.y, record->position.z);
        activeMask.push_back(!activeId.empty() && activeId == node.system_id ? 1.0f : 0.0f);
    }

    if (positions.size() < 2)
    {
        if (positions.empty())
        {
            routeVertexCount_ = 0;
            return true;
        }

        if (!ensureRouteCapacity(device_.Get(), static_cast<UINT>(positions.size())))
        {
            return false;
        }

        for (std::size_t i = 0; i < positions.size(); ++i)
        {
            mappedRouteVertices_[i] = RouteVertex{positions[i], 0.0f, activeMask[i], 0.0f};
        }
        routeVertexCount_ = static_cast<UINT>(positions.size());
        return true;
    }

    if (!ensureRouteCapacity(device_.Get(), static_cast<UINT>(positions.size())))
    {
        return false;
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

    return true;
}
