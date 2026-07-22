#pragma once
//
// Log do mod -> UE4SS.log / console do UE4SS.
//
#include <string_view>

namespace palbreed
{
    auto log_info(std::string_view message) -> void;
    auto log_error(std::string_view message) -> void;
} // namespace palbreed
