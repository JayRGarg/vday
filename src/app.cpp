#include "app.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/canvas.hpp>
#include <ftxui/dom/elements.hpp>

namespace vday {

namespace {

std::vector<std::string> SplitParagraphs(const std::string& text) {
  std::vector<std::string> chunks;
  std::string current;
  std::istringstream input(text);
  std::string line;
  bool last_blank = false;

  while (std::getline(input, line)) {
    if (line.empty()) {
      if (!current.empty()) {
        chunks.push_back(current);
        current.clear();
      }
      last_blank = true;
      continue;
    }
    if (last_blank && !current.empty()) {
      chunks.push_back(current);
      current.clear();
    }
    if (!current.empty()) {
      current += "\n";
    }
    current += line;
    last_blank = false;
  }
  if (!current.empty()) {
    chunks.push_back(current);
  }
  return chunks;
}

std::string Repeat(const std::string& value, int count) {
  std::string out;
  if (count <= 0) {
    return out;
  }
  out.reserve(static_cast<size_t>(count) * value.size());
  for (int i = 0; i < count; ++i) {
    out += value;
  }
  return out;
}

ftxui::Element RenderGameCanvas(const GameSnapshot& snapshot) {
  using namespace ftxui;
  Canvas canvas(snapshot.width + 2, snapshot.height + 2);

  const std::string top = "\xE2\x94\x8C" + Repeat("\xE2\x94\x80", snapshot.width) + "\xE2\x94\x90";  // ┌ ─ ┐
  const std::string bottom = "\xE2\x94\x94" + Repeat("\xE2\x94\x80", snapshot.width) + "\xE2\x94\x98";  // └ ─ ┘
  canvas.DrawText(0, 0, top);
  for (int y = 1; y <= snapshot.height; ++y) {
    canvas.DrawText(0, y, "\xE2\x94\x82");  // │
    canvas.DrawText(snapshot.width + 1, y, "\xE2\x94\x82");  // │
  }
  canvas.DrawText(0, snapshot.height + 1, bottom);

  for (const auto& note : snapshot.notes) {
    int y = static_cast<int>(note.y);
    if (y < 0 || y >= snapshot.height) {
      continue;
    }
    std::string symbol = "?";
    Color color = Color::White;
    switch (note.type) {
      case ItemType::Heart:
        symbol = "\xE2\x99\xA5";  // ♥
        color = Color::RedLight;
        break;
      case ItemType::LoveNote:
        symbol = "\xF0\x9F\x92\x8C";  // 💌
        color = Color::YellowLight;
        break;
      case ItemType::Kiss:
        symbol = "\xF0\x9F\x92\x8B";  // 💋
        color = Color::MagentaLight;
        break;
      case ItemType::BrokenHeart:
        symbol = "\xF0\x9F\x92\x94";  // 💔
        color = Color::GrayLight;
        break;
    }
    canvas.DrawText(1 + note.x, 1 + y, symbol, color);
  }

  int catcher_y = 1 + snapshot.height - 3;
  int catcher_x = 1 + snapshot.player_x;
  const std::string catcher = "\xE2\x96\x82\xE2\x96\x88\xE2\x96\x82";  // ▂█▂
  int start_x = catcher_x - 1;
  if (start_x < 1) {
    start_x = 1;
  }
  if (start_x > snapshot.width - 2) {
    start_x = snapshot.width - 2;
  }
  canvas.DrawText(start_x, catcher_y, catcher, Color::CyanLight);
  return ftxui::canvas(std::move(canvas));
}

}  // namespace

App::App() {
  dashboard_items_ = {"Start Game", "Letter", "Dinner Menu", "Settings", "Quit"};
  menu_items_ = {"Rose Petal Salad", "Crimson Risotto", "Heartfire Steak", "Velvet Tiramisu"};
  menu_descriptions_ = {
      "Arugula, strawberries, feta, toasted almonds, balsamic glaze.",
      "Beet-infused risotto with parmesan and herb oil.",
      "Char-grilled sirloin with smoked pepper butter.",
      "Coffee-soaked layers, cacao, and berry syrup.",
  };

  progress_ = persistence_.Load();
  audio_requested_ = progress_.settings.audio_enabled;
  last_unlocked_ = progress_.unlocked_chunks;

  LoadLetter();
}

void App::Run() {
  game_.Start();
  audio_.Start();
  PushAudioEnabled(audio_requested_);

  using namespace ftxui;
  auto screen = ScreenInteractive::Fullscreen();
  int tab_index = 0;
  auto set_screen = [&](Screen next) {
    screen_ = next;
    switch (screen_) {
      case Screen::Dashboard:
        tab_index = 0;
        break;
      case Screen::Game:
        tab_index = 1;
        break;
      case Screen::Letter:
        tab_index = 2;
        break;
      case Screen::Menu:
        tab_index = 3;
        break;
      case Screen::Settings:
        tab_index = 4;
        break;
      case Screen::Quit:
        tab_index = 0;
        break;
    }
  };

  auto dashboard_menu = Menu(&dashboard_items_, &dashboard_selected_);
  auto dashboard = Renderer(dashboard_menu, [&] {
    auto content = vbox({
        text("Valentine's Day Terminal") | bold | center,
        separator(),
        dashboard_menu->Render() | center,
        separator(),
        text("Progress: " + std::to_string(progress_.unlocked_chunks) + " chunks") | center,
        text("Best Score: " + std::to_string(progress_.best_score)) | center,
    });
    return content | border;
  });

  dashboard = CatchEvent(dashboard, [&](Event event) {
    if (event == Event::Return) {
      switch (dashboard_selected_) {
        case 0:
          set_screen(Screen::Game);
          game_.PushInput(InputAction::Reset);
          break;
        case 1:
          set_screen(Screen::Letter);
          break;
        case 2:
          set_screen(Screen::Menu);
          break;
        case 3:
          set_screen(Screen::Settings);
          break;
        case 4:
          set_screen(Screen::Quit);
          running_ = false;
          screen.Exit();
          break;
      }
      return true;
    }
    return false;
  });

  auto game_view = Renderer([&] {
    DrainGameEvents();
    DrainAudioCommands();
    auto snapshot = game_.Snapshot();
    progress_.best_score = std::max(progress_.best_score, snapshot.score);

    auto stats = hbox({
        text("Score: " + std::to_string(snapshot.score)),
        text("  Streak: " + std::to_string(snapshot.streak)),
        text("  Misses: " + std::to_string(snapshot.misses)),
        text("  Unlocked: " + std::to_string(progress_.unlocked_chunks)),
        snapshot.paused ? text("  [PAUSED]") | bold : text(""),
    });

    auto instructions = text("Arrows/A-D move  P pause  R reset  Esc back");
    return vbox({
               text("Falling Love Notes") | bold | center,
               separator(),
               RenderGameCanvas(snapshot) | center,
               separator(),
               stats | center,
               instructions | center,
           }) |
           border;
  });

  game_view = CatchEvent(game_view, [&](Event event) {
    if (event == Event::ArrowLeft || event == Event::Character('a')) {
      game_.PushInput(InputAction::MoveLeft);
      return true;
    }
    if (event == Event::ArrowRight || event == Event::Character('d')) {
      game_.PushInput(InputAction::MoveRight);
      return true;
    }
    if (event == Event::Character('p') || event == Event::Character('P')) {
      game_.PushInput(InputAction::TogglePause);
      return true;
    }
    if (event == Event::Character('r') || event == Event::Character('R')) {
      game_.PushInput(InputAction::Reset);
      return true;
    }
    if (event == Event::Escape) {
      set_screen(Screen::Dashboard);
      return true;
    }
    return false;
  });

  auto letter_view = Renderer([&] {
    UpdateLetterReveal();
    Elements blocks;
    for (size_t i = 0; i < letter_chunks_.size(); ++i) {
      const auto& chunk = letter_chunks_[i];
      if (static_cast<int>(i) < progress_.unlocked_chunks) {
        std::string visible = chunk.text.substr(0, std::min(chunk.revealed, chunk.text.size()));
        blocks.push_back(paragraph(visible));
      } else {
        blocks.push_back(paragraph("[Locked - play the game to reveal more]") | dim);
      }
    }

    auto content = vbox({
        text("Letter Reveal") | bold | center,
        separator(),
        vbox(std::move(blocks)) | frame | flex,
        separator(),
        text("Esc to return") | center,
    });
    return content | border;
  });

  letter_view = CatchEvent(letter_view, [&](Event event) {
    if (event == Event::Escape) {
      set_screen(Screen::Dashboard);
      return true;
    }
    return false;
  });

  auto dinner_menu = Menu(&menu_items_, &menu_selected_);
  auto menu_view = Renderer(dinner_menu, [&] {
    std::string description = menu_descriptions_[menu_selected_];
    auto content = hbox({
        vbox({
            text("Dinner Menu") | bold | center,
            separator(),
            dinner_menu->Render() | flex,
            separator(),
            text("Esc to return") | center,
        }) | border | size(ftxui::WIDTH, ftxui::LESS_THAN, 40),
        vbox({
            text("Description") | bold | center,
            separator(),
            paragraph(description) | flex,
        }) | border | flex,
    });
    return content;
  });

  menu_view = CatchEvent(menu_view, [&](Event event) {
    if (event == Event::Escape) {
      set_screen(Screen::Dashboard);
      return true;
    }
    return false;
  });

  auto audio_checkbox = Checkbox("Enable audio (SDL2_mixer)", &audio_requested_);
  auto settings_view = Renderer(audio_checkbox, [&] {
    if (audio_requested_ != progress_.settings.audio_enabled) {
      progress_.settings.audio_enabled = audio_requested_;
      PushAudioEnabled(audio_requested_);
    }
    auto content = vbox({
        text("Settings") | bold | center,
        separator(),
        audio_checkbox->Render() | center,
        separator(),
        text("Esc to return") | center,
    });
    return content | border;
  });

  settings_view = CatchEvent(settings_view, [&](Event event) {
    if (event == Event::Escape) {
      set_screen(Screen::Dashboard);
      return true;
    }
    return false;
  });

  auto tabs = Container::Tab({
      dashboard,
      game_view,
      letter_view,
      menu_view,
      settings_view,
  }, &tab_index);

  auto root = CatchEvent(tabs, [&](Event event) {
    if (screen_ == Screen::Game) {
      if (event == Event::ArrowLeft) {
        game_.PushInput(InputAction::MoveLeft);
        return true;
      }
      if (event == Event::ArrowRight) {
        game_.PushInput(InputAction::MoveRight);
        return true;
      }
      if (event.is_character()) {
        std::string c = event.character();
        if (c == "a" || c == "A") {
          game_.PushInput(InputAction::MoveLeft);
          return true;
        }
        if (c == "d" || c == "D") {
          game_.PushInput(InputAction::MoveRight);
          return true;
        }
        if (c == "p" || c == "P") {
          game_.PushInput(InputAction::TogglePause);
          return true;
        }
        if (c == "r" || c == "R") {
          game_.PushInput(InputAction::Reset);
          return true;
        }
      }
      if (event == Event::Escape) {
        set_screen(Screen::Dashboard);
        return true;
      }
    }
    if (event.is_character()) {
      std::string c = event.character();
      if (c == "q" || c == "Q") {
        running_ = false;
        screen.Exit();
        return true;
      }
    }
    return false;
  });

  auto root_renderer = Renderer(root, [&] {
    screen.RequestAnimationFrame();
    return root->Render();
  });

  screen.Loop(root_renderer);

  game_.Stop();
  audio_.Stop();
  persistence_.Save(progress_);
}

void App::LoadLetter() {
  std::filesystem::path path = std::filesystem::current_path() / "assets" / "letter.txt";
  std::ifstream file(path);
  std::string content;
  if (file.is_open()) {
    std::stringstream buffer;
    buffer << file.rdbuf();
    content = buffer.str();
  } else {
    content = "Dear You,\n\nThis is a placeholder letter.\n\nWith love,\nMe";
  }

  auto chunks = SplitParagraphs(content);
  letter_chunks_.clear();
  for (const auto& chunk : chunks) {
    letter_chunks_.push_back(LetterChunk{chunk, 0u, false});
  }
  progress_.unlocked_chunks = std::min<int>(progress_.unlocked_chunks, static_cast<int>(letter_chunks_.size()));
  last_unlocked_ = progress_.unlocked_chunks;
  last_reveal_tick_ = std::chrono::steady_clock::now();
  UpdateLetterReveal();
}

void App::UpdateLetterReveal() {
  auto now = std::chrono::steady_clock::now();
  if (now - last_reveal_tick_ < std::chrono::milliseconds(50)) {
    return;
  }
  last_reveal_tick_ = now;

  for (size_t i = 0; i < letter_chunks_.size(); ++i) {
    if (static_cast<int>(i) < progress_.unlocked_chunks) {
      auto& chunk = letter_chunks_[i];
      chunk.unlocked = true;
      chunk.revealed = std::min(chunk.text.size(), chunk.revealed + static_cast<size_t>(3));
    }
  }
}

void App::OnUnlock(int count) {
  int capped = std::min<int>(count, static_cast<int>(letter_chunks_.size()));
  progress_.unlocked_chunks = std::max(progress_.unlocked_chunks, capped);
}

void App::DrainGameEvents() {
  GameEvent event;
  while (game_.TryPopEvent(event)) {
    if (event.type == GameEventType::UnlockChunk) {
      OnUnlock(event.value);
    }
  }
}

void App::DrainAudioCommands() {
  AudioCommand command;
  while (game_.TryPopAudio(command)) {
    audio_.PushCommand(command);
  }
}

void App::PushAudioEnabled(bool enabled) {
  audio_.PushCommand(AudioCommand{AudioCommandType::SetEnabled, enabled});
}

}  // namespace vday
