# More Save Slots

A [REPENTOGON](https://github.com/TeamREPENTOGON/REPENTOGON) plugin for **The Binding of Isaac: Repentance+** that adds pages to the file select menu: up to **9 save files** on one Steam account, in pages of 3.

- **Page 1 (FILE 1-3):** your normal files, in Steam Cloud if you use it.
- **Pages 2 and 3 (FILE 4-6 and 7-9):** stored on this PC only, never in Steam Cloud.
- Every page can earn **Dead God** and the **infinity banner** on its own.

## Install

1. Install REPENTOGON: <https://repentogon.com/install.html>
2. Download **`MoreSaveSlotsInstaller.exe`** from the [latest release](https://github.com/maflatv/more-save-slots/releases/latest) and run it. It finds your REPENTOGON folders; leave them ticked and press **Install**.
3. Start the game with REPENTOGON.

To remove it, run the installer again and press **Uninstall**. Uninstalling never deletes saves.

Optional: subscribe to the [Steam Workshop companion](https://steamcommunity.com/app/250900/workshop/) ("More Save Slots"). It shows a message in game if REPENTOGON or the plugin is missing or out of date. The Workshop cannot install the plugin itself, because REPENTOGON never loads native plugins from the `mods` folder.

### Manual install

The release zip contains `zhlMoreSaveSlots.dll` and the `more-save-slots` folder. Copy both next to `zhlREPENTOGON.dll` (in the game folder, and in its `Repentogon` subfolder if it has one).

## How to use

| In the file select menu | Result |
|---|---|
| Right on File 3 | Next page (slide animation) |
| Right on File 3 of the last page | Selects the **+** tab; confirm it to add a page (up to 3) |
| Left on File 1 | Previous page |
| LB / RB | Previous / next page |

The menu remembers the last page used. Settings: `Repentogon/more_save_slots.ini`, inside the save folder.

## Good to know

- Files 4-9 only show up while REPENTOGON and the plugin are installed. Uninstalling never deletes them: install again and they are back.
- The first time the game starts with the plugin, a backup of your saves is made in `Documents\My Games\Binding of Isaac Repentance+\Repentogon\more_save_slots_backup`.
- Each file keeps its own run in progress, unlocks and mod data.
- REPENTOGON cannot play Online, and Repentance+ daily runs are part of Online mode, so their six achievements (Dedication, The Marathon, Broken Modem, Play Online, Win Online, Win Online Daily) could never be earned in files 4-9 and Dead God would be impossible there. The plugin marks those six in files 4-9 only, directly in the save data: nothing is sent to Steam and files 1-3 are never touched.
- Built for **REPENTOGON 1.1.3**. If REPENTOGON updates to a version the plugin does not know, the plugin turns itself off (the game keeps working normally) until an update is released.
- The FILE 4-9 labels are in English.

## How it works

The game, REPENTOGON and other mods always see slots 1-3: their internal tables only accept those numbers. While a slot of page 2 or 3 is active, the plugin only changes the slot digit of the save file names on disk. It does this in the Windows file functions (`CreateFile`, `DeleteFile`, `MoveFileEx`, `GetFileAttributes`...) imported by the game's own modules (`isaac-ng.exe`, `ucrtbase`, `msvcp140`, `zhlREPENTOGON`). Steam and system modules are never patched.

Redirected files: `rgon_persistentgamedataN.dat`, `rep+persistentgamedataN.dat` and their backups, `gamestateN.dat`, `rep+rerunstateN.dat`, REPENTOGON's `*gamestateN*` sidecars, `completionmarksN.json`, `achievementsN.json` and `data/<mod>/saveN.dat`.

With Steam Cloud on, the plugin turns the engine's cloud flag off while a slot 4-9 is active or page 2-3 is on screen, so those files are always local. `options.ini` always keeps your real setting.

## Build from source

Requirements: Visual Studio Build Tools (MSVC x86, Windows SDK, CMake) and, for the installer, ".NET desktop build tools".

```
git clone --branch 1.1.3 --depth 1 --recurse-submodules -c core.longpaths=true https://github.com/TeamREPENTOGON/REPENTOGON.git build\rgon
build.bat                  builds bin\zhlMoreSaveSlots.dll against REPENTOGON 1.1.3's libzhl
installer\build.bat        builds installer\dist\MoreSaveSlotsInstaller.exe with the plugin embedded
```

The plugin must be linked against the libzhl of the same REPENTOGON version it runs on. The graphics in `assets/` are generated from the game's own resources with `python tools\make_graphics.py <game resources> assets`. The Workshop companion is in `workshop/` (`main.lua` + `metadata.xml`).

## Credits

- Made by MaflaTV.
- Built on [REPENTOGON](https://github.com/TeamREPENTOGON/REPENTOGON) and libzhl (_Kilburn and the REPENTOGON team).
- Menu art derived from The Binding of Isaac by Edmund McMillen and Nicalis.

☕ **Find it useful?** Support me on **[Ko-fi](https://ko-fi.com/maflatv)**. Follow me on [YouTube](https://www.youtube.com/@MaflaTV) · [Twitch](https://www.twitch.tv/maflatv) · [TikTok](https://www.tiktok.com/@maflatv) · [Instagram](https://www.instagram.com/mafla.tv/) · [GitHub](https://github.com/maflatv).

## License

GPL-2.0, see [LICENSE](LICENSE). The plugin is built with headers generated from libzhl's GPL-licensed function definitions.
