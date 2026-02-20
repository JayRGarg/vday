#include "app.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <stdexcept>
#include <vector>

#include <Magick++.h>

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

ftxui::Element RenderParagraphsWithBlankLines(const std::string& input) {
  using namespace ftxui;
  Elements lines;
  std::istringstream stream(input);
  std::string line;
  while (std::getline(stream, line)) {
    if (line.empty()) {
      lines.push_back(text(" "));
    } else {
      lines.push_back(paragraph(line));
    }
  }
  if (lines.empty()) {
    lines.push_back(text(" "));
  }
  return vbox(std::move(lines));
}

ftxui::Color RandomValentineColor(std::mt19937& rng) {
  struct ChannelRange {
    int r_min;
    int r_max;
    int g_min;
    int g_max;
    int b_min;
    int b_max;
  };

  // Weighted palette by repeating families: reds/pinks dominate, whites/blues/purples accent.
  static constexpr std::array<ChannelRange, 10> kRanges = {
      ChannelRange{200, 255, 45, 115, 70, 150},   // vivid red (brighter)
      ChannelRange{185, 245, 60, 130, 95, 170},   // rose red (brighter)
      ChannelRange{230, 255, 145, 220, 190, 245}, // bright pink (brighter)
      ChannelRange{218, 255, 125, 205, 170, 235}, // soft pink (brighter)
      ChannelRange{242, 255, 210, 242, 232, 255}, // blush tint (no neutral white)
      ChannelRange{236, 255, 196, 232, 226, 255}, // cool blush tint
      ChannelRange{95, 170, 145, 220, 210, 255},  // sky blue
      ChannelRange{120, 195, 170, 235, 220, 255}, // powder blue
      ChannelRange{145, 220, 90, 170, 190, 255},  // violet
      ChannelRange{160, 235, 105, 180, 210, 255}, // soft purple
  };

  std::uniform_int_distribution<size_t> family_dist(0, kRanges.size() - 1);
  auto looks_brown_or_dull = [](int r, int g, int b) {
    // Brown-like colors are typically warm, low-blue, and mid-luminance.
    const int lum = (2126 * r + 7152 * g + 722 * b) / 10000;
    const bool warm_order = r > g && g > b;
    const bool low_blue = b < 120;
    const bool mid_green = g >= 55 && g <= 185;
    const bool mid_lum = lum >= 45 && lum <= 190;
    const bool not_vivid_red = (r - g) <= 120;
    const bool brownish = warm_order && low_blue && mid_green && mid_lum && not_vivid_red;

    // Reject dark/bland warm tones (especially reds/pinks).
    const int max_c = std::max({r, g, b});
    const int min_c = std::min({r, g, b});
    const int chroma = max_c - min_c;
    const bool warm_family = r >= g && r >= b;
    const bool dark_or_bland_warm = warm_family && (lum < 118 || chroma < 60);

    // Remove near-neutral/gray colors in every family.
    const int rg = std::abs(r - g);
    const int rb = std::abs(r - b);
    const int gb = std::abs(g - b);
    const bool near_neutral = (rg < 26 && rb < 26 && gb < 26);
    const bool low_chroma_any = chroma < 48;
    return brownish || dark_or_bland_warm || near_neutral || low_chroma_any;
  };

  for (int attempt = 0; attempt < 8; ++attempt) {
    const ChannelRange& c = kRanges[family_dist(rng)];
    std::uniform_int_distribution<int> r_dist(c.r_min, c.r_max);
    std::uniform_int_distribution<int> g_dist(c.g_min, c.g_max);
    std::uniform_int_distribution<int> b_dist(c.b_min, c.b_max);
    const int r = r_dist(rng);
    const int g = g_dist(rng);
    const int b = b_dist(rng);
    if (!looks_brown_or_dull(r, g, b)) {
      return ftxui::Color::RGB(r, g, b);
    }
  }

  // Safety fallback: saturated pink (never brown).
  return ftxui::Color::RGB(245, 130, 205);
}

bool TryFindPhotoPath(std::filesystem::path& out) {
  const std::filesystem::path assets = std::filesystem::current_path() / "assets";
  const std::vector<std::string> preferred = {
      "photo.jpg", "photo.jpeg", "photo.png", "photo.webp", "photo.heic",
  };
  for (const auto& name : preferred) {
    std::filesystem::path candidate = assets / name;
    if (std::filesystem::exists(candidate)) {
      out = candidate;
      return true;
    }
  }
  for (const auto& entry : std::filesystem::directory_iterator(assets)) {
    if (!entry.is_regular_file()) {
      continue;
    }
    const std::string ext = entry.path().extension().string();
    if (ext == ".jpg" || ext == ".jpeg" || ext == ".png" || ext == ".webp" || ext == ".heic" ||
        ext == ".JPG" || ext == ".JPEG" || ext == ".PNG" || ext == ".WEBP" || ext == ".HEIC") {
      out = entry.path();
      return true;
    }
  }
  return false;
}

ftxui::Element RenderGameCanvas(const GameSnapshot& snapshot) {
  using namespace ftxui;
  constexpr int kCanvasCellWidth = 2;
  constexpr int kCanvasCellHeight = 4;
  auto cx = [&](int cell_x) { return cell_x * kCanvasCellWidth; };
  auto cy = [&](int cell_y) { return cell_y * kCanvasCellHeight; };

  Canvas canvas((snapshot.width + 2) * kCanvasCellWidth,
                (snapshot.height + 2) * kCanvasCellHeight);

  const std::string top = "\xE2\x94\x8C" + Repeat("\xE2\x94\x80", snapshot.width) + "\xE2\x94\x90";  // ┌ ─ ┐
  const std::string bottom = "\xE2\x94\x94" + Repeat("\xE2\x94\x80", snapshot.width) + "\xE2\x94\x98";  // └ ─ ┘
  canvas.DrawText(cx(0), cy(0), top);
  for (int y = 1; y <= snapshot.height; ++y) {
    canvas.DrawText(cx(0), cy(y), "\xE2\x94\x82");  // │
    canvas.DrawText(cx(snapshot.width + 1), cy(y), "\xE2\x94\x82");  // │
  }
  canvas.DrawText(cx(0), cy(snapshot.height + 1), bottom);

  for (const auto& note : snapshot.notes) {
    int y = static_cast<int>(note.y);
    if (y < 0 || y >= snapshot.height) {
      continue;
    }
    std::string symbol = "?";
    Color color = Color::White;
    switch (note.type) {
      case ItemType::Heart:
        symbol = "\xF0\x9F\x92\x96";  // 💖
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
    const int max_note_x = std::max(0, snapshot.width - ItemVisualWidth(note.type));
    const int clamped_note_x = std::clamp(note.x, 0, max_note_x);
    const int draw_x = 1 + clamped_note_x;
    canvas.DrawText(cx(draw_x), cy(1 + y), symbol, color);
  }

  int catcher_y = 1 + CatcherRow(snapshot.height);
  int start_x = 1 + CatcherStartColumn(snapshot.player_x, snapshot.width);
  const bool catcher_flash = snapshot.catcher_flash_frames > 0;
  const Color catcher_color = catcher_flash ? Color::YellowLight : Color::CyanLight;
  // Draw catcher as a single token to avoid terminal-specific per-cell artifacts.
  canvas.DrawText(cx(start_x), cy(catcher_y), "|___|", catcher_color);
  if (catcher_flash && catcher_y > 1) {
    const std::string sparkle = (snapshot.catcher_flash_frames % 2 == 0) ? " * " : " + ";
    canvas.DrawText(cx(start_x + 1), cy(catcher_y - 1), sparkle, Color::White);
  }
  return ftxui::canvas(std::move(canvas));
}

}  // namespace

