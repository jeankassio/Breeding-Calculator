// Preview da janela fora do jogo: sobe um D3D11 proprio, desenha exatamente a
// mesma Ui do mod e salva um PNG. Serve para ajustar o visual sem precisar
// abrir o Palworld a cada mudanca.
//
//   preview.exe <caminho\dos\icones> <saida.bmp> [macho] [femea]

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <Windows.h>
#include <d3d11.h>

#include <imgui.h>
#include <backends/imgui_impl_dx11.h>
#include <backends/imgui_impl_win32.h>

#include "Breeding.hpp"
#include "Renderer.hpp"
#include "Ui.hpp"

#pragma comment(lib, "d3d11.lib")

namespace
{
    constexpr int kWidth = 1100;
    constexpr int kHeight = 880;

    auto save_bmp(const char* path, const std::vector<uint8_t>& bgra, int width, int height) -> bool
    {
        const uint32_t pixel_bytes = static_cast<uint32_t>(width) * height * 4;
        BITMAPFILEHEADER file{};
        BITMAPINFOHEADER info{};
        file.bfType = 0x4D42;
        file.bfOffBits = sizeof(file) + sizeof(info);
        file.bfSize = file.bfOffBits + pixel_bytes;
        info.biSize = sizeof(info);
        info.biWidth = width;
        info.biHeight = -height;                 // top-down
        info.biPlanes = 1;
        info.biBitCount = 32;
        info.biCompression = BI_RGB;

        FILE* out{};
        if (fopen_s(&out, path, "wb") != 0 || out == nullptr)
        {
            return false;
        }
        std::fwrite(&file, sizeof(file), 1, out);
        std::fwrite(&info, sizeof(info), 1, out);
        std::fwrite(bgra.data(), 1, pixel_bytes, out);
        std::fclose(out);
        return true;
    }
} // namespace

int main(int argc, char** argv)
{
    const char* icons_dir = argc > 1 ? argv[1] : "";
    const char* output = argc > 2 ? argv[2] : "preview.bmp";
    const char* male_id = argc > 3 ? argv[3] : "SheepBall";
    const char* female_id = argc > 4 ? argv[4] : "PinkCat";

    WNDCLASSEXW window_class{};
    window_class.cbSize = sizeof(window_class);
    window_class.lpfnWndProc = DefWindowProcW;
    window_class.hInstance = GetModuleHandleW(nullptr);
    window_class.lpszClassName = L"PalBreedCalcPreview";
    RegisterClassExW(&window_class);
    HWND window = CreateWindowExW(0, window_class.lpszClassName, L"preview", WS_OVERLAPPEDWINDOW,
                                  0, 0, kWidth, kHeight, nullptr, nullptr, window_class.hInstance, nullptr);

    DXGI_SWAP_CHAIN_DESC desc{};
    desc.BufferCount = 2;
    desc.BufferDesc.Width = kWidth;
    desc.BufferDesc.Height = kHeight;
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
    if (FAILED(D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 1,
                                             D3D11_SDK_VERSION, &desc, &swapchain, &device, &obtained,
                                             &context)))
    {
        std::printf("D3D11CreateDeviceAndSwapChain falhou\n");
        return 1;
    }

    ID3D11Texture2D* back_buffer{};
    ID3D11RenderTargetView* render_target{};
    swapchain->GetBuffer(0, IID_PPV_ARGS(&back_buffer));
    device->CreateRenderTargetView(back_buffer, nullptr, &render_target);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(static_cast<float>(kWidth), static_cast<float>(kHeight));
    io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf", 20.0f, nullptr,
                                 io.Fonts->GetGlyphRangesDefault());
    ImGui::StyleColorsDark();
    ImGui_ImplWin32_Init(window);

    // mesmo renderizador do mod, para o preview exercitar o caminho real
    auto renderer = palbreed::make_d3d11_renderer(device);
    if (!renderer->init(swapchain, window))
    {
        std::printf("renderizador falhou\n");
        return 1;
    }

    const palbreed::Engine engine{};
    palbreed::Ui ui(engine);
    if (std::strcmp(male_id, "reverse") == 0)
    {
        // preview.exe <icones> <saida> reverse <filhote>
        ui.select_child(engine.find(female_id));
    }
    else
    {
        ui.select(engine.find(male_id), engine.find(female_id));
    }

    // alguns frames: o ImGui precisa de um para medir o layout antes de acertar
    for (int frame = 0; frame < 4; ++frame)
    {
        renderer->new_frame();
        ImGui_ImplWin32_NewFrame();
        io.DisplaySize = ImVec2(static_cast<float>(kWidth), static_cast<float>(kHeight));
        ImGui::NewFrame();

        ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
        ImGui::SetNextWindowSize(io.DisplaySize, ImGuiCond_Always);
        bool open = true;
        ui.render(&open, &renderer->textures(), icons_dir);

        ImGui::Render();
        const float clear[4] = {0.09f, 0.09f, 0.11f, 1.0f};
        context->OMSetRenderTargets(1, &render_target, nullptr);
        context->ClearRenderTargetView(render_target, clear);
        renderer->render(swapchain);
        swapchain->Present(0, 0);
    }

    // copia o back buffer para memoria e grava
    D3D11_TEXTURE2D_DESC copy_desc{};
    back_buffer->GetDesc(&copy_desc);
    copy_desc.Usage = D3D11_USAGE_STAGING;
    copy_desc.BindFlags = 0;
    copy_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ID3D11Texture2D* staging{};
    device->CreateTexture2D(&copy_desc, nullptr, &staging);
    context->CopyResource(staging, back_buffer);

    D3D11_MAPPED_SUBRESOURCE mapped{};
    std::vector<uint8_t> pixels(static_cast<std::size_t>(kWidth) * kHeight * 4);
    if (SUCCEEDED(context->Map(staging, 0, D3D11_MAP_READ, 0, &mapped)))
    {
        for (int y = 0; y < kHeight; ++y)
        {
            const uint8_t* row = static_cast<const uint8_t*>(mapped.pData) + y * mapped.RowPitch;
            uint8_t* dest = pixels.data() + static_cast<std::size_t>(y) * kWidth * 4;
            for (int x = 0; x < kWidth; ++x)
            {
                dest[x * 4 + 0] = row[x * 4 + 2];   // RGBA -> BGRA
                dest[x * 4 + 1] = row[x * 4 + 1];
                dest[x * 4 + 2] = row[x * 4 + 0];
                dest[x * 4 + 3] = 255;
            }
        }
        context->Unmap(staging, 0);
    }
    const bool saved = save_bmp(output, pixels, kWidth, kHeight);
    std::printf(saved ? "gravado %s\n" : "falha ao gravar %s\n", output);

    ui.shutdown();
    renderer->shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    return saved ? 0 : 1;
}
