// =====================================================================
// PalBreedCalc - mod C++ do UE4SS
//
//   A tecla configurada em config.ini (F6 por padrao) abre/fecha a
//   calculadora de reproducao dentro do jogo.
//
// Este arquivo cuida so do ciclo de vida do mod e do atalho:
//   Overlay  -> hook do swapchain + ImGui (Overlay.cpp)
//   Ui       -> a janela (Ui.cpp)
//   Engine   -> a regra de reproducao (Breeding.cpp)
//   PalData  -> dados extraidos do jogo (PalData.gen.cpp)
//   Config   -> config.ini do usuario (Config.cpp)
// =====================================================================

#include <memory>

#include <Mod/CppUserModBase.hpp>
#include <Input/KeyDef.hpp>

#include "Breeding.hpp"
#include "Config.hpp"
#include "Log.hpp"
#include "Overlay.hpp"
#include "Ui.hpp"

using namespace RC;

class PalBreedCalcMod : public CppUserModBase
{
  public:
    PalBreedCalcMod() : CppUserModBase()
    {
        ModName = STR("PalBreedCalc");
        ModVersion = STR("1.1.0");
        ModDescription = STR("Calculadora de reproducao de Pals");
        ModAuthors = STR("Jean Kassio");

        m_ui = std::make_unique<palbreed::Ui>(m_engine);
        register_hotkey();
    }

    ~PalBreedCalcMod() override
    {
        // uninstall() para o desenho, espera as threads sairem dos hooks e
        // destroi o renderizador -- que ja libera as texturas que criou. Nao
        // da para chamar Ui::shutdown() aqui: ela pediria ao backend para
        // liberar de novo, e nesse ponto ele pode estar sendo usado pela
        // thread de render. A cache do Ui morre junto guardando so numeros.
        palbreed::Overlay::get().uninstall();
    }

    // O swapchain do jogo so existe depois que a engine subiu.
    auto on_unreal_init() -> void override
    {
        m_overlay_ready = palbreed::Overlay::get().install([this]() {
            bool open = true;
            m_ui->render(&open, palbreed::Overlay::get().textures(), nullptr,
                         palbreed::Overlay::get().renderer_generation());
            if (!open)
            {
                palbreed::Overlay::get().set_visible(false);
            }
        });

        const auto& cfg = palbreed::config();
        palbreed::log_info(m_overlay_ready
                               ? "ready -- press " + cfg.hotkey_name + " to open the calculator"
                               : std::string("overlay could not be installed"));
        if (m_overlay_ready && !palbreed::config_path().empty())
        {
            palbreed::log_info("hotkey and window size can be changed in " + palbreed::config_path());
        }
    }

  private:
    auto register_hotkey() -> void
    {
        const auto& cfg = palbreed::config();
        const auto key = static_cast<Input::Key>(cfg.hotkey_vk);

        const auto on_press = [this]() {
            if (!m_overlay_ready)
            {
                palbreed::log_error("overlay unavailable -- see the messages above");
                return;
            }
            // Quando a janela nao aparece, o log tem que dizer por que: antes
            // a tecla simplesmente ficava muda e o jogador achava que o mod
            // tinha quebrado.
            auto& overlay = palbreed::Overlay::get();
            if (overlay.status() != palbreed::OverlayStatus::Ready && !overlay.visible())
            {
                palbreed::log_error(overlay.status_text());
            }
            overlay.toggle();
        };

        // O UE4SS so dispara o callback quando os modificadores batem exatamente
        // com os registrados, entao Ctrl/Alt/Shift precisam ir na inscricao --
        // conferir GetAsyncKeyState dentro do callback nao funcionaria.
        if (cfg.ctrl || cfg.alt || cfg.shift)
        {
            Input::Handler::ModifierKeyArray modifiers{};
            std::size_t next{};
            if (cfg.ctrl) modifiers[next++] = Input::ModifierKey::CONTROL;
            if (cfg.alt) modifiers[next++] = Input::ModifierKey::ALT;
            if (cfg.shift) modifiers[next++] = Input::ModifierKey::SHIFT;
            register_keydown_event(key, modifiers, on_press);
        }
        else
        {
            register_keydown_event(key, on_press);
        }
    }

    palbreed::Engine m_engine{};
    std::unique_ptr<palbreed::Ui> m_ui{};
    bool m_overlay_ready{};
};

#define PALBREEDCALC_API __declspec(dllexport)
extern "C"
{
    PALBREEDCALC_API CppUserModBase* start_mod()
    {
        return new PalBreedCalcMod();
    }

    PALBREEDCALC_API void uninstall_mod(CppUserModBase* mod)
    {
        delete mod;
    }
}
