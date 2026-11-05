"""
Asynchronous TCP Network Client for Fake-VR.
Synchronizes spatial position, echolocation pulses, acoustic fashion auras, and chat events.
"""

from __future__ import annotations
import socket
import threading
import json
import time
from typing import Callable, Dict, Optional, Any

from shared.protocol import MessageType, encode_packet, decode_packet
from shared.models import Vector3, SurfaceType, AuraType, PlayerState


class NetworkClient:
    """Manages connection to the Fake-VR multiplayer server."""

    def __init__(self, host: str = "127.0.0.1", port: int = 7777):
        self.host = host
        self.port = port
        self.socket: Optional[socket.socket] = None
        self.is_connected = False
        self.player_id: Optional[str] = None
        self.username: str = "Player"

        self._recv_thread: Optional[threading.Thread] = None
        self._handlers: Dict[MessageType, Callable[[Dict[str, Any]], None]] = {}
        self._lock = threading.Lock()

    def register_handler(self, msg_type: MessageType, handler: Callable[[Dict[str, Any]], None]) -> None:
        """Registers a callback for a specific incoming server message type."""
        self._handlers[msg_type] = handler

    def connect(self, username: str, initial_aura: AuraType = AuraType.NONE) -> bool:
        """Connects to the server and initiates handshake."""
        self.username = username
        try:
            self.socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.socket.connect((self.host, self.port))
            self.is_connected = True

            # Send join packet
            join_payload = {
                "username": self.username,
                "room_id": "resonance_lounge",
                "aura": initial_aura.value
            }
            self.socket.sendall(encode_packet(MessageType.C2S_JOIN, join_payload))

            # Start receiver background thread
            self._recv_thread = threading.Thread(target=self._receive_loop, daemon=True)
            self._recv_thread.start()
            return True
        except Exception as e:
            print(f"Network connection failed to {self.host}:{self.port} - {e}")
            self.is_connected = False
            return False

    def disconnect(self) -> None:
        """Closes connection."""
        self.is_connected = False
        if self.socket:
            try:
                self.socket.close()
            except Exception:
                pass
            self.socket = None

    def send_packet(self, msg_type: MessageType, payload: Dict[str, Any]) -> None:
        """Sends an outgoing JSON message packet."""
        if not self.is_connected or not self.socket:
            return

        try:
            data = encode_packet(msg_type, payload)
            with self._lock:
                self.socket.sendall(data)
        except Exception as e:
            print(f"Send packet failed: {e}")
            self.disconnect()

    def send_move(
        self,
        pos: Vector3,
        yaw: float,
        pitch: float,
        is_moving: bool,
        surface: SurfaceType
    ) -> None:
        """Sends spatial position and orientation update."""
        payload = {
            "position": pos.to_dict(),
            "yaw": round(yaw, 1),
            "pitch": round(pitch, 1),
            "is_moving": is_moving,
            "surface": surface.value
        }
        self.send_packet(MessageType.C2S_MOVE, payload)

    def send_echolocation(self, pos: Vector3, yaw: float) -> None:
        """Notifies server that player triggered an active echolocation snap."""
        payload = {
            "position": pos.to_dict(),
            "yaw": round(yaw, 1)
        }
        self.send_packet(MessageType.C2S_ECHOLOCATION, payload)

    def send_aura_change(self, aura: AuraType) -> None:
        """Notifies server of a new acoustic fashion aura."""
        payload = {"aura": aura.value}
        self.send_packet(MessageType.C2S_AURA_CHANGE, payload)

    def send_room_change(self, room_id: str) -> None:
        """Requests room/world transfer."""
        payload = {"room_id": room_id}
        self.send_packet(MessageType.C2S_ROOM_CHANGE, payload)

    def send_chat(self, text: str) -> None:
        """Sends a text message or voice-beacon."""
        payload = {"text": text}
        self.send_packet(MessageType.C2S_CHAT, payload)

    def _receive_loop(self) -> None:
        """Background loop reading newline-delimited JSON packets from server."""
        buffer = b""
        while self.is_connected and self.socket:
            try:
                chunk = self.socket.recv(4096)
                if not chunk:
                    print("Server closed connection.")
                    break
                buffer += chunk
                while b"\n" in buffer:
                    line, buffer = buffer.split(b"\n", 1)
                    packet = decode_packet(line)
                    if packet:
                        self._dispatch_packet(packet)
            except Exception as e:
                if self.is_connected:
                    print(f"Network error in receive loop: {e}")
                break

        self.disconnect()

    def _dispatch_packet(self, packet: Dict[str, Any]) -> None:
        raw_type = packet.get("type")
        payload = packet.get("payload", {})

        try:
            msg_type = MessageType(raw_type)
        except ValueError:
            return

        if msg_type == MessageType.S2C_WELCOME:
            self.player_id = payload.get("player_id")

        handler = self._handlers.get(msg_type)
        if handler:
            try:
                handler(payload)
            except Exception as e:
                print(f"Error handling message {msg_type}: {e}")
