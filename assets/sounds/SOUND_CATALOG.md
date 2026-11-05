# Fake-VR Sound Design Specification & Asset Manifest

This document outlines all audio assets required for **Fake-VR**—an audio-first, spatial cyberspace platform designed specifically for blind and low-vision players.

Because vision is replaced by sound, **audio is the primary rendering engine**. Assets must be designed with clean dynamic range, authentic physical materiality, precise transient cues, and no synthetic clipping or artificial brick-wall compression.

---

## 1. Technical Delivery Specifications

| Parameter | Requirement | Notes |
|---|---|---|
| **Format** | WAV (Uncompressed PCM) | Standard RIFF container |
| **Bit Depth** | **24-bit** signed integer (or 16-bit) | 24-bit preferred for maximum dynamic range |
| **Sample Rate** | **48,000 Hz** (48 kHz) | Matches hardware WASAPI output engine |
| **Channel Layout** | **Mono** for spatialized sources / **Stereo** for field ambiences | Footsteps, impacts, destructions, and clicks **must be Mono** so Steam Audio can accurately place them in 3D HRTF space |
| **Peak Normalization** | -1.0 dBFS to -3.0 dBFS peak | Preserve head-room for binaural HRTF convolution |
| **Tail Truncation** | Clean zero-crossing fades | Prevent clicks/pops at asset boundaries |

---

## 2. Core Interactive Sound Assets (Active Gameplay)

### A. Locomotion & Footsteps (`assets/sounds/footsteps/`)
*All footstep assets must be recorded as close-mic dry mono samples. Room decay is dynamically calculated by the raytracer and impulse convolution.*

| Asset File Name | Description & Acoustic Character | Duration |
|---|---|---|
| `wood.wav` | Hardwood residential floor. Dry heel-toe strike, subtle oak body resonance. | 0.35s - 0.45s |
| `carpet.wav` | Soft plush wool rug. Muffled, deep low-mid thud, almost zero high-frequency click. | 0.30s - 0.40s |
| `grass.wav` | Outdoor turf / meadow lawn. Crisp blade rustle followed by damp earth impact. | 0.35s - 0.50s |
| `stone.wav` | Polished cobblestone / concrete pavement. Sharp transient tap with hard density. | 0.30s - 0.40s |
| `tile.wav` | Glazed ceramic bathroom/kitchen tile. High-frequency click with bright reflective slap. | 0.25s - 0.35s |
| `metal.wav` | Hollow steel plating or corrugated walkway. Distinct metallic ping and slight ring. | 0.35s - 0.50s |

### B. Active Echolocation (`assets/sounds/echolocation/`)
*The echolocation click is the player's primary spatial probe. It must cut through any room reverb with absolute clarity.*

| Asset File Name | Description & Acoustic Character | Duration |
|---|---|---|
| `click.wav` | Ultra-crisp human tongue-palate click or mechanical clicker. Extremely sharp transient (1.5kHz - 6kHz emphasis), minimal tail. Must not sound painful at high volumes. | 0.04s - 0.08s |

### C. Combat & Physical Attack (`assets/sounds/combat/`)
*Players hit objects with weapons or bare hands to gauge density and destroy obstacles.*

| Asset File Name | Description & Acoustic Character | Duration |
|---|---|---|
| `punch_miss.wav` | Swung fist through open air. Subtle aerodynamic whoosh / cloth rustle. | 0.20s - 0.30s |
| `punch_impact.wav` | Bare knuckle strike against a solid surface. Fleshy bone impact + dense contact smack. | 0.15s - 0.25s |
| `hammer_swing.wav` | Heavy sledgehammer / mallet swing through air. Deep low-frequency whoosh. | 0.30s - 0.45s |
| `hammer_hit_wood.wav` | Solid sledgehammer blow into oak timber. Heavy wood fracture crack + deep thud. | 0.35s - 0.50s |
| `hammer_hit_stone.wav` | Sledgehammer striking rock/boulder. Violent high-pitch chime + dense shockwave. | 0.30s - 0.45s |
| `hammer_hit_metal.wav` | Sledgehammer striking tool shed or container. Massive metallic anvil clang with sub ring. | 0.45s - 0.70s |
| `hammer_hit_glass.wav` | Sledgehammer fracturing thick glass pane. Heavy impact with immediate spiderweb cracks. | 0.30s - 0.45s |

