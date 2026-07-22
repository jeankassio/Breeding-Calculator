#pragma once
//
// Shim: o GUI/GUI.hpp real do UE4SS puxa LiveView.hpp -> <Unreal/...>, headers
// que vem do submodulo UEPseudo (repositorio privado desde 2025). Nada disso e
// necessario aqui: da cadeia de includes so usamos GUITab, que precisa apenas
// de StringType e de um ponteiro de funcao.
//
// Este arquivo entra ANTES do include do UE4SS na lista de diretorios, entao
// substitui o original. Se algum dia o mod precisar de LiveView/Console, este
// shim tem que sair e a SDK real tem que entrar.
//
#include <File/Macros.hpp>
#include <String/StringType.hpp>
