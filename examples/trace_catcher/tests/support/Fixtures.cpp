#include "Fixtures.h"

#include <cstdlib>
#include <fstream>

namespace tracecore_test {

std::string Json::str(const std::string& k, const std::string& def) const {
  auto it = f.find(k);
  return it == f.end() || it->second.kind != JsonValue::Str ? def : it->second.s;
}
long long Json::integer(const std::string& k, long long def) const {
  auto it = f.find(k);
  return it == f.end() || it->second.kind != JsonValue::Int ? def : it->second.i;
}
double Json::num(const std::string& k, double def) const {
  auto it = f.find(k);
  if (it == f.end()) return def;
  if (it->second.kind == JsonValue::Int) return (double)it->second.i;
  return it->second.kind == JsonValue::Num ? it->second.d : def;
}
bool Json::boolean(const std::string& k, bool def) const {
  auto it = f.find(k);
  return it == f.end() || it->second.kind != JsonValue::Bool ? def : it->second.b;
}

namespace {

struct P {
  const std::string& t;
  size_t i = 0;
  void ws() { while (i < t.size() && (t[i] == ' ' || t[i] == '\t')) i++; }
  bool eat(char c) { ws(); if (i < t.size() && t[i] == c) { i++; return true; } return false; }

  bool string(std::string& out, std::string& err) {
    if (!eat('"')) { err = "se esperaba '\"'"; return false; }
    out.clear();
    while (i < t.size() && t[i] != '"') {
      char c = t[i++];
      if (c == '\\' && i < t.size()) {
        const char e = t[i++];
        c = e == 'n' ? '\n' : e == 't' ? '\t' : e;  // \" \\ \/ y compañía
      }
      out += c;
    }
    if (i >= t.size()) { err = "cadena sin cerrar"; return false; }
    i++;
    return true;
  }

  bool value(JsonValue& v, std::string& err) {
    ws();
    if (i >= t.size()) { err = "valor ausente"; return false; }
    if (t[i] == '"') { v.kind = JsonValue::Str; return string(v.s, err); }
    if (t.compare(i, 4, "true") == 0) { v.kind = JsonValue::Bool; v.b = true; i += 4; return true; }
    if (t.compare(i, 5, "false") == 0) { v.kind = JsonValue::Bool; v.b = false; i += 5; return true; }
    if (t[i] == '{' || t[i] == '[' || t.compare(i, 4, "null") == 0) { err = "solo se admiten valores planos"; return false; }
    const char* start = t.c_str() + i;
    char* end = nullptr;
    const double d = std::strtod(start, &end);
    if (end == start) { err = "valor no reconocido"; return false; }
    const std::string lit(start, (const char*)end);
    const bool isInt = lit.find_first_of(".eE") == std::string::npos;
    v.kind = isInt ? JsonValue::Int : JsonValue::Num;
    v.d = d;
    v.i = isInt ? std::strtoll(lit.c_str(), nullptr, 10) : (long long)d;
    i += lit.size();
    return true;
  }
};

}  // namespace

bool parseJson(const std::string& text, Json& out, std::string& err) {
  out.f.clear();
  P p{text};
  if (!p.eat('{')) { err = "se esperaba '{'"; return false; }
  if (p.eat('}')) return true;
  for (;;) {
    std::string key;
    if (!p.string(key, err)) return false;
    if (!p.eat(':')) { err = "se esperaba ':'"; return false; }
    JsonValue v;
    if (!p.value(v, err)) return false;
    out.f[key] = v;
    if (p.eat(',')) continue;
    if (p.eat('}')) break;
    err = "se esperaba ',' o '}'";
    return false;
  }
  p.ws();
  if (p.i != text.size()) { err = "texto tras el objeto"; return false; }
  return true;
}

bool readFixtureFile(const std::string& path, std::vector<Entry>& out, std::vector<std::string>& errors) {
  std::ifstream f(path);
  if (!f) { errors.push_back(path + ": no se puede abrir"); return false; }
  std::string line;
  int n = 0;
  const size_t errors_before = errors.size();
  while (std::getline(f, line)) {
    n++;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    size_t a = line.find_first_not_of(" \t");
    // Comentario: '#' solo o seguido de espacio. `#HELLO`, `#OK`, `#WARN` son líneas del protocolo, no comentarios.
    const bool comment = a != std::string::npos && line[a] == '#' && (a + 1 == line.size() || line[a + 1] == ' ' || line[a + 1] == '\t');
    if (a == std::string::npos || comment) continue;
    const std::string where = path + ":" + std::to_string(n);
    const size_t tab = line.find('\t');
    if (tab == std::string::npos || line.find_first_not_of(" \t", tab) == std::string::npos) {
      errors.push_back(where + ": fixture sin expectativa. Anótalo con un TAB y un JSON, p. ej.: "
                       "<HEX><TAB>{\"kind\":\"echo\",\"tag\":\"78563412\",\"hash\":\"A1\",\"snr_up_q\":-25}");
      continue;
    }
    Entry e;
    e.payload = line.substr(0, tab);
    e.where = where;
    std::string err;
    if (!parseJson(line.substr(tab + 1), e.json, err)) {
      errors.push_back(where + ": JSON inválido: " + err);
      continue;
    }
    out.push_back(std::move(e));
  }
  return errors.size() == errors_before;
}

}  // namespace tracecore_test
