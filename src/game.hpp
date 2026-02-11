#pragma once

#include <atomic>
#include <mutex>
#include <random>
#include <thread>
#include <vector>

#include "thread_queue.hpp"

namespace vday {

enum class InputAction {
  MoveLeft,
  MoveRight,
  TogglePause,
  ReturnToDashboard,
  Reset,
};

enum class ItemType {
  Heart,
  LoveNote,
  Kiss,
  BrokenHeart,
};

struct Note {
  int x = 0;
  float y = 0.0f;
  ItemType type = ItemType::Heart;
};

struct GameSnapshot {
  int width = 40;
  int height = 20;
  int player_x = 20;
  bool paused = false;
  int score = 0;
  int streak = 0;
  int misses = 0;
  int unlocked_chunks = 0;
  int catcher_flash_frames = 0;
  std::vector<Note> notes;
};

enum class GameEventType {
  UnlockChunk,
};

struct GameEvent {
  GameEventType type = GameEventType::UnlockChunk;
  int value = 0;
};

enum class AudioCommandType {
  PlayCatch,
  PlayMiss,
  PlayUnlock,
  SetEnabled,
  Stop,
};

struct AudioCommand {
  AudioCommandType type = AudioCommandType::PlayCatch;
  bool enabled = true;
};

int CatcherStartColumn(int player_x, int width);
int ItemVisualWidth(ItemType type);

class GameEngine {
 public:
  GameEngine();
  ~GameEngine();

  void Start();
  void Stop();

  void PushInput(InputAction action);
  bool TryPopEvent(GameEvent& out);
  bool TryPopAudio(AudioCommand& out);
  GameSnapshot Snapshot();

  void Reset();

 private:
  void RunLoop();
  void StepSimulation(float dt);
  void HandleInput(InputAction action);
  void SpawnNote();
  int CatchOrMiss(Note& note);
  int ScoreFor(ItemType type) const;

  std::atomic<bool> running_{false};
  std::thread thread_;

  ThreadSafeQueue<InputAction> input_queue_;
  ThreadSafeQueue<GameEvent> event_queue_;
  ThreadSafeQueue<AudioCommand> audio_queue_;

  std::mutex snapshot_mutex_;
  GameSnapshot snapshot_;

  std::mt19937 rng_;
  float spawn_timer_ = 0.0f;
  int unlock_score_step_ = 100;
};

}  // namespace vday