App::App() {
  Magick::InitializeMagick(nullptr);
  menu_items_ = {"Mediterranean Salad", "Lovers Pasta", "Frozen Chocolate Fruit", "Sparkling Nature Drink"};
  menu_descriptions_ = {
      "Tomato, avocado, butter lettuce, and a Mediterranean spice blend.",
      "Heart-shaped pasta from Italy, the finest plant-based ground meat, vodka sauce, an Italian spice "
      "blend, and the finest cheese in the land.",
      "Frozen chocolate-covered fruit with a customized yogurt topping.",
      "The finest sparkling water in all the land with nature's freshest treats mixed in.",
  };

  progress_ = persistence_.Load();
  audio_requested_ = progress_.settings.audio_enabled;
  last_unlocked_ = progress_.unlocked_chunks;
  std::random_device rd;
  letter_rng_ = std::mt19937(rd());

  LoadLetter();
  LoadMission();
  LoadAsciiArt();
  ApplyProgressToLetterState();
  RefreshDashboardItems();
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

  auto neon_frame = [&] {
    using clock = std::chrono::steady_clock;
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        clock::now().time_since_epoch())
                        .count();
    const int phase = static_cast<int>((ms / 220) % 4);
    if (phase == 0) {
      return Color::RGB(255, 96, 128);  // hot pink
    }
    if (phase == 1) {
      return Color::RGB(255, 72, 96);  // valentine red
    }
    if (phase == 2) {
      return Color::RGB(214, 112, 255);  // soft purple
    }
    return Color::RGB(255, 224, 240);  // blush white
  };
  const auto hacker_purple = Color::RGB(188, 122, 255);
  const auto hacker_white = Color::RGB(230, 240, 255);
  const auto mission_red = Color::RGB(255, 112, 132);

  auto dashboard_menu = Menu(&dashboard_items_, &dashboard_selected_);
  auto dashboard = Renderer(dashboard_menu, [&] {
    RefreshDashboardItems();
    if (reset_confirm_pending_ &&
        (dashboard_selected_ < 0 || dashboard_selected_ >= static_cast<int>(dashboard_actions_.size()) ||
         dashboard_actions_[dashboard_selected_] != DashboardAction::ResetProgress)) {
      reset_confirm_pending_ = false;
    }
    int mission_lines = 1;
    mission_lines += static_cast<int>(std::count(mission_content_.begin(), mission_content_.end(), '\n'));
    const int mission_panel_height = std::max(9, mission_lines + 4);

    auto mission_panel = vbox({
                            text("[ MISSION: VALENTINE_MATRIX ]") | bold | center |
                                color(hacker_purple),
                            separator(),
                            RenderParagraphsWithBlankLines(mission_content_) | color(mission_red),
                        }) |
                        borderDouble | color(neon_frame()) |
                        size(ftxui::HEIGHT, ftxui::EQUAL, mission_panel_height);

    auto dashboard_panel = vbox({
                             dashboard_menu->Render() | center | color(hacker_white),
                             separator(),
                             text([&] {
                               const int total_chunks = std::max(1, static_cast<int>(letter_chunks_.size()));
                               const int unlocked =
                                   std::clamp(progress_.unlocked_chunks, 0, total_chunks);
                               const int pct = (unlocked * 100) / total_chunks;
                               return "Mission Progress: " + std::to_string(pct) + "%";
                             }()) |
                                 center | color(hacker_white),
                             reset_confirm_pending_
                                 ? text("Press Enter on Reset again to confirm") | center | bold
                                 : text(""),
                         }) |
                         borderDouble | color(neon_frame());
    return vbox({
        mission_panel,
        separator(),
        dashboard_panel | flex,
    });
  });

  dashboard = CatchEvent(dashboard, [&](Event event) {
    if (event == Event::Return) {
      RefreshDashboardItems();
      if (dashboard_actions_.empty() || dashboard_selected_ < 0 ||
          dashboard_selected_ >= static_cast<int>(dashboard_actions_.size())) {
        return true;
      }

      const DashboardAction action = dashboard_actions_[dashboard_selected_];
      if (action == DashboardAction::StartGame) {
        reset_confirm_pending_ = false;
        set_screen(Screen::Game);
        game_.PushInput(InputAction::Reset);
      } else if (action == DashboardAction::Letter) {
        reset_confirm_pending_ = false;
        set_screen(Screen::Letter);
      } else if (action == DashboardAction::Menu) {
        reset_confirm_pending_ = false;
        set_screen(Screen::Menu);
      } else if (action == DashboardAction::ResetProgress) {
        if (reset_confirm_pending_) {
          ResetProgress();
          reset_confirm_pending_ = false;
        } else {
          reset_confirm_pending_ = true;
        }
      } else if (action == DashboardAction::Settings) {
        reset_confirm_pending_ = false;
        set_screen(Screen::Settings);
      } else if (action == DashboardAction::Quit) {
        reset_confirm_pending_ = false;
        set_screen(Screen::Quit);
        running_ = false;
        screen.Exit();
      }
      return true;
    }
    return false;
  });

  auto render_letter_progress = [&](bool show_escape_hint, int score, bool reveal_all) {
    UpdateLetterReveal(score, reveal_all);
    RefreshScrambleState(reveal_all);
    auto build_colored_letter = [&](bool reveal_all_text) {
      Elements lines;
      Elements current_line;
      auto flush_line = [&] {
        if (current_line.empty()) {
          current_line.push_back(text(""));
        }
        lines.push_back(hbox(std::move(current_line)));
        current_line.clear();
      };

      for (size_t i = 0; i < letter_content_.size(); ++i) {
        const char ch = letter_content_[i];
        if (ch == '\n') {
          flush_line();
          continue;
        }

        const bool is_space = std::isspace(static_cast<unsigned char>(ch));
        std::string out = std::string(1, ch);
        const bool is_revealed =
            reveal_all_text || (i < revealed_letters_.size() && revealed_letters_[i]);
        if (!is_space && !is_revealed && i < scrambled_letters_.size() &&
            !scrambled_letters_[i].empty()) {
          out = scrambled_letters_[i];
        }

        auto cell = text(out);
        if (!is_space && is_revealed && i < revealed_color_assigned_.size() &&
            revealed_color_assigned_[i] && i < revealed_colors_.size()) {
          cell = cell | color(revealed_colors_[i]);
        } else if (!is_space && !is_revealed && i < scrambled_colors_.size()) {
          cell = cell | color(scrambled_colors_[i]);
        }
        current_line.push_back(cell);
      }
      flush_line();
      return vbox(std::move(lines));
    };

    Elements content = {
        text("[ DECODING LETTER STREAM ]") | bold | center | color(hacker_purple),
        separator(),
        build_colored_letter(reveal_all) | frame | flex,
    };
    if (show_escape_hint) {
      content.push_back(separator());
      content.push_back(text("Esc to return") | center | color(hacker_white));
    }
    return vbox(std::move(content)) | borderDouble | color(neon_frame());
  };

  auto render_ascii_progress = [&](int score, bool reveal_all) {
    UpdateAsciiReveal(score, reveal_all);
    RefreshAsciiScrambleState(reveal_all);

    static const std::vector<std::string> kPixelScrambleGlyphs = {
        "\xE2\x96\x88",  // █
        "\xE2\x96\x93",  // ▓
        "\xE2\x96\x92",  // ▒
        "\xE2\x96\x91",  // ░
        "\xE2\x96\x80",  // ▀
        "\xE2\x96\x84",  // ▄
    };
    auto hash_u32 = [](uint64_t x) -> uint32_t {
      x ^= x >> 33;
      x *= 0xff51afd7ed558ccdULL;
      x ^= x >> 33;
      x *= 0xc4ceb9fe1a85ec53ULL;
      x ^= x >> 33;
      return static_cast<uint32_t>(x & 0xffffffffULL);
    };
    auto blend_rgb = [](const std::array<uint8_t, 3>& a, const std::array<uint8_t, 3>& b,
                        float t) -> std::array<uint8_t, 3> {
      const float clamped_t = std::clamp(t, 0.0f, 1.0f);
      std::array<uint8_t, 3> out{};
      for (int i = 0; i < 3; ++i) {
        const float v = static_cast<float>(a[static_cast<size_t>(i)]) * (1.0f - clamped_t) +
                        static_cast<float>(b[static_cast<size_t>(i)]) * clamped_t;
        out[static_cast<size_t>(i)] = static_cast<uint8_t>(std::clamp(static_cast<int>(std::round(v)), 0, 255));
      }
      return out;
    };

    bool all_photo_revealed = true;
    if (!reveal_all) {
      for (size_t i = 0; i < ascii_revealed_.size(); ++i) {
        if (!ascii_revealed_[i]) {
          all_photo_revealed = false;
          break;
        }
      }
    }
    const bool animate_matte = !reveal_all && !all_photo_revealed;

    constexpr int kMattePaddingX = 24;
    constexpr int kMattePaddingY = 6;
    const int matte_w = ascii_art_width_ + kMattePaddingX * 2;
    const int matte_h = ascii_art_height_ + kMattePaddingY * 2;
    const auto tick = animate_matte
                          ? static_cast<uint64_t>(
                                std::chrono::duration_cast<std::chrono::milliseconds>(
                                    std::chrono::steady_clock::now().time_since_epoch())
                                    .count() /
                                std::max(1, ascii_scramble_interval_ms_))
                          : static_cast<uint64_t>(0);

    Elements lines;
    lines.reserve(static_cast<size_t>(matte_h));
    for (int my = 0; my < matte_h; ++my) {
      Elements row;
      row.reserve(static_cast<size_t>(matte_w));
      for (int mx = 0; mx < matte_w; ++mx) {
        const int x = mx - kMattePaddingX;
        const int y = my - kMattePaddingY;
        const bool in_photo = (x >= 0 && x < ascii_art_width_ && y >= 0 && y < ascii_art_height_);

        if (in_photo) {
          const size_t idx = static_cast<size_t>(y * ascii_art_width_ + x);
          const bool is_revealed = reveal_all || (idx < ascii_revealed_.size() && ascii_revealed_[idx]);
          const std::string base_glyph = (idx < ascii_base_glyphs_.size()) ? ascii_base_glyphs_[idx] : " ";
          const bool is_drawable = !base_glyph.empty();

          std::string out = base_glyph;
          if (is_drawable && !is_revealed && idx < ascii_scrambled_chars_.size() &&
              !ascii_scrambled_chars_[idx].empty()) {
            out = ascii_scrambled_chars_[idx];
          }

          auto cell = ftxui::text(out);
          if (is_drawable && is_revealed && idx < ascii_color_assigned_.size() &&
              ascii_color_assigned_[idx] && idx < ascii_revealed_colors_.size() &&
              idx < ascii_revealed_bg_colors_.size()) {
            cell = cell | ftxui::color(ascii_revealed_colors_[idx]) |
                   ftxui::bgcolor(ascii_revealed_bg_colors_[idx]);
          } else if (is_drawable && !is_revealed && idx < ascii_scrambled_colors_.size() &&
                     idx < ascii_scrambled_bg_colors_.size()) {
            cell = cell | ftxui::color(ascii_scrambled_colors_[idx]) |
                   ftxui::bgcolor(ascii_scrambled_bg_colors_[idx]);
          }
          row.push_back(cell);
          continue;
        }

        if (ascii_art_width_ <= 0 || ascii_art_height_ <= 0 || ascii_base_fg_rgb_.empty() ||
            ascii_base_bg_rgb_.empty()) {
          row.push_back(ftxui::text(" "));
          continue;
        }

        const int nearest_x = std::clamp(x, 0, ascii_art_width_ - 1);
        const int nearest_y = std::clamp(y, 0, ascii_art_height_ - 1);
        const size_t edge_idx = static_cast<size_t>(nearest_y * ascii_art_width_ + nearest_x);

        const int dx = (x < 0) ? -x : ((x >= ascii_art_width_) ? (x - ascii_art_width_ + 1) : 0);
        const int dy = (y < 0) ? -y : ((y >= ascii_art_height_) ? (y - ascii_art_height_ + 1) : 0);
        const int dist = std::max(dx, dy);
        // Keep edge-adjacent matte cells anchored to photo edge color, then fade outward.
        const int edge_dist = std::max(0, dist - 1);
        const int fade_range = std::max(1, std::max(kMattePaddingX, kMattePaddingY) - 1);
        const float t = static_cast<float>(edge_dist) / static_cast<float>(fade_range);

        const std::array<uint8_t, 3> dark_fg = {26, 20, 34};
        const std::array<uint8_t, 3> dark_bg = {12, 10, 18};
        const std::array<uint8_t, 3> sky_fg = {148, 204, 255};
        const std::array<uint8_t, 3> sky_bg = {78, 138, 210};
        std::array<uint8_t, 3> fg = blend_rgb(ascii_base_fg_rgb_[edge_idx], dark_fg, t);
        std::array<uint8_t, 3> bg = blend_rgb(ascii_base_bg_rgb_[edge_idx], dark_bg, t);
        // Keep the matte in a sky-blue family while preserving edge-driven variation.
        fg = blend_rgb(fg, sky_fg, 0.60f);
        bg = blend_rgb(bg, sky_bg, 0.65f);

        const uint64_t seed = (static_cast<uint64_t>(mx) << 32U) ^ static_cast<uint64_t>(my) ^
                              (tick * 0x9e3779b97f4a7c15ULL);
        const uint32_t h1 = hash_u32(seed);
        const uint32_t h2 = hash_u32(seed ^ 0xa0761d6478bd642fULL);
        const int jitter_fg = animate_matte ? (static_cast<int>(h1 % 19U) - 9) : 0;
        const int jitter_bg = animate_matte ? (static_cast<int>(h2 % 15U) - 7) : 0;
        for (int i = 0; i < 3; ++i) {
          fg[static_cast<size_t>(i)] = static_cast<uint8_t>(
              std::clamp(static_cast<int>(fg[static_cast<size_t>(i)]) + jitter_fg, 0, 255));
          bg[static_cast<size_t>(i)] = static_cast<uint8_t>(
              std::clamp(static_cast<int>(bg[static_cast<size_t>(i)]) + jitter_bg, 0, 255));
        }

        std::string glyph = "\xE2\x96\x92";  // ▒
        if (animate_matte) {
          glyph = kPixelScrambleGlyphs[static_cast<size_t>(h1 % static_cast<uint32_t>(kPixelScrambleGlyphs.size()))];
        } else {
          if (dist <= 2) {
            glyph = "\xE2\x96\x93";  // ▓
          } else if (dist <= 6) {
            glyph = "\xE2\x96\x92";  // ▒
          } else {
            glyph = "\xE2\x96\x91";  // ░
          }
        }
        row.push_back(ftxui::text(glyph) | ftxui::color(ftxui::Color::RGB(fg[0], fg[1], fg[2])) |
                      ftxui::bgcolor(ftxui::Color::RGB(bg[0], bg[1], bg[2])));
      }
      lines.push_back(ftxui::hbox(std::move(row)));
    }

    auto centered_art = ftxui::vbox({
        ftxui::filler(),
        ftxui::vbox(std::move(lines)) | ftxui::center,
        ftxui::filler(),
        ftxui::filler(),
    });

    return ftxui::vbox({
               ftxui::text("[ DECRYPTING PHOTO SIGNAL ]") | ftxui::bold | ftxui::center |
                   ftxui::color(hacker_purple),
               ftxui::separator(),
               centered_art | ftxui::frame | ftxui::flex,
           }) |
           ftxui::borderDouble | ftxui::color(neon_frame());
  };

  auto game_view = Renderer([&] {
    DrainGameEvents();
    DrainAudioCommands();
    auto snapshot = game_.Snapshot();
    progress_.best_score = std::max(progress_.best_score, snapshot.score);
    const int total_chunks = static_cast<int>(letter_chunks_.size());
    const bool mission_complete =
        total_chunks > 0 && progress_.unlocked_chunks >= total_chunks;

    auto stats = hbox({
        text("Score: " + std::to_string(snapshot.score)) | color(hacker_white),
        text("  Streak: " + std::to_string(snapshot.streak)) | color(hacker_white),
        text("  Misses: " + std::to_string(snapshot.misses)) | color(hacker_white),
        text("  Unlocked: " + std::to_string(progress_.unlocked_chunks)) | color(hacker_white),
        snapshot.paused ? text("  [PAUSED]") | bold | color(hacker_purple) : text(""),
    });

    auto instructions = text("Arrows/A-D move  P pause  R reset  Esc back") | color(hacker_purple);
    auto game_panel = vbox({
                          text("<< LOVE_PROTOCOL.exe >>") | bold | center | color(hacker_purple),
                          separator(),
                          RenderGameCanvas(snapshot) | center,
                          separator(),
                          stats | center,
                          instructions | center,
                          mission_complete ? text("[MISSION COMPLETE]") | bold | center | color(Color::GreenLight)
                                           : text(""),
                          mission_complete ? text("Press M to open menu") | center | color(hacker_white)
                                           : text(""),
                      }) |
                      borderDouble | color(neon_frame()) |
                      size(ftxui::WIDTH, ftxui::EQUAL, 46);
    auto letter_panel = render_letter_progress(false, snapshot.score, false) |
                        size(ftxui::WIDTH, ftxui::EQUAL, 58);
    auto ascii_panel = render_ascii_progress(snapshot.score, false) |
                       flex;
    return hbox({
        game_panel,
        letter_panel,
        ascii_panel,
    });
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
    if (event == Event::Character('m') || event == Event::Character('M')) {
      DrainGameEvents();
      const int total_chunks = static_cast<int>(letter_chunks_.size());
      const auto snapshot = game_.Snapshot();
      const int unlocked_now = std::max(progress_.unlocked_chunks, snapshot.unlocked_chunks);
      const bool mission_complete =
          total_chunks > 0 && unlocked_now >= total_chunks;
      if (mission_complete) {
        set_screen(Screen::Menu);
      }
      return true;
    }
    if (event == Event::Escape) {
      set_screen(Screen::Dashboard);
      return true;
    }
    return false;
  });

  auto letter_view = Renderer([&] { return render_letter_progress(true, reveal_threshold_score_, true); });

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
    return hbox({
        vbox({
            text("Course") | bold | center | color(hacker_purple),
            separator(),
            dinner_menu->Render() | flex | color(hacker_white),
            separator(),
            text("Esc to return") | center | color(hacker_white),
        }) |
            borderDouble | color(neon_frame()) | size(ftxui::WIDTH, ftxui::EQUAL, 60),
        vbox({
            text("Ingredient Array") | bold | center | color(hacker_purple),
            separator(),
            paragraph(description) | flex | color(hacker_white),
        }) |
            borderDouble | color(neon_frame()) | flex,
    });
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
        text("Settings") | bold | center | color(hacker_purple),
        separator(),
        audio_checkbox->Render() | center | color(hacker_white),
        separator(),
        text("Esc to return") | center | color(hacker_white),
    });
    return content | borderDouble | color(neon_frame());
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
        if (c == "m" || c == "M") {
          DrainGameEvents();
          const int total_chunks = static_cast<int>(letter_chunks_.size());
          const auto snapshot = game_.Snapshot();
          const int unlocked_now = std::max(progress_.unlocked_chunks, snapshot.unlocked_chunks);
          if (total_chunks > 0 && unlocked_now >= total_chunks) {
            set_screen(Screen::Menu);
          }
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
    return root->Render() | bgcolor(Color::Black);
  });

  screen.Loop(root_renderer);

  game_.Stop();
  audio_.Stop();
  persistence_.Save(progress_);
}

