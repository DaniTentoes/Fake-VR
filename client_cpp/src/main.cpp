#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include "audio_engine_cpp.hpp"
#include "screen_reader.hpp"
#include "models.hpp"
#include "network_client.hpp"

#include <iostream>
#include <string>
#include <conio.h>

using namespace fakevr;

int main(int argc, char* argv[]) {
    std::cout << "=========================================================\n";
    std::cout << "         FAKE-VR REALTIME MULTIPLAYER CLIENT            \n";
    std::cout << "=========================================================\n";

    ScreenReader sr;
    sr.speak("Starting Fake-VR realtime audio client.");

    // Default server connection credentials
    std::string serverHost = "bore.pub";
    int serverPort = 32827;
    std::string username = "Player";

    if (argc >= 2) {
        serverHost = argv[1];
    }
    if (argc >= 3) {
        serverPort = std::stoi(argv[2]);
    }
    if (argc >= 4) {
        username = argv[3];
    } else if (argc < 2) {
        // Interactive prompt when launched without CLI arguments
        std::cout << "Connect to server Host/IP (Press Enter for default: " << serverHost << "): ";
        std::string inputHost;
        std::getline(std::cin, inputHost);
        if (!inputHost.empty()) {
            // Check if user entered host:port format
            size_t colonPos = inputHost.find(':');
            if (colonPos != std::string::npos) {
                serverHost = inputHost.substr(0, colonPos);
                try {
                    serverPort = std::stoi(inputHost.substr(colonPos + 1));
                } catch (...) {}
            } else {
                serverHost = inputHost;
            }
        }

        std::cout << "Connect to Port (Press Enter for default: " << serverPort << "): ";
        std::string inputPort;
        std::getline(std::cin, inputPort);
        if (!inputPort.empty()) {
            try {
                serverPort = std::stoi(inputPort);
            } catch (...) {}
        }

        std::cout << "Enter your username (Press Enter for default: " << username << "): ";
        std::string inputUser;
        std::getline(std::cin, inputUser);
        if (!inputUser.empty()) {
            username = inputUser;
        }
    }

    AudioEngineCpp audio(48000, 1024);
    WorldMap world = create_realistic_lounge();
    Vector3 playerPos = world.spawn_point;
    float playerYaw = 0.0f;
    SurfaceType curSurface = world.get_surface_at(playerPos);
    ClothingType curClothes = ClothingType::REGULAR;
    const WorldObject* curNearbyObj = world.get_nearby_object(playerPos);
    const RoomRegion* curRegion = world.get_region_at(playerPos);

    audio.setMap(world);
    audio.setListener(playerPos, playerYaw);

    if (!audio.start()) {
        std::cerr << "Failed to start native audio engine!\n";
        return 1;
    }

    // Connect to multiplayer server
    NetworkClient net;
    bool isMultiplayer = net.connectToServer(serverHost, serverPort, username);
    if (isMultiplayer) {
        sr.speak("Connected to server as " + username);
        // Send initial position
        net.sendMove(playerPos, playerYaw, curSurface, curClothes, false);
    } else {
        sr.speak("Server offline. Continuing in standalone offline mode.");
        std::cout << "[Note]: Running in standalone offline simulation mode.\n";
    }

    std::string startMsg = "Welcome to Fake-VR, " + username + 
                           ". Press W, A, S, D to walk. U for echolocation. Spacebar to attack. T to toggle clothes. P for players online. C for status. Q to quit.";
    sr.speak(startMsg);

    std::cout << "\nControls:\n";
    std::cout << "  W / S : Step forward / backward\n";
    std::cout << "  A / D : Strafe left / right\n";
    std::cout << "  Left / Right Arrow : Turn body 30 degrees\n";
    std::cout << "  T : Cycle outfit (Regular Clothes, Naked, Suit of Armor)\n";
    std::cout << "  U : Active Echolocation Snap (8 Physical Acoustic Rays + Apple IR Tail)\n";
    std::cout << "  Spacebar : Attack / Strike object in front (Bare hands 25 DMG)\n";
    std::cout << "  P : Announce nearby players and distances\n";
    std::cout << "  C : Announce coordinates, floor, clothing & nearby objects\n";
    std::cout << "  H : Keybinding help\n";
    std::cout << "  Q / Escape : Quit\n\n";

    bool running = true;
    const float stepSize = 0.55f;
    const float turnStep = 30.0f;
    const DWORD walkStepIntervalMs = 380;
    DWORD lastWalkTime = 0;

    while (running) {
        // --- 1. POLL INCOMING MULTIPLAYER EVENTS ---
        if (isMultiplayer) {
            // Process announcements (player joined, left, object destroyed)
            std::string ann;
            while (!(ann = net.pollAnnouncement()).empty()) {
                sr.speak(ann);
            }

            // Process remote player footsteps in 3D binaural space
            RemoteStepEvent stepEv;
            while (net.pollStep(stepEv)) {
                audio.triggerMovement(stepEv.position, stepEv.surface, stepEv.clothing, false);
            }

            // Process remote player attacks
            RemoteAttackEvent atkEv;
            while (net.pollAttack(atkEv)) {
                audio.triggerAttackImpact(atkEv.impactPos, atkEv.material, atkEv.destroyed);
            }

            // Process remote player echolocation snaps
            RemoteEchoEvent echoEv;
            while (net.pollEcho(echoEv)) {
                audio.triggerEcholocation(echoEv.position, echoEv.yaw);
            }
        }

        // --- 2. LOCAL PLAYER INPUT HANDLING ---
        if (_kbhit()) {
            int ch = _getch();
            if (ch == 0 || ch == 0xE0) {
                // Arrow keys
                int code = _getch();
                if (code == 75) { // Left arrow
                    playerYaw = std::fmod(playerYaw - turnStep + 360.0f, 360.0f);
                    audio.setListener(playerPos, playerYaw);
                    if (isMultiplayer) net.sendMove(playerPos, playerYaw, curSurface, curClothes, false);
                    sr.speak("Facing " + std::to_string(int(playerYaw)) + " degrees.");
                } else if (code == 77) { // Right arrow
                    playerYaw = std::fmod(playerYaw + turnStep, 360.0f);
                    audio.setListener(playerPos, playerYaw);
                    if (isMultiplayer) net.sendMove(playerPos, playerYaw, curSurface, curClothes, false);
                    sr.speak("Facing " + std::to_string(int(playerYaw)) + " degrees.");
                }
                continue;
            }

            char c = (char)std::tolower(ch);
            if (c == 'q' || ch == 27) { // Q or Escape
                running = false;
                sr.speak("Exiting Fake-VR.");
                break;
            }

            if (c == 'w' || c == 's' || c == 'a' || c == 'd') {
                DWORD now = GetTickCount();
                if (now - lastWalkTime < walkStepIntervalMs) {
                    continue;
                }
                lastWalkTime = now;

                float fwd = (c == 'w') ? 1.0f : ((c == 's') ? -1.0f : 0.0f);
                float str = (c == 'd') ? 1.0f : ((c == 'a') ? -1.0f : 0.0f);

                float rad = playerYaw * (float)M_PI / 180.0f;
                float dx = (std::sin(rad) * fwd + std::cos(rad) * str) * stepSize;
                float dz = (std::cos(rad) * fwd - std::sin(rad) * str) * stepSize;

                Vector3 targetPos(playerPos.x + dx, playerPos.y, playerPos.z + dz);
                bool collided = false;
                std::string hitObstacle;
                Vector3 newPos = world.try_move(playerPos, targetPos, collided, hitObstacle);

                if (collided) {
                    audio.triggerWallBump(targetPos);
                    if (!hitObstacle.empty()) {
                        sr.speak("Bumped into " + hitObstacle);
                    }
                } else {
                    SurfaceType oldSurface = curSurface;
                    curSurface = world.get_surface_at(newPos);
                    
                    const WorldObject* oldObj = curNearbyObj;
                    curNearbyObj = world.get_nearby_object(newPos);

                    const RoomRegion* oldRegion = curRegion;
                    curRegion = world.get_region_at(newPos);

                    playerPos = newPos;

                    audio.setListener(playerPos, playerYaw);
                    audio.triggerMovement(playerPos, curSurface, curClothes, true);

                    // Sync step to server
                    if (isMultiplayer) {
                        net.sendMove(playerPos, playerYaw, curSurface, curClothes, true);
                    }

                    if (curRegion != oldRegion && curRegion != nullptr) {
                        sr.speak("Entering " + curRegion->name);
                    } else if (curNearbyObj != oldObj && curNearbyObj != nullptr) {
                        sr.speak("Next to " + curNearbyObj->name + ", " + surface_to_string(curNearbyObj->material));
                    } else if (curSurface != oldSurface) {
                        sr.speak(surface_to_string(curSurface) + " floor.");
                    }
                }
            } else if (c == 't') { // 'T' -> Cycle Clothing / Foley
                if (curClothes == ClothingType::REGULAR) curClothes = ClothingType::NAKED;
                else if (curClothes == ClothingType::NAKED) curClothes = ClothingType::ARMOR;
                else curClothes = ClothingType::REGULAR;

                sr.speak("Wearing " + clothing_to_string(curClothes));
                audio.triggerMovement(playerPos, curSurface, curClothes, true);
                if (isMultiplayer) net.sendMove(playerPos, playerYaw, curSurface, curClothes, false);
            } else if (c == 'u') { // 'U' -> Active Echolocation Snap
                audio.triggerEcholocation(playerPos, playerYaw);
                if (isMultiplayer) net.sendEcholocation(playerPos, playerYaw);
            } else if (ch == ' ') { // Spacebar -> Attack & Destroy Objects
                std::string hitName;
                SurfaceType hitMat = SurfaceType::WOOD;
                bool destroyed = false;
                int hpLeft = 0;
                Vector3 impactPos;

                int bareHandsDamage = 25;
                if (world.attack_object_in_front(playerPos, playerYaw, bareHandsDamage, hitName, hitMat, destroyed, hpLeft, impactPos)) {
                    audio.triggerAttackImpact(impactPos, hitMat, destroyed);
                    audio.setMap(world);

                    if (isMultiplayer) {
                        net.sendAttack(hitName, hitMat, destroyed, impactPos);
                    }

                    if (destroyed) {
                        sr.speak("Destroyed " + hitName + "!");
                    } else {
                        sr.speak("Struck " + hitName + ". " + std::to_string(hpLeft) + " health remaining.");
                    }
                } else {
                    sr.speak("Swung into thin air.");
                }
            } else if (c == 'p') { // 'P' -> Players Online Radar
                if (!isMultiplayer) {
                    sr.speak("Offline mode. You are alone.");
                } else {
                    auto remotePlayers = net.getPlayers();
                    if (remotePlayers.empty()) {
                        sr.speak("No other players nearby.");
                    } else {
                        std::string radar = std::to_string(remotePlayers.size()) + " player" + (remotePlayers.size() > 1 ? "s" : "") + " nearby: ";
                        for (const auto& rp : remotePlayers) {
                            float d = playerPos.distance_xz(rp.position);
                            radar += rp.username.empty() ? "Guest" : rp.username;
                            radar += " at " + std::to_string(int(d)) + " meters, wearing " + clothing_to_string(rp.clothing) + ". ";
                        }
                        sr.speak(radar);
                    }
                }
            } else if (c == 'c') {
                std::string regTitle = curRegion ? curRegion->name : "Area";
                std::string msg = regTitle + ". Position: X " + std::to_string(int(playerPos.x)) + 
                                  ", Z " + std::to_string(int(playerPos.z)) + 
                                  ". Facing " + std::to_string(int(playerYaw)) + 
                                  " degrees. Ground: " + surface_to_string(curSurface) + 
                                  ". Attire: " + clothing_to_string(curClothes) + ".";
                if (curNearbyObj && !curNearbyObj->isDestroyed) {
                    msg += " Beside " + curNearbyObj->name + " (" + surface_to_string(curNearbyObj->material) + ", Health " + std::to_string(curNearbyObj->health) + ").";
                }
                sr.speak(msg);
            } else if (c == 'h') {
                sr.speak("Controls: W, A, S, D to walk. Left and Right arrows to turn. T to change clothes. U for echolocation. Spacebar to attack. P for players online. C for coordinates. Q to quit.");
            }
        }
        Sleep(10);
    }

    if (isMultiplayer) {
        net.disconnect();
    }
    audio.stop();
    return 0;
}
