#pragma once
//
// Cache de icones. Os .dds ficam em Mods/PalBreedCalc/icons/ e vem direto do
// pak (mip 0 em BC3/BC7) — ver tools/extract_icons.py. O arquivo e o mesmo nos
// dois modos gráficos; quem muda e o backend que cria a textura.
//
#include <cstdint>
#include <string>
#include <unordered_map>

#include <dxgiformat.h>
#include <imgui.h>

namespace palbreed
{
    // Mip 0 de um .dds ja lido para a memoria.
    struct DdsImage
    {
        uint32_t width{};
        uint32_t height{};
        DXGI_FORMAT format{};
        uint32_t block_bytes{};      // 0 = formato nao comprimido (BGRA)
        const uint8_t* pixels{};
        std::size_t size{};

        // Bytes por linha de blocos (ou de pixels, se nao comprimido).
        auto row_pitch() const -> uint32_t
        {
            return block_bytes == 0 ? width * 4 : ((width + 3) / 4) * block_bytes;
        }

        auto rows() const -> uint32_t
        {
            return block_bytes == 0 ? height : (height + 3) / 4;
        }

        // Quantos bytes o backend vai mesmo ler. Um .dds truncado (download
        // incompleto, extracao pela metade) faria a copia passar do fim do
        // buffer -- violacao de acesso dentro do Present, ou seja, jogo fechado.
        auto expected_bytes() const -> std::size_t
        {
            return static_cast<std::size_t>(row_pitch()) * rows();
        }

        auto complete() const -> bool
        {
            return width > 0 && height > 0 && size >= expected_bytes();
        }
    };

    // Implementado por TextureBackendD3D11/D3D12.
    class ITextureBackend
    {
      public:
        virtual ~ITextureBackend() = default;
        virtual auto create(const DdsImage& image) -> ImTextureID = 0;
        virtual auto release_all() -> void = 0;
    };

    class TextureCache
    {
      public:
        // icons_dir vazio = ao lado da DLL (Mods/PalBreedCalc/icons). O preview
        // fora do jogo passa o caminho do repositorio.
        auto init(ITextureBackend* backend, const char* icons_dir = nullptr) -> void;
        auto shutdown() -> void;

        // Esquece os identificadores sem tocar no backend: usado quando o
        // renderizador e recriado (troca de resolucao, retomada do DX12). Os
        // ImTextureID antigos apontam para descritores que ja nao existem, e
        // desenhar com eles derruba o jogo.
        auto forget() -> void;

        // Chamar no inicio de cada frame: limita quantos icones sao lidos do
        // disco/enviados a GPU por frame. Sem isso, o primeiro F6 carrega os
        // 263 icones das listas num unico Present (em DX12 cada um espera uma
        // fence) -- o "peso" relatado ao abrir a janela.
        auto begin_frame() -> void;
        auto ready() const -> bool
        {
            return m_backend != nullptr;
        }

        // Carregam sob demanda; devolvem 0 quando o icone nao existe (a janela
        // desenha um espaco vazio no lugar).
        auto pal(const char* pal_id, const char* tribe) -> ImTextureID;
        auto egg(const char* icon_name) -> ImTextureID;

      private:
        auto load(const std::string& key) -> ImTextureID;

        ITextureBackend* m_backend{};
        std::string m_icons_dir{};
        std::unordered_map<std::string, ImTextureID> m_textures{};
        int m_frame_budget{16};
    };
} // namespace palbreed
