#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace vday {

struct Settings {
  bool audio_enabled = true;
};

struct ProgressData {
  int unlocked_chunks = 0;
  int best_score = 0;
  Settings settings;
};

class Persistence {
 public:
  Persistence();

  ProgressData Load();
  void Save(const ProgressData& data);

 private:
  std::filesystem::path SavePath() const;
  static int ParseInt(const std::string& content, const std::string& key, int fallback);
  static bool ParseBool(const std::string& content, const std::string& key, bool fallback);
};

}  // namespace vday
