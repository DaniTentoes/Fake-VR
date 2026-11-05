"""
High-Performance ctypes wrapper for Valve's Steam Audio (Phonon) v4.8.1.
Provides HRTF binaural 3D spatialization, distance attenuation, acoustic buffer processing,
and environmental reverberation (IPLReflectionEffect).
"""

from __future__ import annotations
import os
import math
import ctypes
from typing import Optional, Tuple
import numpy as np
from shared.models import Vector3

# Steam Audio Constants
IPL_HRTFTYPE_DEFAULT = 0
IPL_HRTFNORMTYPE_NONE = 0
IPL_HRTFINTERPOLATION_BILINEAR = 1
STEAMAUDIO_VERSION = (4 << 16) | (8 << 8) | 1  # 4.8.1 -> 0x040801
IPL_REFLECTIONEFFECTTYPE_PARAMETRIC = 1


# Ctypes struct definitions matching phonon.h
class IPLContextSettings(ctypes.Structure):
    _fields_ = [
        ('version', ctypes.c_uint32),
        ('logCallback', ctypes.c_void_p),
        ('allocateCallback', ctypes.c_void_p),
        ('freeCallback', ctypes.c_void_p),
        ('simdLevel', ctypes.c_int32),
        ('flags', ctypes.c_uint32)
    ]


class IPLAudioSettings(ctypes.Structure):
    _fields_ = [
        ('samplingRate', ctypes.c_int32),
        ('frameSize', ctypes.c_int32)
    ]


class IPLHRTFSettings(ctypes.Structure):
    _fields_ = [
        ('type', ctypes.c_int32),
        ('sofaFileName', ctypes.c_char_p),
        ('sofaData', ctypes.c_void_p),
        ('sofaDataSize', ctypes.c_int32),
        ('volume', ctypes.c_float),
        ('normType', ctypes.c_int32)
    ]


class IPLBinauralEffectSettings(ctypes.Structure):
    _fields_ = [
        ('hrtf', ctypes.c_void_p)
    ]


class IPLVector3(ctypes.Structure):
    _fields_ = [
        ('x', ctypes.c_float),
        ('y', ctypes.c_float),
        ('z', ctypes.c_float)
    ]


class IPLBinauralEffectParams(ctypes.Structure):
    _fields_ = [
        ('direction', IPLVector3),
        ('interpolation', ctypes.c_int32),
        ('spatialBlend', ctypes.c_float),
        ('hrtf', ctypes.c_void_p),
        ('peakDelays', ctypes.POINTER(ctypes.c_float))
    ]


class IPLReflectionEffectSettings(ctypes.Structure):
    _fields_ = [
        ('type', ctypes.c_int32),
        ('irSize', ctypes.c_int32),
        ('numChannels', ctypes.c_int32)
    ]


class IPLReflectionEffectParams(ctypes.Structure):
    _fields_ = [
        ('type', ctypes.c_int32),
        ('ir', ctypes.c_void_p),
        ('reverbTimes', ctypes.c_float * 3),
        ('eq', ctypes.c_float * 3),
        ('delay', ctypes.c_int32),
        ('numChannels', ctypes.c_int32),
        ('irSize', ctypes.c_int32),
        ('tanDevice', ctypes.c_void_p),
        ('tanSlot', ctypes.c_int32)
    ]


class IPLAudioBuffer(ctypes.Structure):
    _fields_ = [
        ('numChannels', ctypes.c_int32),
        ('numSamples', ctypes.c_int32),
        ('data', ctypes.POINTER(ctypes.POINTER(ctypes.c_float)))
    ]


