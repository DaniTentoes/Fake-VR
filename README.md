# Fake-VR: Audio-First Social Metaverse for Blind Users

**Fake-VR** is an accessible real-time virtual hangout platform inspired by VRChat, built specifically for blind and low-vision users.

Instead of relying on vision, Fake-VR delivers social presence through:
- **Acoustic Fashion & Auras:** Express your avatar's identity through customized ambient sound textures (Vinyl Warmth, Crystal Chime, Deep Hum, Silk Rustle) attached to your 3D presence.
- **The Resonance Lounge:** A virtual hangout space with an **Acoustic Mirror**—a chamber with heightened reflections where you can hear your sound aura, snap your fingers to hear the room breathe, and chat with friends standing near you.
- **Raytraced Echolocation & Surface Physics:** Press `Space` to emit an active sonic snap. Room reflections return in real-time calculated with **Valve's Steam Audio HRTF**, while different floor materials (wood, stone, carpet, grass, metal, tile) alter your footsteps.
- **NVDA Screen Reader Integration:** All menus, coordinate readouts, room transitions, and nearby player radars are spoken directly through NVDA.
- **Zero Audio Lag:** All spatial audio renders client-side on your local machine using Steam Audio.

---

## Getting Started

### 1. Requirements
- Windows 10/11 (64-bit)
- Python 3.10+
- NVDA (or any SAPI-compatible screen reader)
- Stereo headphones (recommended for 3D binaural audio)

### 2. How to Host the Server (On Your Computer)
Run the server launcher from the project folder:
```powershell
python server/run_server.py
```
The server console will automatically display:
- Your local IP address for friends on your Wi-Fi/LAN (e.g., `192.168.1.12:7777`)
- Your localhost address for testing on the same PC (`127.0.0.1:7777`)
- Tailscale IP if connected

#### Sharing Securely with Friends Over the Internet (Tailscale Port 7777 Only):
To let friends connect without port-forwarding your home router AND without exposing your PC:
1. Double-click `server\enable_tailscale_serve.bat` (or run it in terminal).
2. It logs into Tailscale and runs:
   ```powershell
   tailscale serve --bg --tcp 7777 tcp://127.0.0.1:7777
   ```
3. This creates a secure raw TCP tunnel exposing **only port 7777**. All other files, Windows network shares, and ports remain completely inaccessible to friends.
4. Give your friends your Tailscale IP (e.g. `100.x.y.z:7777`).

**Host Console Commands:**
- `list`: Show all connected players, their rooms, and auras.
- `rooms`: Show active world rooms and player counts.
- `say <message>`: Broadcast a host announcement to all players.
- `quit`: Stop server.

---

### 3. How to Connect as a Client (You & Your Friends)
Run the client launcher:
```powershell
# Connect locally on your computer:
python client/run_client.py --username Alice

# Friends connecting over Wi-Fi / LAN:
python client/run_client.py --username Bob --server 192.168.1.50 --port 7777
```

---

## 4. Running the C++ Native Client (Recommended: Zero Crackle)

We built a native 64-bit C++ engine (`bin\FakeVR_Client.exe`) for deterministic, crackle-free audio performance:
- Double-click `run_client_cpp.bat` (or run `bin\FakeVR_Client.exe` in terminal).
- To recompile anytime: run `build_cpp.bat`.

---

## Keyboard Controls & NVDA Keybindings

| Key | Action | NVDA Announcement |
|---|---|---|
| **W / S** | Move Forward / Backward | Plays footstep based on ground surface |
| **A / D** | Strafe Left / Right | Plays footstep |
| **Left / Right Arrow** | Turn head / body (30° step) | Speaks new compass heading (North, East, etc.) |
| **Spacebar** | **Active Echolocation Snap** | Emits sonic pulse; returns 3D acoustic reflections from walls |
| **1 - 5** | Quick-switch Acoustic Aura | Speaks equipped aura (Vinyl, Chime, Hum, Silk, Off) |
| **C** | Announce Coordinates & Surface | Speaks X/Z location, compass angle, and floor type |
| **P** | Nearby Players Radar | Speaks distance and clock directions (e.g. *"Bob is at 2 o'clock, 4.5m away"*) |
| **M** or **Tab** | Open Accessible Menu | Opens navigable hierarchy |
| **Up / Down Arrow** | Navigate Menu Items | Speaks selected option and item index |
| **Enter** | Confirm / Drill into Submenu | Activates option |
| **Escape** | Back / Exit Menu | Returns to movement |
| **H** | Keybinding Help | Speaks full controls summary |

---

## Adding Your Own Sounds (Replacing Blank Placeholders)

All sound files in `assets/sounds/` are currently **blank (silent) placeholders** with valid 48kHz WAV headers. The game will run and spatialise immediately, but when you are ready to add real audio:

1. Open `assets/sounds/`
2. Drop your recorded `.wav` files into the appropriate folders:
   - `footsteps/`: `wood.wav`, `stone.wav`, `carpet.wav`, `metal.wav`, `grass.wav`, `tile.wav`
   - `echolocation/`: `click.wav`, `echo_wall.wav`, `echo_resonance.wav`
   - `auras/`: `vinyl.wav`, `chime.wav`, `hum.wav`, `silk.wav`
   - `environment/`: `wall_bump.wav`, `resonance_ambient.wav`, `door_transition.wav`
   - `ui/`: `menu_navigate.wav`, `menu_select.wav`, `player_join.wav`, etc.
3. Recommended format: **48,000 Hz Mono 16-bit PCM WAV** (Mono ensures Steam Audio can pinpoint the sound in 3D binaural space).
4. See `assets/sounds/SOUND_CATALOG.md` for full descriptions of each sound.

---

## Architecture For Developers
See `AGENTS.md` for the complete engineering documentation, Steam Audio C-API ctypes bindings, and network protocol specifications.
