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

        // Depois de tantos frames ruins seguidos o overlay se desliga. Um frame
        // isolado que falha (um resize no meio, um upload que nao coube) nao
        // pode condenar a janela para sempre -- era o que fazia o atalho "parar
        // de funcionar" sem nenhum aviso.
        constexpr int kMaxConsecutiveFailures = 3;

        // Duas chamadas do atalho dentro desta janela contam como uma. Protege
        // contra o callback ficar registrado em duplicata (hot reload do UE4SS
        // nao desregistra eventos de mod C++), caso em que os dois toggles se
        // anulavam e a tecla parecia morta.
        constexpr uint64_t kToggleDebounceMs = 200;

        struct QueuedMessage
        {
            HWND window;
            UINT message;
            WPARAM wparam;
            LPARAM lparam;
        };

        // Um swapchain hookado, com os ponteiros originais dele. Guardar por
        // vtable (e nao um par global) importa: DX11 e DX12 podem ter
        // implementacoes diferentes, e ai chamar o "original" do outro seria
        // um salto para a funcao errada.
        struct HookEntry
        {
            void** vtable{};
            PresentFn present{};
            ResizeBuffersFn resize{};
        };

        constexpr int kMaxHooks = 4;

        struct State
        {
            std::function<void()> render{};
            std::atomic<bool> visible{false};
            std::atomic<bool> installed{false};

            // O WndProc roda na thread da janela e o Present na de render;
            // alimentar o ImGui direto do WndProc e uma corrida de dados (era
            // uma causa real de crash ao abrir a janela). As mensagens sao
            // enfileiradas aqui e reaplicadas dentro do Present.
            std::mutex message_mutex{};
            std::vector<QueuedMessage> pending_messages{};

            // Preenchidos ANTES de a vtable ser trocada, e lidos sem lock pelos
            // hooks: hook_count so cresce depois que a entrada inteira ja esta
            // escrita (store/load com release/acquire).
            HookEntry hooks[kMaxHooks]{};
            std::atomic<int> hook_count{0};

            ExecuteCommandListsFn original_execute{};
            void** queue_vtable{};

            std::unique_ptr<IRenderer> renderer{};
            std::atomic<uint32_t> renderer_generation{0};
            HWND window{};
            WNDPROC original_wndproc{};

            std::atomic<bool> disabled{false};      // desligado de vez
            std::atomic<int> consecutive_failures{0};
            // NewFrame chamado sem o Render correspondente. Um frame que morreu
            // no meio deixa o contexto do ImGui aberto, e o NewFrame seguinte
            // trabalharia em cima de um estado invalido -- ou seja, a segunda
            // tentativa quebraria por causa da primeira.
            std::atomic<bool> frame_open{false};
            std::atomic<bool> alive{false};         // ha um renderizador pronto
            std::atomic<uint64_t> last_toggle_ms{0};

            // Quantas threads estao dentro dos hooks agora. uninstall() espera
            // zerar antes de devolver as vtables, senao um Present em voo
            // voltaria para dentro de uma DLL ja descarregada.
            std::atomic<int> in_flight{0};

            std::mutex init_mutex{};
        };

        auto state() -> State&
        {
            static State s{};
            return s;
        }

        auto write_vtable_entry(void** vtable, int index, void* replacement) -> bool
        {
            DWORD old_protection{};
            if (!VirtualProtect(&vtable[index], sizeof(void*), PAGE_EXECUTE_READWRITE, &old_protection))
            {
                return false;
            }
            vtable[index] = replacement;
            VirtualProtect(&vtable[index], sizeof(void*), old_protection, &old_protection);
            return true;
        }

        // Qual entrada corresponde a este swapchain. A vtable do objeto e a
        // chave -- a mesma que foi trocada na instalacao.
        auto hook_for(void* object) -> const HookEntry*
        {
            auto& s = state();
            void** vtable = *reinterpret_cast<void***>(object);
            const int count = s.hook_count.load(std::memory_order_acquire);
            for (int i = 0; i < count; ++i)
            {
                if (s.hooks[i].vtable == vtable)
                {
                    return &s.hooks[i];
                }
            }
            return nullptr;
        }

        auto overlay_alive() -> bool
        {
            auto& s = state();
            return s.alive.load(std::memory_order_relaxed)
                   && !s.disabled.load(std::memory_order_relaxed);
        }

        // A janela so engole input enquanto realmente existe um overlay
        // desenhando. Sem esta checagem, um overlay que morreu no meio do
        // caminho continuava comendo teclado e mouse e o jogo parecia travado.
        auto should_capture_input() -> bool
        {
            return state().visible.load(std::memory_order_relaxed) && overlay_alive();
        }

        auto disable_overlay(const char* reason) -> void
        {
            auto& s = state();
            s.visible.store(false);
            s.alive.store(false);
            s.disabled.store(true);
            log_error(reason);
        }

        auto is_input_message(UINT message) -> bool
        {
            switch (message)
            {
            case WM_INPUT:                  // o UE le o mouse por raw input
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
            case WM_KEYDOWN:
            case WM_KEYUP:
            case WM_CHAR:
            case WM_SYSKEYDOWN:
            case WM_SYSKEYUP:
                return true;
            default:
                return false;
            }
        }

        auto overlay_wndproc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) -> LRESULT
        {
            auto& s = state();
            if (should_capture_input() && is_input_message(message))
            {
                // Nada de ImGui aqui (thread errada): so enfileira e decide se
                // o jogo ve a mensagem. Com a janela aberta ela e modal: todo
                // input de mouse/teclado e engolido, senao a camera continua
                // girando por tras da janela.
                {
                    std::lock_guard<std::mutex> lock(s.message_mutex);
                    if (s.pending_messages.size() < 512)
                    {
                        s.pending_messages.push_back({window, message, wparam, lparam});
                    }
                }
                // WM_INPUT precisa passar pelo DefWindowProc mesmo quando e
                // engolido: e ele quem libera o buffer de raw input. Devolver
                // 0 direto vaza a cada movimento do mouse.
                if (message == WM_INPUT)
                {
                    return DefWindowProcW(window, message, wparam, lparam);
                }
                return 0;
            }
            if (s.original_wndproc == nullptr)
            {
                return DefWindowProcW(window, message, wparam, lparam);
            }
            return CallWindowProcW(s.original_wndproc, window, message, wparam, lparam);
        }

        // Reaplica as mensagens acumuladas -- chamado dentro do Present, na
        // mesma thread que usa o contexto do ImGui.
        auto replay_messages() -> void
        {
            auto& s = state();
            std::vector<QueuedMessage> messages;
            {
                std::lock_guard<std::mutex> lock(s.message_mutex);
                messages.swap(s.pending_messages);
            }
            if (!s.visible.load(std::memory_order_relaxed))
            {
                return;                     // janela fechou: descarta
            }
            for (const auto& m : messages)
            {
                ImGui_ImplWin32_WndProcHandler(m.window, m.message, m.wparam, m.lparam);
            }
        }

        // Cria o renderizador conforme a API que o jogo esta usando. O device do
        // swapchain e quem diz: ID3D11Device -> DX11, ID3D12Device -> DX12.
        auto create_renderer(IDXGISwapChain* swapchain) -> std::unique_ptr<IRenderer>
        {
            // DX12 primeiro: um jogo em DX12 que use D3D11On12 responde as duas
            // perguntas, e ai o caminho DX11 desenharia num back buffer que nao
            // e o dele.
            ID3D12Device* device12{};
            if (SUCCEEDED(swapchain->GetDevice(IID_PPV_ARGS(&device12))) && device12 != nullptr)
            {
                auto renderer = make_d3d12_renderer(device12);
                device12->Release();
                return renderer;
            }
            ID3D11Device* device11{};
            if (SUCCEEDED(swapchain->GetDevice(IID_PPV_ARGS(&device11))) && device11 != nullptr)
            {
                auto renderer = make_d3d11_renderer(device11);
                device11->Release();
                return renderer;
            }
            return nullptr;
        }

        auto init_overlay(IDXGISwapChain* swapchain) -> bool
        {
            auto& s = state();

            auto renderer = create_renderer(swapchain);
            if (renderer == nullptr)
            {
                disable_overlay("swapchain is neither D3D11 nor D3D12 -- overlay disabled");
                return false;
            }

            DXGI_SWAP_CHAIN_DESC desc{};
            if (FAILED(swapchain->GetDesc(&desc)) || desc.OutputWindow == nullptr)
            {
                return false;               // swapchain sem janela: tenta no proximo
            }
            s.window = desc.OutputWindow;

            IMGUI_CHECKVERSION();
            ImGui::CreateContext();
            ImGuiIO& io = ImGui::GetIO();
            io.IniFilename = nullptr;                 // sem imgui.ini na pasta do jogo
            io.MouseDrawCursor = true;                // o jogo esconde o cursor do SO
            ImGui::StyleColorsDark();

            // A fonte padrao do ImGui nao cobre acentuacao; Segoe UI cobre e
            // esta em qualquer Windows. Se faltar, seguimos com a padrao.
            io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf", 20.0f);
            if (io.Fonts->Fonts.empty())
            {
                io.Fonts->AddFontDefault();
            }

            if (!ImGui_ImplWin32_Init(s.window))
            {
                log_error("ImGui_ImplWin32_Init failed -- overlay disabled");
                ImGui::DestroyContext();
                disable_overlay("win32 backend unavailable");
                return false;
            }

            if (!renderer->init(swapchain, s.window))
            {
                // no DX12 isso acontece enquanto a fila de comandos do jogo
                // ainda nao passou pelo hook: vale tentar no proximo frame
                renderer->shutdown();
                ImGui_ImplWin32_Shutdown();
                ImGui::DestroyContext();
                return false;
            }

            s.renderer = std::move(renderer);
            s.renderer_generation.fetch_add(1, std::memory_order_release);
            s.original_wndproc = reinterpret_cast<WNDPROC>(
                SetWindowLongPtrW(s.window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(overlay_wndproc)));
            s.alive.store(true);
            return true;
        }

        // O frame em si. Separado do guarda de SEH abaixo porque tem objetos
        // com destrutor (o MSVC nao aceita __try nessas funcoes).
        auto draw_frame(IDXGISwapChain* swapchain) -> bool
        {
            auto& s = state();
            try
            {
                replay_messages();
                s.renderer->new_frame();
                ImGui_ImplWin32_NewFrame();
                ImGui::NewFrame();
                s.frame_open.store(true);
                if (s.render)
                {
                    s.render();
                }
                ImGui::Render();            // fecha o frame
                s.frame_open.store(false);
                s.renderer->render(swapchain);
                return true;
            }
            catch (...)
            {
                return false;
            }
        }

        // Depois de um frame que morreu no meio, devolve o contexto do ImGui a
        // um estado em que o proximo NewFrame funcione.
        auto close_dangling_frame() -> void
        {
            if (state().frame_open.exchange(false))
            {
                ImGui::EndFrame();
            }
        }

        // Estamos dentro do Present: uma violacao de acesso aqui derruba o
        // jogo inteiro. O /EHsc do projeto faz `catch (...)` NAO pegar
        // excecoes do Windows (SEH), entao o guarda de verdade e este.
        // Nao pode haver objeto com destrutor nesta funcao (C2712).
        auto draw_frame_guarded(IDXGISwapChain* swapchain) -> bool
        {
            __try
            {
                return draw_frame(swapchain);
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return false;
            }
        }

        auto close_dangling_frame_guarded() -> void
        {
            __try
            {
                close_dangling_frame();
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
            }
        }

        auto STDMETHODCALLTYPE hooked_present(IDXGISwapChain* swapchain, UINT sync_interval, UINT flags)
            -> HRESULT
        {
            auto& s = state();
            const HookEntry* hook = hook_for(swapchain);
            // Sem o original nao ha para onde voltar. Nunca deveria acontecer
            // (a entrada e escrita antes de a vtable ser trocada), mas chamar
            // um ponteiro nulo aqui seria um crash certo.
            if (hook == nullptr || hook->present == nullptr)
            {
                return DXGI_ERROR_INVALID_CALL;
            }

            s.in_flight.fetch_add(1, std::memory_order_acquire);

            if (!s.disabled.load(std::memory_order_relaxed))
            {
                std::lock_guard<std::mutex> lock(s.init_mutex);
                if (s.renderer == nullptr && !s.disabled.load(std::memory_order_relaxed))
                {
                    init_overlay(swapchain);
                }
                if (s.renderer && s.visible.load(std::memory_order_relaxed))
                {
                    if (draw_frame_guarded(swapchain))
                    {
                        s.consecutive_failures.store(0, std::memory_order_relaxed);
                    }
                    else
                    {
                        close_dangling_frame_guarded();
                        if (s.consecutive_failures.fetch_add(1, std::memory_order_relaxed) + 1
                            >= kMaxConsecutiveFailures)
                        {
                            disable_overlay("too many failed overlay frames -- overlay disabled "
                                            "to keep the game alive");
                        }
                    }
                }
            }

            const HRESULT result = hook->present(swapchain, sync_interval, flags);
            s.in_flight.fetch_sub(1, std::memory_order_release);
            return result;
        }

        auto STDMETHODCALLTYPE hooked_resize_buffers(IDXGISwapChain* swapchain, UINT buffer_count,
                                                     UINT width, UINT height, DXGI_FORMAT format,
                                                     UINT flags) -> HRESULT
        {
            auto& s = state();
            const HookEntry* hook = hook_for(swapchain);
            if (hook == nullptr || hook->resize == nullptr)
            {
                return DXGI_ERROR_INVALID_CALL;
            }

            s.in_flight.fetch_add(1, std::memory_order_acquire);
            // as render targets apontam para os back buffers antigos: soltar
            // antes do resize, recriar na proxima apresentacao
            {
                std::lock_guard<std::mutex> lock(s.init_mutex);
                if (s.renderer)
                {
                    s.renderer->release_targets();
                }
            }
            const HRESULT result = hook->resize(swapchain, buffer_count, width, height, format, flags);
            s.in_flight.fetch_sub(1, std::memory_order_release);
            return result;
        }

        auto STDMETHODCALLTYPE hooked_execute_command_lists(ID3D12CommandQueue* queue, UINT count,
                                                            ID3D12CommandList* const* lists) -> void
        {
            auto& s = state();
            // A fila de comandos do jogo nao e alcancavel pelo swapchain; e aqui
            // que ela aparece. So interessa a primeira fila DIRECT.
            if (d3d12_captured_queue() == nullptr && queue != nullptr
                && queue->GetDesc().Type == D3D12_COMMAND_LIST_TYPE_DIRECT)
            {
                d3d12_capture_queue(queue);
            }
            if (s.original_execute != nullptr)
            {
                s.original_execute(queue, count, lists);
            }
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

        // Engancha uma vtable de swapchain. A ordem importa muito: a entrada
        // com os ponteiros originais fica pronta e visivel ANTES de a vtable
        // ser trocada. Se fosse ao contrario, um Present chegando no meio
        // encontraria "original = nullptr" e pularia para o vazio.
        auto hook_swapchain_vtable(void** vtable) -> bool
        {
            auto& s = state();
            const int count = s.hook_count.load(std::memory_order_relaxed);
            if (vtable == nullptr || count >= kMaxHooks)
            {
                return false;
            }
            for (int i = 0; i < count; ++i)
            {
                if (s.hooks[i].vtable == vtable)
                {
                    return true;            // DX11 e DX12 compartilham a vtable
                }
            }

            s.hooks[count].vtable = vtable;
            s.hooks[count].present = reinterpret_cast<PresentFn>(vtable[kPresentIndex]);
            s.hooks[count].resize = reinterpret_cast<ResizeBuffersFn>(vtable[kResizeBuffersIndex]);
            if (s.hooks[count].present == nullptr || s.hooks[count].resize == nullptr)
            {
                return false;
            }
            s.hook_count.store(count + 1, std::memory_order_release);

            if (!write_vtable_entry(vtable, kPresentIndex, reinterpret_cast<void*>(&hooked_present)))
            {
                s.hook_count.store(count, std::memory_order_release);
                return false;
            }
            if (!write_vtable_entry(vtable, kResizeBuffersIndex,
                                    reinterpret_cast<void*>(&hooked_resize_buffers)))
            {
                // Present ja esta trocado; devolver e o unico jeito de nao
                // deixar o jogo com meio hook.
                write_vtable_entry(vtable, kPresentIndex,
                                   reinterpret_cast<void*>(s.hooks[count].present));
                s.hook_count.store(count, std::memory_order_release);
                return false;
            }
            return true;
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
        hook_swapchain_vtable(tables.swapchain11);
        hook_swapchain_vtable(tables.swapchain12);

        if (s.hook_count.load() == 0)
        {
            log_error("could not hook the swapchain -- overlay disabled");
            return false;
        }

        if (tables.queue12 != nullptr)
        {
            s.original_execute = reinterpret_cast<ExecuteCommandListsFn>(
                tables.queue12[kExecuteCommandListsIndex]);
            if (s.original_execute != nullptr
                && write_vtable_entry(tables.queue12, kExecuteCommandListsIndex,
                                      reinterpret_cast<void*>(&hooked_execute_command_lists)))
            {
                s.queue_vtable = tables.queue12;
            }
            else
            {
                s.original_execute = nullptr;
            }
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
        s.visible.store(false);
        s.alive.store(false);

        // Devolve as vtables ao estado original antes de descarregar a DLL,
        // senao o proximo Present pula para memoria que nao existe mais.
        const int count = s.hook_count.load(std::memory_order_acquire);
        for (int i = 0; i < count; ++i)
        {
            write_vtable_entry(s.hooks[i].vtable, kPresentIndex,
                               reinterpret_cast<void*>(s.hooks[i].present));
            write_vtable_entry(s.hooks[i].vtable, kResizeBuffersIndex,
                               reinterpret_cast<void*>(s.hooks[i].resize));
        }
        if (s.queue_vtable && s.original_execute)
        {
            write_vtable_entry(s.queue_vtable, kExecuteCommandListsIndex,
                               reinterpret_cast<void*>(s.original_execute));
            s.queue_vtable = nullptr;
        }
        s.installed.store(false);

        // As vtables ja estao limpas, mas pode haver uma thread ainda dentro
        // de um hook. Descarregar a DLL agora seria um crash na volta.
        for (int spins = 0; s.in_flight.load(std::memory_order_acquire) > 0 && spins < 1000; ++spins)
        {
            Sleep(1);
        }
        s.hook_count.store(0, std::memory_order_release);

        std::lock_guard<std::mutex> lock(s.init_mutex);
        if (s.renderer)
        {
            if (s.original_wndproc && s.window)
            {
                SetWindowLongPtrW(s.window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(s.original_wndproc));
                s.original_wndproc = nullptr;
            }
            s.renderer->shutdown();
            s.renderer.reset();
            s.renderer_generation.fetch_add(1, std::memory_order_release);
            ImGui_ImplWin32_Shutdown();
            ImGui::DestroyContext();
        }
    }

    auto Overlay::toggle() -> void
    {
        auto& s = state();
        const uint64_t now = GetTickCount64();
        const uint64_t previous = s.last_toggle_ms.load(std::memory_order_relaxed);
        if (now - previous < kToggleDebounceMs)
        {
            return;                         // eco do mesmo toque
        }
        s.last_toggle_ms.store(now, std::memory_order_relaxed);
        set_visible(!s.visible.load());
    }

    auto Overlay::set_visible(bool visible) -> void
    {
        auto& s = state();
        s.visible.store(visible);
        if (!visible)
        {
            // Nao deixa input velho esperando para ser reaplicado no proximo
            // frame em que a janela abrir.
            std::lock_guard<std::mutex> lock(s.message_mutex);
            s.pending_messages.clear();
        }
    }

    auto Overlay::visible() const -> bool
    {
        return state().visible.load();
    }

    auto Overlay::status() const -> OverlayStatus
    {
        auto& s = state();
        if (s.disabled.load())
        {
            return OverlayStatus::Disabled;
        }
        if (!s.installed.load())
        {
            return OverlayStatus::NotInstalled;
        }
        return s.alive.load() ? OverlayStatus::Ready : OverlayStatus::WaitingForDevice;
    }

    auto Overlay::status_text() const -> const char*
    {
        switch (status())
        {
        case OverlayStatus::NotInstalled:
            return "the overlay hooks are not installed -- see the messages above";
        case OverlayStatus::WaitingForDevice:
            return "the overlay is still waiting for the game's renderer "
                   "(load into a world and try again)";
        case OverlayStatus::Disabled:
            return "the overlay was disabled after repeated errors -- restart the game";
        case OverlayStatus::Ready:
        default:
            return "the overlay is ready";
        }
    }

    auto Overlay::renderer_generation() const -> uint32_t
    {
        return state().renderer_generation.load(std::memory_order_acquire);
    }

    auto Overlay::textures() -> ITextureBackend*
    {
        auto& s = state();
        return s.renderer ? &s.renderer->textures() : nullptr;
    }
} // namespace palbreed
