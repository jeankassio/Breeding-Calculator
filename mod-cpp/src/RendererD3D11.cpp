#include "Renderer.hpp"
#include "Log.hpp"

#include <vector>

#include <Windows.h>
#include <d3d11.h>
#include <dxgi.h>

#include <imgui.h>
#include <backends/imgui_impl_dx11.h>

namespace palbreed
{
    namespace
    {
        class D3D11TextureBackend final : public ITextureBackend
        {
          public:
            explicit D3D11TextureBackend(ID3D11Device* device) : m_device(device)
            {
            }

            auto create(const DdsImage& image) -> ImTextureID override
            {
                D3D11_SUBRESOURCE_DATA initial{};
                initial.pSysMem = image.pixels;
                initial.SysMemPitch = image.row_pitch();

                D3D11_TEXTURE2D_DESC desc{};
                desc.Width = image.width;
                desc.Height = image.height;
                desc.MipLevels = 1;
                desc.ArraySize = 1;
                desc.Format = image.format;
                desc.SampleDesc.Count = 1;
                desc.Usage = D3D11_USAGE_IMMUTABLE;
                desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

                ID3D11Texture2D* texture{};
                if (FAILED(m_device->CreateTexture2D(&desc, &initial, &texture)) || texture == nullptr)
                {
                    return 0;
                }

                D3D11_SHADER_RESOURCE_VIEW_DESC view_desc{};
                view_desc.Format = desc.Format;
                view_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
                view_desc.Texture2D.MipLevels = 1;

                ID3D11ShaderResourceView* view{};
                const HRESULT hr = m_device->CreateShaderResourceView(texture, &view_desc, &view);
                texture->Release();
                if (FAILED(hr))
                {
                    return 0;
                }
                m_views.push_back(view);
                return reinterpret_cast<ImTextureID>(view);
            }

            auto release_all() -> void override
            {
                for (auto* view : m_views)
                {
                    view->Release();
                }
                m_views.clear();
            }

          private:
            ID3D11Device* m_device{};
            std::vector<ID3D11ShaderResourceView*> m_views{};
        };

        class D3D11Renderer final : public IRenderer
        {
          public:
            explicit D3D11Renderer(ID3D11Device* device) : m_device(device), m_textures(device)
            {
                m_device->AddRef();
                m_device->GetImmediateContext(&m_context);
            }

            ~D3D11Renderer() override
            {
                shutdown();
            }

            auto init(IDXGISwapChain* swapchain, HWND window) -> bool override
            {
                if (!ImGui_ImplDX11_Init(m_device, m_context))
                {
                    log_error("ImGui_ImplDX11_Init failed");
                    return false;
                }
                create_render_target(swapchain);
                log_info("overlay ready (DX11)");
                return true;
            }

            auto shutdown() -> void override
            {
                if (m_initialized)
                {
                    ImGui_ImplDX11_Shutdown();
                    m_initialized = false;
                }
                m_textures.release_all();
                release_targets();
                if (m_context) { m_context->Release(); m_context = nullptr; }
                if (m_device) { m_device->Release(); m_device = nullptr; }
            }

            auto new_frame() -> void override
            {
                ImGui_ImplDX11_NewFrame();
                m_initialized = true;
            }

            auto render(IDXGISwapChain* swapchain) -> void override
            {
                if (m_render_target == nullptr)
                {
                    create_render_target(swapchain);
                }
                if (m_render_target == nullptr)
                {
                    return;
                }
                m_context->OMSetRenderTargets(1, &m_render_target, nullptr);
                ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
            }

            auto release_targets() -> void override
            {
                if (m_render_target)
                {
                    m_render_target->Release();
                    m_render_target = nullptr;
                }
            }

            auto textures() -> ITextureBackend& override
            {
                return m_textures;
            }

          private:
            auto create_render_target(IDXGISwapChain* swapchain) -> void
            {
                ID3D11Texture2D* back_buffer{};
                if (FAILED(swapchain->GetBuffer(0, IID_PPV_ARGS(&back_buffer))) || back_buffer == nullptr)
                {
                    return;
                }
                m_device->CreateRenderTargetView(back_buffer, nullptr, &m_render_target);
                back_buffer->Release();
            }

            ID3D11Device* m_device{};
            ID3D11DeviceContext* m_context{};
            ID3D11RenderTargetView* m_render_target{};
            D3D11TextureBackend m_textures;
            bool m_initialized{};
        };
    } // namespace

    auto make_d3d11_renderer(ID3D11Device* device) -> std::unique_ptr<IRenderer>
    {
        return std::make_unique<D3D11Renderer>(device);
    }
} // namespace palbreed