### D. Material Destruction & Structural Collapse (`assets/sounds/environment/`)
*Played in 3D when an object's health reaches 0.*

| Asset File Name | Description & Acoustic Character | Duration |
|---|---|---|
| `destroy_glass.wav` | Display cabinet / greenhouse shattering. High-energy explosive fracture followed by a shower of sharp falling glass shards. | 0.8s - 1.2s |
| `destroy_wood.wav` | Coffee table / bookcase / bench snapping. Violent structural fracture, splintering timber, and heavy wooden pieces falling to the floor. | 0.7s - 1.0s |
| `destroy_metal.wav` | Tool shed / equipment rack / tractor collapse. Heavy sheet metal buckling, deep structural gong groan, bolts shearing, and heavy steel drop. | 0.9s - 1.4s |
| `destroy_stone.wav` | Boulder / monolith / well crumbling. Deep subterranean fracture crack, tumbling granite rocks, gravel spray, and heavy rubble impact. | 0.8s - 1.3s |

### E. Environment Physical Feedback (`assets/sounds/environment/`)

| Asset File Name | Description & Acoustic Character | Duration |
|---|---|---|
| `wall_bump.wav` | Avatar walking directly into a wall or obstacle. Low-frequency chest/shoulder thump against drywall/wood (60Hz - 120Hz). | 0.20s - 0.30s |
| `door_creak_open.wav` | Wooden residential door swinging open into outdoors. Realistic hinge friction + gentle latch release. | 0.6s - 1.0s |
| `door_creak_close.wav` | Door swinging shut with a solid, satisfying latch click into the wooden frame. | 0.5s - 0.8s |

---

## 3. Spatial Environmental Ambiences (Bed Layers)

*Ambiences in Fake-VR are crucial: they provide psychoacoustic anchors for blind players to distinguish indoor vs. outdoor spaces, orientation, and weather.*

### A. Indoor Residential Lounge (`assets/sounds/ambience/interior/`)
*All interior loops should be seamless, stereo, and mixed quietly (-24 dBFS to -30 dBFS).*

| Asset File Name | Description & Acoustic Character | Duration |
|---|---|---|
| `lounge_room_tone.wav` | Subtle residential room air. Air conditioning vent hiss, room pressure, gentle floorboard micro-creak. | 30s - 60s (Loop) |
| `clock_tick.wav` | Vintage mantle clock on bookcase. Warm mechanical tick-tock (mono 3D beacon to help blind users navigate by ear). | 1.0s (Loop) |
| `fireplace_gentle.wav` | Cozy embers crackling with subtle wood pops (mono 3D sound source). | 30s - 45s (Loop) |

### B. Great Outdoor Field (`assets/sounds/ambience/exterior/`)
*Conveys a 100m x 100m open sky atmosphere.*

| Asset File Name | Description & Acoustic Character | Duration |
|---|---|---|
| `field_wind_open.wav` | Broad outdoor wind blowing across tall grass. Dynamic stereo panning, gentle gust swells, no whistling. | 45s - 60s (Loop) |
| `field_distant_birds.wav` | Sparse songbirds calling from distant tree line boundaries (North/East). | 40s - 60s (Loop) |
| `field_crickets_night.wav` | Nighttime outdoor field setting with distributed field crickets and distant wind. | 30s - 45s (Loop) |
| `water_well_drips.wav` | Old Stone Well spatial audio: deep resonant water drips falling into subterranean water pool (mono 3D beacon at $X=18, Z=42$). | 10s - 15s (Loop) |

