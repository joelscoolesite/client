#include "render.h"

#include "config.h"
#include "ui.h"

#include <windows.h>
#include <d3d11.h>
#include <d3d12.h>
#include <dxgi1_4.h>

#include <MinHook.h>
#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_dx12.h>
#include <imgui_impl_win32.h>

#include <vector>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace {

using PresentFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);
using ResizeBuffersFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);
using ExecuteCommandListsFn = void(STDMETHODCALLTYPE*)(ID3D12CommandQueue*, UINT, ID3D12CommandList* const*);

PresentFn g_originalPresent = nullptr;
ResizeBuffersFn g_originalResizeBuffers = nullptr;
ExecuteCommandListsFn g_originalExecuteCommandLists = nullptr;

template <typename T>
void SafeRelease(T*& p) {
    if (p) {
        p->Release();
        p = nullptr;
    }
}

enum class Api { None, DX11, DX12 };

Api g_api = Api::None;
bool g_imguiReady = false;
HWND g_hwnd = nullptr;
WNDPROC g_originalWndProc = nullptr;
IDXGISwapChain* g_swapChain = nullptr; // not owned

// DX11
ID3D11Device* g_d3d11Device = nullptr;
ID3D11DeviceContext* g_d3d11Context = nullptr;
ID3D11RenderTargetView* g_d3d11Rtv = nullptr;

// DX12
constexpr UINT kFramesInFlight = 3;
struct FrameContext {
    ID3D12CommandAllocator* allocator = nullptr;
    UINT64 fenceValue = 0;
};
ID3D12Device* g_d3d12Device = nullptr;
ID3D12CommandQueue* g_commandQueue = nullptr; // captured from the game
ID3D12DescriptorHeap* g_rtvHeap = nullptr;
ID3D12DescriptorHeap* g_srvHeap = nullptr;
ID3D12GraphicsCommandList* g_commandList = nullptr;
ID3D12Fence* g_fence = nullptr;
HANDLE g_fenceEvent = nullptr;
UINT64 g_fenceValue = 0;
FrameContext g_frames[kFramesInFlight];
UINT64 g_frameCounter = 0;
std::vector<ID3D12Resource*> g_backBuffers;
std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> g_backBufferRtvs;

// ---------------------------------------------------------------- window input

LRESULT CALLBACK WndProcDetour(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (g_imguiReady && ui::MenuOpen()) {
        ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam);

        // Keep the game from reacting to clicks and typing while the menu is open.
        const bool keyboard = msg >= WM_KEYFIRST && msg <= WM_KEYLAST;
        const bool mouse = msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST;
        if ((keyboard && wParam != static_cast<WPARAM>(g_config.menuKey)) || mouse) return 0;
        if (msg == WM_INPUT) return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    return CallWindowProcW(g_originalWndProc, hwnd, msg, wParam, lParam);
}

struct FindWindowData {
    DWORD pid;
    HWND best;
    LONG bestArea;
};

BOOL CALLBACK FindGameWindow(HWND hwnd, LPARAM param) {
    auto* data = reinterpret_cast<FindWindowData*>(param);
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid != data->pid || !IsWindowVisible(hwnd)) return TRUE;
    RECT r;
    GetClientRect(hwnd, &r);
    const LONG area = (r.right - r.left) * (r.bottom - r.top);
    if (area > data->bestArea) {
        data->best = hwnd;
        data->bestArea = area;
    }
    return TRUE;
}

HWND WindowFor(IDXGISwapChain* swapChain) {
    DXGI_SWAP_CHAIN_DESC desc{};
    if (SUCCEEDED(swapChain->GetDesc(&desc)) && desc.OutputWindow) return desc.OutputWindow;

    IDXGISwapChain1* swapChain1 = nullptr;
    HWND hwnd = nullptr;
    if (SUCCEEDED(swapChain->QueryInterface(IID_PPV_ARGS(&swapChain1)))) {
        swapChain1->GetHwnd(&hwnd);
        swapChain1->Release();
    }
    if (hwnd) return hwnd;

    FindWindowData data{GetCurrentProcessId(), nullptr, 0};
    EnumWindows(FindGameWindow, reinterpret_cast<LPARAM>(&data));
    return data.best;
}

// ---------------------------------------------------------------- back buffers

