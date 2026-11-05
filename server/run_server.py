"""
Server entrypoint and hosting console for Fake-VR.
Runs on the host computer for friends to connect over LAN or the Internet.
"""

from __future__ import annotations
import os
import sys
import socket
import asyncio
import threading

# Ensure project root is in sys.path
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from server.server import FakeVRServer
from shared.world import WORLD_REGISTRY


def get_tailscale_ip() -> str | None:
    """Retrieves Tailscale IPv4 if Tailscale is running."""
    import subprocess
    ts_exe = r"C:\Program Files\Tailscale\tailscale.exe"
    if os.path.exists(ts_exe):
        try:
            res = subprocess.run([ts_exe, "ip", "-4"], capture_output=True, text=True, timeout=2)
            if res.returncode == 0:
                ip = res.stdout.strip()
                if ip.startswith("100."):
                    return ip
        except Exception:
            pass
    return None


def get_local_ip_addresses() -> list[str]:
    """Retrieves all local network IP addresses for the host machine."""
    ips = []
    try:
        hostname = socket.gethostname()
        _, _, ip_list = socket.gethostbyname_ex(hostname)
        ips = [ip for ip in ip_list if not ip.startswith("127.") and not ip.startswith("100.")]
    except Exception:
        pass

    # Fallback probe
    if not ips:
        try:
            s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            s.connect(("8.8.8.8", 80))
            probe = s.getsockname()[0]
            if not probe.startswith("100."):
                ips.append(probe)
            s.close()
        except Exception:
            ips.append("127.0.0.1")

    return ips


def print_banner(port: int) -> None:
    ips = get_local_ip_addresses()
    ts_ip = get_tailscale_ip()
    print("=" * 65)
    print("           FAKE-VR MULTIPLAYER SERVER (HOST CONSOLE)            ")
    print("=" * 65)
    print(f"Server Port: {port}")
    print("Connection addresses for your friends:")
    print("  • On this same computer: 127.0.0.1:" + str(port))
    for ip in ips:
        print(f"  • Friends on Home Wi-Fi / LAN: {ip}:{port}")
    if ts_ip:
        print(f"  • Friends on Tailscale (Single-Port): {ts_ip}:{port}")
    else:
        print("  • Tailscale: Run 'server\\enable_tailscale_serve.bat' to share only port 7777")
    print("=" * 65)
    print("Type 'help' for console commands, or 'quit' to stop.\n")


def console_thread(server: FakeVRServer, loop: asyncio.AbstractEventLoop) -> None:
    """Reads administrative commands from the host terminal."""
    while server.is_running:
        try:
            cmd_line = input().strip()
            if not cmd_line:
                continue

            parts = cmd_line.split(" ", 1)
            cmd = parts[0].lower()
            arg = parts[1] if len(parts) > 1 else ""

            if cmd in ("quit", "exit", "stop"):
                print("[Console]: Stopping server...")
                asyncio.run_coroutine_threadsafe(server.stop(), loop)
                break

            elif cmd == "list":
                players = list(server.room_mgr.players.values())
                print(f"[Console]: {len(players)} player(s) online:")
                for p in players:
                    print(f"  - {p.username} (ID: {p.player_id}) | Room: {p.room_id} | Aura: {p.aura.value} | Pos: ({p.position.x:.1f}, {p.position.z:.1f})")

            elif cmd == "rooms":
                print("[Console]: World Rooms:")
                for rid, room in WORLD_REGISTRY.items():
                    count = len(server.room_mgr.rooms.get(rid, []))
                    print(f"  - {room.title} (ID: {rid}) -> {count} players")

            elif cmd == "say":
                if not arg:
                    print("[Console]: Usage: say <message>")
                    continue
                # Broadcast announcement to all rooms
                for rid in server.room_mgr.rooms:
                    asyncio.run_coroutine_threadsafe(
                        server._broadcast_to_room(
                            rid,
                            MessageType.S2C_CHAT,
                            {"player_id": "SERVER", "username": "[SERVER ANNOUNCEMENT]", "text": arg, "position": {"x": 0, "y": 0, "z": 0}}
                        ),
                        loop
                    )
                print(f"[Console]: Broadcasted: {arg}")

            elif cmd == "help":
                print("[Console]: Available commands:")
                print("  list         - Show connected players")
                print("  rooms        - Show world rooms and player counts")
                print("  say <msg>    - Broadcast an announcement to all players")
                print("  quit         - Shut down server")

            else:
                print(f"[Console]: Unknown command '{cmd}'. Type 'help' for commands.")

        except (EOFError, KeyboardInterrupt):
            asyncio.run_coroutine_threadsafe(server.stop(), loop)
            break


async def main_async(port: int) -> None:
    server = FakeVRServer(host="0.0.0.0", port=port)
    await server.start()
    print_banner(port)

    loop = asyncio.get_running_loop()
    t = threading.Thread(target=console_thread, args=(server, loop), daemon=True)
    t.start()

    while server.is_running:
        await asyncio.sleep(1.0)


def main():
    import argparse
    parser = argparse.ArgumentParser(description="Fake-VR Multiplayer Server")
    parser.add_argument("--port", "-p", type=int, default=7777, help="Port to host on (default: 7777)")
    args = parser.parse_args()

    try:
        asyncio.run(main_async(args.port))
    except KeyboardInterrupt:
        print("\nServer terminated by host.")


if __name__ == "__main__":
    main()
