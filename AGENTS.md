# AGENTS.md — Fake-VR (Audio-First Social Metaverse for Blind Users)

## Executive Summary & Mission
**Fake-VR** is an accessible, real-time audio-first social cyberspace platform inspired by VRChat, designed specifically for blind and low-vision users. Instead of treating accessibility as an afterthought (or reducing the experience to a robotic text-to-speech spreadsheet), Fake-VR elevates **spatial acoustics, raytraced echolocation, surface materiality, and acoustic fashion** as first-class expressive media.

---

## Core Propositions Implemented

1. **Realistic Residential Lounge & Doorway to $100\text{m} \times 100\text{m}$ Outdoor Field:**
   - Features a cozy residential lounge ($7.0\text{m} \times 8.0\text{m}$) with a north doorway opening at $X: [-0.7, 0.7], Z = 4.0$.
   - **Seamless Outdoor Transition:** Walking through the doorway exits onto a massive **$100\text{m} \times 100\text{m}$ open grass field** ($X: [-50, 50], Z: [4, 104]$).
   - Stepping outside switches ground footstep materiality immediately to **grass** footsteps and open-air acoustic propagation.

2. **Real Physical & Destructible Objects:**
   - Populated with authentic tangible items in both the lounge and the field:
     - *In Lounge:* Fabric Sofa Couch, Solid Oak Coffee Table, Metal Equipment Rack, Wooden Bookcase, Glass Display Cabinet.
     - *In Field:* Wooden Park Bench, Steel Tool Shed, Granite Boulder, Wooden Gazebo Pavilion, Metal Cargo Container, Stone Water Well, Lumber Stack Pile, Glass Greenhouse Nursery, Steel Farm Tractor, and Ancient Stone Monolith.
   - All objects feature **health points (`health`), collision bounds, material physics**, and a `destructible = true` flag with linked obstacle segments ready for destruction mechanics.

3. **Hybrid Acoustic Model: Live 8-Ray Omnidirectional Raytracing + Apple IR Tails:**
   - Direct sound, early reflections, and proximity intensification are rendered live with 16 independent Valve Steam Audio binaural effect units.
   - Reflections dynamically follow physical laws: $t = \frac{2d}{343\text{ m/s}}$, amplitude falloff, and surface absorption (Glass: 0.96, Metal: 0.92, Stone: 0.80, Wood: 0.58, Carpet: 0.18, Grass: 0.15).
   - Doorways are physically permeable: acoustic rays escape out into the open field, signaling doorways and openings to blind users naturally by ear.
   - Convolved with Apple `03 Small Spaces` (`0.4s_Little Room.SDIR`) for natural room reverberation without cavernous washing.

3. **Material Acoustic Physics:**
   - Materials feature distinct physical parameters:
     - `reflectivity`: Absorption factor ($0.0$ to $1.0$).
     - `highFreqDamping`: Muffling factor (e.g. carpet/fabric: $0.85$, glass/metal: $0.05$).
     - `resonancePitch`: Characteristic material pitch resonant frequency.

4. **Realistic Movement Cadence & Zero-Lag Audio Rendering:**
   - Walking pace is regulated by a natural human locomotion throttle ($380\text{ms}$ step cadence with $0.55\text{m}$ stride length $\approx 1.45\text{ m/s}$), preventing key-repeat speed exploits and allowing footsteps to settle naturally with the room acoustics.
   - Sound effects and spatial reflections are rendered **locally** using PortAudio and Steam Audio to guarantee instant auditory feedback on keypresses without network latency jitter.

5. **NVDA First-Class Controller (SAPI Completely Disabled):**
   - Linked directly to `nvdaControllerClient64.dll` (`nvdaController_speakText`).
   - SAPI COM voice is completely stripped and disabled. If NVDA is running, all announcements speak through your active NVDA speech synthesizer.

6. **Combat & Destruction Physics:**
   - **`Spacebar`**: Attack / strike the object directly in front of the avatar.
   - **`U`**: Active 8-ray omnidirectional echolocation pulse.
   - Bare hands inflict **$25\text{ damage}$** per hit. When an object's health reaches $0$:
     - It shatters / collapses with **material-specific destruction audio**:
       - *Glass:* High-frequency glass shatter (`destroy_glass.wav`)
       - *Wood:* Splintering fracture & wood collapse (`destroy_wood.wav`)
       - *Metal:* Heavy metallic gong & clang crash (`destroy_metal.wav`)
       - *Stone:* Dense rock rubble crumble (`destroy_stone.wav`)
     - Once destroyed, its collision bounds dissolve and sound rays pass freely through its space.

