"""
Generator for blank/placeholder audio files for Fake-VR.
Creates valid 48kHz 16-bit mono PCM WAV files containing silence, allowing the audio engine
to load and spatialise buffers seamlessly until high-fidelity recordings are dropped in.
"""

import os
import wave
import struct

# Standard 48kHz sampling rate, 16-bit mono
SAMPLING_RATE = 48000
SAMPLE_WIDTH = 2  # 16-bit = 2 bytes
CHANNELS = 1      # Mono source for 3D binaural spatialization

SOUND_MANIFEST = {
    # Footstep physics for surface detection
    "footsteps/wood.wav": {"duration_sec": 0.25, "desc": "Footstep on polished wood floor"},
    "footsteps/stone.wav": {"duration_sec": 0.25, "desc": "Footstep on stone / marble floor"},
    "footsteps/carpet.wav": {"duration_sec": 0.25, "desc": "Muffled footstep on plush carpet"},
    "footsteps/metal.wav": {"duration_sec": 0.25, "desc": "Resonant footstep on metal grating"},
    "footsteps/grass.wav": {"duration_sec": 0.25, "desc": "Soft crunch footstep on outdoor grass"},
    "footsteps/tile.wav": {"duration_sec": 0.25, "desc": "Crisp footstep on ceramic tile"},

    # Active echolocation pulses and acoustic reflections
    "echolocation/click.wav": {"duration_sec": 0.15, "desc": "Sharp finger-snap / tongue-click impulse for echolocation"},
    "echolocation/echo_wall.wav": {"duration_sec": 0.30, "desc": "Diffuse acoustic return reflected from bounding wall"},
    "echolocation/echo_resonance.wav": {"duration_sec": 0.60, "desc": "Bright, focused acoustic return inside the Resonance Lounge mirror"},

    # Acoustic fashion auras (Avatar identity)
    "auras/vinyl.wav": {"duration_sec": 2.0, "desc": "Warm vinyl crackle & dust texture loop"},
    "auras/chime.wav": {"duration_sec": 2.0, "desc": "Gentle crystal chime ambient harmonic loop"},
    "auras/hum.wav": {"duration_sec": 2.0, "desc": "Deep sub-harmonic resonance drone loop"},
    "auras/silk.wav": {"duration_sec": 2.0, "desc": "Whispering silk / fabric rustle acoustic loop"},

    # Environmental acoustic cues
    "environment/wall_bump.wav": {"duration_sec": 0.30, "desc": "Physical dull thud when colliding with room boundary"},
    "environment/resonance_ambient.wav": {"duration_sec": 3.0, "desc": "Subtle acoustic air resonance inside the Resonance Lounge"},
    "environment/door_transition.wav": {"duration_sec": 0.50, "desc": "Acoustic transition swoosh when teleporting / switching rooms"},

    # Non-verbal UI feedback
    "ui/menu_navigate.wav": {"duration_sec": 0.10, "desc": "Subtle mechanical click for menu cursor movement"},
    "ui/menu_select.wav": {"duration_sec": 0.15, "desc": "Crisp rising chime for confirming a menu selection"},
    "ui/menu_close.wav": {"duration_sec": 0.10, "desc": "Low falling click for exiting a menu"},
    "ui/player_join.wav": {"duration_sec": 0.40, "desc": "Welcoming spatialized chime when another player joins"},
    "ui/player_leave.wav": {"duration_sec": 0.40, "desc": "Gentle fade-out chime when a player disconnects"},
    "ui/chat_notification.wav": {"duration_sec": 0.25, "desc": "Soft pop when a chat message or beacon is received"}
}


def create_blank_wav(file_path: str, duration_sec: float) -> None:
    """Generates a valid silent mono PCM WAV file."""
    os.makedirs(os.path.dirname(file_path), exist_ok=True)
    total_samples = int(SAMPLING_RATE * duration_sec)
    # 16-bit silence is zeros
    silent_data = struct.pack('<h', 0) * total_samples

    with wave.open(file_path, 'wb') as wf:
        wf.setnchannels(CHANNELS)
        wf.setsampwidth(SAMPLE_WIDTH)
        wf.setframerate(SAMPLING_RATE)
        wf.writeframes(silent_data)


def initialize_blank_sound_assets(base_dir: str = "assets/sounds") -> None:
    """Creates all blank placeholder sound files if they do not already exist."""
    print(f"Initializing blank sound bank in: {base_dir}")
    created_count = 0
    skipped_count = 0

    for relative_path, meta in SOUND_MANIFEST.items():
        full_path = os.path.join(base_dir, relative_path)
        if not os.path.exists(full_path):
            create_blank_wav(full_path, meta["duration_sec"])
            created_count += 1
        else:
            skipped_count += 1

    print(f"Sound bank initialization complete: {created_count} created, {skipped_count} existing retained.")


if __name__ == "__main__":
    initialize_blank_sound_assets()
