#pragma once
//
// Um renderizador por API grafica. O Overlay descobre qual o jogo esta usando
// (pelo device do swapchain) e cria o correspondente; o resto do mod nao sabe
// a diferenca.
//
#include <memory>

#include "Textures.hpp"

struct IDXGISwapChain;
struct ID3D11Device;
struct ID3D12Device;
struct ID3D12CommandQueue;
struct HWND__;
using HWND = HWND__*;

namespace palbreed
{
    class IRenderer
    {
      public:
        virtual ~IRenderer() = default;

        virtual auto init(IDXGISwapChain* swapchain, HWND window) -> bool = 0;
        virtual auto shutdown() -> void = 0;

        // Chamados dentro do Present interceptado.
        virtual auto new_frame() -> void = 0;
        virtual auto render(IDXGISwapChain* swapchain) -> void = 0;

        // Antes de ResizeBuffers: solta o que aponta para os back buffers.
        virtual auto release_targets() -> void = 0;

        virtual auto textures() -> ITextureBackend& = 0;
    };

    auto make_d3d11_renderer(ID3D11Device* device) -> std::unique_ptr<IRenderer>;

    auto make_d3d12_renderer(ID3D12Device* device) -> std::unique_ptr<IRenderer>;

    // Fila de comandos do jogo, capturada pelo hook de
    // ID3D12CommandQueue::ExecuteCommandLists (ver Overlay.cpp) — o swapchain
    // nao da acesso a ela.
    auto d3d12_captured_queue() -> ID3D12CommandQueue*;
    auto d3d12_capture_queue(ID3D12CommandQueue* queue) -> void;
} // namespace palbreed
