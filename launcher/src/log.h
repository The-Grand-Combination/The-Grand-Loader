#pragma once

#include <string>
#include <string_view>

namespace tgl::log {

void open(std::wstring_view path);
void line(std::string_view prefix, std::string_view message);
void linef(std::string_view prefix, _Printf_format_string_ const char* fmt, ...);

// UTF-8 of a wide string, for log lines.
std::string narrow(const std::wstring& s);

}  // namespace tgl::log