bool App::IsGameCompleted() const {
  if (letter_chunks_.empty()) {
    return false;
  }
  return progress_.unlocked_chunks >= static_cast<int>(letter_chunks_.size());
}

void App::RefreshDashboardItems() {
  dashboard_items_.clear();
  dashboard_actions_.clear();

  dashboard_items_.push_back("Start Mission");
  dashboard_actions_.push_back(DashboardAction::StartGame);

  if (IsGameCompleted()) {
    dashboard_items_.push_back("Letter");
    dashboard_actions_.push_back(DashboardAction::Letter);
    dashboard_items_.push_back("Dinner Menu");
    dashboard_actions_.push_back(DashboardAction::Menu);
  }

  dashboard_items_.push_back("Reset");
  dashboard_actions_.push_back(DashboardAction::ResetProgress);
  dashboard_items_.push_back("Settings");
  dashboard_actions_.push_back(DashboardAction::Settings);
  dashboard_items_.push_back("Quit");
  dashboard_actions_.push_back(DashboardAction::Quit);

  if (dashboard_selected_ >= static_cast<int>(dashboard_items_.size())) {
    dashboard_selected_ = std::max(0, static_cast<int>(dashboard_items_.size()) - 1);
  }
}

void App::ApplyProgressToLetterState() {
  const int unlocked = std::clamp(progress_.unlocked_chunks, 0, static_cast<int>(letter_chunks_.size()));
  progress_.unlocked_chunks = unlocked;
  if (revealed_letters_.size() != letter_content_.size()) {
    revealed_letters_.assign(letter_content_.size(), false);
  }
  if (revealed_color_assigned_.size() != letter_content_.size()) {
    revealed_color_assigned_.assign(letter_content_.size(), false);
    revealed_colors_.assign(letter_content_.size(), ftxui::Color::White);
  }

  const bool completed = unlocked >= static_cast<int>(letter_chunks_.size());
  revealed_target_ = 0;
  if (completed) {
    for (size_t i = 0; i < letter_content_.size(); ++i) {
      if (letter_content_[i] != '\n' &&
          !std::isspace(static_cast<unsigned char>(letter_content_[i]))) {
        revealed_letters_[i] = true;
        if (!revealed_color_assigned_[i]) {
          revealed_colors_[i] = RandomValentineColor(letter_rng_);
          revealed_color_assigned_[i] = true;
        }
        revealed_target_ += 1;
      }
    }
  } else {
    std::fill(revealed_letters_.begin(), revealed_letters_.end(), false);
    std::fill(revealed_color_assigned_.begin(), revealed_color_assigned_.end(), false);
  }

  // Keep ASCII art completion state aligned with overall unlock completion.
  if (completed && ascii_art_width_ > 0 && ascii_art_height_ > 0) {
    ascii_revealed_target_ = 0;
    for (int y = 0; y < ascii_art_height_; ++y) {
      for (int x = 0; x < ascii_art_width_; ++x) {
        const size_t idx = static_cast<size_t>(y * ascii_art_width_ + x);
        if (idx >= ascii_base_glyphs_.size() || ascii_base_glyphs_[idx].empty()) {
          continue;
        }
        ascii_revealed_[idx] = true;
        if (!ascii_color_assigned_[idx]) {
          if (idx < ascii_base_fg_colors_.size() && idx < ascii_base_bg_colors_.size()) {
            ascii_revealed_colors_[idx] = ascii_base_fg_colors_[idx];
            ascii_revealed_bg_colors_[idx] = ascii_base_bg_colors_[idx];
          } else {
            ascii_revealed_colors_[idx] = ftxui::Color::White;
            ascii_revealed_bg_colors_[idx] = ftxui::Color::Black;
          }
          ascii_color_assigned_[idx] = true;
        }
        ascii_revealed_target_ += 1;
      }
    }
  } else {
    ascii_revealed_target_ = 0;
    std::fill(ascii_revealed_.begin(), ascii_revealed_.end(), false);
    std::fill(ascii_color_assigned_.begin(), ascii_color_assigned_.end(), false);
  }
}

