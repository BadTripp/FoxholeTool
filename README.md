## Foxhole Helper (Cross‑Platform Autoclicker)

Simple cross‑platform autoclicker / key holder designed for games like Foxhole.  
It automates repeated mouse clicks and holds down movement / mouse buttons using global hotkeys.

> ⚠️ **AI‑assisted code notice**  
> This codebase was written with the help of an AI assistant and reviewed / supervised by a human developer.  
> Always review the code yourself before using it.

### Features

- Spam left mouse clicks at a saved screen position.
- Hold `W` / `S` movement keys.
- Hold left / right mouse buttons.
- Global hotkeys on:
  - **Windows**: Win32 `RegisterHotKey` + `SendInput`.
  - **Linux (X11)**: `XGrabKey` + `XTestFake*` (requires `libX11` + `libXtst`).
- Lightweight overlay HUD showing the current hotkeys and active actions:
  - Transparently drawn over the game window.
  - Can be hidden / shown with `F11`.

### Default Hotkeys

These are the logical actions and their default bindings (can be overridden via config):

- `F2`  → spam left click at saved position (~30 ms)
- `F3`  → hold `W`
- `F4`  → hold `S`
- `F6`  → hold right mouse button
- `F7`  → hold left mouse button
- `F9`  → suspend / resume all actions
- `F10` → exit the tool
- `F11` → show / hide overlay HUD (fixed extra hotkey)

On both Windows and Linux, the overlay text reflects the current mapping loaded from the config file.

### Platforms & Requirements

**Windows**

- Windows 10/11 desktop.
- Uses:
  - `RegisterHotKey` for global hotkeys.
  - `SendInput` for keyboard and mouse injection.
  - A transparent top‑most layered window for the HUD.

**Linux (X11)**

- Xorg/X11 sessions.
- Not supported: Wayland.
- Requires:
  - `libX11`
  - `libXtst`
  - A compositor that supports ARGB overlays for best visual result.

The code tries to detect the Foxhole/War game window by process path (`_NET_WM_PID` + `/proc/<pid>` checks) and positions the overlay near it.

### Build Instructions

**Linux (X11)**

```bash
gcc clicker.c -o foxholetool -lX11 -lXtst -lpthread
```

Or with optimizations:

```bash
gcc -O2 clicker.c -o foxholetool -lX11 -lXtst -lpthread
```

**Windows (MSVC)**

```bat
cl /O2 clicker.c user32.lib
```

**Windows (MinGW, senza console)**

```bash
gcc -O2 -mwindows clicker.c -o foxholetool.exe -luser32 -lgdi32
```

### Usage

1. Start the game .
2. Run the compiled binary:
   - `./foxholetool` on Linux.
   - `foxholetool.exe` on Windows.
3. Look for the overlay bar at the top, aligned with the game window.
4. Use the hotkeys:
   - `F2` once to save the current mouse position and start spamming left click there.
   - `F3` / `F4` to hold `W` / `S`.
   - `F6` / `F7` to hold right/left mouse buttons.
   - `F9` to suspend/resume all actions (and release held keys/buttons).
   - `F10` to exit the tool.
   - `F11` to hide or show the HUD without stopping the logic.

Pressing the same hotkey again toggles its action off.

### Hotkey Configuration

The tool reads and writes a plain text config file:

- File name: `foxtool_hotkeys.cfg`
- Location: same directory as the executable.

Example:

```text
Spam LMB=F2
Hold W=F3
Hold S=F4
Hold RMB=F6
Hold LMB=F7
Suspend=F9
Exit=F10
```

You can edit this file manually to change which function key controls each action (within the supported set).  
If the file is missing, the defaults described above are used.

> Note: `F11` (overlay toggle) is not read from this config and stays fixed.

### Overlay Notes (Linux/X11)

- Uses an ARGB visual where available to draw a truly transparent overlay.
- Background is fully transparent; only the text is visible.
- The code:
  - Clears only the text area before redraw.
  - Uses a `select()`‑based loop with:
    - Faster redraw when overlay is visible or actions are active.
    - Slower tick when hidden and idle to reduce CPU usage.

If you see issues with overlays on full‑screen games:

- Disable “unredirect fullscreen windows” in the compositor.
- Or run the game in borderless windowed mode instead of exclusive fullscreen.

### Anti‑Cheat & Responsibility

Automating input may be against some games’ Terms of Service and can be detected by anti‑cheat systems.

- Use this tool at your own risk.
- Check the rules of each game before using it.
- Prefer non‑competitive or private contexts where automation is allowed.

### Repository Layout

- `clicker.c` – main cross‑platform implementation (Windows + Linux/X11).
- `foxtool_hotkeys.cfg` – user hotkey configuration (created at runtime; usually not committed).
- Other source files/copies – experimental or backup variants, if present.
