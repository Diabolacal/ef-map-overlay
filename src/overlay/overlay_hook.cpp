#include "overlay_hook.hpp"

#include "overlay_renderer.hpp"

#include <algorithm>
#include <atomic>
#include <string>
#include <vector>

#include <MinHook.h>

#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>

#include <imgui.h>
#include <backends/imgui_impl_dx12.h>
#include <backends/imgui_impl_win32.h>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

#include <spdlog/spdlog.h>
#include <spdlog/fmt/bundled/format.h>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")

namespace
{
    using Microsoft::WRL::ComPtr;

    void log_info(const std::string& message)
    {
        const auto line = fmt::format("[ef-overlay] {}", message);
        ::OutputDebugStringA(line.c_str());
        spdlog::info("{}", message);
    }

    void log_error(const std::string& message)
    {
        const auto line = fmt::format("[ef-overlay] ERROR {}", message);
        ::OutputDebugStringA(line.c_str());
        spdlog::error("{}", message);
    }

    struct FrameContext
    {
        ComPtr<ID3D12CommandAllocator> allocator;
        ComPtr<ID3D12Resource> renderTarget;
        D3D12_CPU_DESCRIPTOR_HANDLE descriptor{};
        UINT64 fenceValue = 0;
    };

    using PresentFn = HRESULT(__stdcall*)(IDXGISwapChain3*, UINT, UINT);
    using ResizeBuffersFn = HRESULT(__stdcall*)(IDXGISwapChain3*, UINT, UINT, UINT, DXGI_FORMAT, UINT);
    using ExecuteCommandListsFn = void(__stdcall*)(ID3D12CommandQueue*, UINT, ID3D12CommandList* const*);

    PresentFn originalPresent = nullptr;
    ResizeBuffersFn originalResizeBuffers = nullptr;
    ExecuteCommandListsFn originalExecuteCommandLists = nullptr;

    ComPtr<ID3D12Device> g_device;
    ComPtr<ID3D12DescriptorHeap> g_srvHeap;
    ComPtr<ID3D12DescriptorHeap> g_rtvHeap;
    ComPtr<ID3D12GraphicsCommandList> g_commandList;
    ComPtr<ID3D12CommandQueue> g_commandQueue;
    ComPtr<ID3D12Fence> g_fence;
    HANDLE g_fenceEvent = nullptr;
    UINT64 g_fenceValue = 0;
    std::vector<FrameContext> g_frames;
    UINT g_bufferCount = 0;
    UINT g_rtvDescriptorSize = 0;
    HWND g_hwnd = nullptr;
    WNDPROC g_originalWndProc = nullptr;
    std::atomic_bool g_wndProcHooked{false};
    bool g_imguiReady = false;
    std::atomic_bool g_hooksEnabled{false};
    std::atomic_bool g_loggedFirstPresent{false};
    std::atomic_bool g_loggedQueueCapture{false};
    std::atomic_bool g_usingFallbackQueue{false};

    constexpr wchar_t kDummyWindowClass[] = L"EFOverlayDummyClass";

    LRESULT CALLBACK overlayWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    HRESULT __stdcall hookPresent(IDXGISwapChain3* swapChain, UINT syncInterval, UINT flags);
    HRESULT __stdcall hookResizeBuffers(IDXGISwapChain3* swapChain, UINT bufferCount, UINT width, UINT height, DXGI_FORMAT format, UINT flags);
    void __stdcall hookExecuteCommandLists(ID3D12CommandQueue* queue, UINT count, ID3D12CommandList* const* lists);

    void cleanupRenderTargets()
    {
        for (auto& frame : g_frames)
        {
            frame.renderTarget.Reset();
            if (frame.allocator)
            {
                frame.allocator->Reset();
                frame.allocator.Reset();
            }
            frame.fenceValue = 0;
        }
    }

    void waitForGpu();
    void releaseFence();

    void restoreWindowProc()
    {
        if (g_wndProcHooked.load() && g_hwnd && g_originalWndProc)
        {
            ::SetWindowLongPtrW(g_hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(g_originalWndProc));
            g_wndProcHooked.store(false);
            g_originalWndProc = nullptr;
        }
    }

