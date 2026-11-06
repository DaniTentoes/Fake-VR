"""
Sound Bank manager for Fake-VR.
Loads, normalizes, resamples, and caches all WAV sound assets.
Supports 8-bit, 16-bit, 24-bit, 32-bit PCM and IEEE Float WAV formats,
multi-channel stereo downmix, and case-insensitive file resolution.
"""

from __future__ import annotations
import os
import wave
import warnings
from typing import Dict, List, Optional
import numpy as np
from scipy.io import wavfile
from scipy import signal

from shared.models import SurfaceType, AuraType


class SoundBank:
    """Loads, resamples, and caches audio buffers for the client spatial audio mixer."""

    def __init__(self, assets_dir: Optional[str] = None, target_sample_rate: int = 48000):
        # Walk up from client/audio to project root
        base = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
        if assets_dir is None:
            self.search_dirs = [
                os.path.join(base, "assets", "sounds"),
                os.path.join(base, "assets")
            ]
        else:
            self.search_dirs = [assets_dir, os.path.join(base, "assets")]

        self.target_sample_rate = target_sample_rate
        # Maps lowercase relative key (e.g. "footsteps/wood.wav") to np.ndarray
        self._cache: Dict[str, np.ndarray] = {}
        self.load_all()

    def _resolve_file_path(self, relative_path: str) -> Optional[str]:
        """Case-insensitively locates a relative file path across asset search directories."""
        rel_norm = os.path.normpath(relative_path).lower()
        rel_parts = rel_norm.split(os.sep)

        for base_dir in self.search_dirs:
            if not os.path.exists(base_dir):
                continue

            # Try exact direct match first
            direct = os.path.join(base_dir, relative_path)
            if os.path.exists(direct):
                return direct

            # Case-insensitive recursive/stepped match
            curr = base_dir
            found = True
            for part in rel_parts:
                try:
                    entries = os.listdir(curr)
                except OSError:
                    found = False
                    break

                matched_name = next((e for e in entries if e.lower() == part), None)
                if matched_name is None:
                    found = False
                    break
                curr = os.path.join(curr, matched_name)

            if found and os.path.isfile(curr):
                return curr

        return None

    def _load_wav(self, file_path: str) -> np.ndarray:
        """
        Universal WAV loader:
        Decodes 8-bit, 16-bit, 24-bit, 32-bit integer PCM and 32-bit float audio,
        averages multi-channel/stereo to mono, and resamples to target_sample_rate.
        """
        if not file_path or not os.path.exists(file_path):
            return np.zeros(int(self.target_sample_rate * 0.1), dtype=np.float32)

        # 1. Attempt decoding with scipy.io.wavfile (handles 16-bit, 24-bit, 32-bit, float)
        try:
            with warnings.catch_warnings():
                warnings.simplefilter("ignore")
                rate, raw_data = wavfile.read(file_path)

            if raw_data.dtype == np.int16:
                f_data = raw_data.astype(np.float32) / 32768.0
            elif raw_data.dtype == np.int32:
                # Scipy reads 24-bit PCM as 32-bit signed ints shifted left by 8 bits
                f_data = raw_data.astype(np.float32) / 2147483648.0
            elif raw_data.dtype == np.uint8:
                f_data = raw_data.astype(np.float32) / 128.0 - 1.0
            elif np.issubdtype(raw_data.dtype, np.floating):
                f_data = raw_data.astype(np.float32)
            else:
                f_data = raw_data.astype(np.float32)

            # Sum/average multichannel to mono for 3D HRTF spatialization
            if f_data.ndim > 1:
                f_data = f_data.mean(axis=1)

            # Resample if sample rate does not match target (e.g. 44.1kHz -> 48kHz)
            if rate != self.target_sample_rate and len(f_data) > 0:
                num_target = int(round(len(f_data) * self.target_sample_rate / rate))
                f_data = signal.resample(f_data, num_target).astype(np.float32)

            return f_data

        except Exception as e:
            # 2. Fallback to manual wave.open unpacker (with custom 24-bit bit-shift support)
            try:
                with wave.open(file_path, "rb") as wf:
                    n_channels = wf.getnchannels()
                    sampwidth = wf.getsampwidth()
                    framerate = wf.getframerate()
                    frames = wf.readframes(wf.getnframes())

                if sampwidth == 1:
                    data = np.frombuffer(frames, dtype=np.uint8).astype(np.float32) / 128.0 - 1.0
                elif sampwidth == 2:
                    data = np.frombuffer(frames, dtype=np.int16).astype(np.float32) / 32768.0
                elif sampwidth == 3:
                    # 24-bit PCM manual unpack
                    raw = np.frombuffer(frames, dtype=np.uint8).reshape(-1, 3)
                    b0 = raw[:, 0].astype(np.int32)
                    b1 = raw[:, 1].astype(np.int32) << 8
                    b2 = raw[:, 2].astype(np.int32) << 16
                    val = b0 | b1 | b2
                    val = np.where(val >= 0x800000, val - 0x1000000, val)
                    data = val.astype(np.float32) / 8388608.0
                elif sampwidth == 4:
                    data = np.frombuffer(frames, dtype=np.int32).astype(np.float32) / 2147483648.0
                else:
                    return np.zeros(int(self.target_sample_rate * 0.1), dtype=np.float32)

                if n_channels > 1:
                    data = data.reshape(-1, n_channels).mean(axis=1)

                if framerate != self.target_sample_rate and len(data) > 0:
                    num_target = int(round(len(data) * self.target_sample_rate / framerate))
                    data = signal.resample(data, num_target).astype(np.float32)

                return data.astype(np.float32)

            except Exception as e_inner:
                print(f"[SoundBank Warning]: Failed to read '{file_path}': {e_inner}")
                return np.zeros(int(self.target_sample_rate * 0.1), dtype=np.float32)

    def load_all(self) -> None:
        """Preloads all sounds across all asset subdirectories and indexes them case-insensitively."""
        loaded_count = 0

        for base_dir in self.search_dirs:
            if not os.path.exists(base_dir):
                continue

            for root, _, files in os.walk(base_dir):
                for f in files:
                    if f.lower().endswith(".wav"):
                        full_path = os.path.join(root, f)
                        rel_path = os.path.relpath(full_path, base_dir).lower().replace("\\", "/")
                        if rel_path not in self._cache:
                            buf = self._load_wav(full_path)
                            self._cache[rel_path] = buf
                            if rel_path.endswith(".wav"):
                                no_ext = rel_path[:-4]
                                if no_ext not in self._cache:
                                    self._cache[no_ext] = buf
                            loaded_count += 1
                            peak = np.max(np.abs(buf)) if len(buf) > 0 else 0.0
                            if peak > 0.01:
                                print(f"[SoundBank]: Loaded audio '{rel_path}' ({len(buf)/self.target_sample_rate:.2f}s, peak: {peak:.2f})")

        print(f"[SoundBank]: Indexed {len(self._cache)} sound assets.")

    def get(self, key: str) -> np.ndarray:
        """Retrieves an audio buffer by relative key (case-insensitive and tolerant of prefixes/extensions)."""
        norm_key = key.lower().replace("\\", "/").strip().lstrip("/")
        if norm_key.startswith("assets/sounds/"):
            norm_key = norm_key[len("assets/sounds/"):]
        elif norm_key.startswith("sounds/"):
            norm_key = norm_key[len("sounds/"):]

        if norm_key in self._cache:
            return self._cache[norm_key]
        if norm_key.endswith(".wav") and norm_key[:-4] in self._cache:
            return self._cache[norm_key[:-4]]
        if not norm_key.endswith(".wav") and (norm_key + ".wav") in self._cache:
            return self._cache[norm_key + ".wav"]

        # Also check bare filename in cache
        base_name = os.path.basename(norm_key)
        for k, buf in self._cache.items():
            if k == base_name or (base_name.endswith(".wav") and k == base_name[:-4]) or ((k + ".wav") == base_name):
                return buf

        # On-demand lookup if not cached during startup
        resolved = self._resolve_file_path(key)
        if resolved:
            buf = self._load_wav(resolved)
            self._cache[norm_key] = buf
            return buf

        # Fallback to wood_1.wav if available instead of dead silence
        for fb in ("footsteps/wood_1.wav", "wood_1.wav", "footsteps/wood.wav"):
            if fb in self._cache:
                return self._cache[fb]

        # Fallback 0.1s silence
        return np.zeros(int(self.target_sample_rate * 0.1), dtype=np.float32)

    def get_footstep(self, surface: SurfaceType) -> np.ndarray:
        """Returns the appropriate footstep sound for the given ground surface."""
        mapping = {
            SurfaceType.WOOD: "footsteps/wood.wav",
            SurfaceType.STONE: "footsteps/stone.wav",
            SurfaceType.CARPET: "footsteps/carpet.wav",
            SurfaceType.METAL: "footsteps/metal.wav",
            SurfaceType.GRASS: "footsteps/grass.wav",
            SurfaceType.TILE: "footsteps/tile.wav"
        }
        key = mapping.get(surface, "footsteps/wood.wav")
        return self.get(key)

    def get_aura(self, aura: AuraType) -> Optional[np.ndarray]:
        """Returns looping audio buffer for the chosen Acoustic Fashion Aura."""
        mapping = {
            AuraType.VINYL: "auras/vinyl.wav",
            AuraType.CHIME: "auras/chime.wav",
            AuraType.HUM: "auras/hum.wav",
            AuraType.SILK: "auras/silk.wav"
        }
        key = mapping.get(aura)
        if key:
            buf = self.get(key)
            if np.max(np.abs(buf)) > 1e-4:
                return buf
        return None