void App::ResetProgress() {
  const bool keep_audio_enabled = progress_.settings.audio_enabled;
  progress_ = ProgressData{};
  progress_.settings.audio_enabled = keep_audio_enabled;
  last_unlocked_ = 0;
  revealed_target_ = 0;
  std::fill(revealed_letters_.begin(), revealed_letters_.end(), false);
  std::fill(revealed_color_assigned_.begin(), revealed_color_assigned_.end(), false);
  ascii_revealed_target_ = 0;
  std::fill(ascii_revealed_.begin(), ascii_revealed_.end(), false);
  std::fill(ascii_color_assigned_.begin(), ascii_color_assigned_.end(), false);
  last_scramble_tick_ = std::chrono::steady_clock::time_point{};
  last_ascii_scramble_tick_ = std::chrono::steady_clock::time_point{};
  RefreshScrambleState(false);
  RefreshAsciiScrambleState(false);
  ApplyProgressToLetterState();
  last_reveal_tick_ = std::chrono::steady_clock::now();
  last_ascii_reveal_tick_ = std::chrono::steady_clock::now();
  persistence_.Save(progress_);
  RefreshDashboardItems();
}

void App::LoadLetter() {
  std::filesystem::path path = std::filesystem::current_path() / "assets" / "letter.txt";
  std::ifstream file(path);
  if (!file.is_open()) {
    throw std::runtime_error("Failed to open letter file: " + path.string());
  }
  std::stringstream buffer;
  buffer << file.rdbuf();
  std::string content = buffer.str();
  if (content.empty()) {
    throw std::runtime_error("Letter file is empty: " + path.string());
  }

  auto chunks = SplitParagraphs(content);
  letter_chunks_.clear();
  for (const auto& chunk : chunks) {
    letter_chunks_.push_back(LetterChunk{chunk});
  }
  letter_content_ = content;
  revealed_letters_.assign(letter_content_.size(), false);
  revealed_color_assigned_.assign(letter_content_.size(), false);
  revealed_colors_.assign(letter_content_.size(), ftxui::Color::White);
  scrambled_letters_.assign(letter_content_.size(), "");
  scrambled_colors_.assign(letter_content_.size(), ftxui::Color::White);
  reveal_threshold_score_ = std::max(100, static_cast<int>(letter_chunks_.size()) * 100);
  revealed_target_ = 0;
  ApplyProgressToLetterState();
  last_unlocked_ = progress_.unlocked_chunks;
  last_reveal_tick_ = std::chrono::steady_clock::now();
  last_scramble_tick_ = std::chrono::steady_clock::time_point{};
  UpdateLetterReveal(0, false);
  RefreshScrambleState(false);
}

