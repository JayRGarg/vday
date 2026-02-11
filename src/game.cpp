#include "game.hpp"

#include <algorithm>
#include <chrono>

namespace vday {

namespace {

constexpr int kCatcherWidth = 5;
constexpr int kCatcherWallMargin = 1;

int MinCatcherStart(int width) {
  (void)width;
  return kCatcherWallMargin;
}

int MaxCatcherStart(int width) {
  int max_start = width - kCatcherWidth - kCatcherWallMargin;
  if (max_start < MinCatcherStart(width)) {
    max_start = MinCatcherStart(width);
  }
  return max_start;
}

int MinPlayerX(int width) {
  return MinCatcherStart(width) + 2;
}

int MaxPlayerX(int width) {
  return MaxCatcherStart(width) + 2;
}

}  // namespace

int CatcherStartColumn(int player_x, int width) {
  return std::clamp(player_x - 2, MinCatcherStart(width), MaxCatcherStart(width));
}

int ItemVisualWidth(ItemType type) {
  (void)type;
  // All current note symbols are emoji and render as two terminal cells.
  return 2;
}

GameEngine::GameEngine() {
  std::random_device rd;
  rng_ = std::mt19937(rd());
  snapshot_.width = 40;
  snapshot_.height = 20;
  snapshot_.player_x =
      std::clamp(snapshot_.width / 2, MinPlayerX(snapshot_.width), MaxPlayerX(snapshot_.width));
}

GameEngine::~GameEngine() {
  Stop();
}

void GameEngine::Start() {
  if (running_) {
    return;
  }
  running_ = true;
  thread_ = std::thread(&GameEngine::RunLoop, this);
}

void GameEngine::Stop() {
  if (!running_) {
    return;
  }
  running_ = false;
  if (thread_.joinable()) {
    thread_.join();
  }
}

void GameEngine::Reset() {
  std::lock_guard<std::mutex> lock(snapshot_mutex_);
  snapshot_.notes.clear();
  snapshot_.score = 0;
  snapshot_.streak = 0;
  snapshot_.misses = 0;
  snapshot_.paused = false;
  snapshot_.unlocked_chunks = 0;
  snapshot_.catcher_flash_frames = 0;
  snapshot_.player_x =
      std::clamp(snapshot_.width / 2, MinPlayerX(snapshot_.width), MaxPlayerX(snapshot_.width));
  spawn_timer_ = 0.0f;
  input_queue_.Clear();
  event_queue_.Clear();
}

void GameEngine::PushInput(InputAction action) {
  input_queue_.Push(action);
}

bool GameEngine::TryPopEvent(GameEvent& out) {
  return event_queue_.TryPop(out);
}

bool GameEngine::TryPopAudio(AudioCommand& out) {
  return audio_queue_.TryPop(out);
}

GameSnapshot GameEngine::Snapshot() {
  std::lock_guard<std::mutex> lock(snapshot_mutex_);
  return snapshot_;
}

void GameEngine::RunLoop() {
  using clock = std::chrono::steady_clock;
  auto last = clock::now();
  const float dt = 1.0f / 60.0f;
  float accumulator = 0.0f;

  while (running_) {
    auto now = clock::now();
    std::chrono::duration<float> delta = now - last;
    last = now;
    accumulator += delta.count();

    InputAction action;
    while (input_queue_.TryPop(action)) {
      HandleInput(action);
    }

    while (accumulator >= dt) {
      StepSimulation(dt);
      accumulator -= dt;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  audio_queue_.Push(AudioCommand{AudioCommandType::Stop, false});
}

void GameEngine::HandleInput(InputAction action) {
  std::lock_guard<std::mutex> lock(snapshot_mutex_);
  const int step = 2;
  const int min_player_x = MinPlayerX(snapshot_.width);
  const int max_player_x = MaxPlayerX(snapshot_.width);
  if (action == InputAction::MoveLeft) {
    snapshot_.player_x = std::max(min_player_x, snapshot_.player_x - step);
  } else if (action == InputAction::MoveRight) {
    snapshot_.player_x = std::min(max_player_x, snapshot_.player_x + step);
  } else if (action == InputAction::TogglePause) {
    snapshot_.paused = !snapshot_.paused;
  } else if (action == InputAction::Reset) {
    snapshot_.notes.clear();
    snapshot_.score = 0;
    snapshot_.streak = 0;
    snapshot_.misses = 0;
    snapshot_.paused = false;
    snapshot_.unlocked_chunks = 0;
    snapshot_.catcher_flash_frames = 0;
    snapshot_.player_x =
        std::clamp(snapshot_.width / 2, MinPlayerX(snapshot_.width), MaxPlayerX(snapshot_.width));
    spawn_timer_ = 0.0f;
  }
}

void GameEngine::StepSimulation(float dt) {
  std::lock_guard<std::mutex> lock(snapshot_mutex_);
  if (snapshot_.paused) {
    return;
  }
  if (snapshot_.catcher_flash_frames > 0) {
    snapshot_.catcher_flash_frames -= 1;
  }

  spawn_timer_ += dt;
  if (spawn_timer_ >= 0.6f) {
    spawn_timer_ = 0.0f;
    SpawnNote();
  }

  for (auto& note : snapshot_.notes) {
    note.y += dt * 10.0f;
  }

  int caught = 0;
  int missed = 0;
  for (auto& note : snapshot_.notes) {
    int result = CatchOrMiss(note);
    if (result > 0) {
      caught++;
    } else if (result == 0) {
      missed++;
    }
  }

  snapshot_.notes.erase(
      std::remove_if(snapshot_.notes.begin(), snapshot_.notes.end(), [&](const Note& n) {
        return static_cast<int>(n.y) >= snapshot_.height - 3;
      }),
      snapshot_.notes.end());

  if (missed > 0) {
    snapshot_.misses += missed;
    snapshot_.streak = 0;
    audio_queue_.Push(AudioCommand{AudioCommandType::PlayMiss, false});
  }

  if (caught > 0) {
    snapshot_.catcher_flash_frames = 10;
    audio_queue_.Push(AudioCommand{AudioCommandType::PlayCatch, false});
  }

  int new_unlocked = snapshot_.score / unlock_score_step_;
  if (new_unlocked > snapshot_.unlocked_chunks) {
    snapshot_.unlocked_chunks = new_unlocked;
    event_queue_.Push(GameEvent{GameEventType::UnlockChunk, new_unlocked});
    audio_queue_.Push(AudioCommand{AudioCommandType::PlayUnlock, false});
  }
}

void GameEngine::SpawnNote() {
  std::uniform_int_distribution<int> type_dist(0, 99);

  int roll = type_dist(rng_);
  ItemType type = ItemType::Heart;
  if (roll < 45) {
    type = ItemType::Heart;
  } else if (roll < 70) {
    type = ItemType::LoveNote;
  } else if (roll < 90) {
    type = ItemType::Kiss;
  } else {
    type = ItemType::BrokenHeart;
  }

  const int max_x = std::max(0, snapshot_.width - ItemVisualWidth(type));
  std::uniform_int_distribution<int> x_dist(0, max_x);
  snapshot_.notes.push_back(Note{x_dist(rng_), 0.0f, type});
}

int GameEngine::CatchOrMiss(Note& note) {
  const int catcher_row = snapshot_.height - 3;
  const int note_row = static_cast<int>(note.y);
  if (note_row < catcher_row) {
    return -1;
  }

  const int catcher_start = CatcherStartColumn(snapshot_.player_x, snapshot_.width);
  const int catcher_inner_left = catcher_start + 1;
  const int catcher_inner_right = catcher_start + 3;

  const int note_left = note.x;
  const int note_right = note.x + ItemVisualWidth(note.type) - 1;
  const bool overlaps_catcher =
      note_left <= catcher_inner_right && note_right >= catcher_inner_left;

  if (overlaps_catcher) {
    int delta = ScoreFor(note.type);
    snapshot_.score += delta;
    if (note.type == ItemType::BrokenHeart) {
      snapshot_.streak = 0;
    } else {
      snapshot_.streak += 1;
    }
    return 1;
  }
  return 0;
}

int GameEngine::ScoreFor(ItemType type) const {
  switch (type) {
    case ItemType::Heart:
      return 10;
    case ItemType::LoveNote:
      return 20;
    case ItemType::Kiss:
      return 30;
    case ItemType::BrokenHeart:
      return -15;
  }
  return 0;
}

}  // namespace vday
