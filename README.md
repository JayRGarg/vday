# Valentine TUI

A single-terminal Valentine's Day experience built with C++20 and FTXUI.

## Build (Arch Linux)

Install dependencies:

```bash
sudo pacman -S --needed base-devel cmake ninja git
sudo pacman -S --needed sdl2 sdl2_mixer   # optional audio
```

Initialize the FTXUI submodule:

```bash
git submodule update --init --recursive
```

Build and run:

```bash
cmake -S . -B build -G Ninja
cmake --build build
./build/valentine_tui
```