    void destroyDeviceObjects()
    {
        waitForGpu();

        if (g_imguiReady)
        {
            ImGui_ImplDX12_Shutdown();
            ImGui_ImplWin32_Shutdown();
            ImGui::DestroyContext();
            g_imguiReady = false;
        }

    cleanupRenderTargets();
    restoreWindowProc();
        g_frames.clear();
        g_commandList.Reset();
        g_srvHeap.Reset();
        g_rtvHeap.Reset();
        g_commandQueue.Reset();
        g_usingFallbackQueue.store(false);
        g_device.Reset();
        releaseFence();
    }

    void createRenderTargets(IDXGISwapChain3* swapChain)
    {
        const UINT bufferCount = g_bufferCount;
        D3D12_CPU_DESCRIPTOR_HANDLE handle = g_rtvHeap->GetCPUDescriptorHandleForHeapStart();
        const UINT stride = g_rtvDescriptorSize;

        for (UINT i = 0; i < bufferCount; ++i)
        {
            FrameContext& frame = g_frames[i];
            frame.renderTarget.Reset();
            HRESULT hr = swapChain->GetBuffer(i, IID_PPV_ARGS(&frame.renderTarget));
            if (FAILED(hr))
            {
                spdlog::error("Failed to acquire swap chain buffer {} (hr=0x{:08X})", i, static_cast<unsigned>(hr));
                continue;
            }

            frame.fenceValue = 0;
            frame.descriptor = handle;
            g_device->CreateRenderTargetView(frame.renderTarget.Get(), nullptr, handle);
            handle.ptr += stride;
        }
    }

    struct DummyContext
    {
        HWND hwnd{nullptr};
        ComPtr<ID3D12Device> device;
        ComPtr<ID3D12CommandQueue> queue;
        ComPtr<IDXGISwapChain3> swapChain;
    };

