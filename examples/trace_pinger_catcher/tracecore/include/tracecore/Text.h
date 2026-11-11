#pragma once
#include <cctype>
#include <string_view>

namespace tracecore {

// Comparación de palabras clave sin distinguir mayúsculas.
inline bool ieq(std::string_view a, std::string_view b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); i++)
    if (std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)b[i])) return false;
  return true;
}

}  // namespace tracecore
