#pragma once

#include <atomic>
#include <chrono>
#include <string>
#include <vector>

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

  struct LetterChunk {
    std::string text;
    size_t revealed = 0;
    bool unlocked = false;
  };

  void LoadLetter();
  void UpdateLetterReveal();
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
  int dashboard_selected_ = 0;

  std::vector<LetterChunk> letter_chunks_;
  int last_unlocked_ = 0;
  std::chrono::steady_clock::time_point last_reveal_tick_;

  std::vector<std::string> menu_items_;
  std::vector<std::string> menu_descriptions_;
  int menu_selected_ = 0;

  bool running_ = true;
  bool audio_requested_ = true;
};

}  // namespace vday