void ReleaseBackBuffers() {
    SafeRelease(g_d3d11Rtv);
    for (auto*& b : g_backBuffers) SafeRelease(b);
    g_backBuffers.clear();
    g_backBufferRtvs.clear();
}

void WaitForGpu() {
    if (!g_commandQueue || !g_fence) return;
    const UINT64 value = ++g_fenceValue;
    if (FAILED(g_commandQueue->Signal(g_fence, value))) return;
    if (g_fence->GetCompletedValue() < value) {
        g_fence->SetEventOnCompletion(value, g_fenceEvent);
        WaitForSingleObject(g_fenceEvent, 2000);
    }
}

bool CreateBackBuffers(IDXGISwapChain* swapChain) {
    DXGI_SWAP_CHAIN_DESC desc{};
    if (FAILED(swapChain->GetDesc(&desc))) return false;

    if (g_api == Api::DX11) {
        ID3D11Texture2D* backBuffer = nullptr;
        if (FAILED(swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer)))) return false;
        const HRESULT hr = g_d3d11Device->CreateRenderTargetView(backBuffer, nullptr, &g_d3d11Rtv);
        backBuffer->Release();
        return SUCCEEDED(hr);
    }

    // DX12: one RTV per swap chain buffer. The heap is sized for the max DXGI buffer count.
    const UINT rtvSize = g_d3d12Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    D3D12_CPU_DESCRIPTOR_HANDLE handle = g_rtvHeap->GetCPUDescriptorHandleForHeapStart();
    for (UINT i = 0; i < desc.BufferCount; ++i) {
        ID3D12Resource* buffer = nullptr;
        if (FAILED(swapChain->GetBuffer(i, IID_PPV_ARGS(&buffer)))) {
            ReleaseBackBuffers();
            return false;
        }
        g_d3d12Device->CreateRenderTargetView(buffer, nullptr, handle);
        g_backBuffers.push_back(buffer);
        g_backBufferRtvs.push_back(handle);
        handle.ptr += rtvSize;
    }
    return true;
}

// ---------------------------------------------------------------- setup

bool InitDx12Objects(IDXGISwapChain* swapChain) {
    DXGI_SWAP_CHAIN_DESC desc{};
    swapChain->GetDesc(&desc);

    D3D12_DESCRIPTOR_HEAP_DESC rtvDesc{};
    rtvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvDesc.NumDescriptors = DXGI_MAX_SWAP_CHAIN_BUFFERS;
    if (FAILED(g_d3d12Device->CreateDescriptorHeap(&rtvDesc, IID_PPV_ARGS(&g_rtvHeap)))) return false;

    D3D12_DESCRIPTOR_HEAP_DESC srvDesc{};
    srvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvDesc.NumDescriptors = 1;
    srvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FAILED(g_d3d12Device->CreateDescriptorHeap(&srvDesc, IID_PPV_ARGS(&g_srvHeap)))) return false;

    for (auto& frame : g_frames) {
        if (FAILED(g_d3d12Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                         IID_PPV_ARGS(&frame.allocator))))
            return false;
    }
    if (FAILED(g_d3d12Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, g_frames[0].allocator, nullptr,
                                                IID_PPV_ARGS(&g_commandList))))
        return false;
    g_commandList->Close();

    if (FAILED(g_d3d12Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&g_fence)))) return false;
    g_fenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!g_fenceEvent) return false;

    return ImGui_ImplDX12_Init(g_d3d12Device, kFramesInFlight, desc.BufferDesc.Format, g_srvHeap,
                               g_srvHeap->GetCPUDescriptorHandleForHeapStart(),
                               g_srvHeap->GetGPUDescriptorHandleForHeapStart());
}

bool g_initFailed = false;