void App::LoadMission() {
  std::filesystem::path path = std::filesystem::current_path() / "assets" / "mission.txt";
  std::ifstream file(path);
  if (!file.is_open()) {
    throw std::runtime_error("Failed to open mission file: " + path.string());
  }
  std::stringstream buffer;
  buffer << file.rdbuf();
  mission_content_ = buffer.str();
  if (mission_content_.empty()) {
    throw std::runtime_error("Mission file is empty: " + path.string());
  }
}

void App::LoadAsciiArt() {
  std::filesystem::path photo_path;
  if (!TryFindPhotoPath(photo_path)) {
    throw std::runtime_error(
        "No photo found in assets/. Add assets/photo.jpg (or .jpeg/.png/.webp/.heic).");
  }

  Magick::Image image;
  try {
    image.read(photo_path.string());
  } catch (const std::exception& e) {
    throw std::runtime_error("Failed to read photo for ASCII conversion: " + std::string(e.what()));
  }

  // Trim letterbox/pillarbox black bars (common in screenshots) before rendering.
  const int source_w = static_cast<int>(image.columns());
  const int source_h = static_cast<int>(image.rows());
  auto row_luminance = [&](int y) {
    double sum = 0.0;
    int samples = 0;
    const int stride = std::max(1, source_w / 120);
    for (int x = 0; x < source_w; x += stride) {
      const Magick::ColorRGB c = image.pixelColor(static_cast<size_t>(x), static_cast<size_t>(y));
      sum += 0.2126 * c.red() + 0.7152 * c.green() + 0.0722 * c.blue();
      samples += 1;
    }
    return samples > 0 ? sum / static_cast<double>(samples) : 0.0;
  };
  auto col_luminance = [&](int x) {
    double sum = 0.0;
    int samples = 0;
    const int stride = std::max(1, source_h / 120);
    for (int y = 0; y < source_h; y += stride) {
      const Magick::ColorRGB c = image.pixelColor(static_cast<size_t>(x), static_cast<size_t>(y));
      sum += 0.2126 * c.red() + 0.7152 * c.green() + 0.0722 * c.blue();
      samples += 1;
    }
    return samples > 0 ? sum / static_cast<double>(samples) : 0.0;
  };

  constexpr double kBlackBarThreshold = 0.055;
  int top = 0;
  int bottom = source_h - 1;
  int left = 0;
  int right = source_w - 1;
  while (top < source_h - 1 && row_luminance(top) < kBlackBarThreshold) {
    top += 1;
  }
  while (bottom > top && row_luminance(bottom) < kBlackBarThreshold) {
    bottom -= 1;
  }
  while (left < source_w - 1 && col_luminance(left) < kBlackBarThreshold) {
    left += 1;
  }
  while (right > left && col_luminance(right) < kBlackBarThreshold) {
    right -= 1;
  }
  if (bottom > top && right > left) {
    const size_t crop_w = static_cast<size_t>(right - left + 1);
    const size_t crop_h = static_cast<size_t>(bottom - top + 1);
    image.crop(Magick::Geometry(crop_w, crop_h, static_cast<ssize_t>(left), static_cast<ssize_t>(top)));
  }

  // Sample edge color for zoom-out padding so it blends instead of adding black pixels.
  const int cropped_w = static_cast<int>(image.columns());
  const int cropped_h = static_cast<int>(image.rows());
  double sr = 0.0;
  double sg = 0.0;
  double sb = 0.0;
  int sample_count = 0;
  const int edge_stride_x = std::max(1, cropped_w / 80);
  const int edge_stride_y = std::max(1, cropped_h / 80);
  for (int x = 0; x < cropped_w; x += edge_stride_x) {
    const Magick::ColorRGB t = image.pixelColor(static_cast<size_t>(x), 0);
    const Magick::ColorRGB b = image.pixelColor(static_cast<size_t>(x), static_cast<size_t>(cropped_h - 1));
    sr += t.red() + b.red();
    sg += t.green() + b.green();
    sb += t.blue() + b.blue();
    sample_count += 2;
  }
  for (int y = 0; y < cropped_h; y += edge_stride_y) {
    const Magick::ColorRGB l = image.pixelColor(0, static_cast<size_t>(y));
    const Magick::ColorRGB r = image.pixelColor(static_cast<size_t>(cropped_w - 1), static_cast<size_t>(y));
    sr += l.red() + r.red();
    sg += l.green() + r.green();
    sb += l.blue() + r.blue();
    sample_count += 2;
  }
  const double inv = sample_count > 0 ? 1.0 / static_cast<double>(sample_count) : 1.0;
  const Magick::ColorRGB pad_color(sr * inv, sg * inv, sb * inv);

  // Keep full photo content (no center-crop).
  const size_t expanded_w = static_cast<size_t>(std::round(static_cast<double>(image.columns()) * 1.0));
  const size_t expanded_h = static_cast<size_t>(std::round(static_cast<double>(image.rows()) * 1.0));
  image.backgroundColor(pad_color);
  image.extent(Magick::Geometry(expanded_w, expanded_h), Magick::CenterGravity);

  // Render as terminal pixel art: one cell uses "▀" with top/bottom colors.
  const int target_cell_width = 68;
  const double src_w = static_cast<double>(image.columns());
  const double src_h = static_cast<double>(image.rows());
  if (src_w <= 0.0 || src_h <= 0.0) {
    throw std::runtime_error("Photo has invalid dimensions: " + photo_path.string());
  }
  const double aspect = src_h / src_w;
  int target_cell_height =
      static_cast<int>(std::round(aspect * static_cast<double>(target_cell_width) * 0.5));
  target_cell_height = std::clamp(target_cell_height, 14, 24);

  const int target_pixel_width = target_cell_width;
  const int target_pixel_height = target_cell_height * 2;
  image.filterType(Magick::PointFilter);
  image.resize(Magick::Geometry(static_cast<size_t>(target_pixel_width),
                                static_cast<size_t>(target_pixel_height)));
  image.autoGamma();
  image.autoLevel();

  const int pixel_width = static_cast<int>(image.columns());
  const int pixel_height = static_cast<int>(image.rows());
  ascii_art_width_ = pixel_width;
  ascii_art_height_ = pixel_height / 2;
  ascii_base_glyphs_.assign(static_cast<size_t>(ascii_art_width_ * ascii_art_height_), "\xE2\x96\x80");

  const size_t total_cells = static_cast<size_t>(ascii_art_width_ * ascii_art_height_);
  ascii_revealed_.assign(total_cells, false);
  ascii_color_assigned_.assign(total_cells, false);
  ascii_base_fg_colors_.assign(total_cells, ftxui::Color::White);
  ascii_base_bg_colors_.assign(total_cells, ftxui::Color::Black);
  ascii_base_fg_rgb_.assign(total_cells, {255, 255, 255});
  ascii_base_bg_rgb_.assign(total_cells, {0, 0, 0});
  ascii_revealed_colors_.assign(total_cells, ftxui::Color::White);
  ascii_revealed_bg_colors_.assign(total_cells, ftxui::Color::Black);
  ascii_scrambled_chars_.assign(total_cells, "");
  ascii_scrambled_colors_.assign(total_cells, ftxui::Color::White);
  ascii_scrambled_bg_colors_.assign(total_cells, ftxui::Color::Black);

  for (int y = 0; y < ascii_art_height_; ++y) {
    for (int x = 0; x < ascii_art_width_; ++x) {
      const size_t idx = static_cast<size_t>(y * ascii_art_width_ + x);
      const int top_y = std::min(pixel_height - 1, y * 2);
      const int bottom_y = std::min(pixel_height - 1, y * 2 + 1);
      const int px = std::min(pixel_width - 1, x);

      const Magick::ColorRGB top_color =
          image.pixelColor(static_cast<size_t>(px), static_cast<size_t>(top_y));
      const Magick::ColorRGB bottom_color =
          image.pixelColor(static_cast<size_t>(px), static_cast<size_t>(bottom_y));

      const int tr = std::clamp(static_cast<int>(std::round(top_color.red() * 255.0)), 0, 255);
      const int tg = std::clamp(static_cast<int>(std::round(top_color.green() * 255.0)), 0, 255);
      const int tb = std::clamp(static_cast<int>(std::round(top_color.blue() * 255.0)), 0, 255);
      const int br = std::clamp(static_cast<int>(std::round(bottom_color.red() * 255.0)), 0, 255);
      const int bg = std::clamp(static_cast<int>(std::round(bottom_color.green() * 255.0)), 0, 255);
      const int bb = std::clamp(static_cast<int>(std::round(bottom_color.blue() * 255.0)), 0, 255);

      ascii_base_fg_colors_[idx] = ftxui::Color::RGB(tr, tg, tb);
      ascii_base_bg_colors_[idx] = ftxui::Color::RGB(br, bg, bb);
      ascii_base_fg_rgb_[idx] = {static_cast<uint8_t>(tr), static_cast<uint8_t>(tg),
                                 static_cast<uint8_t>(tb)};
      ascii_base_bg_rgb_[idx] = {static_cast<uint8_t>(br), static_cast<uint8_t>(bg),
                                 static_cast<uint8_t>(bb)};
    }
  }

  ascii_revealed_target_ = 0;
  last_ascii_reveal_tick_ = std::chrono::steady_clock::now();
  last_ascii_scramble_tick_ = std::chrono::steady_clock::time_point{};
  UpdateAsciiReveal(0, false);
  RefreshAsciiScrambleState(false);
}

