#pragma once
//
// Overlay ImGui desenhado dentro do jogo.
//
// O UE4SS renderiza a GUI dele numa janela separada (RenderMode=ExternalThread,
// GLFW/OpenGL), entao nao da para pendurar a calculadora nela e ter uma janela
// "dentro" do jogo. Aqui a gente engancha o swapchain do proprio jogo:
//
//   * vtable de IDXGISwapChain::Present/ResizeBuffers trocada na mao
//     (a vtable e compartilhada por todos os swapchains do processo, entao
//     swapchains fantasmas criados na inicializacao dao os enderecos certos);
//   * no DX12, ID3D12CommandQueue::ExecuteCommandLists tambem e enganchado,
//     porque a fila de comandos do jogo nao e alcancavel pelo swapchain;
//   * ImGui proprio (contexto separado do UE4SS), com o renderizador escolhido
//     conforme o device do swapchain — ver Renderer.hpp;
//   * WndProc do jogo interceptado para alimentar o ImGui e engolir o input
//     enquanto a janela esta aberta.
//
#include <cstdint>
#include <functional>

namespace palbreed
{
    class ITextureBackend;

    enum class OverlayStatus
    {
        NotInstalled,        // install() ainda nao rodou (ou falhou)
        WaitingForDevice,    // hooks no lugar, esperando o primeiro Present
        Ready,               // desenhando
        Disabled,            // desligado depois de falhas seguidas
    };

    class Overlay
    {
      public:
        static auto get() -> Overlay&;

        // Instala os hooks. Pode ser chamado uma vez; devolve false se as
        // vtables nao puderem ser capturadas (o log diz o motivo).
        auto install(std::function<void()> render_callback) -> bool;
        auto uninstall() -> void;

        auto toggle() -> void;
        auto set_visible(bool visible) -> void;
        auto visible() const -> bool;

        auto status() const -> OverlayStatus;
        // Frase pronta para o log quando o atalho nao consegue abrir a janela.
        auto status_text() const -> const char*;

        // Disponivel so depois do primeiro frame (quando o swapchain aparece).
        auto textures() -> ITextureBackend*;

        // Muda toda vez que um renderizador novo e criado. Quem guarda
        // ImTextureID (a cache de icones) compara com o valor anterior e
        // esquece o que tinha: os identificadores do renderizador antigo
        // apontam para memoria da GPU que ja foi liberada.
        auto renderer_generation() const -> uint32_t;
    };
} // namespace palbreed
