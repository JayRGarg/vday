#include "audio.hpp"

#include <filesystem>
#include <iostream>

#ifdef HAVE_SDL2_MIXER
#include <SDL2/SDL.h>
#include <SDL2/SDL_mixer.h>
#endif

namespace vday {

AudioEngine::AudioEngine() = default;

AudioEngine::~AudioEngine() {
  Stop();
}

void AudioEngine::Start() {
  if (running_) {
    return;
  }
  running_ = true;
  thread_ = std::thread(&AudioEngine::RunLoop, this);
}

void AudioEngine::Stop() {
  if (!running_) {
    return;
  }
  running_ = false;
  queue_.Push(AudioCommand{AudioCommandType::Stop, false});
  if (thread_.joinable()) {
    thread_.join();
  }
}

void AudioEngine::PushCommand(const AudioCommand& command) {
  queue_.Push(command);
}

void AudioEngine::RunLoop() {
  bool enabled = true;

#ifdef HAVE_SDL2_MIXER
  if (SDL_Init(SDL_INIT_AUDIO) != 0) {
    std::cerr << "SDL_Init failed: " << SDL_GetError() << "\n";
  }
  if (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 2048) < 0) {
    std::cerr << "Mix_OpenAudio failed: " << Mix_GetError() << "\n";
  }

  std::filesystem::path base = std::filesystem::current_path() / "assets" / "audio";
  std::filesystem::path catch_path = base / "catch.wav";
  std::filesystem::path miss_path = base / "miss.wav";
  std::filesystem::path unlock_path = base / "unlock.wav";

  Mix_Chunk* catch_sfx = nullptr;
  Mix_Chunk* miss_sfx = nullptr;
  Mix_Chunk* unlock_sfx = nullptr;

  if (std::filesystem::exists(catch_path)) {
    catch_sfx = Mix_LoadWAV(catch_path.string().c_str());
  }
  if (std::filesystem::exists(miss_path)) {
    miss_sfx = Mix_LoadWAV(miss_path.string().c_str());
  }
  if (std::filesystem::exists(unlock_path)) {
    unlock_sfx = Mix_LoadWAV(unlock_path.string().c_str());
  }
#endif

  while (running_) {
    AudioCommand command;
    queue_.WaitPop(command);
    if (command.type == AudioCommandType::Stop) {
      break;
    }
    if (command.type == AudioCommandType::SetEnabled) {
      enabled = command.enabled;
      continue;
    }

#ifdef HAVE_SDL2_MIXER
    if (!enabled) {
      continue;
    }
    if (command.type == AudioCommandType::PlayCatch && catch_sfx) {
      Mix_PlayChannel(-1, catch_sfx, 0);
    } else if (command.type == AudioCommandType::PlayMiss && miss_sfx) {
      Mix_PlayChannel(-1, miss_sfx, 0);
    } else if (command.type == AudioCommandType::PlayUnlock && unlock_sfx) {
      Mix_PlayChannel(-1, unlock_sfx, 0);
    }
#else
    (void)enabled;
#endif
  }

#ifdef HAVE_SDL2_MIXER
  if (catch_sfx) {
    Mix_FreeChunk(catch_sfx);
  }
  if (miss_sfx) {
    Mix_FreeChunk(miss_sfx);
  }
  if (unlock_sfx) {
    Mix_FreeChunk(unlock_sfx);
  }
  Mix_CloseAudio();
  SDL_QuitSubSystem(SDL_INIT_AUDIO);
#endif
}

}  // namespace vday
