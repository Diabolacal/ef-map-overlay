#pragma once

#include "overlay_schema.hpp"
#include "star_catalog.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

#include <DirectXMath.h>
#include <d3d12.h>
#include <wrl/client.h>

class StarfieldRenderer
{
public:
    static StarfieldRenderer& instance();

    bool initialize(ID3D12Device* device, DXGI_FORMAT targetFormat);
    void shutdown();

    bool isReady() const noexcept { return ready_; }
    bool hasCatalog() const noexcept { return catalog_.has_value(); }

    void render(ID3D12GraphicsCommandList* commandList, UINT width, UINT height, const overlay::OverlayState* state);

private:
    struct StarVertex
    {
        DirectX::XMFLOAT3 position;
        float brightness;
        float security;
    };

    struct RouteVertex
    {
        DirectX::XMFLOAT3 position;
        float progress;
        float state;
        float pad;
    };

    struct FrameConstants
    {
        DirectX::XMFLOAT4X4 viewProj;
        DirectX::XMFLOAT4 cameraPosition;
        DirectX::XMFLOAT4 clusterCenterRadius; // xyz center, w radius
        DirectX::XMFLOAT4 params;              // x: star base size, y: aspect ratio, z/w reserved
    };

    StarfieldRenderer() = default;
    ~StarfieldRenderer() = default;

    StarfieldRenderer(const StarfieldRenderer&) = delete;
    StarfieldRenderer& operator=(const StarfieldRenderer&) = delete;

    bool ensureCatalogLoaded();
    std::filesystem::path resolveCatalogPath() const;
    bool createPipeline(ID3D12Device* device, DXGI_FORMAT targetFormat);
    bool createVertexBuffer(ID3D12Device* device);
    bool createConstantBuffer(ID3D12Device* device);
    bool ensureRouteCapacity(ID3D12Device* device, UINT vertexCount);
    bool updateConstants(const overlay::OverlayState* state, UINT width, UINT height);
    bool updateRouteBuffer(const overlay::OverlayState* state);

    std::optional<overlay::StarCatalog> catalog_;
    std::filesystem::path catalogPath_;

    Microsoft::WRL::ComPtr<ID3D12Device> device_;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> starfieldPipeline_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> routePipeline_;
    Microsoft::WRL::ComPtr<ID3D12Resource> starVertexBuffer_;
    Microsoft::WRL::ComPtr<ID3D12Resource> routeVertexBuffer_;
    Microsoft::WRL::ComPtr<ID3D12Resource> constantBuffer_;

    D3D12_VERTEX_BUFFER_VIEW starVertexView_{};
    D3D12_VERTEX_BUFFER_VIEW routeVertexView_{};

    UINT starVertexCount_{0};
    UINT routeVertexCapacity_{0};
    UINT routeVertexCount_{0};

    FrameConstants* mappedConstants_{nullptr};
    RouteVertex* mappedRouteVertices_{nullptr};

    DirectX::XMFLOAT3 boundsCenter_{0.0f, 0.0f, 0.0f};
    float boundsRadius_{1.0f};

    std::uint64_t lastRouteTimestamp_{0};
    std::uint32_t lastRouteCount_{0};
    std::string lastActiveNodeId_;

    bool ready_{false};
};
