cmake_minimum_required(VERSION 3.22)
project(PalBreedCalc CXX)

# O UE4SS instalado e Release/MD; um mod compilado em outra configuracao de
# runtime quebra na hora de trocar objetos com ele.
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(UE4SS_SRC "${CMAKE_CURRENT_SOURCE_DIR}/../external/RE-UE4SS")
set(UE4SS_LIB "${CMAKE_CURRENT_SOURCE_DIR}/../external/lib/UE4SS.lib")
set(IMGUI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../external/imgui")

if(NOT EXISTS "${UE4SS_LIB}")
    message(FATAL_ERROR "UE4SS.lib ausente -- rode: python tools/make_import_lib.py")
endif()

add_library(PalBreedCalc SHARED
    src/main.cpp
    src/Log.cpp
    src/Language.cpp
    src/Breeding.cpp
    src/PalData.gen.cpp
    src/Overlay.cpp
    src/RendererD3D11.cpp
    src/RendererD3D12.cpp
    src/Textures.cpp
    src/Ui.cpp
    # ImGui proprio: contexto separado do UE4SS, renderizado no swapchain do jogo
    "${IMGUI_DIR}/imgui.cpp"
    "${IMGUI_DIR}/imgui_draw.cpp"
    "${IMGUI_DIR}/imgui_tables.cpp"
    "${IMGUI_DIR}/imgui_widgets.cpp"
    "${IMGUI_DIR}/backends/imgui_impl_win32.cpp"
    "${IMGUI_DIR}/backends/imgui_impl_dx11.cpp"
    "${IMGUI_DIR}/backends/imgui_impl_dx12.cpp"
)

target_include_directories(PalBreedCalc PRIVATE
    # o shim precisa vir antes dos includes reais do UE4SS
    "${CMAKE_CURRENT_SOURCE_DIR}/shim"
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${UE4SS_SRC}/UE4SS/include"
    "${UE4SS_SRC}/deps/first/String/include"
    "${UE4SS_SRC}/deps/first/File/include"
    "${UE4SS_SRC}/deps/first/Input/include"
    "${UE4SS_SRC}/deps/first/DynamicOutput/include"
    "${UE4SS_SRC}/deps/first/Helpers/include"
    "${UE4SS_SRC}/deps/first/Constructs/include"
    "${UE4SS_SRC}/deps/first/Function/include"
    "${IMGUI_DIR}"
    "${IMGUI_DIR}/backends"
    # fmt e usado pelos headers de log do UE4SS (DynamicOutput)
    "${CMAKE_CURRENT_SOURCE_DIR}/../external/fmt/include"
)

target_link_libraries(PalBreedCalc PRIVATE "${UE4SS_LIB}")

target_compile_definitions(PalBreedCalc PRIVATE
    UNICODE _UNICODE
    NOMINMAX WIN32_LEAN_AND_MEAN
    # o fmt do UE4SS e compilado dentro da DLL dele; aqui usamos header-only
    FMT_HEADER_ONLY
    _SILENCE_ALL_CXX17_DEPRECATION_WARNINGS
)

target_compile_options(PalBreedCalc PRIVATE /permissive- /W3 /EHsc /utf-8)

# Conferencia offline do motor (nao precisa do jogo): despeja todos os
# cruzamentos possiveis para tools/validate_cpp.py comparar com o Python.
add_executable(engine_dump tests/engine_dump.cpp src/Breeding.cpp src/PalData.gen.cpp)
target_include_directories(engine_dump PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_options(engine_dump PRIVATE /EHsc /utf-8)

# Preview da janela fora do jogo (screenshot), para ajustar o visual sem
# precisar abrir o Palworld a cada mudanca.
add_executable(preview
    tests/preview.cpp
    src/Ui.cpp src/Textures.cpp src/RendererD3D11.cpp src/Log.cpp src/Language.cpp
    src/Breeding.cpp src/PalData.gen.cpp
    "${IMGUI_DIR}/imgui.cpp"
    "${IMGUI_DIR}/imgui_draw.cpp"
    "${IMGUI_DIR}/imgui_tables.cpp"
    "${IMGUI_DIR}/imgui_widgets.cpp"
    "${IMGUI_DIR}/backends/imgui_impl_win32.cpp"
    "${IMGUI_DIR}/backends/imgui_impl_dx11.cpp"
)
target_include_directories(preview PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/shim"
    "${UE4SS_SRC}/UE4SS/include"
    "${UE4SS_SRC}/deps/first/String/include"
    "${UE4SS_SRC}/deps/first/File/include"
    "${UE4SS_SRC}/deps/first/DynamicOutput/include"
    "${UE4SS_SRC}/deps/first/Helpers/include"
    "${UE4SS_SRC}/deps/first/Constructs/include"
    "${UE4SS_SRC}/deps/first/Function/include"
    "${IMGUI_DIR}" "${IMGUI_DIR}/backends"
    "${CMAKE_CURRENT_SOURCE_DIR}/../external/fmt/include"
)
# PALBREED_STANDALONE: o preview roda sem a UE4SS.dll, entao o log vai para
# stdout em vez de chamar o Output do UE4SS (que exigiria a DLL carregada).
target_compile_definitions(preview PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN
                                           PALBREED_STANDALONE)
target_compile_options(preview PRIVATE /EHsc /utf-8)

# O UE4SS carrega Mods/<nome>/dlls/main.dll
set_target_properties(PalBreedCalc PROPERTIES
    OUTPUT_NAME "main"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../mod/PalBreedCalc/dlls"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_CURRENT_SOURCE_DIR}/../mod/PalBreedCalc/dlls"
)
