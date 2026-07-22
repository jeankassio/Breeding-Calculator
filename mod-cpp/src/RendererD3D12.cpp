#include "Renderer.hpp"
#include "Log.hpp"

#include <atomic>
#include <vector>

#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>

#include <imgui.h>
#include <backends/imgui_impl_dx12.h>

namespace palbreed
{
    namespace
    {
        // O swapchain nao entrega a fila de comandos do jogo; ela e capturada
        // pelo hook de ID3D12CommandQueue::ExecuteCommandLists (Overlay.cpp).
        std::atomic<ID3D12CommandQueue*> g_queue{nullptr};

        constexpr uint32_t kSrvDescriptors = 1024;   // fonte do ImGui + icones

        // Alocador simples de descritores SRV, compartilhado com o ImGui.
        class DescriptorHeap
        {
          public:
            auto create(ID3D12Device* device) -> bool
            {
                D3D12_DESCRIPTOR_HEAP_DESC desc{};
                desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
                desc.NumDescriptors = kSrvDescriptors;
                desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
                if (FAILED(device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&m_heap))))
                {
                    return false;
                }
                m_step = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
                m_cpu_start = m_heap->GetCPUDescriptorHandleForHeapStart();
                m_gpu_start = m_heap->GetGPUDescriptorHandleForHeapStart();
                m_free.reserve(kSrvDescriptors);
                for (uint32_t i = kSrvDescriptors; i > 0; --i)
                {
                    m_free.push_back(i - 1);
                }
                return true;
            }

            auto release() -> void
            {
                if (m_heap)
                {
                    m_heap->Release();
                    m_heap = nullptr;
                }
                m_free.clear();
            }

            auto allocate(D3D12_CPU_DESCRIPTOR_HANDLE* cpu, D3D12_GPU_DESCRIPTOR_HANDLE* gpu) -> bool
            {
                if (m_free.empty())
                {
                    return false;
                }
                const uint32_t index = m_free.back();
                m_free.pop_back();
                cpu->ptr = m_cpu_start.ptr + static_cast<SIZE_T>(index) * m_step;
                gpu->ptr = m_gpu_start.ptr + static_cast<UINT64>(index) * m_step;
                return true;
            }

            auto free(D3D12_CPU_DESCRIPTOR_HANDLE cpu) -> void
            {
                if (m_heap == nullptr)
                {
                    return;
                }
                m_free.push_back(static_cast<uint32_t>((cpu.ptr - m_cpu_start.ptr) / m_step));
            }

            auto get() -> ID3D12DescriptorHeap*
            {
                return m_heap;
            }