bool InitForSwapChain(IDXGISwapChain* swapChain) {
    g_hwnd = WindowFor(swapChain);
    if (!g_hwnd) return false;

    if (SUCCEEDED(swapChain->GetDevice(IID_PPV_ARGS(&g_d3d12Device)))) {
        // DX12 needs the game's command queue, which we grab in ExecuteCommandLists.
        if (!g_commandQueue) {
            SafeRelease(g_d3d12Device);
            return false;
        }
        g_api = Api::DX12;
    } else if (SUCCEEDED(swapChain->GetDevice(IID_PPV_ARGS(&g_d3d11Device)))) {
        g_d3d11Device->GetImmediateContext(&g_d3d11Context);
        g_api = Api::DX11;
    } else {
        return false;
    }

    // From here on a failure is permanent, so we never create a second ImGui context.
    g_initFailed = true;

    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
    if (!ImGui_ImplWin32_Init(g_hwnd)) {
        Log("ImGui_ImplWin32_Init failed");
        return false;
    }

    const bool ok = g_api == Api::DX11 ? ImGui_ImplDX11_Init(g_d3d11Device, g_d3d11Context)
                                       : InitDx12Objects(swapChain);
    if (!ok) {
        Log("Failed to initialise ImGui renderer");
        return false;
    }

    ui::Setup();
    g_swapChain = swapChain;
    g_originalWndProc = reinterpret_cast<WNDPROC>(
        SetWindowLongPtrW(g_hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&WndProcDetour)));
    g_initFailed = false;
    g_imguiReady = true;
    Log(g_api == Api::DX12 ? "Overlay ready (DirectX 12)" : "Overlay ready (DirectX 11)");
    return true;
}

// ---------------------------------------------------------------- per frame

void RenderDx11() {
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    ui::Draw();
    ImGui::Render();
    g_d3d11Context->OMSetRenderTargets(1, &g_d3d11Rtv, nullptr);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
}

void RenderDx12(IDXGISwapChain* swapChain) {
    IDXGISwapChain3* swapChain3 = nullptr;
    if (FAILED(swapChain->QueryInterface(IID_PPV_ARGS(&swapChain3)))) return;
    const UINT backBufferIndex = swapChain3->GetCurrentBackBufferIndex();
    swapChain3->Release();
    if (backBufferIndex >= g_backBuffers.size()) return;

    FrameContext& frame = g_frames[g_frameCounter++ % kFramesInFlight];
    if (g_fence->GetCompletedValue() < frame.fenceValue) {
        g_fence->SetEventOnCompletion(frame.fenceValue, g_fenceEvent);
        WaitForSingleObject(g_fenceEvent, 2000);
    }

    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    ui::Draw();
    ImGui::Render();

    frame.allocator->Reset();
    g_commandList->Reset(frame.allocator, nullptr);

    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = g_backBuffers[backBufferIndex];
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    g_commandList->ResourceBarrier(1, &barrier);

    g_commandList->OMSetRenderTargets(1, &g_backBufferRtvs[backBufferIndex], FALSE, nullptr);
    g_commandList->SetDescriptorHeaps(1, &g_srvHeap);
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), g_commandList);

    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
    g_commandList->ResourceBarrier(1, &barrier);
    g_commandList->Close();

    ID3D12CommandList* lists[] = {g_commandList};
    g_originalExecuteCommandLists(g_commandQueue, 1, lists);
    frame.fenceValue = ++g_fenceValue;
    g_commandQueue->Signal(g_fence, frame.fenceValue);
}

// ---------------------------------------------------------------- hooks

HRESULT STDMETHODCALLTYPE PresentDetour(IDXGISwapChain* swapChain, UINT syncInterval, UINT flags) {
    if (!(flags & DXGI_PRESENT_TEST)) {
        if (!g_imguiReady && !g_initFailed) InitForSwapChain(swapChain);

        if (g_imguiReady && swapChain == g_swapChain) {
            const bool haveTarget = g_api == Api::DX11 ? g_d3d11Rtv != nullptr : !g_backBuffers.empty();
            if (haveTarget || CreateBackBuffers(swapChain)) {
                if (g_api == Api::DX11) RenderDx11();
                else RenderDx12(swapChain);
            }
        }
    }
    return g_originalPresent(swapChain, syncInterval, flags);
}

HRESULT STDMETHODCALLTYPE ResizeBuffersDetour(IDXGISwapChain* swapChain, UINT bufferCount, UINT width, UINT height,
                                              DXGI_FORMAT format, UINT flags) {
    if (g_imguiReady && swapChain == g_swapChain) {
        // The swap chain can only resize once nobody holds its buffers.
        if (g_api == Api::DX12) WaitForGpu();
        ReleaseBackBuffers();
    }
    return g_originalResizeBuffers(swapChain, bufferCount, width, height, format, flags);
}