void App::UpdateLetterReveal(int score, bool reveal_all) {
  auto now = std::chrono::steady_clock::now();
  if (now - last_reveal_tick_ < std::chrono::milliseconds(90)) {
    return;
  }
  last_reveal_tick_ = now;

  int revealable_count = 0;
  std::vector<size_t> hidden_indices;
  hidden_indices.reserve(letter_content_.size());
  for (size_t i = 0; i < letter_content_.size(); ++i) {
    const char ch = letter_content_[i];
    if (ch == '\n' || std::isspace(static_cast<unsigned char>(ch))) {
      continue;
    }
    revealable_count += 1;
    if (!revealed_letters_[i]) {
      hidden_indices.push_back(i);
    }
  }

  int desired_reveal = 0;
  if (reveal_all || score >= reveal_threshold_score_) {
    desired_reveal = revealable_count;
  } else if (reveal_threshold_score_ > 0) {
    const float ratio = std::clamp(static_cast<float>(score) / static_cast<float>(reveal_threshold_score_),
                                   0.0f, 1.0f);
    desired_reveal = static_cast<int>(ratio * static_cast<float>(revealable_count));
  }

  revealed_target_ = std::max(revealed_target_, desired_reveal);
  const int missing = revealed_target_ - (revealable_count - static_cast<int>(hidden_indices.size()));
  if (missing <= 0 || hidden_indices.empty()) {
    return;
  }

  std::shuffle(hidden_indices.begin(), hidden_indices.end(), letter_rng_);
  const int to_reveal = std::min<int>(missing, static_cast<int>(hidden_indices.size()));
  for (int i = 0; i < to_reveal; ++i) {
    const size_t idx = hidden_indices[i];
    revealed_letters_[idx] = true;
    if (!revealed_color_assigned_[idx]) {
      revealed_colors_[idx] = RandomValentineColor(letter_rng_);
      revealed_color_assigned_[idx] = true;
    }
  }
}

