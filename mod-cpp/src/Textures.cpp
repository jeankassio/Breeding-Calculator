#include "Textures.hpp"

#include <cstring>
#include <fstream>
#include <vector>

#include <Windows.h>

namespace palbreed
{
    namespace
    {
        struct DdsPixelFormat
        {
            uint32_t size, flags, four_cc, rgb_bit_count;
            uint32_t r_mask, g_mask, b_mask, a_mask;
        };

        struct DdsHeader
        {
            uint32_t size, flags, height, width, pitch_or_linear_size, depth, mip_count;
            uint32_t reserved[11];
            DdsPixelFormat pixel_format;
            uint32_t caps, caps2, caps3, caps4, reserved2;
        };

        constexpr auto four_cc(char a, char b, char c, char d) -> uint32_t
        {
            return static_cast<uint32_t>(a) | (static_cast<uint32_t>(b) << 8)
                   | (static_cast<uint32_t>(c) << 16) | (static_cast<uint32_t>(d) << 24);
        }

        // Endereco qualquer dentro desta DLL, para descobrir o caminho dela.
        auto module_anchor() -> void
        {
        }

        auto dll_icons_dir() -> std::string
        {
            HMODULE self{};
            GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
                                   | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               reinterpret_cast<LPCWSTR>(&module_anchor), &self);
            wchar_t path[MAX_PATH]{};
            GetModuleFileNameW(self, path, MAX_PATH);

            // Mods/PalBreedCalc/dlls/main.dll -> Mods/PalBreedCalc/icons/
            std::wstring dll_path(path);
            const auto dlls_dir = dll_path.find_last_of(L"\\/");
            const auto mod_dir = dll_path.find_last_of(L"\\/", dlls_dir - 1);
            const std::wstring icons = dll_path.substr(0, mod_dir + 1) + L"icons\\";

            const int needed =
                WideCharToMultiByte(CP_UTF8, 0, icons.c_str(), -1, nullptr, 0, nullptr, nullptr);
            std::string out(static_cast<std::size_t>(needed > 0 ? needed - 1 : 0), '\0');
            WideCharToMultiByte(CP_UTF8, 0, icons.c_str(), -1, out.data(), needed, nullptr, nullptr);
            return out;
        }

        struct Layout
        {
            DXGI_FORMAT format;
            uint32_t block_bytes;   // 0 = nao comprimido
        };

        auto layout_of(const DdsPixelFormat& pf, const uint8_t* dx10_header) -> Layout
        {
            if (pf.flags & 0x4)     // DDPF_FOURCC
            {
                switch (pf.four_cc)
                {
                case four_cc('D', 'X', 'T', '1'):
                    return {DXGI_FORMAT_BC1_UNORM, 8};
                case four_cc('D', 'X', 'T', '3'):
                    return {DXGI_FORMAT_BC2_UNORM, 16};
                case four_cc('D', 'X', 'T', '5'):
                    return {DXGI_FORMAT_BC3_UNORM, 16};
                case four_cc('B', 'C', '4', 'U'):
                    return {DXGI_FORMAT_BC4_UNORM, 8};
                case four_cc('B', 'C', '5', 'U'):
                    return {DXGI_FORMAT_BC5_UNORM, 16};
                case four_cc('D', 'X', '1', '0'):
                {
                    uint32_t dxgi{};
                    std::memcpy(&dxgi, dx10_header, sizeof(dxgi));
                    return {static_cast<DXGI_FORMAT>(dxgi), 16};   // BC6H/BC7
                }
                default:
                    return {DXGI_FORMAT_UNKNOWN, 0};
                }
            }
            // sem FourCC: BGRA de 32 bits (ver dds_header em extract_icons.py)
            return {DXGI_FORMAT_B8G8R8A8_UNORM, 0};
        }
    } // namespace

    auto TextureCache::init(ITextureBackend* backend, const char* icons_dir) -> void
    {
        m_backend = backend;
        if (icons_dir != nullptr && *icons_dir != '\0')
        {
            m_icons_dir = icons_dir;
            if (m_icons_dir.back() != '\\' && m_icons_dir.back() != '/')
            {
                m_icons_dir += '\\';
            }
        }
        else
        {
            m_icons_dir = dll_icons_dir();
        }
    }

    auto TextureCache::shutdown() -> void
    {
        if (m_backend)
        {
            m_backend->release_all();
        }
        m_textures.clear();
        m_backend = nullptr;
    }

    auto TextureCache::load(const std::string& key) -> ImTextureID
    {
        if (const auto cached = m_textures.find(key); cached != m_textures.end())
        {
            return cached->second;
        }
        m_textures[key] = 0;                 // memoriza a falha tambem
        if (m_backend == nullptr)
        {
            return 0;
        }

        std::ifstream file(m_icons_dir + key + ".dds", std::ios::binary);
        if (!file)
        {
            return 0;
        }
        const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)),
                                         std::istreambuf_iterator<char>());
        if (bytes.size() < 4 + sizeof(DdsHeader) || std::memcmp(bytes.data(), "DDS ", 4) != 0)
        {
            return 0;
        }

        DdsHeader header{};
        std::memcpy(&header, bytes.data() + 4, sizeof(header));
        std::size_t payload_offset = 4 + sizeof(DdsHeader);
        const Layout layout = layout_of(header.pixel_format, bytes.data() + payload_offset);
        if (header.pixel_format.four_cc == four_cc('D', 'X', '1', '0'))
        {
            payload_offset += 20;            // DDS_HEADER_DXT10
        }
        if (layout.format == DXGI_FORMAT_UNKNOWN || payload_offset >= bytes.size())
        {
            return 0;
        }

        DdsImage image{};
        image.width = header.width;
        image.height = header.height;
        image.format = layout.format;
        image.block_bytes = layout.block_bytes;
        image.pixels = bytes.data() + payload_offset;
        image.size = bytes.size() - payload_offset;

        const ImTextureID texture = m_backend->create(image);
        m_textures[key] = texture;
        return texture;
    }

    auto TextureCache::pal(const char* pal_id, const char* tribe) -> ImTextureID
    {
        if (pal_id == nullptr)
        {
            return 0;
        }
        if (const auto texture = load(std::string("pal_") + pal_id))
        {
            return texture;
        }
        // variantes sem icone proprio caem no icone da especie
        return tribe ? load(std::string("pal_") + tribe) : 0;
    }

    auto TextureCache::egg(const char* icon_name) -> ImTextureID
    {
        return icon_name ? load(std::string("egg_") + icon_name) : 0;
    }
} // namespace palbreed
