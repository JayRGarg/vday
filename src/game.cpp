#include "game.hpp"

#include <algorithm>
#include <chrono>

namespace vday {

GameEngine::GameEngine() {
  std::random_device rd;
  rng_ = std::mt19937(rd());
  snapshot_.width = 40;
  snapshot_.height = 20;
  snapshot_.player_x = snapshot_.width / 2;
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
  snapshot_.player_x = snapshot_.width / 2;
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
  if (action == InputAction::MoveLeft) {
    snapshot_.player_x = std::max(0, snapshot_.player_x - step);
  } else if (action == InputAction::MoveRight) {
    snapshot_.player_x = std::min(snapshot_.width - 1, snapshot_.player_x + step);
  } else if (action == InputAction::TogglePause) {
    snapshot_.paused = !snapshot_.paused;
  } else if (action == InputAction::Reset) {
    snapshot_.notes.clear();
    snapshot_.score = 0;
    snapshot_.streak = 0;
    snapshot_.misses = 0;
    snapshot_.paused = false;
    snapshot_.unlocked_chunks = 0;
    snapshot_.player_x = snapshot_.width / 2;
    spawn_timer_ = 0.0f;
  }
}

void GameEngine::StepSimulation(float dt) {
  std::lock_guard<std::mutex> lock(snapshot_mutex_);
  if (snapshot_.paused) {
    return;
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
  std::uniform_int_distribution<int> x_dist(0, snapshot_.width - 1);
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

  snapshot_.notes.push_back(Note{x_dist(rng_), 0.0f, type});
}

int GameEngine::CatchOrMiss(Note& note) {
  const int catcher_row = snapshot_.height - 3;
  const int note_row = static_cast<int>(note.y);
  if (note_row < catcher_row) {
    return -1;
  }

  if (note.x >= snapshot_.player_x - 1 && note.x <= snapshot_.player_x + 1) {
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
