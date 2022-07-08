#ifndef TRANCE_SRC_COMMON_COMMON_H
#define TRANCE_SRC_COMMON_COMMON_H
#include <filesystem>
#include <string>

static const std::string DEFAULT_SESSION_PATH = "default.session";
static const std::string SYSTEM_CONFIG_PATH = "system.cfg";
static const std::string TRANCE_EXE_PATH = "trance.exe";
static const std::size_t MAXIMUM_STACK = 256;
static const uint32_t DEFAULT_BORDER = 2;

inline std::string get_system_config_path(const std::string& directory)
{
  return (std::filesystem::path{directory} / SYSTEM_CONFIG_PATH).string();
}

inline std::string get_trance_exe_path(const std::string& directory)
{
  return (std::filesystem::path{directory} / TRANCE_EXE_PATH).string();
}

inline std::string format_time(uint64_t seconds, bool precise)
{
  auto minutes = seconds / 60;
  seconds = seconds % 60;
  auto hours = minutes / 60;
  minutes = minutes % 60;

  std::string result;
  if (hours) {
    result += std::to_string(hours) + (precise ? ":" : "h");
  }
  if (precise) {
    result += (minutes < 10 ? "0" : "") + std::to_string(minutes) + ":" +
        (seconds < 10 ? "0" : "") + std::to_string(seconds);
  } else {
    result += (hours && minutes < 10 ? "0" : "") + std::to_string(minutes) + "m";
  }
  return result;
};

template <typename T>
inline void hash_combine(size_t& seed, const T& v)
{
  seed ^= std::hash<T>{}(v) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
}

#endif
