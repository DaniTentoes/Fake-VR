"""
High-performance asynchronous TCP server for Fake-VR.
Listens on 0.0.0.0 and synchronizes spatial presence, acoustic auras, and echolocation events.
"""

from __future__ import annotations
import asyncio
import socket
from typing import Dict, Optional

from shared.protocol import MessageType, encode_packet, decode_packet
from shared.models import Vector3, SurfaceType, AuraType
from server.room_manager import RoomManager, ConnectedPlayer

TICK_RATE_HZ = 20  # 20 updates/second for smooth spatial audio interpolation


class FakeVRServer:
    """Asyncio TCP server orchestrating Fake-VR world instances."""

    def __init__(self, host: str = "0.0.0.0", port: int = 7777):
        self.host = host
        self.port = port
        self.room_mgr = RoomManager()
        self.server: Optional[asyncio.Server] = None
        self.is_running = False
        self._writer_to_player: Dict[asyncio.StreamWriter, ConnectedPlayer] = {}

    async def start(self) -> None:
        """Starts the TCP server and background broadcast tick loop."""
        self.is_running = True
        self.server = await asyncio.start_server(self._handle_client, self.host, self.port)
        print(f"[Server]: Fake-VR Server listening on {self.host}:{self.port}")

        # Start 20Hz tick loop
        asyncio.create_task(self._tick_loop())

    async def stop(self) -> None:
        """Shuts down server."""
        self.is_running = False
        if self.server:
            self.server.close()
            await self.server.wait_closed()
        print("[Server]: Server stopped.")

    async def _handle_client(self, reader: asyncio.StreamReader, writer: asyncio.StreamWriter) -> None:
        """Connection handler for each incoming player socket."""
        addr = writer.get_extra_info("peername")
        print(f"[Server]: New connection established from {addr}")
        player: Optional[ConnectedPlayer] = None

        try:
            while self.is_running:
                line = await reader.readline()
                if not line:
                    break  # Client disconnected

                packet = decode_packet(line)
                if not packet:
                    continue

                msg_type = packet.get("type")
                payload = packet.get("payload", {})

                if msg_type == MessageType.C2S_JOIN.value:
                    username = payload.get("username", "Player")
                    room_id = payload.get("room_id", "resonance_lounge")
                    aura = AuraType(payload.get("aura", AuraType.NONE.value))

                    player = self.room_mgr.add_player(writer, username, room_id, aura)
                    self._writer_to_player[writer] = player
                    print(f"[Server]: Player joined -> '{username}' (ID: {player.player_id}) in room '{room_id}'")

                    # Send welcome packet to new player
                    welcome_payload = {
                        "player_id": player.player_id,
                        "room_id": player.room_id,
                        "position": player.position.to_dict(),
                        "other_players": [
                            p.to_player_state().to_dict()
                            for p in self.room_mgr.get_room_players(player.room_id)
                            if p.player_id != player.player_id
                        ]
                    }
                    await self._send_packet(writer, MessageType.S2C_WELCOME, welcome_payload)

                    # Broadcast player joined to others in room
                    await self._broadcast_to_room(
                        player.room_id,
                        MessageType.S2C_PLAYER_JOINED,
                        {"player": player.to_player_state().to_dict()},
                        exclude_player_id=player.player_id
                    )

                elif player is not None:
                    # Player is authenticated, handle game actions
                    if msg_type == MessageType.C2S_MOVE.value:
                        pos_dict = payload.get("position", {})
                        player.position = Vector3.from_dict(pos_dict)
                        player.yaw = float(payload.get("yaw", 0.0))
                        player.pitch = float(payload.get("pitch", 0.0))
                        player.is_moving = bool(payload.get("is_moving", False))
                        player.surface = SurfaceType(payload.get("surface", SurfaceType.WOOD.value))
                        clothing_val = payload.get("clothing", "regular")

                        # If player took a physical step, immediately broadcast to other clients in room
                        if player.is_moving:
                            await self._broadcast_to_room(
                                player.room_id,
                                MessageType.C2S_MOVE, # Relay movement step
                                {
                                    "player_id": player.player_id,
                                    "position": player.position.to_dict(),
                                    "yaw": player.yaw,
                                    "surface": player.surface.value,
                                    "clothing": clothing_val
                                },
                                exclude_player_id=player.player_id
                            )

                    elif msg_type == MessageType.C2S_ATTACK.value:
                        target_name = payload.get("target_name", "")
                        mat_val = payload.get("material", "wood")
                        destroyed = bool(payload.get("destroyed", False))
                        impact_dict = payload.get("impact_pos", player.position.to_dict())
                        await self._broadcast_to_room(
                            player.room_id,
                            MessageType.S2C_ATTACK_EVENT,
                            {
                                "player_id": player.player_id,
                                "username": player.username,
                                "target_name": target_name,
                                "material": mat_val,
                                "destroyed": destroyed,
                                "impact_pos": impact_dict
                            },
                            exclude_player_id=player.player_id
                        )

                    elif msg_type == MessageType.C2S_ECHOLOCATION.value:
                        # Broadcast echolocation click to all players in the room
                        event_payload = {
                            "player_id": player.player_id,
                            "position": player.position.to_dict(),
                            "yaw": player.yaw
                        }
                        await self._broadcast_to_room(
                            player.room_id,
                            MessageType.S2C_ECHOLOCATION_EVENT,
                            event_payload,
                            exclude_player_id=player.player_id
                        )

                    elif msg_type == MessageType.C2S_AURA_CHANGE.value:
                        aura_val = payload.get("aura", AuraType.NONE.value)
                        player.aura = AuraType(aura_val)
                        print(f"[Server]: {player.username} changed acoustic aura to {player.aura.value}")
                        await self._broadcast_to_room(
                            player.room_id,
                            MessageType.S2C_AURA_CHANGE,
                            {"player_id": player.player_id, "aura": player.aura.value},
                            exclude_player_id=player.player_id
                        )

                    elif msg_type == MessageType.C2S_ROOM_CHANGE.value:
                        new_room = payload.get("room_id")
                        old_room = player.room_id
                        if self.room_mgr.change_room(player.player_id, new_room):
                            # Tell old room
                            await self._broadcast_to_room(
                                old_room,
                                MessageType.S2C_PLAYER_LEFT,
                                {"player_id": player.player_id, "username": player.username}
                            )
                            # Tell new room
                            await self._broadcast_to_room(
                                new_room,
                                MessageType.S2C_PLAYER_JOINED,
                                {"player": player.to_player_state().to_dict()},
                                exclude_player_id=player.player_id
                            )

                    elif msg_type == MessageType.C2S_CHAT.value:
                        chat_text = payload.get("text", "")
                        print(f"[Chat] {player.username}: {chat_text}")
                        await self._broadcast_to_room(
                            player.room_id,
                            MessageType.S2C_CHAT,
                            {
                                "player_id": player.player_id,
                                "username": player.username,
                                "text": chat_text,
                                "position": player.position.to_dict()
                            }
                        )

        except (asyncio.IncompleteReadError, ConnectionResetError, BrokenPipeError):
            pass
        finally:
            if player:
                print(f"[Server]: Player disconnected -> '{player.username}' ({player.player_id})")
                self.room_mgr.remove_player(player.player_id)
                self._writer_to_player.pop(writer, None)
                await self._broadcast_to_room(
                    player.room_id,
                    MessageType.S2C_PLAYER_LEFT,
                    {"player_id": player.player_id, "username": player.username}
                )

            try:
                writer.close()
                await writer.wait_closed()
            except Exception:
                pass

    async def _send_packet(self, writer: asyncio.StreamWriter, msg_type: MessageType, payload: dict) -> None:
        try:
            data = encode_packet(msg_type, payload)
            writer.write(data)
            await writer.drain()
        except Exception:
            pass

    async def _broadcast_to_room(
        self,
        room_id: str,
        msg_type: MessageType,
        payload: dict,
        exclude_player_id: Optional[str] = None
    ) -> None:
        players = self.room_mgr.get_room_players(room_id)
        data = encode_packet(msg_type, payload)
        for p in players:
            if exclude_player_id and p.player_id == exclude_player_id:
                continue
            try:
                p.writer.write(data)
                await p.writer.drain()
            except Exception:
                pass

    async def _tick_loop(self) -> None:
        """20Hz World broadcast loop syncing player positions and states."""
        interval = 1.0 / TICK_RATE_HZ
        while self.is_running:
            start_time = asyncio.get_event_loop().time()

            for room_id in self.room_mgr.rooms:
                players = self.room_mgr.get_room_players(room_id)
                if not players:
                    continue

                snapshot = [p.to_player_state().to_dict() for p in players]
                data = encode_packet(MessageType.S2C_WORLD_TICK, {"players": snapshot})

                for p in players:
                    try:
                        p.writer.write(data)
                    except Exception:
                        pass

            # Flush writers
            for p in self.room_mgr.players.values():
                try:
                    await p.writer.drain()
                except Exception:
                    pass

            elapsed = asyncio.get_event_loop().time() - start_time
            sleep_time = max(0.001, interval - elapsed)
            await asyncio.sleep(sleep_time)