7. **Character Foley & Clothing Acoustic System:**
   - Locomotion renders both ground footstep audio and torso-level **clothing foley** simultaneously through 3D Steam Audio.
   - **Outfits Supported (`T` key to cycle):**
     - **Regular Clothes:** Cotton / denim fabric friction rustles (`assets/sounds/foley/regular/regular_1..4.wav`).
     - **Naked:** Subtle flesh / skin-on-skin motion taps (`assets/sounds/foley/naked/naked_1..4.wav`).
     - **Suit of Armor:** Heavy steel plate clangs, chainmail rattles, and buckle friction (`assets/sounds/foley/armor/armor_1..4.wav`).
   - **4-Way Randomized Variations:** Footsteps (`wood_1..4`, `stone_1..4`, `carpet_1..4`, `grass_1..4`, `tile_1..4`, `metal_1..4`) and foley files are randomly selected each step to prevent machine-gun audio repetition.
   - Total indexed sound bank expanded to **68 sound assets**.

---

## System Architecture

```
Fake-VR/
├── AGENTS.md                     # Technical record & architecture
├── README.md                     # End-user & host documentation
├── build_cpp.bat                 # MSVC x64 build script for native client
├── run_client_cpp.bat            # Launcher for native C++ client
├── bin/                          # Compiled native C++ binaries & runtime DLLs
│   ├── FakeVR_Client.exe         # High-performance native 64-bit client
│   ├── phonon.dll                # Valve Steam Audio v4.8.1 core
│   ├── GPUUtilities.dll
│   └── TrueAudioNext.dll
├── client_cpp/                   # Native C++ Audio & Client Subsystem
│   ├── include/                  # miniaudio.h, phonon.h, models.hpp, audio_engine_cpp.hpp
│   └── src/                      # main.cpp (Console loop, keyboard, WASD navigation)
├── shared/                       # Common domain definitions & protocols
│   ├── __init__.py
│   ├── models.py                 # Vector3, SurfaceType, AuraType, PlayerState
│   ├── world.py                  # Room definitions, Raycasting, Resonance Lounge
│   └── protocol.py               # JSON-framed TCP packet types & serialization
├── server/                       # Multiplayer host server module
│   ├── __init__.py
│   ├── server.py                 # 20Hz Asyncio TCP server & broadcaster
│   ├── room_manager.py           # Room partitioning & player sessions
│   └── run_server.py             # Server launcher with LAN/IP auto-discovery
├── client/                       # Client game application
│   ├── __init__.py
│   ├── run_client.py             # Client main loop & orchestrator
│   ├── input_handler.py          # WASD movement, orientation, hotkeys
│   ├── screen_reader.py          # NVDA Controller Client & TTS bridge
│   ├── ui_menu.py                # Accessible hierarchical menu (Up/Down/Enter/Esc)
│   ├── network_client.py         # Asynchronous TCP client
│   ├── audio/                    # Spatial audio subsystem
│   │   ├── __init__.py
│   │   ├── steam_audio.py        # Valve Steam Audio v4.8.1 ctypes wrapper
│   │   ├── audio_engine.py       # PortAudio stream & binaural mixer
│   │   ├── sound_bank.py         # WAV cache & surface/aura asset resolver
│   │   └── blank_assets.py       # Blank WAV placeholder generator
│   └── lib/                      # Precompiled 64-bit binaries
│       ├── phonon.dll            # Valve Steam Audio v4.8.1 core
│       ├── GPUUtilities.dll
│       └── TrueAudioNext.dll
└── assets/
    └── sounds/                   # 48kHz 16-bit Mono PCM WAV files
        ├── SOUND_CATALOG.md      # Specification & guide for replacing placeholders
        ├── footsteps/            # wood, stone, carpet, metal, grass, tile
        ├── echolocation/         # click, echo_wall, echo_resonance
        ├── auras/                # vinyl, chime, hum, silk
        ├── environment/          # wall_bump, resonance_ambient, door_transition
        └── ui/                   # menu_navigate, menu_select, player_join, etc.
```

---

## Steam Audio v4.8.1 ctypes Integration

Steam Audio is wrapped directly via `ctypes` in `client/audio/steam_audio.py`.

