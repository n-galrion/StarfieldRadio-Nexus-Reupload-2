// D3D12 Present hook using MinHook - based on proven StarfieldTogether approach
#include "pch.h"
#include "DXHook.h"
#include "RadioMenu.h"

#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx12.h"

#include "MinHook.h"

using Microsoft::WRL::ComPtr;

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
void ConsoleExecute(std::string command);

namespace DXHook {

static bool                    g_initialized = false;
static std::atomic<bool>       g_menuOpen{false};
static HWND                    g_hwnd = nullptr;
static WNDPROC                 g_origWndProc = nullptr;

// D3D12 resources
static ComPtr<ID3D12Device>              g_device;
static ComPtr<ID3D12DescriptorHeap>      g_srvHeap;
static ComPtr<ID3D12DescriptorHeap>      g_rtvHeap;
static ComPtr<ID3D12GraphicsCommandList> g_cmdList;
static ComPtr<ID3D12CommandAllocator>    g_cmdAllocator;
static ComPtr<ID3D12Fence>               g_fence;
static HANDLE                            g_fenceEvent = nullptr;
static UINT64                            g_fenceValue = 0;
static UINT                              g_bufferCount = 0;
static ID3D12CommandQueue*               g_commandQueue = nullptr;
static DXGI_FORMAT                       g_backBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

// Hook originals
typedef HRESULT(STDMETHODCALLTYPE* PFN_Present)(IDXGISwapChain3*, UINT, UINT);
typedef void(STDMETHODCALLTYPE* PFN_ExecuteCommandLists)(ID3D12CommandQueue*, UINT, ID3D12CommandList* const*);
static PFN_Present              g_origPresent = nullptr;
static PFN_ExecuteCommandLists  g_origExecuteCommandLists = nullptr;

bool IsInitialized() { return g_initialized; }
bool IsMenuOpen() { return g_menuOpen.load(); }
void SetMenuOpen(bool open) { g_menuOpen.store(open); }

// WndProc hook
static LRESULT CALLBACK WndProcHook(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (g_menuOpen.load()) {
        if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
            return 1;

        // Consume mouse/keyboard when menu is open
        if (msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST) return 1;
        if (msg >= WM_KEYFIRST && msg <= WM_KEYLAST) return 1;
        if (msg == WM_SETCURSOR) { SetCursor(LoadCursor(NULL, IDC_ARROW)); return 1; }
    }
    return CallWindowProcW(g_origWndProc, hWnd, msg, wParam, lParam);
}

static void WaitForGpu()
{
    if (!g_commandQueue || !g_fence || !g_fenceEvent) return;
    g_fenceValue++;
    g_commandQueue->Signal(g_fence.Get(), g_fenceValue);
    if (g_fence->GetCompletedValue() < g_fenceValue) {
        g_fence->SetEventOnCompletion(g_fenceValue, g_fenceEvent);
        WaitForSingleObject(g_fenceEvent, 100);
    }
}

static bool InitImGui(IDXGISwapChain3* swapChain)
{
    HRESULT hr = swapChain->GetDevice(IID_PPV_ARGS(&g_device));
    if (FAILED(hr)) { REX::INFO("Radio UI: Failed to get D3D12 device"); return false; }

    DXGI_SWAP_CHAIN_DESC desc = {};
    swapChain->GetDesc(&desc);
    g_bufferCount = desc.BufferCount;
    g_hwnd = desc.OutputWindow;
    g_backBufferFormat = desc.BufferDesc.Format;

    // SRV heap for ImGui fonts
    D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heapDesc.NumDescriptors = 1;
    heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    hr = g_device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&g_srvHeap));
    if (FAILED(hr)) return false;

    // RTV heap
    D3D12_DESCRIPTOR_HEAP_DESC rtvDesc = {};
    rtvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvDesc.NumDescriptors = g_bufferCount;
    hr = g_device->CreateDescriptorHeap(&rtvDesc, IID_PPV_ARGS(&g_rtvHeap));
    if (FAILED(hr)) return false;

    hr = g_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&g_cmdAllocator));
    if (FAILED(hr)) return false;

    hr = g_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, g_cmdAllocator.Get(), nullptr, IID_PPV_ARGS(&g_cmdList));
    if (FAILED(hr)) return false;
    g_cmdList->Close();

    hr = g_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&g_fence));
    if (FAILED(hr)) return false;
    g_fenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);

    // ImGui init
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;

    ImGui_ImplWin32_Init(g_hwnd);

    ImGui_ImplDX12_InitInfo initInfo = {};
    initInfo.Device = g_device.Get();
    initInfo.CommandQueue = g_commandQueue;
    initInfo.NumFramesInFlight = g_bufferCount;
    initInfo.RTVFormat = g_backBufferFormat;
    initInfo.SrvDescriptorHeap = g_srvHeap.Get();
    initInfo.LegacySingleSrvCpuDescriptor = g_srvHeap->GetCPUDescriptorHandleForHeapStart();
    initInfo.LegacySingleSrvGpuDescriptor = g_srvHeap->GetGPUDescriptorHandleForHeapStart();
    ImGui_ImplDX12_Init(&initInfo);

    // WndProc hook for input
    g_origWndProc = (WNDPROC)SetWindowLongPtrW(g_hwnd, GWLP_WNDPROC, (LONG_PTR)WndProcHook);

    // Apply theme
    RadioMenu::ApplyTheme();

    g_initialized = true;
    REX::INFO("Radio UI: ImGui D3D12 initialized");
    return true;
}

