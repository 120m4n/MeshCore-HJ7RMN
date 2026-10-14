#pragma once
// Lector de fixtures `<payload>\t<json>` (spec 002, contracts/fixture-format.md) con un
// mini-parser JSON plano: un solo objeto por línea, con strings, enteros, números y bools.
// Se evita una librería JSON completa (Constitución VI).
#include <map>
#include <string>
#include <vector>

namespace tracecore_test {

struct JsonValue {
  enum Kind { Str, Int, Num, Bool } kind = Str;
  std::string s;
  long long i = 0;
  double d = 0;
  bool b = false;
};

struct Json {
  std::map<std::string, JsonValue> f;
  bool has(const std::string& k) const { return f.count(k) != 0; }
  std::string str(const std::string& k, const std::string& def = "") const;
  long long integer(const std::string& k, long long def = 0) const;
  double num(const std::string& k, double def = 0) const;
  bool boolean(const std::string& k, bool def = false) const;
};

// false y `err` con la causa si el texto no es un objeto plano válido.
bool parseJson(const std::string& text, Json& out, std::string& err);

struct Entry {
  std::string payload;  // hex (tramas) o línea de protocolo (líneas)
  Json json;
  std::string where;    // "archivo:línea", para mensajes
};

// Lee un archivo de fixtures. Ignora líneas vacías y las que empiezan con '#'.
// Una línea sin TAB+JSON, o con JSON inválido, se añade a `errors` (no se aceptan fixtures sin expectativa).
bool readFixtureFile(const std::string& path, std::vector<Entry>& out, std::vector<std::string>& errors);

}  // namespace tracecore_test
