#include "persistence.hpp"

#include <cstdlib>
#include <fstream>
#include <sstream>

namespace vday {

namespace {

std::filesystem::path LegacySavePath() {
  const char* home = std::getenv("HOME");
  std::filesystem::path base = home ? home : ".";
  return base / ".valentine_tui" / "progress.json";
}

std::filesystem::path PreferredSavePath() {
  const char* xdg_state_home = std::getenv("XDG_STATE_HOME");
  if (xdg_state_home && xdg_state_home[0] != '\0') {
    return std::filesystem::path(xdg_state_home) / "valentine_tui" / "progress.json";
  }

  const char* home = std::getenv("HOME");
  std::filesystem::path base = home ? home : ".";
  return base / ".local" / "state" / "valentine_tui" / "progress.json";
}

bool ReadWholeFile(const std::filesystem::path& path, std::string& out) {
  std::ifstream file(path);
  if (!file.is_open()) {
    return false;
  }
  std::stringstream buffer;
  buffer << file.rdbuf();
  out = buffer.str();
  return true;
}

}  // namespace

Persistence::Persistence() = default;

ProgressData Persistence::Load() {
  ProgressData data;
  std::string content;
  const std::filesystem::path path = SavePath();
  if (!ReadWholeFile(path, content)) {
    const std::filesystem::path legacy_path = LegacySavePath();
    if (!ReadWholeFile(legacy_path, content)) {
      return data;
    }
  }

  data.unlocked_chunks = ParseInt(content, "unlocked_chunks", data.unlocked_chunks);
  data.best_score = ParseInt(content, "best_score", data.best_score);
  data.settings.audio_enabled = ParseBool(content, "audio_enabled", data.settings.audio_enabled);

  if (!std::filesystem::exists(path)) {
    Save(data);
  }

  return data;
}

void Persistence::Save(const ProgressData& data) {
  std::filesystem::path path = SavePath();
  std::filesystem::create_directories(path.parent_path());

  std::ofstream file(path, std::ios::trunc);
  if (!file.is_open()) {
    return;
  }
  file << "{\n";
  file << "  \"unlocked_chunks\": " << data.unlocked_chunks << ",\n";
  file << "  \"best_score\": " << data.best_score << ",\n";
  file << "  \"settings\": {\n";
  file << "    \"audio_enabled\": " << (data.settings.audio_enabled ? "true" : "false") << "\n";
  file << "  }\n";
  file << "}\n";
}

std::filesystem::path Persistence::SavePath() const {
  return PreferredSavePath();
}

int Persistence::ParseInt(const std::string& content, const std::string& key, int fallback) {
  std::string pattern = "\"" + key + "\"";
  auto pos = content.find(pattern);
  if (pos == std::string::npos) {
    return fallback;
  }
  pos = content.find(':', pos);
  if (pos == std::string::npos) {
    return fallback;
  }
  pos += 1;
  while (pos < content.size() && (content[pos] == ' ' || content[pos] == '\t')) {
    pos++;
  }
  size_t end = pos;
  while (end < content.size() && (content[end] == '-' || (content[end] >= '0' && content[end] <= '9'))) {
    end++;
  }
  try {
    return std::stoi(content.substr(pos, end - pos));
  } catch (...) {
    return fallback;
  }
}

bool Persistence::ParseBool(const std::string& content, const std::string& key, bool fallback) {
  std::string pattern = "\"" + key + "\"";
  auto pos = content.find(pattern);
  if (pos == std::string::npos) {
    return fallback;
  }
  pos = content.find(':', pos);
  if (pos == std::string::npos) {
    return fallback;
  }
  pos += 1;
  while (pos < content.size() && (content[pos] == ' ' || content[pos] == '\t')) {
    pos++;
  }
  if (content.compare(pos, 4, "true") == 0) {
    return true;
  }
  if (content.compare(pos, 5, "false") == 0) {
    return false;
  }
  return fallback;
}

}  // namespace vday
