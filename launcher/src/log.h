#pragma once

#include <string_view>

namespace v2cp::log {

void open(std::wstring_view path);
void line(std::string_view prefix, std::string_view message);
void linef(std::string_view prefix, const char* fmt, ...);

}  // namespace v2cp::log
