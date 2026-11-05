"""
Network protocol definitions and serialization for Fake-VR.
Uses framed JSON packets for clear, inspectable, and resilient communication over TCP.
"""

from __future__ import annotations
import json
from enum import Enum
from typing import Any, Dict, Optional


class MessageType(str, Enum):
    # Handshake & Lifecycle
    C2S_JOIN = "c2s_join"
    S2C_WELCOME = "s2c_welcome"
    S2C_PLAYER_JOINED = "s2c_player_joined"
    S2C_PLAYER_LEFT = "s2c_player_left"

    # Real-Time Spatial Movement & World Tick
    C2S_MOVE = "c2s_move"
    S2C_WORLD_TICK = "s2c_world_tick"

    # Acoustic Mechanics
    C2S_ECHOLOCATION = "c2s_echolocation"
    S2C_ECHOLOCATION_EVENT = "s2c_echolocation_event"

    # Acoustic Fashion & Auras
    C2S_AURA_CHANGE = "c2s_aura_change"
    S2C_AURA_CHANGE = "s2c_aura_change"

    # World Transitions
    C2S_ROOM_CHANGE = "c2s_room_change"
    S2C_ROOM_CHANGE = "s2c_room_change"

    # Combat & Destruction
    C2S_ATTACK = "c2s_attack"
    S2C_ATTACK_EVENT = "s2c_attack_event"

    # Social Chat / Beacons
    C2S_CHAT = "c2s_chat"
    S2C_CHAT = "s2c_chat"

    # Keepalive
    PING = "ping"
    PONG = "pong"


def encode_packet(msg_type: MessageType, payload: Optional[Dict[str, Any]] = None) -> bytes:
    """Encodes a message into a newline-terminated UTF-8 JSON payload."""
    data = {
        "type": msg_type.value,
        "payload": payload or {}
    }
    return (json.dumps(data) + "\n").encode("utf-8")


def decode_packet(line_bytes: bytes) -> Optional[Dict[str, Any]]:
    """Decodes a single line into a message dict."""
    try:
        text = line_bytes.decode("utf-8").strip()
        if not text:
            return None
        return json.loads(text)
    except Exception:
        return None
