#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace tracecore_test {

// Hex en mayúsculas, sin separadores.
std::string toHex(const uint8_t* b, size_t n);
inline std::string toHex(const std::vector<uint8_t>& v) { return toHex(v.data(), v.size()); }

// Acepta mayúsculas y minúsculas. false si la longitud es impar o hay un carácter no hex.
bool fromHex(const std::string& s, std::vector<uint8_t>& out);

}  // namespace tracecore_test