void App::RefreshScrambleState(bool reveal_all) {
  if (scrambled_letters_.size() != letter_content_.size()) {
    scrambled_letters_.assign(letter_content_.size(), "");
    scrambled_colors_.assign(letter_content_.size(), ftxui::Color::White);
  }
  if (reveal_all) {
    return;
  }

  auto now = std::chrono::steady_clock::now();
  if (now - last_scramble_tick_ < std::chrono::milliseconds(scramble_interval_ms_)) {
    return;
  }
  last_scramble_tick_ = now;

  static const std::vector<std::string> kCrypticGlyphs = {
      "Ж", "Ψ", "Δ", "Ξ", "Л", "Й", "Ф", "Я",
      "Ø", "Ⱥ", "¿", "§", "Ƶ", "Ʃ", "Ƭ", "Ɣ",
  };
  std::uniform_int_distribution<size_t> glyph_dist(0, kCrypticGlyphs.size() - 1);
  constexpr float kMutationRatio = 0.35f;
  std::vector<size_t> mutable_indices;
  mutable_indices.reserve(letter_content_.size());

  for (size_t i = 0; i < letter_content_.size(); ++i) {
    const char ch = letter_content_[i];
    const bool is_space = (ch == '\n') || std::isspace(static_cast<unsigned char>(ch));
    const bool is_revealed = i < revealed_letters_.size() && revealed_letters_[i];
    if (is_space || is_revealed) {
      continue;
    }
    mutable_indices.push_back(i);
    if (scrambled_letters_[i].empty()) {
      scrambled_letters_[i] = kCrypticGlyphs[glyph_dist(letter_rng_)];
      scrambled_colors_[i] = RandomValentineColor(letter_rng_);
    }
  }

  if (mutable_indices.empty()) {
    return;
  }

  std::shuffle(mutable_indices.begin(), mutable_indices.end(), letter_rng_);
  int to_mutate = static_cast<int>(mutable_indices.size() * kMutationRatio);
  to_mutate = std::clamp(to_mutate, 1, static_cast<int>(mutable_indices.size()));

  for (int i = 0; i < to_mutate; ++i) {
    const size_t idx = mutable_indices[static_cast<size_t>(i)];
    scrambled_letters_[idx] = kCrypticGlyphs[glyph_dist(letter_rng_)];
    scrambled_colors_[idx] = RandomValentineColor(letter_rng_);
  }
}

