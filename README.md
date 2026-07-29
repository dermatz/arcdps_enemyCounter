# Enemy Counter - ArcDps Addon

**Prowdly presents by Rage Society - German WvW Guild - Visit us on [RageSociety.de](https://ragesociety.de)**

A lightweight [arcdps](https://www.deltaconnected.com/arcdps/) addon for Guild Wars 2 that counts enemy players you encounter during combat. It is designed primarily for WvW but works wherever arcdps provides combat events.

## What it does

- **Counts enemy players** in real time while you fight.
- **Shows a class breakdown** so you can see which professions are around you.
- **Tracks a history of recent fights** with timestamps and class distribution.
- **Respects your privacy:** it uses only the official arcdps C-API. No memory reading, no injection, no external network calls.

## Features

| Feature | Description |
| --- | --- |
| Live enemy counter | Displays the current number of unique enemy players detected in combat. |
| Class breakdown | Lists enemy professions with optional arcdps profession colors. |
| Configurable timeout | Choose how long enemies stay in the list after their last combat event (5–300 seconds). |
| Fight history | Automatically saves a snapshot of each fight on combat end. History size is configurable (1–50 entries). |
| Sorting options | Sort the class list by count, alphabetically, or by profession order. |
| Active-combat filter | Optional mode that only counts enemies dealing or receiving real combat actions. |
| Toggle hotkey | Show or hide the window with a single key press. The key can be customized in the options. |

## Installation

1. Download the `arcdps_enemy_counter.zip` from the latest release from the [Releases](../../releases) page.
2. Extract `arcdps_enemy_counter.dll`.
3. Copy it into your Guild Wars 2 folder:
   ```
   Guild Wars 2/addons/arcdps/arcdps_enemy_counter.dll
   ```
4. Start the game.
5. Open the arcdps options window with `Alt+Shift+T`.
6. Go to **Interface → Extension Windows** and enable **Enemy Counter**.

## Usage

- The **Enemy Counter** window appears once enabled.
- Open the **Enemy Counter** tab in the arcdps options to change settings:
  - Show or hide the class breakdown
  - Use profession colors
  - Only count active combat enemies
  - Show the total count as a large number
  - Adjust the timeout
  - Change class list sorting
  - Set the fight history size
  - Show the fight history window
  - Customize the toggle hotkey
- Press your configured hotkey (default `F7`) to quickly show or hide the window.

---

## For Developers - Building from source

### Requirements

- Visual Studio 2022 (Community, Professional, Enterprise or Build Tools)
- Windows SDK
- [ImGui 1.92.7](https://github.com/ocornut/imgui) sources

### Steps

1. Clone the repository.
2. Download the ImGui sources:
   ```batch
   setup_imgui.bat
   ```
3. Build the x64 DLL:
   ```batch
   build_enemy_counter.bat
   ```
4. Find the compiled addon at:
   ```
   out/arcdps_enemy_counter.dll
   ```

## Development notes

- Enemy detection happens entirely inside the arcdps `cb_combat` callback.
- Only player agents are counted (`prof != 0`, `elite != 0xFFFFFFFF`, `self == 0`).
- All stored data is kept in memory only and cleared when the addon is unloaded.

## Project files

| File | Purpose |
| --- | --- |
| `enemy_counter.cpp` | Main plugin source code. |
| `build_enemy_counter.bat` | Builds the x64 DLL. |
| `setup_imgui.bat` | Downloads the required ImGui sources. |
| `release-please-config.json` | Release Please configuration. |
| `.github/workflows/release-please.yml` | Creates release pull requests automatically. |
| `.github/workflows/build-release.yml` | Builds the DLL and attaches it to releases. |

## License

This project is provided as-is for the Guild Wars 2 community. Use it at your own risk and in accordance with ArenaNet's terms of service.