### Key Bindings:
- **`IPLContext`**: Initialized with `STEAMAUDIO_VERSION = (4 << 16) | (8 << 8) | 1` (`0x040801`).
- **`IPLHRTF`**: Default HRTF model loaded via `iplHRTFCreate`.
- **`IPLBinauralEffect`**: Applied per frame to mono buffers to generate binaural stereo with pinna and torso filtering.
- **`IPLReflectionEffect`**: 3-band parametric room reverberator (`IPL_REFLECTIONEFFECTTYPE_PARAMETRIC`) generating diffuse room decay tails and acoustic slapback.
- **Coordinate Transformation**:
  - World coordinate system: `+X` = East, `+Y` = Up, `+Z` = North (Yaw 0°).
  - Steam Audio local frame: `+X` = Right, `+Y` = Up, `-Z` = Ahead.
  - Listener orientation rotation is calculated before feeding unit direction vectors to `iplBinauralEffectApply`.
- **Audio Output**: 48,000 Hz, 1024 frame blocksize via Windows **WASAPI** (Device auto-selection matching system default to prevent MME 44.1kHz sample rate conversions and buffer crackling).

---

## Network Protocol Specification

TCP Newline-delimited JSON (`\n` framing).

| Packet Type | Direction | Payload | Description |
|---|---|---|---|
| `c2s_join` | Client -> Server | `{"username": str, "room_id": str, "aura": str}` | Handshake request |
| `s2c_welcome` | Server -> Client | `{"player_id": str, "room_id": str, "position": dict, "other_players": list}` | Handshake confirmation |
| `c2s_move` | Client -> Server | `{"position": dict, "yaw": float, "pitch": float, "is_moving": bool, "surface": str}` | Player position update |
| `s2c_world_tick` | Server -> Client | `{"players": list}` | 20Hz authoritative snapshot |
| `c2s_echolocation` | Client -> Server | `{"position": dict, "yaw": float}` | Active snap trigger |
| `s2c_echolocation_event` | Server -> Client | `{"player_id": str, "position": dict, "yaw": float}` | Relayed snap event |
| `c2s_aura_change` | Client -> Server | `{"aura": str}` | Acoustic fashion update |
| `s2c_aura_change` | Server -> Client | `{"player_id": str, "aura": str}` | Relayed aura change |
| `c2s_room_change` | Client -> Server | `{"room_id": str}` | World transfer request |
| `s2c_chat` | Server <-> Client | `{"player_id": str, "username": str, "text": str, "position": dict}` | Spatial chat / voice beacon |

---

## Audio Asset Strategy & Universal Decoder
Sound files in `assets/sounds/` can be replaced with custom recordings anytime:
- **Formats Supported:** 8-bit unsigned PCM, 16-bit signed PCM, **24-bit signed PCM**, 32-bit signed PCM, and 32-bit IEEE Float WAV.
- **Channels:** Both Mono and Stereo recordings are automatically handled (stereo recordings are averaged down to mono dynamically for 3D Steam Audio spatialization).
- **Sampling Rates:** Any sampling rate (e.g. 44.1kHz, 48kHz, 96kHz) is automatically resampled to the 48,000 Hz hardware output rate via `scipy.signal.resample`.
- **Case-Insensitive Resolution:** Windows filesystem case quirks (e.g. `Stone.wav` vs `stone.wav`) are normalized automatically.
- **Blank Placeholders:** Unreplaced sounds remain silent 48kHz WAV placeholders so the engine never throws missing-file exceptions.

---

## Instructions for Future Collaborating Agents

1. **Adding New Worlds/Rooms:**
   - Define a new `Room` object in `shared/world.py` with boundaries, surface zones, spawn points, and optional `AcousticMirrorZone`.
   - Register it in `WORLD_REGISTRY`. The client menu will automatically pick it up in "World Travel".
2. **Expanding Steam Audio Capabilities:**
   - Steam Audio also supports Geometry Meshes (`IPLStaticMesh`), Materials (`IPLMaterial`), and Raytraced Indirect Sound (Reverb/Reflections via `IPLSimulator`).
   - If full 3D OBJ mesh reverberation is required later, look at `iplStaticMeshCreate` and `iplSimulatorRunReflections`.
3. **Voice Chat (VoIP):**
   - The current architecture uses text/beacon chat. To add real-time microphone VoIP:
     - Use `sounddevice.InputStream` on the client.
     - Compress with Opus (`opuslib` / `pyogg`).
     - Transmit voice packets over UDP to the server.
     - Spatialize incoming remote voice buffers through `SteamAudioSpatializer.spatialize_frame`.
4. **Hardware Extension (Haptics & 6DOF):**
   - If VR controllers or haptic vests (bHaptics) are added later, hook haptic pulses into `InputHandler.on_wall_bump` and proximity distances in `AudioEngine._audio_callback`.
