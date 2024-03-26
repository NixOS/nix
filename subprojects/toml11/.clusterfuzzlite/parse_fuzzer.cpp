
#include <toml.hpp>

#include <sstream>
#include <string>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  std::string s(reinterpret_cast<const char *>(data), size);
  std::istringstream iss(s);
  try {
    const auto ref = toml::parse(iss);
  } catch (...) {
  }

  return 0;
}