---

## 4. Acoustic Fashion & Avatar Auras (`assets/sounds/auras/`)

*In Fake-VR, visual avatar skins are replaced with Acoustic Auras. These are subtle, loopable 3D psychoacoustic signatures attached to players so other blind users can recognize them by ear.*

| Asset File Name | Description & Acoustic Character | Duration |
|---|---|---|
| `vinyl.wav` | **Vinyl Warmth:** Cozy analog record crackle, gentle needle dust hiss, warm low-end tube saturation. | 15s - 25s (Loop) |
| `chime.wav` | **Crystal Chimes:** Delicate wind chimes in a soft breeze. Pure, crystalline upper harmonics (3kHz - 8kHz) that shimmer delicately. | 15s - 20s (Loop) |
| `hum.wav` | **Sub-Bass Hum:** Deep meditative harmonic hum (55Hz - 110Hz). Warm transformer vibration, grounded and reassuring. | 20s - 30s (Loop) |
| `silk.wav` | **Silk Rustle:** Luxurious fabric motion. Gentle micro-frictional whisper of fine garments moving through space. | 10s - 15s (Loop) |
| `ember.wav` (New) | **Ember Hearth:** Faint, rhythmic coal sizzle with deep thermal warmth. | 15s - 20s (Loop) |
| `clockwork.wav` (New) | **Pocket Watch:** Intricate brass gears and escapement wheel ticking softly in 3D space. | 10s - 15s (Loop) |

---

## 5. User Interface & Accessible Navigation (`assets/sounds/ui/`)

*Screen-reader users rely on instant, non-fatiguing earcons for system events.*

| Asset File Name | Description & Acoustic Character | Duration |
|---|---|---|
| `menu_navigate.wav` | Up/Down list navigation. Short, subtle woody marimba pop or subtle blip. | 0.04s - 0.08s |
| `menu_select.wav` | Confirmation/activation. Ascending bright acoustic interval (e.g. major third bell). | 0.10s - 0.15s |
| `menu_back.wav` | Back/Escape out of menu. Descending soft acoustic tone. | 0.10s - 0.15s |
| `player_join.wav` | Another player enters the world. Warm welcoming spatial acoustic chime. | 0.40s - 0.60s |
| `player_leave.wav` | Player disconnects. Soft receding tone that fades out naturally. | 0.40s - 0.60s |
| `inventory_pickup.wav` | Grabbing an item / weapon from the ground. Crisp leather/metal equip rustle. | 0.15s - 0.25s |
| `chat_whisper.wav` | Spatial text-to-speech chat notification or beacon ping. Subtle radar blip. | 0.12s - 0.20s |

---

## 6. Priority Order for the Audio Engineer

1. **Phase 1 (Immediate / Core Gameplay):**
   - Clean 48kHz mono footstep replacements (`wood.wav`, `stone.wav`, `grass.wav`, `metal.wav`, `tile.wav`, `carpet.wav`).
   - High-fidelity tongue/clicker echolocation snap (`click.wav`).
   - Material destruction sounds (`destroy_glass.wav`, `destroy_wood.wav`, `destroy_metal.wav`, `destroy_stone.wav`).
   - Physical wall bump and punch sounds (`wall_bump.wav`, `punch_impact.wav`).

2. **Phase 2 (Immersion & Ambiences):**
   - Seamless room tone loop for the lounge (`lounge_room_tone.wav`).
   - Outdoor field wind and tree-line ambience loop (`field_wind_open.wav`).
   - Spatial landmark beacon for the well (`water_well_drips.wav`).

3. **Phase 3 (Multiplayer & Fashion):**
   - Clean avatar aura loops (`vinyl.wav`, `chime.wav`, `hum.wav`, `silk.wav`).
   - Non-fatiguing accessible UI earcons (`menu_navigate.wav`, `menu_select.wav`).
