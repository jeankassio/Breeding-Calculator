// =====================================================================
// PalBreedCalc - mod C++ do UE4SS
//
//   F6 abre/fecha a calculadora de reproducao dentro do jogo.
//
// Este arquivo cuida so do ciclo de vida do mod e do atalho:
//   Overlay  -> hook do swapchain + ImGui (Overlay.cpp)
//   Ui       -> a janela (Ui.cpp)
//   Engine   -> a regra de reproducao (Breeding.cpp)
//   PalData  -> dados extraidos do jogo (PalData.gen.cpp)
// =====================================================================

#include <memory>

#include <Mod/CppUserModBase.hpp>
#include <Input/KeyDef.hpp>

#include "Breeding.hpp"
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
        ModVersion = STR("0.1.0");
        ModDescription = STR("Calculadora de reproducao de Pals (F6)");
        ModAuthors = STR("Jean Kassio");

        m_ui = std::make_unique<palbreed::Ui>(m_engine);

        register_keydown_event(Input::Key::F6, [this]() {
            if (!m_overlay_ready)
            {
                palbreed::log_error("overlay unavailable -- see the messages above");
                return;
            }
            palbreed::Overlay::get().toggle();
        });
    }

    ~PalBreedCalcMod() override
    {
        palbreed::Overlay::get().uninstall();
    }

    // O swapchain do jogo so existe depois que a engine subiu.
    auto on_unreal_init() -> void override
    {
        m_overlay_ready = palbreed::Overlay::get().install([this]() {
            bool open = true;
            m_ui->render(&open, palbreed::Overlay::get().textures());
            if (!open)
            {
                palbreed::Overlay::get().set_visible(false);
            }
        });

        palbreed::log_info(m_overlay_ready ? "ready -- press F6 to open the calculator"
                                           : "overlay could not be installed");
    }

  private:
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
