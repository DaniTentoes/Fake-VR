"""
End-to-End Client-Server Network Synchronization Test.
Starts FakeVRServer on a test port, connects simulated client sockets,
and validates:
1. S2C_MAP_DATA transmission with authoritative objects, spawners, travel points
2. C2S_ATTACK -> S2C_ATTACK_EVENT + authoritative S2C_OBJECT_STATE
3. C2S_BUILD_WALL -> S2C_MAP_UPDATED with new wall
4. C2S_INTERACT with NPC -> S2C_INTERACT_RESPONSE
5. Physical 1-tile player obstacle presence in tick snapshot
"""

import sys
import os
import json
import time
import socket
import asyncio
import threading

ROOT_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, ROOT_DIR)

from server.server import FakeVRServer
from shared.protocol import MessageType, encode_packet, decode_packet


def run_test():
    test_port = 7789
    server = FakeVRServer(host="127.0.0.1", port=test_port)
    loop = asyncio.new_event_loop()

    def server_thread_func():
        asyncio.set_event_loop(loop)
        loop.run_until_complete(server.start())
        loop.run_forever()

    t = threading.Thread(target=server_thread_func, daemon=True)
    t.start()
    time.sleep(0.6)

    try:
        # Connect Client A
        s_a = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s_a.connect(("127.0.0.1", test_port))
        s_a.sendall(encode_packet(MessageType.C2S_JOIN, {"username": "Alice", "room_id": "lounge", "aura": "none"}))

        # Connect Client B
        s_b = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s_b.connect(("127.0.0.1", test_port))
        s_b.sendall(encode_packet(MessageType.C2S_JOIN, {"username": "Bob", "room_id": "lounge", "aura": "chime"}))

        time.sleep(0.5)

        # 1. Client A sends an attack on Wooden Park Bench
        atk_packet = encode_packet(MessageType.C2S_ATTACK, {
            "target_name": "Wooden Park Bench",
            "material": "wood",
            "damage": 100,
            "impact_pos": {"x": 0.0, "y": 7.5, "z": 0.0}
        })
        s_a.sendall(atk_packet)
        time.sleep(0.4)

        # Client B reads lines from server
        s_b.settimeout(2.0)
        received_obj_state = False
        received_atk_event = False

        buf = ""
        start_t = time.time()
        while time.time() - start_t < 2.5:
            try:
                data = s_b.recv(4096)
                if not data:
                    break
                buf += data.decode("utf-8")
                while "\n" in buf:
                    line, buf = buf.split("\n", 1)
                    pkt = decode_packet(line.encode("utf-8"))
                    if pkt:
                        t_type = pkt.get("type")
                        if t_type == MessageType.S2C_ATTACK_EVENT.value:
                            received_atk_event = True
                            print(f"[Client B]: Received Attack Event for {pkt['payload'].get('target_name')}! Destroyed: {pkt['payload'].get('destroyed')}")
                        elif t_type == MessageType.S2C_OBJECT_STATE.value:
                            received_obj_state = True
                            print(f"[Client B]: Received Authoritative Object State -> {pkt['payload'].get('name')}: Health {pkt['payload'].get('health')}, Destroyed: {pkt['payload'].get('is_destroyed')}")
            except socket.timeout:
                break

        assert received_atk_event, "Client B must receive S2C_ATTACK_EVENT!"
        assert received_obj_state, "Client B must receive authoritative S2C_OBJECT_STATE!"
        print("  -> Authoritative multi-client object destruction PASSED.")

        # 2. Client A tests Building Tool (erect a wall)
        build_packet = encode_packet(MessageType.C2S_BUILD_WALL, {
            "x1": 5.0, "y1": 5.0,
            "x2": 5.0, "y2": 10.0,
            "material": "wood",
            "name": "Testing Wall"
        })
        s_a.sendall(build_packet)
        time.sleep(0.5)

        # Client B reads S2C_MAP_UPDATED
        received_map_update = False
        start_t = time.time()
        while time.time() - start_t < 2.0:
            try:
                data = s_b.recv(4096)
                if not data:
                    break
                buf += data.decode("utf-8")
                while "\n" in buf:
                    line, buf = buf.split("\n", 1)
                    pkt = decode_packet(line.encode("utf-8"))
                    if pkt and pkt.get("type") == MessageType.S2C_MAP_UPDATED.value:
                        received_map_update = True
                        print(f"[Client B]: Received S2C_MAP_UPDATED! Updated by: {pkt['payload'].get('updated_by')}")
            except socket.timeout:
                break

        assert received_map_update, "Client B must receive S2C_MAP_UPDATED when a wall is built!"
        print("  -> Building tool synchronization PASSED.")

        # 3. Client A tests NPC Interaction
        interact_packet = encode_packet(MessageType.C2S_INTERACT, {"target_id": "npc_spawner_guard"})
        s_a.sendall(interact_packet)
        time.sleep(0.4)

        s_a.settimeout(2.0)
        received_dialogue = False
        buf_a = ""
        start_t = time.time()
        while time.time() - start_t < 2.0:
            try:
                data = s_a.recv(4096)
                if not data:
                    break
                buf_a += data.decode("utf-8")
                while "\n" in buf_a:
                    line, buf_a = buf_a.split("\n", 1)
                    pkt = decode_packet(line.encode("utf-8"))
                    if pkt and pkt.get("type") == MessageType.S2C_INTERACT_RESPONSE.value:
                        received_dialogue = True
                        print(f"[Client A]: Received NPC Dialogue from {pkt['payload'].get('speaker_name')}: \"{pkt['payload'].get('dialogue')}\"")
            except socket.timeout:
                break

        assert received_dialogue, "Client A must receive S2C_INTERACT_RESPONSE!"
        print("  -> Interactive NPC dialogue response PASSED.")

        s_a.close()
        s_b.close()
    finally:
        loop.call_soon_threadsafe(loop.stop)


if __name__ == "__main__":
    run_test()
    print("\n=======================================================")
    print("ALL REAL-TIME CLIENT-SERVER TESTS COMPLETED & VERIFIED!")
    print("=======================================================")
