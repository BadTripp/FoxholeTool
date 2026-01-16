## FoxholeTool Release

### What this tool does

FoxholeTool is a small helper for Foxhole that automates repeated inputs so you don’t have to keep keys or mouse buttons pressed by hand.  
It works both on Windows and Linux (X11).

### Main features

- Spam left mouse clicks at a saved screen position (around every 30 ms).
- Automatically hold the `W` and `S` movement keys.
- Automatically hold left and right mouse buttons.
- Global hotkeys that work while the game is focused.
- Lightweight overlay bar at the top of the screen:
  - Shows the current hotkeys and which actions are active.
  - Can be shown or hidden without stopping the tool.

### Default hotkeys

- `F2`  → spam left click at saved position.
- `F3`  → hold `W`.
- `F4`  → hold `S`.
- `F6`  → hold right mouse button.
- `F7`  → hold left mouse button.
- `F9`  → suspend or resume all actions.
- `F10` → exit the tool.
- `F11` → show or hide the overlay HUD.

### How to use

1. Start Foxhole.
2. Run FoxholeTool:
   - On Linux: run `./foxholetool`.
   - On Windows: run `foxholetool.exe`.
3. Look for the small overlay bar at the top of the screen, aligned with the game window.
4. Press the hotkeys above to enable or disable each action.  
   Pressing the same hotkey again turns that action off.

### Customizing hotkeys

Hotkeys can be changed by editing the file `foxtool_hotkeys.cfg` in the same folder as the executable.  
If the file is missing, FoxholeTool will create it automatically with the default bindings.
