#pragma once

#include <atomic>
#include <string>
#include <thread>

#include "game.hpp"
#include "thread_queue.hpp"

namespace vday {

class AudioEngine {
 public:
  AudioEngine();
  ~AudioEngine();

  void Start();
  void Stop();

  void PushCommand(const AudioCommand& command);

 private:
  void RunLoop();

  std::atomic<bool> running_{false};
  std::thread thread_;
  ThreadSafeQueue<AudioCommand> queue_;
};

}  // namespace vday
