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
#include <functional>

namespace palbreed
{
    class ITextureBackend;

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

        // Disponivel so depois do primeiro frame (quando o swapchain aparece).
        auto textures() -> ITextureBackend*;
    };
} // namespace palbreed
