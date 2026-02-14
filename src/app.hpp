#pragma once

#include <atomic>
#include <array>
#include <chrono>
#include <cstdint>
#include <random>
#include <string>
#include <vector>

#include <ftxui/screen/color.hpp>

#include "audio.hpp"
#include "game.hpp"
#include "persistence.hpp"

namespace vday {

class App {
 public:
  App();
  void Run();

 private:
  enum class Screen {
    Dashboard,
    Game,
    Letter,
    Menu,
    Settings,
    Quit,
  };

  enum class DashboardAction {
    StartGame,
    Letter,
    Menu,
    ResetProgress,
    Settings,
    Quit,
  };

  struct LetterChunk {
    std::string text;
  };

  bool IsGameCompleted() const;
  void RefreshDashboardItems();
  void ApplyProgressToLetterState();
  void ResetProgress();
  void LoadLetter();
  void LoadMission();
  void LoadAsciiArt();
  void UpdateLetterReveal(int score, bool reveal_all);
  void RefreshScrambleState(bool reveal_all);
  void UpdateAsciiReveal(int score, bool reveal_all);
  void RefreshAsciiScrambleState(bool reveal_all);
  void OnUnlock(int count);
  void DrainGameEvents();
  void DrainAudioCommands();
  void PushAudioEnabled(bool enabled);

  GameEngine game_;
  AudioEngine audio_;
  Persistence persistence_;
  ProgressData progress_;

  Screen screen_ = Screen::Dashboard;
  std::vector<std::string> dashboard_items_;
  std::vector<DashboardAction> dashboard_actions_;
  int dashboard_selected_ = 0;
  bool reset_confirm_pending_ = false;

  std::vector<LetterChunk> letter_chunks_;
  std::string letter_content_;
  std::vector<bool> revealed_letters_;
  std::vector<bool> revealed_color_assigned_;
  std::vector<ftxui::Color> revealed_colors_;
  std::vector<std::string> scrambled_letters_;
  std::vector<ftxui::Color> scrambled_colors_;
  int reveal_threshold_score_ = 100;
  int revealed_target_ = 0;
  int last_unlocked_ = 0;
  std::chrono::steady_clock::time_point last_reveal_tick_;
  std::chrono::steady_clock::time_point last_scramble_tick_;
  int scramble_interval_ms_ = 70;
  std::mt19937 letter_rng_;

  std::vector<std::string> menu_items_;
  std::vector<std::string> menu_descriptions_;
  std::string mission_content_;
  int menu_selected_ = 0;

  std::vector<std::string> ascii_base_glyphs_;
  int ascii_art_width_ = 0;
  int ascii_art_height_ = 0;
  std::vector<bool> ascii_revealed_;
  std::vector<bool> ascii_color_assigned_;
  std::vector<ftxui::Color> ascii_base_fg_colors_;
  std::vector<ftxui::Color> ascii_base_bg_colors_;
  std::vector<std::array<uint8_t, 3>> ascii_base_fg_rgb_;
  std::vector<std::array<uint8_t, 3>> ascii_base_bg_rgb_;
  std::vector<ftxui::Color> ascii_revealed_colors_;
  std::vector<ftxui::Color> ascii_revealed_bg_colors_;
  std::vector<std::string> ascii_scrambled_chars_;
  std::vector<ftxui::Color> ascii_scrambled_colors_;
  std::vector<ftxui::Color> ascii_scrambled_bg_colors_;
  int ascii_revealed_target_ = 0;
  std::chrono::steady_clock::time_point last_ascii_reveal_tick_;
  std::chrono::steady_clock::time_point last_ascii_scramble_tick_;
  int ascii_scramble_interval_ms_ = 70;

  bool running_ = true;
  bool audio_requested_ = true;
};

}  // namespace vday