class SteamAudioSpatializer:
    """Manages Steam Audio Context, HRTF dataset, binaural spatialization, and room reverberation."""

    def __init__(self, dll_path: Optional[str] = None, sample_rate: int = 48000, frame_size: int = 512):
        self.sample_rate = sample_rate
        self.frame_size = frame_size
        self._is_initialized = False

        if dll_path is None:
            # Look in client/lib/phonon.dll or standard relative path
            base = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
            dll_path = os.path.join(base, "lib", "phonon.dll")

        if not os.path.exists(dll_path):
            raise FileNotFoundError(f"Cannot find Steam Audio library: {dll_path}")

        self.phonon = ctypes.CDLL(dll_path)

        # 1. Context creation
        ctx_settings = IPLContextSettings(
            version=STEAMAUDIO_VERSION,
            logCallback=None,
            allocateCallback=None,
            freeCallback=None,
            simdLevel=0,
            flags=0
        )
        self.context = ctypes.c_void_p()
        res = self.phonon.iplContextCreate(ctypes.byref(ctx_settings), ctypes.byref(self.context))
        if res != 0:
            raise RuntimeError(f"Steam Audio: iplContextCreate failed with status {res}")

        # 2. HRTF creation
        self.audio_settings = IPLAudioSettings(samplingRate=self.sample_rate, frameSize=self.frame_size)
        hrtf_settings = IPLHRTFSettings(
            type=IPL_HRTFTYPE_DEFAULT,
            sofaFileName=None,
            sofaData=None,
            sofaDataSize=0,
            volume=1.0,
            normType=IPL_HRTFNORMTYPE_NONE
        )
        self.hrtf = ctypes.c_void_p()
        res = self.phonon.iplHRTFCreate(
            self.context,
            ctypes.byref(self.audio_settings),
            ctypes.byref(hrtf_settings),
            ctypes.byref(self.hrtf)
        )
        if res != 0:
            raise RuntimeError(f"Steam Audio: iplHRTFCreate failed with status {res}")

        # 3. Binaural Effect creation
        binaural_settings = IPLBinauralEffectSettings(hrtf=self.hrtf)
        self.binaural_effect = ctypes.c_void_p()
        res = self.phonon.iplBinauralEffectCreate(
            self.context,
            ctypes.byref(self.audio_settings),
            ctypes.byref(binaural_settings),
            ctypes.byref(self.binaural_effect)
        )
        if res != 0:
            raise RuntimeError(f"Steam Audio: iplBinauralEffectCreate failed with status {res}")

        # 4. Environmental Reflection / Reverb Effect creation (Stereo diffuse output)
        refl_settings = IPLReflectionEffectSettings(
            type=IPL_REFLECTIONEFFECTTYPE_PARAMETRIC,
            irSize=0,
            numChannels=2
        )
        self.refl_effect = ctypes.c_void_p()
        res = self.phonon.iplReflectionEffectCreate(
            self.context,
            ctypes.byref(self.audio_settings),
            ctypes.byref(refl_settings),
            ctypes.byref(self.refl_effect)
        )
        if res != 0:
            raise RuntimeError(f"Steam Audio: iplReflectionEffectCreate failed with status {res}")

        # Allocate native audio buffers for fast reused processing
        self.in_buf = IPLAudioBuffer()
        self.out_buf = IPLAudioBuffer()
        self.refl_in_buf = IPLAudioBuffer()
        self.refl_out_buf = IPLAudioBuffer()

        self.phonon.iplAudioBufferAllocate(self.context, 1, self.frame_size, ctypes.byref(self.in_buf))
        self.phonon.iplAudioBufferAllocate(self.context, 2, self.frame_size, ctypes.byref(self.out_buf))
        self.phonon.iplAudioBufferAllocate(self.context, 1, self.frame_size, ctypes.byref(self.refl_in_buf))
        self.phonon.iplAudioBufferAllocate(self.context, 2, self.frame_size, ctypes.byref(self.refl_out_buf))

        # Reusable numpy stereo output buffers
        self._out_stereo = np.zeros((self.frame_size, 2), dtype=np.float32)
        self._refl_stereo = np.zeros((self.frame_size, 2), dtype=np.float32)
        self._is_initialized = True

    def calculate_relative_direction(
        self,
        listener_pos: Vector3,
        listener_yaw_deg: float,
        source_pos: Vector3
    ) -> Tuple[IPLVector3, float]:
        """
        Converts world coordinates to listener's local coordinate space for Steam Audio.
        Steam Audio Local Frame:
        +X = Right, +Y = Up, -Z = Ahead
        """
        dx = source_pos.x - listener_pos.x
        dy = source_pos.y - listener_pos.y
        dz = source_pos.z - listener_pos.z

        dist = math.sqrt(dx * dx + dy * dy + dz * dz)
        if dist < 1e-4:
            return IPLVector3(0.0, 0.0, -1.0), 0.1

        yaw_rad = math.radians(listener_yaw_deg)
        cos_y = math.cos(yaw_rad)
        sin_y = math.sin(yaw_rad)

        local_x = dx * cos_y - dz * sin_y
        local_z = dx * sin_y + dz * cos_y

        dir_x = float(local_x / dist)
        dir_y = float(dy / dist)
        dir_z = float(-local_z / dist)  # -Z is ahead

        return IPLVector3(x=dir_x, y=dir_y, z=dir_z), dist

    def spatialize_frame(
        self,
        mono_frame: np.ndarray,
        listener_pos: Vector3,
        listener_yaw: float,
        source_pos: Vector3,
        ref_distance: float = 1.0,
        max_distance: float = 30.0,
        rolloff: float = 1.0
    ) -> np.ndarray:
        """Applies Steam Audio HRTF binaural processing and distance attenuation."""
        if not self._is_initialized:
            return np.zeros((self.frame_size, 2), dtype=np.float32)

        if len(mono_frame) != self.frame_size:
            if len(mono_frame) < self.frame_size:
                mono_frame = np.pad(mono_frame, (0, self.frame_size - len(mono_frame)))
            else:
                mono_frame = mono_frame[:self.frame_size]

        if mono_frame.dtype != np.float32:
            mono_frame = mono_frame.astype(np.float32)

        direction, dist = self.calculate_relative_direction(listener_pos, listener_yaw, source_pos)

        clamped_dist = max(ref_distance, min(dist, max_distance))
        attenuation = float(ref_distance / (ref_distance + rolloff * (clamped_dist - ref_distance)))

        ctypes.memmove(self.in_buf.data[0], mono_frame.ctypes.data, self.frame_size * 4)

        params = IPLBinauralEffectParams(
            direction=direction,
            interpolation=IPL_HRTFINTERPOLATION_BILINEAR,
            spatialBlend=1.0,
            hrtf=self.hrtf,
            peakDelays=None
        )

        self.phonon.iplBinauralEffectApply(
            self.binaural_effect,
            ctypes.byref(params),
            ctypes.byref(self.in_buf),
            ctypes.byref(self.out_buf)
        )

        ctypes.memmove(self._out_stereo[:, 0].ctypes.data, self.out_buf.data[0], self.frame_size * 4)
        ctypes.memmove(self._out_stereo[:, 1].ctypes.data, self.out_buf.data[1], self.frame_size * 4)

        if attenuation != 1.0:
            self._out_stereo *= attenuation

        return self._out_stereo.copy()

    def render_environmental_reverb(
        self,
        mono_frame: np.ndarray,
        rt60_seconds: float = 1.4,
        clarity_boost: float = 1.0
    ) -> np.ndarray:
        """
        Renders Steam Audio's parametric environmental reverberation.
        Produces diffuse binaural room decay tails and acoustic slapback.
        """
        if not self._is_initialized or len(mono_frame) == 0:
            return np.zeros((self.frame_size, 2), dtype=np.float32)

        if len(mono_frame) != self.frame_size:
            if len(mono_frame) < self.frame_size:
                mono_frame = np.pad(mono_frame, (0, self.frame_size - len(mono_frame)))
            else:
                mono_frame = mono_frame[:self.frame_size]

        if mono_frame.dtype != np.float32:
            mono_frame = mono_frame.astype(np.float32)

        ctypes.memmove(self.refl_in_buf.data[0], mono_frame.ctypes.data, self.frame_size * 4)

        # 3-band RT60: Low (warm bass room resonance), Mid (voice/body), High (treble air)
        rt_low = float(rt60_seconds * 1.15)
        rt_mid = float(rt60_seconds * 1.0)
        rt_high = float(rt60_seconds * 0.85 * clarity_boost)
        reverb_times = (ctypes.c_float * 3)(rt_low, rt_mid, rt_high)
        eq = (ctypes.c_float * 3)(1.0, 1.0, 1.0 * clarity_boost)

        params = IPLReflectionEffectParams(
            type=IPL_REFLECTIONEFFECTTYPE_PARAMETRIC,
            ir=None,
            reverbTimes=reverb_times,
            eq=eq,
            delay=0,
            numChannels=2,
            irSize=0,
            tanDevice=None,
            tanSlot=0
        )

        self.phonon.iplReflectionEffectApply(
            self.refl_effect,
            ctypes.byref(params),
            ctypes.byref(self.refl_in_buf),
            ctypes.byref(self.refl_out_buf),
            None
        )

        ctypes.memmove(self._refl_stereo[:, 0].ctypes.data, self.refl_out_buf.data[0], self.frame_size * 4)
        ctypes.memmove(self._refl_stereo[:, 1].ctypes.data, self.refl_out_buf.data[1], self.frame_size * 4)

        return self._refl_stereo.copy()

    def shutdown(self) -> None:
        """Frees all Steam Audio resources cleanly."""
        if not self._is_initialized:
            return

        try:
            self.phonon.iplAudioBufferFree(self.context, ctypes.byref(self.in_buf))
            self.phonon.iplAudioBufferFree(self.context, ctypes.byref(self.out_buf))
            self.phonon.iplAudioBufferFree(self.context, ctypes.byref(self.refl_in_buf))
            self.phonon.iplAudioBufferFree(self.context, ctypes.byref(self.refl_out_buf))
            self.phonon.iplReflectionEffectRelease(ctypes.byref(self.refl_effect))
            self.phonon.iplBinauralEffectRelease(ctypes.byref(self.binaural_effect))
            self.phonon.iplHRTFRelease(ctypes.byref(self.hrtf))
            self.phonon.iplContextRelease(ctypes.byref(self.context))
        except Exception as e:
            print("Steam Audio cleanup error:", e)
        finally:
            self._is_initialized = False
