#include "Overlay.hpp"
#include "Log.hpp"
#include "Renderer.hpp"

#include <atomic>
#include <mutex>
#include <vector>

#include <Windows.h>
#include <d3d11.h>
#include <d3d12.h>
#include <dxgi1_4.h>

#include <imgui.h>
#include <backends/imgui_impl_win32.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace palbreed
{
    namespace
    {
        using PresentFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);
        using ResizeBuffersFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT, UINT,
                                                            DXGI_FORMAT, UINT);
        using ExecuteCommandListsFn = void(STDMETHODCALLTYPE*)(ID3D12CommandQueue*, UINT,
                                                               ID3D12CommandList* const*);

        // indices na vtable de IDXGISwapChain (IUnknown 0-2, IDXGIObject 3-6,
        // IDXGIDeviceSubObject 7, entao Present == 8 e ResizeBuffers == 13)
        constexpr int kPresentIndex = 8;
        constexpr int kResizeBuffersIndex = 13;
        // ID3D12CommandQueue: IUnknown 0-2, ID3D12Object 3-6, ID3D12DeviceChild 7,
        // UpdateTileMappings 8, CopyTileMappings 9, ExecuteCommandLists 10
        constexpr int kExecuteCommandListsIndex = 10;

        struct State
        {
            std::function<void()> render{};
            std::atomic<bool> visible{false};
            std::atomic<bool> installed{false};

            PresentFn original_present{};
            ResizeBuffersFn original_resize{};
            ExecuteCommandListsFn original_execute{};
            std::vector<void**> hooked_vtables{};
            void** queue_vtable{};

            std::unique_ptr<IRenderer> renderer{};
            HWND window{};
            WNDPROC original_wndproc{};
            bool give_up{};                 // erro fatal: nao tenta de novo
            std::mutex init_mutex{};
        };

        auto state() -> State&
        {
            static State s{};
            return s;
        }

        auto write_vtable_entry(void** vtable, int index, void* replacement) -> void*
        {
            DWORD old_protection{};
            if (!VirtualProtect(&vtable[index], sizeof(void*), PAGE_EXECUTE_READWRITE, &old_protection))
            {
                return nullptr;
            }
            void* original = vtable[index];
            vtable[index] = replacement;
            VirtualProtect(&vtable[index], sizeof(void*), old_protection, &old_protection);
            return original;
        }

        auto overlay_wndproc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) -> LRESULT
        {
            auto& s = state();
            if (s.visible.load(std::memory_order_relaxed) && s.renderer)
            {
                ImGui_ImplWin32_WndProcHandler(window, message, wparam, lparam);
                const ImGuiIO& io = ImGui::GetIO();

                // Com a janela aberta o jogo nao pode receber input: o UE le o
                // mouse por raw input (WM_INPUT), entao esse tambem e engolido,
                // senao a camera continua girando por tras da janela.
                switch (message)
                {
                case WM_INPUT:
                    return 0;
                case WM_MOUSEMOVE:
                case WM_LBUTTONDOWN:
                case WM_LBUTTONUP:
                case WM_LBUTTONDBLCLK:
                case WM_RBUTTONDOWN:
                case WM_RBUTTONUP:
                case WM_MBUTTONDOWN:
                case WM_MBUTTONUP:
                case WM_MOUSEWHEEL:
                case WM_MOUSEHWHEEL:
                    if (io.WantCaptureMouse)
                    {
                        return 0;
                    }
                    break;
                case WM_KEYDOWN:
                case WM_KEYUP:
                case WM_CHAR:
                case WM_SYSKEYDOWN:
                case WM_SYSKEYUP:
                    if (io.WantCaptureKeyboard || io.WantTextInput)
                    {
                        return 0;
                    }
                    break;
                default:
                    break;
                }
            }
            return CallWindowProcW(s.original_wndproc, window, message, wparam, lparam);
        }

        // Cria o renderizador conforme a API que o jogo esta usando. O device do
        // swapchain e quem diz: ID3D11Device -> DX11, ID3D12Device -> DX12.
        auto create_renderer(IDXGISwapChain* swapchain) -> std::unique_ptr<IRenderer>
        {
            ID3D11Device* device11{};
            if (SUCCEEDED(swapchain->GetDevice(IID_PPV_ARGS(&device11))) && device11 != nullptr)
            {
                auto renderer = make_d3d11_renderer(device11);
                device11->Release();
                return renderer;
            }
            ID3D12Device* device12{};
            if (SUCCEEDED(swapchain->GetDevice(IID_PPV_ARGS(&device12))) && device12 != nullptr)
            {
                auto renderer = make_d3d12_renderer(device12);
                device12->Release();
                return renderer;
            }
            return nullptr;
        }

        auto init_overlay(IDXGISwapChain* swapchain) -> bool
        {
            auto& s = state();

            DXGI_SWAP_CHAIN_DESC desc{};
            swapchain->GetDesc(&desc);
            s.window = desc.OutputWindow;

            auto renderer = create_renderer(swapchain);
            if (renderer == nullptr)
            {
                log_error("swapchain is neither D3D11 nor D3D12 -- overlay disabled");
                s.give_up = true;
                return false;
            }

            IMGUI_CHECKVERSION();
            ImGui::CreateContext();
            ImGuiIO& io = ImGui::GetIO();
            io.IniFilename = nullptr;                 // sem imgui.ini na pasta do jogo
            io.MouseDrawCursor = true;                // o jogo esconde o cursor do SO
            ImGui::StyleColorsDark();

            // A fonte padrao do ImGui nao cobre acentuacao; Segoe UI cobre e
            // esta em qualquer Windows. Se faltar, seguimos com a padrao.
            io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf", 20.0f, nullptr,
                                         io.Fonts->GetGlyphRangesDefault());
            if (io.Fonts->Fonts.empty())
            {
                io.Fonts->AddFontDefault();
            }

            if (!ImGui_ImplWin32_Init(s.window))
            {
                log_error("ImGui_ImplWin32_Init failed -- overlay disabled");
                ImGui::DestroyContext();
                s.give_up = true;
                return false;
            }

            if (!renderer->init(swapchain, s.window))
            {
                // no DX12 isso acontece enquanto a fila de comandos do jogo
                // ainda nao passou pelo hook: vale tentar no proximo frame
                ImGui_ImplWin32_Shutdown();
                ImGui::DestroyContext();
                return false;
            }

            s.renderer = std::move(renderer);
            s.original_wndproc = reinterpret_cast<WNDPROC>(
                SetWindowLongPtrW(s.window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(overlay_wndproc)));
            return true;
        }

        auto STDMETHODCALLTYPE hooked_present(IDXGISwapChain* swapchain, UINT sync_interval, UINT flags)
            -> HRESULT
        {
            auto& s = state();
            if (!s.give_up)
            {
                std::lock_guard<std::mutex> lock(s.init_mutex);
                if (s.renderer == nullptr && !s.give_up)
                {
                    init_overlay(swapchain);
                }
                if (s.renderer && s.visible.load(std::memory_order_relaxed))
                {
                    s.renderer->new_frame();
                    ImGui_ImplWin32_NewFrame();
                    ImGui::NewFrame();
                    if (s.render)
                    {
                        s.render();
                    }
                    ImGui::Render();
                    s.renderer->render(swapchain);
                }
            }
            return s.original_present(swapchain, sync_interval, flags);
        }

        auto STDMETHODCALLTYPE hooked_resize_buffers(IDXGISwapChain* swapchain, UINT buffer_count,
                                                     UINT width, UINT height, DXGI_FORMAT format,
                                                     UINT flags) -> HRESULT
        {
            auto& s = state();
            // as render targets apontam para os back buffers antigos: soltar
            // antes do resize, recriar na proxima apresentacao
            {
                std::lock_guard<std::mutex> lock(s.init_mutex);
                if (s.renderer)
                {
                    s.renderer->release_targets();
                }
            }
            return s.original_resize(swapchain, buffer_count, width, height, format, flags);
        }

        auto STDMETHODCALLTYPE hooked_execute_command_lists(ID3D12CommandQueue* queue, UINT count,
                                                            ID3D12CommandList* const* lists) -> void
        {
            // A fila de comandos do jogo nao e alcancavel pelo swapchain; e aqui
            // que ela aparece. So interessa a primeira fila DIRECT.
            if (d3d12_captured_queue() == nullptr && queue != nullptr
                && queue->GetDesc().Type == D3D12_COMMAND_LIST_TYPE_DIRECT)
            {
                d3d12_capture_queue(queue);
            }
            state().original_execute(queue, count, lists);
        }

        // Um swapchain (e uma fila, no DX12) descartaveis so para ler as
        // vtables — elas sao compartilhadas por todos os objetos do processo,
        // inclusive os do jogo.
        struct DummyVTables
        {
            void** swapchain11{};
            void** swapchain12{};
            void** queue12{};
        };

        auto make_dummy_window() -> HWND
        {
            WNDCLASSEXW window_class{};
            window_class.cbSize = sizeof(window_class);
            window_class.lpfnWndProc = DefWindowProcW;
            window_class.hInstance = GetModuleHandleW(nullptr);
            window_class.lpszClassName = L"PalBreedCalcDummy";
            if (RegisterClassExW(&window_class) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
            {
                return nullptr;
            }
            return CreateWindowExW(0, window_class.lpszClassName, L"", WS_OVERLAPPEDWINDOW, 0, 0, 64, 64,
                                   nullptr, nullptr, window_class.hInstance, nullptr);
        }

        auto capture_d3d11(HWND window, DummyVTables& out) -> void
        {
            DXGI_SWAP_CHAIN_DESC desc{};
            desc.BufferCount = 1;
            desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
            desc.OutputWindow = window;
            desc.SampleDesc.Count = 1;
            desc.Windowed = TRUE;
            desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

            IDXGISwapChain* swapchain{};
            ID3D11Device* device{};
            ID3D11DeviceContext* context{};
            const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0};
            D3D_FEATURE_LEVEL obtained{};
            if (SUCCEEDED(D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
                                                        levels, 1, D3D11_SDK_VERSION, &desc, &swapchain,
                                                        &device, &obtained, &context))
                && swapchain != nullptr)
            {
                out.swapchain11 = *reinterpret_cast<void***>(swapchain);
            }
            if (swapchain) swapchain->Release();
            if (context) context->Release();
            if (device) device->Release();
        }

        auto capture_d3d12(HWND window, DummyVTables& out) -> void
        {
            ID3D12Device* device{};
            if (FAILED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)))
                || device == nullptr)
            {
                return;     // maquina/driver sem DX12: so o caminho DX11 existe
            }

            D3D12_COMMAND_QUEUE_DESC queue_desc{};
            queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
            ID3D12CommandQueue* queue{};
            IDXGIFactory4* factory{};
            IDXGISwapChain1* swapchain{};
            if (SUCCEEDED(device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&queue))) && queue)
            {
                out.queue12 = *reinterpret_cast<void***>(queue);

                DXGI_SWAP_CHAIN_DESC1 desc{};
                desc.BufferCount = 2;
                desc.Width = 64;
                desc.Height = 64;
                desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
                desc.SampleDesc.Count = 1;
                desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
                if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))) && factory != nullptr
                    && SUCCEEDED(factory->CreateSwapChainForHwnd(queue, window, &desc, nullptr, nullptr,
                                                                 &swapchain))
                    && swapchain != nullptr)
                {
                    out.swapchain12 = *reinterpret_cast<void***>(swapchain);
                }
            }
            if (swapchain) swapchain->Release();
            if (factory) factory->Release();
            if (queue) queue->Release();
            device->Release();
        }
    } // namespace

    auto Overlay::get() -> Overlay&
    {
        static Overlay overlay{};
        return overlay;
    }

    auto Overlay::install(std::function<void()> render_callback) -> bool
    {
        auto& s = state();
        if (s.installed.load())
        {
            return true;
        }
        s.render = std::move(render_callback);

        HWND window = make_dummy_window();
        if (window == nullptr)
        {
            log_error("could not create the helper window");
            return false;
        }
        DummyVTables tables{};
        capture_d3d11(window, tables);
        capture_d3d12(window, tables);
        DestroyWindow(window);

        // DX11 e DX12 podem ou nao compartilhar a mesma implementacao de
        // swapchain; enganchar as duas (sem repetir) cobre os dois modos.
        for (void** vtable : {tables.swapchain11, tables.swapchain12})
        {
            if (vtable == nullptr)
            {
                continue;
            }
            bool already = false;
            for (void** hooked : s.hooked_vtables)
            {
                already = already || hooked == vtable;
            }
            if (already)
            {
                continue;
            }
            auto* present = reinterpret_cast<PresentFn>(
                write_vtable_entry(vtable, kPresentIndex, reinterpret_cast<void*>(&hooked_present)));
            auto* resize = reinterpret_cast<ResizeBuffersFn>(write_vtable_entry(
                vtable, kResizeBuffersIndex, reinterpret_cast<void*>(&hooked_resize_buffers)));
            if (present == nullptr || resize == nullptr)
            {
                continue;
            }
            s.original_present = present;
            s.original_resize = resize;
            s.hooked_vtables.push_back(vtable);
        }

        if (s.hooked_vtables.empty())
        {
            log_error("could not hook the swapchain -- overlay disabled");
            return false;
        }

        if (tables.queue12 != nullptr)
        {
            s.original_execute = reinterpret_cast<ExecuteCommandListsFn>(
                write_vtable_entry(tables.queue12, kExecuteCommandListsIndex,
                                   reinterpret_cast<void*>(&hooked_execute_command_lists)));
            s.queue_vtable = s.original_execute ? tables.queue12 : nullptr;
        }

        s.installed.store(true);
        log_info(s.queue_vtable ? "hooks installed (DX11 and DX12)" : "hooks installed (DX11 only)");
        return true;
    }

    auto Overlay::uninstall() -> void
    {
        auto& s = state();
        if (!s.installed.load())
        {
            return;
        }
        // Devolve as vtables ao estado original antes de descarregar a DLL,
        // senao o proximo Present pula para memoria que nao existe mais.
        for (void** vtable : s.hooked_vtables)
        {
            write_vtable_entry(vtable, kPresentIndex, reinterpret_cast<void*>(s.original_present));
            write_vtable_entry(vtable, kResizeBuffersIndex, reinterpret_cast<void*>(s.original_resize));
        }
        s.hooked_vtables.clear();
        if (s.queue_vtable)
        {
            write_vtable_entry(s.queue_vtable, kExecuteCommandListsIndex,
                               reinterpret_cast<void*>(s.original_execute));
            s.queue_vtable = nullptr;
        }
        s.installed.store(false);
        s.visible.store(false);

        std::lock_guard<std::mutex> lock(s.init_mutex);
        if (s.renderer)
        {
            if (s.original_wndproc && s.window)
            {
                SetWindowLongPtrW(s.window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(s.original_wndproc));
            }
            s.renderer->shutdown();
            s.renderer.reset();
            ImGui_ImplWin32_Shutdown();
            ImGui::DestroyContext();
        }
    }

    auto Overlay::toggle() -> void
    {
        set_visible(!state().visible.load());
    }

    auto Overlay::set_visible(bool visible) -> void
    {
        state().visible.store(visible);
    }

    auto Overlay::visible() const -> bool
    {
        return state().visible.load();
    }

    auto Overlay::textures() -> ITextureBackend*
    {
        auto& s = state();
        return s.renderer ? &s.renderer->textures() : nullptr;
    }
} // namespace palbreed