          private:
            ID3D12DescriptorHeap* m_heap{};
            D3D12_CPU_DESCRIPTOR_HANDLE m_cpu_start{};
            D3D12_GPU_DESCRIPTOR_HANDLE m_gpu_start{};
            uint32_t m_step{};
            std::vector<uint32_t> m_free{};
        };

        // Envia a textura para a GPU numa lista de comandos propria e espera a
        // copia terminar. Sao poucos KB por icone e o carregamento e sob
        // demanda, entao o custo e irrelevante perto de um frame.
        class D3D12TextureBackend final : public ITextureBackend
        {
          public:
            auto setup(ID3D12Device* device, ID3D12CommandQueue* queue, DescriptorHeap* heap) -> bool
            {
                m_device = device;
                m_queue = queue;
                m_heap = heap;
                return SUCCEEDED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                                IID_PPV_ARGS(&m_allocator)))
                       && SUCCEEDED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                              m_allocator, nullptr, IID_PPV_ARGS(&m_list)))
                       && SUCCEEDED(m_list->Close())
                       && SUCCEEDED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)));
            }

            auto create(const DdsImage& image) -> ImTextureID override
            {
                if (m_device == nullptr || m_queue == nullptr)
                {
                    return 0;
                }

                D3D12_RESOURCE_DESC desc{};
                desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
                desc.Width = image.width;
                desc.Height = image.height;
                desc.DepthOrArraySize = 1;
                desc.MipLevels = 1;
                desc.Format = image.format;
                desc.SampleDesc.Count = 1;
                desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

                D3D12_HEAP_PROPERTIES default_heap{};
                default_heap.Type = D3D12_HEAP_TYPE_DEFAULT;
                ID3D12Resource* texture{};
                if (FAILED(m_device->CreateCommittedResource(&default_heap, D3D12_HEAP_FLAG_NONE, &desc,
                                                             D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                             IID_PPV_ARGS(&texture))))
                {
                    return 0;
                }

                D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
                UINT rows{};
                UINT64 row_size{};
                UINT64 total{};
                m_device->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, &rows, &row_size, &total);

                D3D12_HEAP_PROPERTIES upload_heap{};
                upload_heap.Type = D3D12_HEAP_TYPE_UPLOAD;
                D3D12_RESOURCE_DESC buffer{};
                buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
                buffer.Width = total;
                buffer.Height = 1;
                buffer.DepthOrArraySize = 1;
                buffer.MipLevels = 1;
                buffer.Format = DXGI_FORMAT_UNKNOWN;
                buffer.SampleDesc.Count = 1;
                buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

                ID3D12Resource* upload{};
                if (FAILED(m_device->CreateCommittedResource(&upload_heap, D3D12_HEAP_FLAG_NONE, &buffer,
                                                             D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                             IID_PPV_ARGS(&upload))))
                {
                    texture->Release();
                    return 0;
                }

                // a linha na GPU e alinhada em 256 bytes, a do arquivo nao
                uint8_t* mapped{};
                const D3D12_RANGE nothing{0, 0};
                upload->Map(0, &nothing, reinterpret_cast<void**>(&mapped));
                for (uint32_t row = 0; row < rows; ++row)
                {
                    std::memcpy(mapped + footprint.Offset + static_cast<std::size_t>(row) * footprint.Footprint.RowPitch,
                                image.pixels + static_cast<std::size_t>(row) * image.row_pitch(),
                                static_cast<std::size_t>(row_size));
                }
                upload->Unmap(0, nullptr);

                m_allocator->Reset();
                m_list->Reset(m_allocator, nullptr);

                D3D12_TEXTURE_COPY_LOCATION destination{};
                destination.pResource = texture;
                destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                destination.SubresourceIndex = 0;
                D3D12_TEXTURE_COPY_LOCATION source{};
                source.pResource = upload;
                source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
                source.PlacedFootprint = footprint;
                m_list->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);

                D3D12_RESOURCE_BARRIER barrier{};
                barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                barrier.Transition.pResource = texture;
                barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
                barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
                m_list->ResourceBarrier(1, &barrier);
                m_list->Close();

                ID3D12CommandList* lists[] = {m_list};
                m_queue->ExecuteCommandLists(1, lists);
                m_queue->Signal(m_fence, ++m_fence_value);
                while (m_fence->GetCompletedValue() < m_fence_value)
                {
                    Sleep(0);
                }
                upload->Release();

                D3D12_CPU_DESCRIPTOR_HANDLE cpu{};
                D3D12_GPU_DESCRIPTOR_HANDLE gpu{};
                if (!m_heap->allocate(&cpu, &gpu))
                {
                    texture->Release();
                    return 0;
                }

                D3D12_SHADER_RESOURCE_VIEW_DESC view{};
                view.Format = image.format;
                view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
                view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                view.Texture2D.MipLevels = 1;
                m_device->CreateShaderResourceView(texture, &view, cpu);

                m_textures.push_back({texture, cpu});
                return static_cast<ImTextureID>(gpu.ptr);
            }

            auto release_all() -> void override
            {
                for (auto& entry : m_textures)
                {
                    m_heap->free(entry.descriptor);
                    entry.resource->Release();
                }
                m_textures.clear();
                if (m_list) { m_list->Release(); m_list = nullptr; }
                if (m_allocator) { m_allocator->Release(); m_allocator = nullptr; }
                if (m_fence) { m_fence->Release(); m_fence = nullptr; }
                m_device = nullptr;
                m_queue = nullptr;
            }

          private:
            struct Entry
            {
                ID3D12Resource* resource;
                D3D12_CPU_DESCRIPTOR_HANDLE descriptor;
            };

            ID3D12Device* m_device{};
            ID3D12CommandQueue* m_queue{};
            DescriptorHeap* m_heap{};
            ID3D12CommandAllocator* m_allocator{};
            ID3D12GraphicsCommandList* m_list{};
            ID3D12Fence* m_fence{};
            UINT64 m_fence_value{};
            std::vector<Entry> m_textures{};
        };

        class D3D12Renderer final : public IRenderer
        {
          public:
            explicit D3D12Renderer(ID3D12Device* device) : m_device(device)
            {
                m_device->AddRef();
            }

            ~D3D12Renderer() override
            {
                shutdown();
            }

            auto init(IDXGISwapChain* swapchain, HWND window) -> bool override
            {
                m_queue = g_queue.load();
                if (m_queue == nullptr)
                {
                    // sem a fila do jogo nao da para enviar nada; o hook de
                    // ExecuteCommandLists ainda pode nao ter disparado
                    return false;
                }

                DXGI_SWAP_CHAIN_DESC desc{};
                swapchain->GetDesc(&desc);
                m_buffer_count = desc.BufferCount;
                m_format = desc.BufferDesc.Format;

                if (!m_srv_heap.create(m_device))
                {
                    log_error("could not create the descriptor heap");
                    return false;
                }

                D3D12_DESCRIPTOR_HEAP_DESC rtv_desc{};
                rtv_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
                rtv_desc.NumDescriptors = m_buffer_count;
                if (FAILED(m_device->CreateDescriptorHeap(&rtv_desc, IID_PPV_ARGS(&m_rtv_heap))))
                {
                    log_error("could not create the render target heap");
                    return false;
                }
                m_rtv_step = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

                m_frames.resize(m_buffer_count);
                for (auto& frame : m_frames)
                {
                    if (FAILED(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                                IID_PPV_ARGS(&frame.allocator))))
                    {
                        log_error("could not create the command allocators");
                        return false;
                    }
                }
                if (FAILED(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                       m_frames[0].allocator, nullptr,
                                                       IID_PPV_ARGS(&m_list)))
                    || FAILED(m_list->Close())
                    || FAILED(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence))))
                {
                    log_error("could not create the command list");
                    return false;
                }

                if (!m_textures.setup(m_device, m_queue, &m_srv_heap))
                {
                    log_error("could not prepare texture uploads");
                    return false;
                }

                ImGui_ImplDX12_InitInfo info{};
                info.Device = m_device;
                info.CommandQueue = m_queue;
                info.NumFramesInFlight = static_cast<int>(m_buffer_count);
                info.RTVFormat = m_format;
                info.SrvDescriptorHeap = m_srv_heap.get();
                info.UserData = &m_srv_heap;
                info.SrvDescriptorAllocFn = [](ImGui_ImplDX12_InitInfo* init,
                                               D3D12_CPU_DESCRIPTOR_HANDLE* cpu,
                                               D3D12_GPU_DESCRIPTOR_HANDLE* gpu) {
                    static_cast<DescriptorHeap*>(init->UserData)->allocate(cpu, gpu);
                };
                info.SrvDescriptorFreeFn = [](ImGui_ImplDX12_InitInfo* init,
                                              D3D12_CPU_DESCRIPTOR_HANDLE cpu,
                                              D3D12_GPU_DESCRIPTOR_HANDLE) {
                    static_cast<DescriptorHeap*>(init->UserData)->free(cpu);
                };
                if (!ImGui_ImplDX12_Init(&info))
                {
                    log_error("ImGui_ImplDX12_Init failed");
                    return false;
                }

                create_render_targets(swapchain);
                log_info("overlay ready (DX12)");
                return true;
            }

            auto shutdown() -> void override
            {
                if (m_initialized)
                {
                    ImGui_ImplDX12_Shutdown();
                    m_initialized = false;
                }
                m_textures.release_all();
                release_targets();
                for (auto& frame : m_frames)
                {
                    if (frame.allocator) frame.allocator->Release();
                }
                m_frames.clear();
                if (m_list) { m_list->Release(); m_list = nullptr; }
                if (m_fence) { m_fence->Release(); m_fence = nullptr; }
                if (m_rtv_heap) { m_rtv_heap->Release(); m_rtv_heap = nullptr; }
                m_srv_heap.release();
                if (m_device) { m_device->Release(); m_device = nullptr; }
            }

            auto new_frame() -> void override
            {
                ImGui_ImplDX12_NewFrame();
                m_initialized = true;
            }

            auto render(IDXGISwapChain* swapchain) -> void override
            {
                IDXGISwapChain3* swapchain3{};
                if (FAILED(swapchain->QueryInterface(IID_PPV_ARGS(&swapchain3))))
                {
                    return;
                }
                const UINT index = swapchain3->GetCurrentBackBufferIndex();
                swapchain3->Release();

                if (m_frames.empty() || index >= m_frames.size())
                {
                    return;
                }
                if (m_frames[index].target == nullptr)
                {
                    create_render_targets(swapchain);
                    if (m_frames[index].target == nullptr)
                    {
                        return;
                    }
                }

                // espera o frame anterior que usou este alocador terminar
                Frame& frame = m_frames[index];
                if (frame.fence_value != 0 && m_fence->GetCompletedValue() < frame.fence_value)
                {
                    m_fence->SetEventOnCompletion(frame.fence_value, m_wait_event);
                    WaitForSingleObject(m_wait_event, 200);
                }

                frame.allocator->Reset();
                m_list->Reset(frame.allocator, nullptr);

                D3D12_RESOURCE_BARRIER barrier{};
                barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                barrier.Transition.pResource = frame.target;
                barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
                barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
                m_list->ResourceBarrier(1, &barrier);

                m_list->OMSetRenderTargets(1, &frame.descriptor, FALSE, nullptr);
                ID3D12DescriptorHeap* heaps[] = {m_srv_heap.get()};
                m_list->SetDescriptorHeaps(1, heaps);
                ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), m_list);

                std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
                m_list->ResourceBarrier(1, &barrier);
                m_list->Close();

                ID3D12CommandList* lists[] = {m_list};
                m_queue->ExecuteCommandLists(1, lists);
                m_queue->Signal(m_fence, ++m_fence_value);
                frame.fence_value = m_fence_value;
            }

            auto release_targets() -> void override
            {
                for (auto& frame : m_frames)
                {
                    if (frame.target)
                    {
                        frame.target->Release();
                        frame.target = nullptr;
                    }
                }
            }

            auto textures() -> ITextureBackend& override
            {
                return m_textures;
            }

          private:
            struct Frame
            {
                ID3D12CommandAllocator* allocator{};
                ID3D12Resource* target{};
                D3D12_CPU_DESCRIPTOR_HANDLE descriptor{};
                UINT64 fence_value{};
            };

            auto create_render_targets(IDXGISwapChain* swapchain) -> void
            {
                const D3D12_CPU_DESCRIPTOR_HANDLE start = m_rtv_heap->GetCPUDescriptorHandleForHeapStart();
                for (UINT i = 0; i < m_buffer_count && i < m_frames.size(); ++i)
                {
                    ID3D12Resource* buffer{};
                    if (FAILED(swapchain->GetBuffer(i, IID_PPV_ARGS(&buffer))) || buffer == nullptr)
                    {
                        continue;
                    }
                    m_frames[i].descriptor.ptr = start.ptr + static_cast<SIZE_T>(i) * m_rtv_step;
                    m_device->CreateRenderTargetView(buffer, nullptr, m_frames[i].descriptor);
                    m_frames[i].target = buffer;   // guarda a referencia
                }
            }

            ID3D12Device* m_device{};
            ID3D12CommandQueue* m_queue{};
            ID3D12DescriptorHeap* m_rtv_heap{};
            UINT m_rtv_step{};
            UINT m_buffer_count{};
            DXGI_FORMAT m_format{};
            DescriptorHeap m_srv_heap{};
            D3D12TextureBackend m_textures{};
            std::vector<Frame> m_frames{};
            ID3D12GraphicsCommandList* m_list{};
            ID3D12Fence* m_fence{};
            UINT64 m_fence_value{};
            HANDLE m_wait_event{CreateEventW(nullptr, FALSE, FALSE, nullptr)};
            bool m_initialized{};
        };
    } // namespace

    auto d3d12_captured_queue() -> ID3D12CommandQueue*
    {
        return g_queue.load();
    }

    auto d3d12_capture_queue(ID3D12CommandQueue* queue) -> void
    {
        ID3D12CommandQueue* expected = nullptr;
        g_queue.compare_exchange_strong(expected, queue);
    }

    auto make_d3d12_renderer(ID3D12Device* device) -> std::unique_ptr<IRenderer>
    {
        return std::make_unique<D3D12Renderer>(device);
    }
} // namespace palbreed
