# The Simpsons: Hit & Run — Android Port (Experimental Native Mod Support)

> ⚠️ **Experimental Project Notice**
>
> This project is an **experimental** Android port of *The Simpsons: Hit & Run* focused on bringing **PC mod support** natively to Android devices. Porting mods built around a desktop game — many of which depend on *Lucas' Mod Launcher*, x86 memory hooks, injected DLL hacks, custom shaders, audio overrides, and Lua scripting — to run on Android is inherently difficult. Expect rough edges, mod-specific quirks, and ongoing experimentation.
>
> This fork is the result of **vibecode combined with software engineering**, reverse engineering, and iterative debugging. **Everyone is welcome and completely free to test, fork, experiment with, and improve upon everything done so far.**

---

## 📱 Available APK Builds

Only two APK builds are published in **Releases**:

| Variant | Target Architecture | Notes |
| --- | --- | --- |
| **Universal APK** (`*_universal.apk`) | `arm64-v8a` + `armeabi-v7a` | Works on virtually all Android devices (32-bit and 64-bit). |
| **ARM64 APK** (`*_arm64.apk`) | `arm64-v8a` | Smaller download dedicated to modern 64-bit Android devices. |

---

## 📂 Storage & File Placement (`Android/data` Workaround)

Modern Android versions (Android 11+) restrict user access to `Android/data/`. To make setup and mod management easy directly on the phone, **this version bypasses the `Android/data` limitation** (using `MANAGE_EXTERNAL_STORAGE` / all-files access) and moves everything into an accessible folder in **internal shared storage**:

```text
/storage/emulated/0/Simpsons Hit and Run/
├── files/   <-- Put your PC game base assets here (.rcf files, movies/, etc.)
└── mods/    <-- Put your extracted mods here
```

### 1. Base Game Assets (`files/`)
You must provide **your own copy of the official PC release** of *The Simpsons: Hit & Run*. Copy the base game assets (such as `art.rcf`, `sound.rcf`, `music*.rcf`, `scripts.rcf`, `dialog*.rcf`, `missions.rcf`, `movies/`, etc.) into:

```text
/storage/emulated/0/Simpsons Hit and Run/files/
```

> **Note:** Backward-compatible fallback to the legacy paths (`Android/data/com.c4rlox.simpsons/files/` and `/storage/emulated/0/simpsons_mods/`) is still checked if files already live there, though using `/storage/emulated/0/Simpsons Hit and Run/` is recommended.

### 2. Installing Mods (`mods/`)
Place extracted mod folders inside:

```text
/storage/emulated/0/Simpsons Hit and Run/mods/
```

For example:

```text
/storage/emulated/0/Simpsons Hit and Run/mods/MyModName/CustomFiles/...
```

Or loose override files directly under `mods/CustomFiles/...`.

### 3. Configuration (`Simpsons_configuration.txt`)
When launched, the app generates its configuration file where you can toggle or select mods:

- `enable_mods=true`
- `mods_directory=/storage/emulated/0/Simpsons Hit and Run/mods`
- `active_mod=<ModFolderName>` *(optional — auto-discovery and in-game selection are also supported)*

---

## 🔧 What This Experimental Fork Implements

Because PC mods rarely work out of the box outside Windows + *Lucas' Mod Launcher*, this branch adds native C++ / engine support for:

- **Loose-file overrides & case-insensitive lookup**: mod assets override `.rcf` cement containers seamlessly.
- **`CustomFiles.ini` & `AdditionalFiles/` support**: path redirections and occluded paths.
- **Lua mod script integration**: `CustomFiles.lua`, `rewards.lua`, `game.lua`, and path handlers.
- **Extended mission script commands, objectives & conditions**: `DisableTrigger`, `AddStageVehicleCharacter`, `RemoveStageVehicleCharacter`, checkpoints (`CHECKPOINT_HERE`, `IfCurrentCheckpoint`, `SetCheckpoint*`), stage `HitAndRun` controls, `SetParkedCarsEnabled`, `SetPedsEnabled`, `insidetrigger`, `outsidetrigger`, `camera`, `hitpeds`, and `destroycars`.
- **Diagnostic execution logging (`gamelog.txt`)**: optional in-game logger saving diagnostics into `/storage/emulated/0/Simpsons Hit and Run/gamelog.txt` to debug tricky mods on real hardware.

If a PC mod still fails, crashes, or behaves unexpectedly, check `gamelog.txt` — PC mods often target engine hooks or assets not yet replicated on Android. Feel free to open it, inspect what happened, and contribute fixes or improvements!

---

## 🕹️ How to Enable Mods In-Game

1. Launch the game.
2. Go to **Options** → **Mods**.
3. You will see a list of all available mods found inside `/storage/emulated/0/Simpsons Hit and Run/mods/`.
4. Enable the mod you want, then **close and reopen the game** so the mod loads completely.

---

## 🙏 Credits & Upstream Lineage

- Original console ports (*Nintendo Switch* & *PS Vita*): [ZenoArrows/The-Simpsons-Hit-and-Run](https://github.com/ZenoArrows/The-Simpsons-Hit-and-Run)
- Android port foundation: [Carlox33/The-Simpsons-Hit-and-Run-Android](https://github.com/Carlox33/The-Simpsons-Hit-and-Run-Android)
- Experimental native mod loader, vibecode, engine fixes, and Android storage workaround: this repository — open for everyone to test, remix, and improve.