    LRESULT CALLBACK dummyWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }

    bool createDummyDevice(DummyContext& ctx)
    {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(WNDCLASSEXW);
        wc.lpfnWndProc = dummyWndProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = kDummyWindowClass;
        static ATOM atom = 0;
        if (atom == 0)
        {
            atom = RegisterClassExW(&wc);
            if (atom == 0)
            {
                const DWORD regErr = GetLastError();
                if (regErr == ERROR_CLASS_ALREADY_EXISTS)
                {
                    atom = 1;
                }
                else
                {
                    spdlog::error("Failed to register dummy window class (err={})", regErr);
                    return false;
                }
            }
        }

        ctx.hwnd = CreateWindowExW(0, kDummyWindowClass, L"", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 100, 100, nullptr, nullptr, wc.hInstance, nullptr);
        if (!ctx.hwnd)
        {
            spdlog::error("Failed to create dummy window (err={})", GetLastError());
            return false;
        }

        HRESULT hr = D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&ctx.device));
        if (FAILED(hr))
        {
            spdlog::error("Failed to create dummy D3D12 device (hr=0x{:08X})", static_cast<unsigned>(hr));
            return false;
        }

        D3D12_COMMAND_QUEUE_DESC queueDesc{};
        queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        queueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
        queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
        queueDesc.NodeMask = 0;
        hr = ctx.device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&ctx.queue));
        if (FAILED(hr))
        {
            spdlog::error("Failed to create dummy command queue (hr=0x{:08X})", static_cast<unsigned>(hr));
            return false;
        }

        ComPtr<IDXGIFactory4> factory;
        hr = CreateDXGIFactory1(IID_PPV_ARGS(&factory));
        if (FAILED(hr))
        {
            spdlog::error("CreateDXGIFactory1 failed (hr=0x{:08X})", static_cast<unsigned>(hr));
            return false;
        }

        DXGI_SWAP_CHAIN_DESC1 scDesc{};
        scDesc.BufferCount = 2;
        scDesc.Width = 0;
        scDesc.Height = 0;
        scDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        scDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        scDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        scDesc.SampleDesc.Count = 1;

        ComPtr<IDXGISwapChain1> swapChain1;
        hr = factory->CreateSwapChainForHwnd(ctx.queue.Get(), ctx.hwnd, &scDesc, nullptr, nullptr, &swapChain1);
        if (FAILED(hr))
        {
            spdlog::error("Failed to create dummy swap chain (hr=0x{:08X})", static_cast<unsigned>(hr));
            return false;
        }

        hr = swapChain1.As(&ctx.swapChain);
        if (FAILED(hr))
        {
            spdlog::error("Failed to query IDXGISwapChain3 for dummy swap chain (hr=0x{:08X})", static_cast<unsigned>(hr));
            return false;
        }

        return true;
    }

    void destroyDummyDevice(DummyContext& ctx)
    {
        if (ctx.hwnd)
        {
            DestroyWindow(ctx.hwnd);
            ctx.hwnd = nullptr;
        }
        ctx.swapChain.Reset();
        ctx.queue.Reset();
        ctx.device.Reset();
    }

    void ensureFenceObjects()
    {
        if (!g_device)
        {
            return;
        }

        if (!g_fence)
        {
            const HRESULT hr = g_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&g_fence));
            if (FAILED(hr))
            {
                spdlog::warn("Failed to create GPU fence (hr=0x{:08X})", static_cast<unsigned>(hr));
                return;
            }
            g_fenceValue = 0;
        }

        if (!g_fenceEvent)
        {
            g_fenceEvent = ::CreateEventW(nullptr, FALSE, FALSE, nullptr);
            if (!g_fenceEvent)
            {
                spdlog::warn("Failed to create fence event (err={})", ::GetLastError());
            }
        }
    }

    void waitForFrame(FrameContext& frame)
    {
        if (!g_fence || frame.fenceValue == 0)
        {
            return;
        }

        if (g_fence->GetCompletedValue() >= frame.fenceValue)
        {
            frame.fenceValue = 0;
            return;
        }

        if (!g_fenceEvent)
        {
            return;
        }

        const HRESULT hr = g_fence->SetEventOnCompletion(frame.fenceValue, g_fenceEvent);
        if (SUCCEEDED(hr))
        {
            ::WaitForSingleObject(g_fenceEvent, 5000);
        }

        frame.fenceValue = 0;
    }

    bool createHooks()
    {
        log_info("createHooks: starting dummy device creation");

        DummyContext dummy;
        if (!createDummyDevice(dummy))
        {
            log_error("createHooks: dummy device creation failed");
            destroyDummyDevice(dummy);
            return false;
        }

        log_info("createHooks: dummy device ready");

        void** swapChainVTable = *reinterpret_cast<void***>(dummy.swapChain.Get());
        void** commandQueueVTable = *reinterpret_cast<void***>(dummy.queue.Get());

        auto status = MH_Initialize();
        if (status != MH_OK && status != MH_ERROR_ALREADY_INITIALIZED)
        {
            log_error(fmt::format("MinHook initialization failed (status={})", static_cast<int>(status)));
            destroyDummyDevice(dummy);
            return false;
        }

        if (MH_CreateHook(swapChainVTable[8], reinterpret_cast<void*>(&hookPresent), reinterpret_cast<void**>(&originalPresent)) != MH_OK)
        {
            log_error("Failed to create Present hook");
            destroyDummyDevice(dummy);
            return false;
        }

        log_info("createHooks: Present hook created");

        if (MH_CreateHook(swapChainVTable[13], reinterpret_cast<void*>(&hookResizeBuffers), reinterpret_cast<void**>(&originalResizeBuffers)) != MH_OK)
        {
            log_error("Failed to create ResizeBuffers hook");
            destroyDummyDevice(dummy);
            return false;
        }

        log_info("createHooks: ResizeBuffers hook created");

        const int executeIndex = 10;
        if (MH_CreateHook(commandQueueVTable[executeIndex], reinterpret_cast<void*>(&hookExecuteCommandLists), reinterpret_cast<void**>(&originalExecuteCommandLists)) != MH_OK)
        {
            log_error("Failed to create ExecuteCommandLists hook");
            destroyDummyDevice(dummy);
            return false;
        }

        log_info("createHooks: ExecuteCommandLists hook created");

        destroyDummyDevice(dummy);

        if (MH_EnableHook(MH_ALL_HOOKS) != MH_OK)
        {
            log_error("Failed to enable DirectX hooks");
            return false;
        }

        g_hooksEnabled.store(true);
        log_info("createHooks: hooks enabled");
        return true;
    }

    void setupFrameContexts(UINT bufferCount)
    {
        g_frames.clear();
        g_frames.resize(bufferCount);
        for (auto& frame : g_frames)
        {
            const HRESULT hr = g_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&frame.allocator));
            if (FAILED(hr))
            {
                spdlog::error("Failed to create command allocator (hr=0x{:08X})", static_cast<unsigned>(hr));
            }
        }

        if (!g_commandList)
        {
            const HRESULT hr = g_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, g_frames[0].allocator.Get(), nullptr, IID_PPV_ARGS(&g_commandList));
            if (SUCCEEDED(hr))
            {
                g_commandList->Close();
            }
            else
            {
                spdlog::error("Failed to create command list (hr=0x{:08X})", static_cast<unsigned>(hr));
            }
        }
    }

    bool initializeImgui(IDXGISwapChain3* swapChain)
    {
        if (g_imguiReady)
        {
            return true;
        }

        log_info("initializeImgui: begin");

        HRESULT hr = swapChain->GetDevice(IID_PPV_ARGS(&g_device));
        if (FAILED(hr))
        {
            log_error(fmt::format("Failed to query device from swap chain (hr=0x{:08X})", static_cast<unsigned>(hr)));
            return false;
        }

        log_info("initializeImgui: obtained device");

        if (!g_commandQueue)
        {
            log_info("initializeImgui: command queue not captured; creating dedicated queue");

            D3D12_COMMAND_QUEUE_DESC queueDesc{};
            queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
            queueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
            queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
            queueDesc.NodeMask = 0;

            ComPtr<ID3D12CommandQueue> overlayQueue;
            hr = g_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&overlayQueue));
            if (FAILED(hr))
            {
                log_error(fmt::format("Failed to create fallback command queue (hr=0x{:08X})", static_cast<unsigned>(hr)));
                return false;
            }

            g_commandQueue = overlayQueue;
            g_usingFallbackQueue.store(true);
            log_info(fmt::format("initializeImgui: fallback command queue created ({})", static_cast<void*>(g_commandQueue.Get())));
        }
        else
        {
            g_usingFallbackQueue.store(false);
            log_info(fmt::format("initializeImgui: using captured command queue ({})", static_cast<void*>(g_commandQueue.Get())));
        }

        DXGI_SWAP_CHAIN_DESC desc{};
        swapChain->GetDesc(&desc);
        g_hwnd = desc.OutputWindow;
        g_bufferCount = desc.BufferCount;

        log_info(fmt::format("initializeImgui: bufferCount={} format={} hwnd=0x{:X}", g_bufferCount, static_cast<int>(desc.BufferDesc.Format), reinterpret_cast<std::uintptr_t>(g_hwnd)));

        D3D12_DESCRIPTOR_HEAP_DESC srvDesc{};
        srvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        srvDesc.NumDescriptors = 1;
        srvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        srvDesc.NodeMask = 0;
        hr = g_device->CreateDescriptorHeap(&srvDesc, IID_PPV_ARGS(&g_srvHeap));
        if (FAILED(hr))
        {
            log_error(fmt::format("Failed to create SRV heap (hr=0x{:08X})", static_cast<unsigned>(hr)));
            return false;
        }

        log_info("initializeImgui: created SRV heap");

        D3D12_DESCRIPTOR_HEAP_DESC rtvDesc{};
        rtvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        rtvDesc.NumDescriptors = g_bufferCount;
        rtvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        rtvDesc.NodeMask = 0;
        hr = g_device->CreateDescriptorHeap(&rtvDesc, IID_PPV_ARGS(&g_rtvHeap));
        if (FAILED(hr))
        {
            log_error(fmt::format("Failed to create RTV heap (hr=0x{:08X})", static_cast<unsigned>(hr)));
            return false;
        }

        log_info("initializeImgui: created RTV heap");

        g_rtvDescriptorSize = g_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

        setupFrameContexts(g_bufferCount);
        createRenderTargets(swapChain);

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
        io.ConfigWindowsResizeFromEdges = true;
        ImGui::StyleColorsDark();

        if (!ImGui_ImplWin32_Init(g_hwnd))
        {
            log_error("ImGui Win32 backend initialization failed");
            return false;
        }

        if (!g_wndProcHooked.load())
        {
            g_originalWndProc = reinterpret_cast<WNDPROC>(
                ::SetWindowLongPtrW(g_hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&overlayWndProc)));
            if (g_originalWndProc)
            {
                g_wndProcHooked.store(true);
                log_info(fmt::format("initializeImgui: Win32 WndProc hooked (hwnd=0x{:X})", reinterpret_cast<std::uintptr_t>(g_hwnd)));
            }
            else
            {
                log_error("initializeImgui: failed to hook window procedure");
            }
        }

        log_info("initializeImgui: ImGui Win32 backend initialized");

        auto cpuHandle = g_srvHeap->GetCPUDescriptorHandleForHeapStart();
        auto gpuHandle = g_srvHeap->GetGPUDescriptorHandleForHeapStart();
        if (!ImGui_ImplDX12_Init(g_device.Get(), g_bufferCount, desc.BufferDesc.Format, g_srvHeap.Get(), cpuHandle, gpuHandle))
        {
            log_error("ImGui DX12 backend initialization failed");
            return false;
        }

        g_imguiReady = true;
        log_info("initializeImgui: ImGui DX12 backend initialized");
        return true;
    }

    void renderOverlay(IDXGISwapChain3* swapChain)
    {
        if (!g_imguiReady)
        {
            if (!initializeImgui(swapChain))
            {
                return;
            }
        }

        if (!swapChain)
        {
            log_error("renderOverlay: swapChain null");
            return;
        }

        UINT bufferIndex = swapChain->GetCurrentBackBufferIndex();
        if (bufferIndex >= g_frames.size())
        {
            log_error(fmt::format("renderOverlay: buffer index {} out of range (frame count={})", bufferIndex, g_frames.size()));
            return;
        }

        FrameContext& frame = g_frames[bufferIndex];
        if (!frame.renderTarget)
        {
            log_error(fmt::format("renderOverlay: missing render target for buffer {}", bufferIndex));
            createRenderTargets(swapChain);
            frame = g_frames[bufferIndex];
            if (!frame.renderTarget)
            {
                log_error(fmt::format("renderOverlay: still missing render target {} after recreate", bufferIndex));
                return;
            }
        }

        if (!frame.allocator)
        {
            log_error(fmt::format("renderOverlay: missing command allocator for buffer {}", bufferIndex));
            return;
        }

        if (!g_commandList)
        {
            log_error("renderOverlay: command list missing");
            return;
        }

        UINT width = 0;
        UINT height = 0;
        DXGI_SWAP_CHAIN_DESC desc{};
        if (SUCCEEDED(swapChain->GetDesc(&desc)))
        {
            width = desc.BufferDesc.Width;
            height = desc.BufferDesc.Height;
        }

        ImGui_ImplDX12_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        OverlayRenderer::instance().renderImGui();

        ImGui::Render();

        ImDrawData* drawData = ImGui::GetDrawData();
        if (!drawData || drawData->CmdListsCount == 0 || drawData->TotalVtxCount == 0)
        {
            return;
        }

        ensureFenceObjects();
        waitForFrame(frame);

        frame.allocator->Reset();
        g_commandList->Reset(frame.allocator.Get(), nullptr);

        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = frame.renderTarget.Get();
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        g_commandList->ResourceBarrier(1, &barrier);

        g_commandList->OMSetRenderTargets(1, &frame.descriptor, FALSE, nullptr);

    ID3D12DescriptorHeap* heaps[] = { g_srvHeap.Get() };
    g_commandList->SetDescriptorHeaps(1, heaps);
        ImGui_ImplDX12_RenderDrawData(drawData, g_commandList.Get());

        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
        g_commandList->ResourceBarrier(1, &barrier);

        g_commandList->Close();

        ID3D12CommandList* const commandLists[] = { g_commandList.Get() };
        g_commandQueue->ExecuteCommandLists(1, commandLists);

        ensureFenceObjects();
        if (g_fence)
        {
            const UINT64 signalValue = ++g_fenceValue;
            const HRESULT signalHr = g_commandQueue->Signal(g_fence.Get(), signalValue);
            if (SUCCEEDED(signalHr))
            {
                frame.fenceValue = signalValue;
            }
            else
            {
                spdlog::warn("renderOverlay: failed to signal fence (hr=0x{:08X})", static_cast<unsigned>(signalHr));
                frame.fenceValue = 0;
            }
        }

        static std::atomic_bool loggedFirstSubmission{false};
        if (!loggedFirstSubmission.exchange(true))
        {
            log_info(fmt::format("renderOverlay: first command list submitted (buffer={}, size={}x{})", bufferIndex, width, height));
        }
    }

    void waitForGpu()
    {
        if (!g_commandQueue || !g_device)
        {
            return;
        }

        ensureFenceObjects();
        if (!g_fence)
        {
            return;
        }

        ++g_fenceValue;
        const HRESULT signalHr = g_commandQueue->Signal(g_fence.Get(), g_fenceValue);
        if (FAILED(signalHr))
        {
            spdlog::warn("Failed to signal GPU fence (hr=0x{:08X})", static_cast<unsigned>(signalHr));
            return;
        }

        if (!g_fenceEvent)
        {
            return;
        }

        if (g_fence->GetCompletedValue() < g_fenceValue)
        {
            const HRESULT eventHr = g_fence->SetEventOnCompletion(g_fenceValue, g_fenceEvent);
            if (SUCCEEDED(eventHr))
            {
                ::WaitForSingleObject(g_fenceEvent, 2000);
            }
        }
    }

    void releaseFence()
    {
        g_fence.Reset();
        if (g_fenceEvent)
        {
            ::CloseHandle(g_fenceEvent);
            g_fenceEvent = nullptr;
        }
        g_fenceValue = 0;
    }

    HRESULT __stdcall hookPresent(IDXGISwapChain3* swapChain, UINT syncInterval, UINT flags)
    {
        if (!g_loggedFirstPresent.exchange(true))
        {
            log_info(fmt::format("hookPresent invoked (swapChain={}, syncInterval={}, flags={})", static_cast<void*>(swapChain), syncInterval, flags));
        }

        if (g_hooksEnabled.load() && OverlayRenderer::instance().isInitialized())
        {
            renderOverlay(swapChain);
        }

        return originalPresent(swapChain, syncInterval, flags);
    }

    HRESULT __stdcall hookResizeBuffers(IDXGISwapChain3* swapChain, UINT bufferCount, UINT width, UINT height, DXGI_FORMAT format, UINT flags)
    {
        if (g_imguiReady)
        {
            ImGui_ImplDX12_InvalidateDeviceObjects();
            cleanupRenderTargets();
        }

        HRESULT hr = originalResizeBuffers(swapChain, bufferCount, width, height, format, flags);

        if (SUCCEEDED(hr) && g_imguiReady)
        {
            g_bufferCount = bufferCount;
            setupFrameContexts(g_bufferCount);
            createRenderTargets(swapChain);
            ImGui_ImplDX12_CreateDeviceObjects();
        }

        return hr;
    }

    bool isMouseMessage(UINT msg)
    {
        return (msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST) || msg == WM_MOUSEWHEEL || msg == WM_MOUSEHWHEEL;
    }

    bool isKeyboardMessage(UINT msg)
    {
        switch (msg)
        {
            case WM_KEYDOWN:
            case WM_KEYUP:
            case WM_SYSKEYDOWN:
            case WM_SYSKEYUP:
            case WM_CHAR:
            case WM_SYSCHAR:
            case WM_UNICHAR:
                return true;
            default:
                return false;
        }
    }

    LRESULT CALLBACK overlayWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        if (OverlayRenderer::instance().isInitialized())
        {
            if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam))
            {
                return 1;
            }

            const ImGuiIO& io = ImGui::GetIO();
            if (io.WantCaptureMouse && isMouseMessage(msg))
            {
                return 0;
            }

            if (io.WantCaptureKeyboard && isKeyboardMessage(msg))
            {
                return 0;
            }
        }

        if (g_originalWndProc)
        {
            return CallWindowProcW(g_originalWndProc, hwnd, msg, wParam, lParam);
        }

        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    void __stdcall hookExecuteCommandLists(ID3D12CommandQueue* queue, UINT count, ID3D12CommandList* const* lists)
    {
        if (g_usingFallbackQueue.load() || !g_commandQueue || g_commandQueue.Get() != queue)
        {
            g_commandQueue = queue;
            g_usingFallbackQueue.store(false);
            if (!g_loggedQueueCapture.exchange(true))
            {
                log_info(fmt::format("Command queue captured (queue={}, listCount={})", static_cast<void*>(queue), count));
            }
            else
            {
                log_info(fmt::format("Command queue refreshed from game (queue={}, listCount={})", static_cast<void*>(queue), count));
            }
        }

        originalExecuteCommandLists(queue, count, lists);
    }
}

OverlayHook& OverlayHook::instance()
{
    static OverlayHook instance;
    return instance;
}

bool OverlayHook::initialize(HMODULE)
{
    if (initialized_)
    {
        return true;
    }

    log_info("OverlayHook::initialize starting");

    if (!createHooks())
    {
        log_error("Failed to install DX12 hooks");
        return false;
    }

    initialized_ = true;
    log_info("DX12 swap-chain hooks installed");
    return true;
}

void OverlayHook::shutdown()
{
    if (!initialized_)
    {
        return;
    }

    g_hooksEnabled.store(false);
    MH_DisableHook(MH_ALL_HOOKS);
    MH_Uninitialize();

    destroyDeviceObjects();

    initialized_ = false;
    spdlog::info("DX12 swap-chain hooks removed");
}