void App::UpdateAsciiReveal(int score, bool reveal_all) {
  auto now = std::chrono::steady_clock::now();
  if (now - last_ascii_reveal_tick_ < std::chrono::milliseconds(90)) {
    return;
  }
  last_ascii_reveal_tick_ = now;

  if (ascii_art_width_ <= 0 || ascii_art_height_ <= 0) {
    return;
  }

  int revealable_count = 0;
  std::vector<size_t> hidden_indices;
  hidden_indices.reserve(static_cast<size_t>(ascii_art_width_ * ascii_art_height_));
  for (int y = 0; y < ascii_art_height_; ++y) {
    for (int x = 0; x < ascii_art_width_; ++x) {
      const size_t idx = static_cast<size_t>(y * ascii_art_width_ + x);
      const std::string glyph = (idx < ascii_base_glyphs_.size()) ? ascii_base_glyphs_[idx] : "";
      if (glyph.empty()) {
        continue;
      }
      revealable_count += 1;
      if (idx < ascii_revealed_.size() && !ascii_revealed_[idx]) {
        hidden_indices.push_back(idx);
      }
    }
  }

  int desired_reveal = 0;
  if (reveal_all || score >= reveal_threshold_score_) {
    desired_reveal = revealable_count;
  } else if (reveal_threshold_score_ > 0) {
    const float ratio = std::clamp(static_cast<float>(score) / static_cast<float>(reveal_threshold_score_),
                                   0.0f, 1.0f);
    desired_reveal = static_cast<int>(ratio * static_cast<float>(revealable_count));
  }

  ascii_revealed_target_ = std::max(ascii_revealed_target_, desired_reveal);
  const int missing = ascii_revealed_target_ - (revealable_count - static_cast<int>(hidden_indices.size()));
  if (missing <= 0 || hidden_indices.empty()) {
    return;
  }

  std::shuffle(hidden_indices.begin(), hidden_indices.end(), letter_rng_);
  const int to_reveal = std::min<int>(missing, static_cast<int>(hidden_indices.size()));
  for (int i = 0; i < to_reveal; ++i) {
    const size_t idx = hidden_indices[static_cast<size_t>(i)];
    ascii_revealed_[idx] = true;
    if (!ascii_color_assigned_[idx]) {
      if (idx < ascii_base_fg_colors_.size() && idx < ascii_base_bg_colors_.size()) {
        ascii_revealed_colors_[idx] = ascii_base_fg_colors_[idx];
        ascii_revealed_bg_colors_[idx] = ascii_base_bg_colors_[idx];
      } else {
        ascii_revealed_colors_[idx] = ftxui::Color::White;
        ascii_revealed_bg_colors_[idx] = ftxui::Color::Black;
      }
      ascii_color_assigned_[idx] = true;
    }
  }
}

void App::RefreshAsciiScrambleState(bool reveal_all) {
  const size_t total_cells = static_cast<size_t>(std::max(0, ascii_art_width_ * ascii_art_height_));
  if (ascii_scrambled_chars_.size() != total_cells) {
    ascii_scrambled_chars_.assign(total_cells, "");
    ascii_scrambled_colors_.assign(total_cells, ftxui::Color::White);
    ascii_scrambled_bg_colors_.assign(total_cells, ftxui::Color::Black);
  }
  if (reveal_all || ascii_art_width_ <= 0 || ascii_art_height_ <= 0) {
    return;
  }

  auto now = std::chrono::steady_clock::now();
  if (now - last_ascii_scramble_tick_ < std::chrono::milliseconds(ascii_scramble_interval_ms_)) {
    return;
  }
  last_ascii_scramble_tick_ = now;

  static const std::vector<std::string> kPixelScrambleGlyphs = {
      "\xE2\x96\x88",  // █
      "\xE2\x96\x93",  // ▓
      "\xE2\x96\x92",  // ▒
      "\xE2\x96\x91",  // ░
      "\xE2\x96\x80",  // ▀
      "\xE2\x96\x84",  // ▄
  };
  std::uniform_int_distribution<size_t> glyph_dist(0, kPixelScrambleGlyphs.size() - 1);
  constexpr float kMutationRatio = 0.35f;

  std::vector<size_t> mutable_indices;
  mutable_indices.reserve(total_cells);
  for (int y = 0; y < ascii_art_height_; ++y) {
    for (int x = 0; x < ascii_art_width_; ++x) {
      const size_t idx = static_cast<size_t>(y * ascii_art_width_ + x);
      const std::string glyph = (idx < ascii_base_glyphs_.size()) ? ascii_base_glyphs_[idx] : "";
      if (glyph.empty() || (idx < ascii_revealed_.size() && ascii_revealed_[idx])) {
        continue;
      }
      mutable_indices.push_back(idx);
      if (ascii_scrambled_chars_[idx].empty()) {
        ascii_scrambled_chars_[idx] = kPixelScrambleGlyphs[glyph_dist(letter_rng_)];
        ascii_scrambled_colors_[idx] = RandomValentineColor(letter_rng_);
        ascii_scrambled_bg_colors_[idx] = RandomValentineColor(letter_rng_);
      }
    }
  }

  if (mutable_indices.empty()) {
    return;
  }

  std::shuffle(mutable_indices.begin(), mutable_indices.end(), letter_rng_);
  int to_mutate = static_cast<int>(mutable_indices.size() * kMutationRatio);
  to_mutate = std::clamp(to_mutate, 1, static_cast<int>(mutable_indices.size()));
  for (int i = 0; i < to_mutate; ++i) {
    const size_t idx = mutable_indices[static_cast<size_t>(i)];
    ascii_scrambled_chars_[idx] = kPixelScrambleGlyphs[glyph_dist(letter_rng_)];
    ascii_scrambled_colors_[idx] = RandomValentineColor(letter_rng_);
    ascii_scrambled_bg_colors_[idx] = RandomValentineColor(letter_rng_);
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
      const bool was_complete =
          !letter_chunks_.empty() &&
          progress_.unlocked_chunks >= static_cast<int>(letter_chunks_.size());
      OnUnlock(event.value);
      const bool is_complete =
          !letter_chunks_.empty() &&
          progress_.unlocked_chunks >= static_cast<int>(letter_chunks_.size());
      if (!was_complete && is_complete) {
        audio_.PushCommand(AudioCommand{AudioCommandType::PlayMissionComplete, false});
      }
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