void STDMETHODCALLTYPE ExecuteCommandListsDetour(ID3D12CommandQueue* queue, UINT count,
                                                 ID3D12CommandList* const* lists) {
    if (!g_commandQueue && queue->GetDesc().Type == D3D12_COMMAND_LIST_TYPE_DIRECT) {
        queue->AddRef();
        g_commandQueue = queue;
    }
    g_originalExecuteCommandLists(queue, count, lists);
}

// ---------------------------------------------------------------- vtable lookup

struct VTables {
    void* present = nullptr;
    void* resizeBuffers = nullptr;
    void* executeCommandLists = nullptr;
};

// Creates throwaway D3D objects only to read the function addresses from their vtables.
bool GetVTables(VTables& out) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"BedrockWaypointsDummy";
    RegisterClassExW(&wc);
    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"", WS_OVERLAPPEDWINDOW, 0, 0, 100, 100, nullptr, nullptr,
                                wc.hInstance, nullptr);
    if (!hwnd) return false;

    bool ok = false;

    // DX12 first: gives us the swap chain vtable and the command queue vtable.
    IDXGIFactory4* factory = nullptr;
    ID3D12Device* device = nullptr;
    ID3D12CommandQueue* queue = nullptr;
    IDXGISwapChain1* swapChain = nullptr;
    if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))) &&
        SUCCEEDED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)))) {
        D3D12_COMMAND_QUEUE_DESC queueDesc{};
        queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        if (SUCCEEDED(device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&queue)))) {
            DXGI_SWAP_CHAIN_DESC1 scDesc{};
            scDesc.Width = 100;
            scDesc.Height = 100;
            scDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            scDesc.SampleDesc.Count = 1;
            scDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
            scDesc.BufferCount = 2;
            scDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
            if (SUCCEEDED(factory->CreateSwapChainForHwnd(queue, hwnd, &scDesc, nullptr, nullptr, &swapChain))) {
                void** scVtable = *reinterpret_cast<void***>(swapChain);
                void** queueVtable = *reinterpret_cast<void***>(queue);
                out.present = scVtable[8];
                out.resizeBuffers = scVtable[13];
                out.executeCommandLists = queueVtable[10];
                ok = true;
            }
        }
    }
    SafeRelease(swapChain);
    SafeRelease(queue);
    SafeRelease(device);
    SafeRelease(factory);

    if (!ok) {
        // No DX12 on this PC: the game must be using DX11.
        DXGI_SWAP_CHAIN_DESC scDesc{};
        scDesc.BufferCount = 1;
        scDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        scDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        scDesc.OutputWindow = hwnd;
        scDesc.SampleDesc.Count = 1;
        scDesc.Windowed = TRUE;
        IDXGISwapChain* sc = nullptr;
        ID3D11Device* dev = nullptr;
        ID3D11DeviceContext* ctx = nullptr;
        if (SUCCEEDED(D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
                                                    D3D11_SDK_VERSION, &scDesc, &sc, &dev, nullptr, &ctx))) {
            void** scVtable = *reinterpret_cast<void***>(sc);
            out.present = scVtable[8];
            out.resizeBuffers = scVtable[13];
            ok = true;
        }
        SafeRelease(sc);
        SafeRelease(ctx);
        SafeRelease(dev);
    }

    DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
    return ok;
}

bool Hook(void* target, void* detour, void** original) {
    return MH_CreateHook(target, detour, original) == MH_OK && MH_EnableHook(target) == MH_OK;
}

} // namespace

namespace render {

bool Init(std::string& error) {
    VTables vt;
    if (!GetVTables(vt)) {
        error = "Could not create a dummy DirectX swap chain.";
        return false;
    }
    if (vt.executeCommandLists &&
        !Hook(vt.executeCommandLists, reinterpret_cast<void*>(&ExecuteCommandListsDetour),
              reinterpret_cast<void**>(&g_originalExecuteCommandLists))) {
        error = "Failed to hook ExecuteCommandLists.";
        return false;
    }
    if (!Hook(vt.resizeBuffers, reinterpret_cast<void*>(&ResizeBuffersDetour),
              reinterpret_cast<void**>(&g_originalResizeBuffers)) ||
        !Hook(vt.present, reinterpret_cast<void*>(&PresentDetour), reinterpret_cast<void**>(&g_originalPresent))) {
        error = "Failed to hook Present.";
        return false;
    }
    return true;
}

} // namespace render