static void RenderOverlay(IDXGISwapChain3* swapChain)
{
    WaitForGpu();

    g_cmdAllocator->Reset();
    g_cmdList->Reset(g_cmdAllocator.Get(), nullptr);

    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    // Handle pause state change
    static bool wasOpen = false;
    bool isOpen = g_menuOpen.load();
    if (isOpen && !wasOpen) ConsoleExecute("sgtm 0");
    else if (!isOpen && wasOpen) ConsoleExecute("sgtm 1");
    wasOpen = isOpen;

    if (isOpen) {
        ImGui::GetIO().MouseDrawCursor = true;
        RadioMenu::Render();
    } else {
        ImGui::GetIO().MouseDrawCursor = false;
    }

    ImGui::EndFrame();
    ImGui::Render();

    if (isOpen) {
        UINT idx = swapChain->GetCurrentBackBufferIndex();
        ComPtr<ID3D12Resource> backBuffer;
        swapChain->GetBuffer(idx, IID_PPV_ARGS(&backBuffer));

        UINT rtvSize = g_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = g_rtvHeap->GetCPUDescriptorHandleForHeapStart();
        rtvHandle.ptr += idx * rtvSize;
        g_device->CreateRenderTargetView(backBuffer.Get(), nullptr, rtvHandle);

        g_cmdList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);
        g_cmdList->SetDescriptorHeaps(1, g_srvHeap.GetAddressOf());
        ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), g_cmdList.Get());
    }

    g_cmdList->Close();
    ID3D12CommandList* lists[] = { g_cmdList.Get() };
    g_commandQueue->ExecuteCommandLists(1, lists);
}

// Hook callbacks
static HRESULT STDMETHODCALLTYPE HookedPresent(IDXGISwapChain3* swapChain, UINT syncInterval, UINT flags)
{
    if (!g_initialized) {
        if (g_commandQueue) InitImGui(swapChain);
    }
    if (g_initialized && g_commandQueue) RenderOverlay(swapChain);
    return g_origPresent(swapChain, syncInterval, flags);
}

static void STDMETHODCALLTYPE HookedExecuteCommandLists(ID3D12CommandQueue* queue, UINT numLists, ID3D12CommandList* const* lists)
{
    if (!g_commandQueue) {
        D3D12_COMMAND_QUEUE_DESC desc = queue->GetDesc();
        if (desc.Type == D3D12_COMMAND_LIST_TYPE_DIRECT) {
            g_commandQueue = queue;
            REX::INFO("Radio UI: Captured D3D12 command queue");
        }
    }
    g_origExecuteCommandLists(queue, numLists, lists);
}

bool Install()
{
    REX::INFO("Radio UI: Setting up D3D12 hooks...");

    if (MH_Initialize() != MH_OK) {
        REX::INFO("Radio UI: MinHook init failed");
        return false;
    }

    // Create temp D3D12 device + swap chain to get vtable pointers
    ComPtr<ID3D12Device> tmpDevice;
    if (FAILED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&tmpDevice)))) {
        REX::INFO("Radio UI: Failed to create temp D3D12 device");
        return false;
    }

    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ComPtr<ID3D12CommandQueue> tmpQueue;
    if (FAILED(tmpDevice->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&tmpQueue)))) return false;

    WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, DefWindowProcW, 0, 0,
        GetModuleHandleW(nullptr), nullptr, nullptr, nullptr, nullptr, L"SFRadioDummy", nullptr };
    RegisterClassExW(&wc);
    HWND dummyHwnd = CreateWindowExW(0, wc.lpszClassName, L"", WS_OVERLAPPEDWINDOW,
        0, 0, 100, 100, nullptr, nullptr, wc.hInstance, nullptr);

    DXGI_SWAP_CHAIN_DESC1 swapDesc = {};
    swapDesc.BufferCount = 2;
    swapDesc.Width = 100;
    swapDesc.Height = 100;
    swapDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapDesc.SampleDesc.Count = 1;

    ComPtr<IDXGIFactory4> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
        DestroyWindow(dummyHwnd); UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return false;
    }

    ComPtr<IDXGISwapChain1> tmpSwapChain;
    if (FAILED(factory->CreateSwapChainForHwnd(tmpQueue.Get(), dummyHwnd, &swapDesc, nullptr, nullptr, &tmpSwapChain))) {
        DestroyWindow(dummyHwnd); UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return false;
    }

    // Read vtables
    void** scVtable = *reinterpret_cast<void***>(tmpSwapChain.Get());
    void** qVtable = *reinterpret_cast<void***>(tmpQueue.Get());

    // Hook Present (index 8) and ExecuteCommandLists (index 10)
    MH_CreateHook(scVtable[8], (void*)HookedPresent, (void**)&g_origPresent);
    MH_CreateHook(qVtable[10], (void*)HookedExecuteCommandLists, (void**)&g_origExecuteCommandLists);
    MH_EnableHook(MH_ALL_HOOKS);

    DestroyWindow(dummyHwnd);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);

    REX::INFO("Radio UI: D3D12 hooks installed");
    return true;
}

} // namespace DXHook
