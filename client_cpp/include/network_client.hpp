#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#include "models.hpp"
#include "nlohmann/json.hpp"
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <queue>
#include <iostream>
#include <sstream>
#include <unordered_map>

using json = nlohmann::json;

#pragma comment(lib, "ws2_32.lib")

namespace fakevr {

struct RemotePlayer {
    std::string id;
    std::string username;
    Vector3 position;
    float yaw = 0.0f;
    SurfaceType surface = SurfaceType::WOOD;
    ClothingType clothing = ClothingType::REGULAR;
    bool isJumping = false;
};

struct RemoteAttackEvent {
    std::string playerId;
    std::string username;
    std::string targetName;
    SurfaceType material = SurfaceType::WOOD;
    bool destroyed = false;
    Vector3 impactPos;
};

struct RemoteEchoEvent {
    std::string playerId;
    Vector3 position;
    float yaw = 0.0f;
};

struct RemoteStepEvent {
    std::string playerId;
    Vector3 position;
    SurfaceType surface = SurfaceType::WOOD;
    ClothingType clothing = ClothingType::REGULAR;
};

struct ObjectStateEvent {
    int objectId = -1;
    std::string name;
    int health = 0;
    int maxHealth = 100;
    bool isDestroyed = false;
};

struct NPCInteractResponse {
    std::string speakerName;
    std::string dialogue;
    std::string soundAsset;
};

class NetworkClient {
public:
    NetworkClient() = default;

    ~NetworkClient() {
        disconnect();
    }

    bool connectToServer(const std::string& host, int port, const std::string& username) {
        this->myUsername = username;
        WSADATA wsaData;
        int res = WSAStartup(MAKEWORD(2, 2), &wsaData);
        if (res != 0) {
            std::cerr << "[Network]: WSAStartup failed: " << res << "\n";
            return false;
        }

        sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (sock == INVALID_SOCKET) {
            std::cerr << "[Network]: socket creation failed: " << WSAGetLastError() << "\n";
            WSACleanup();
            return false;
        }

        int flag = 1;
        setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (char*)&flag, sizeof(flag));

        struct addrinfo hints{}, *result = nullptr;
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;

        std::string portStr = std::to_string(port);
        if (getaddrinfo(host.c_str(), portStr.c_str(), &hints, &result) != 0 || result == nullptr) {
            std::cerr << "[Network]: Failed to resolve host: " << host << " (error: " << WSAGetLastError() << ")\n";
            closesocket(sock);
            WSACleanup();
            return false;
        }

        std::cout << "[Network]: Connecting to Fake-VR server at " << host << ":" << port << "...\n";
        if (connect(sock, result->ai_addr, (int)result->ai_addrlen) == SOCKET_ERROR) {
            std::cerr << "[Network]: Failed to connect to server: " << WSAGetLastError() << "\n";
            freeaddrinfo(result);
            closesocket(sock);
            WSACleanup();
            return false;
        }
        freeaddrinfo(result);

        connected = true;
        std::cout << "[Network]: Connected! Sending handshake...\n";

        std::string joinPacket = "{\"type\":\"c2s_join\",\"payload\":{\"username\":\"" + escapeJson(username) + "\",\"room_id\":\"lounge\",\"aura\":\"none\"}}\n";
        sendRaw(joinPacket);

        recvThread = std::thread(&NetworkClient::receiveLoop, this);
        return true;
    }

    void disconnect() {
        if (!connected) return;
        connected = false;
        if (sock != INVALID_SOCKET) {
            shutdown(sock, SD_BOTH);
            closesocket(sock);
            sock = INVALID_SOCKET;
        }
        if (recvThread.joinable()) {
            recvThread.join();
        }
        WSACleanup();
        std::cout << "[Network]: Disconnected from server.\n";
    }

    bool isConnected() const { return connected; }

    // Broadcast local player movement (with X, Y, Z and jumping flag)
    void sendMove(const Vector3& pos, float yaw, SurfaceType surface, ClothingType clothes, bool isMoving, bool isJumping = false) {
        if (!connected) return;
        std::string sName = surface_to_string(surface);
        std::string cName = (clothes == ClothingType::ARMOR) ? "armor" : ((clothes == ClothingType::NAKED) ? "naked" : "regular");

        std::ostringstream ss;
        ss << "{\"type\":\"c2s_move\",\"payload\":{"
           << "\"position\":{\"x\":" << pos.x << ",\"y\":" << pos.y << ",\"z\":" << pos.z << "},"
           << "\"yaw\":" << yaw << ","
           << "\"pitch\":0.0,"
           << "\"is_moving\":" << (isMoving ? "true" : "false") << ","
           << "\"is_jumping\":" << (isJumping ? "true" : "false") << ","
           << "\"surface\":\"" << sName << "\","
           << "\"clothing\":\"" << cName << "\""
           << "}}\n";
        sendRaw(ss.str());
    }

    // Broadcast local player echolocation snap
    void sendEcholocation(const Vector3& pos, float yaw) {
        if (!connected) return;
        std::ostringstream ss;
        ss << "{\"type\":\"c2s_echolocation\",\"payload\":{"
           << "\"position\":{\"x\":" << pos.x << ",\"y\":" << pos.y << ",\"z\":" << pos.z << "},"
           << "\"yaw\":" << yaw
           << "}}\n";
        sendRaw(ss.str());
    }

    // Broadcast attack to server
    void sendAttack(const std::string& targetName, SurfaceType mat, int damage, const Vector3& impactPos) {
        if (!connected) return;
        std::string sMat = surface_to_string(mat);
        std::ostringstream ss;
        ss << "{\"type\":\"c2s_attack\",\"payload\":{"
           << "\"target_name\":\"" << escapeJson(targetName) << "\","
           << "\"material\":\"" << sMat << "\","
           << "\"damage\":" << damage << ","
           << "\"impact_pos\":{\"x\":" << impactPos.x << ",\"y\":" << impactPos.y << ",\"z\":" << impactPos.z << "}"
           << "}}\n";
        sendRaw(ss.str());
    }

    // Building tool: erect a wall marked between Point A and Point B
    void sendBuildWall(float x1, float y1, float x2, float y2, SurfaceType mat, const std::string& name) {
        if (!connected) return;
        std::ostringstream ss;
        ss << "{\"type\":\"c2s_build_wall\",\"payload\":{"
           << "\"x1\":" << x1 << ",\"y1\":" << y1 << ","
           << "\"x2\":" << x2 << ",\"y2\":" << y2 << ","
           << "\"material\":\"" << surface_to_string(mat) << "\","
           << "\"name\":\"" << escapeJson(name) << "\""
           << "}}\n";
        sendRaw(ss.str());
    }

    // Travel to another map via a TravelPoint
    void sendTravel(const std::string& targetMapId, const Vector3& spawnPos) {
        if (!connected) return;
        std::ostringstream ss;
        ss << "{\"type\":\"c2s_travel\",\"payload\":{"
           << "\"target_map\":\"" << escapeJson(targetMapId) << "\","
           << "\"spawn\":{\"x\":" << spawnPos.x << ",\"y\":" << spawnPos.y << ",\"z\":" << spawnPos.z << "}"
           << "}}\n";
        sendRaw(ss.str());
    }

    // Interact with an NPC
    void sendInteract(const std::string& targetId) {
        if (!connected) return;
        std::ostringstream ss;
        ss << "{\"type\":\"c2s_interact\",\"payload\":{"
           << "\"target_id\":\"" << escapeJson(targetId) << "\""
           << "}}\n";
        sendRaw(ss.str());
    }

    // Event Pollers
    bool pollStep(RemoteStepEvent& outEv) {
        std::lock_guard<std::mutex> lock(eventMutex);
        if (stepQueue.empty()) return false;
        outEv = stepQueue.front();
        stepQueue.pop();
        return true;
    }

    bool pollAttack(RemoteAttackEvent& outEv) {
        std::lock_guard<std::mutex> lock(eventMutex);
        if (attackQueue.empty()) return false;
        outEv = attackQueue.front();
        attackQueue.pop();
        return true;
    }

    bool pollObjectState(ObjectStateEvent& outEv) {
        std::lock_guard<std::mutex> lock(eventMutex);
        if (objectStateQueue.empty()) return false;
        outEv = objectStateQueue.front();
        objectStateQueue.pop();
        return true;
    }

    bool pollEcho(RemoteEchoEvent& outEv) {
        std::lock_guard<std::mutex> lock(eventMutex);
        if (echoQueue.empty()) return false;
        outEv = echoQueue.front();
        echoQueue.pop();
        return true;
    }

    bool pollMapUpdate(json& outMap, std::string& outTitle) {
        std::lock_guard<std::mutex> lock(eventMutex);
        if (!hasPendingMap) return false;
        outMap = pendingMapJson;
        outTitle = pendingMapTitle;
        hasPendingMap = false;
        return true;
    }

    bool pollNPCInteract(NPCInteractResponse& outResp) {
        std::lock_guard<std::mutex> lock(eventMutex);
        if (npcResponseQueue.empty()) return false;
        outResp = npcResponseQueue.front();
        npcResponseQueue.pop();
        return true;
    }

    std::string pollAnnouncement() {
        std::lock_guard<std::mutex> lock(eventMutex);
        if (announcements.empty()) return "";
        std::string msg = announcements.front();
        announcements.pop();
        return msg;
    }

    std::vector<RemotePlayer> getPlayers() {
        std::lock_guard<std::mutex> lock(playerMutex);
        std::vector<RemotePlayer> list;
        for (const auto& kv : players) {
            list.push_back(kv.second);
        }
        return list;
    }

    // Returns physical 1-tile player obstacles for movement blocking and echolocation reflection
    std::vector<RemotePlayerObstacle> getRemotePlayerObstacles() {
        std::lock_guard<std::mutex> lock(playerMutex);
        std::vector<RemotePlayerObstacle> list;
        for (const auto& kv : players) {
            RemotePlayerObstacle obs;
            obs.playerId = kv.second.id;
            obs.username = kv.second.username;
            obs.position = kv.second.position;
            obs.tileSize = 1.0f; // Exactly 1 tile physical collision obstacle!
            obs.clothing = kv.second.clothing;
            list.push_back(obs);
        }
        return list;
    }

    std::vector<ActiveNPC> getActiveNPCs() {
        std::lock_guard<std::mutex> lock(npcMutex);
        return activeNPCs;
    }

private:
    std::string myUsername;
    std::string myPlayerId;
    SOCKET sock = INVALID_SOCKET;
    std::atomic<bool> connected{false};
    std::thread recvThread;

    std::mutex playerMutex;
    std::unordered_map<std::string, RemotePlayer> players;

    std::mutex npcMutex;
    std::vector<ActiveNPC> activeNPCs;

    std::mutex eventMutex;
    std::queue<RemoteStepEvent> stepQueue;
    std::queue<RemoteAttackEvent> attackQueue;
    std::queue<ObjectStateEvent> objectStateQueue;
    std::queue<RemoteEchoEvent> echoQueue;
    std::queue<NPCInteractResponse> npcResponseQueue;
    std::queue<std::string> announcements;
    bool hasPendingMap = false;
    json pendingMapJson;
    std::string pendingMapTitle;

    void sendRaw(const std::string& data) {
        if (!connected || sock == INVALID_SOCKET) return;
        send(sock, data.c_str(), (int)data.length(), 0);
    }

    std::string escapeJson(const std::string& s) {
        std::string out;
        for (char c : s) {
            if (c == '"') out += "\\\"";
            else if (c == '\\') out += "\\\\";
            else out += c;
        }
        return out;
    }

    void receiveLoop() {
        std::vector<char> buffer(65536);
        std::string lineAcc;

        while (connected) {
            int bytesRead = recv(sock, buffer.data(), (int)buffer.size() - 1, 0);
            if (bytesRead <= 0) {
                connected = false;
                break;
            }

            buffer[bytesRead] = '\0';
            lineAcc.append(buffer.data(), bytesRead);

            size_t newlinePos = 0;
            while ((newlinePos = lineAcc.find('\n')) != std::string::npos) {
                std::string line = lineAcc.substr(0, newlinePos);
                lineAcc.erase(0, newlinePos + 1);
                handlePacketLine(line);
            }
        }
    }

    void handlePacketLine(const std::string& line) {
        try {
            json root = json::parse(line);
            std::string type = root.value("type", "");
            json payload = root.value("payload", json::object());

            if (type == "s2c_welcome") {
                myPlayerId = payload.value("player_id", "");
                std::cout << "[Network]: Handshake accepted. Assigned Player ID: " << myPlayerId << "\n";
                {
                    std::lock_guard<std::mutex> lock(eventMutex);
                    announcements.push("Connected to Fake-VR server. Ready for multiplayer.");
                }

                if (payload.contains("other_players") && payload["other_players"].is_array()) {
                    std::lock_guard<std::mutex> pLock(playerMutex);
                    for (const auto& p : payload["other_players"]) {
                        std::string pid = p.value("player_id", "");
                        if (!pid.empty() && pid != myPlayerId) {
                            RemotePlayer rp;
                            rp.id = pid;
                            rp.username = p.value("username", "Player");
                            if (p.contains("position") && p["position"].is_object()) {
                                rp.position.x = p["position"].value("x", 0.0f);
                                rp.position.y = p["position"].value("y", p["position"].value("z", 0.0f));
                                rp.position.z = p["position"].value("z", 0.0f);
                                if (p["position"].contains("y") && p["position"].contains("z")) {
                                    rp.position.y = p["position"].value("y", 0.0f);
                                    rp.position.z = p["position"].value("z", 0.0f);
                                }
                            }
                            players[pid] = rp;
                        }
                    }
                }
            }
            else if (type == "s2c_player_joined") {
                json p = payload.contains("player") ? payload["player"] : payload;
                std::string pid = p.value("player_id", "");
                std::string uname = p.value("username", "Player");

                if (!pid.empty() && pid != myPlayerId) {
                    std::lock_guard<std::mutex> pLock(playerMutex);
                    RemotePlayer rp;
                    rp.id = pid;
                    rp.username = uname;
                    if (p.contains("position") && p["position"].is_object()) {
                        rp.position.x = p["position"].value("x", 0.0f);
                        rp.position.y = p["position"].value("y", p["position"].value("z", 0.0f));
                        rp.position.z = p["position"].value("z", 0.0f);
                        if (p["position"].contains("y") && p["position"].contains("z")) {
                            rp.position.y = p["position"].value("y", 0.0f);
                            rp.position.z = p["position"].value("z", 0.0f);
                        }
                    }
                    players[pid] = rp;
                }

                std::lock_guard<std::mutex> lock(eventMutex);
                announcements.push(uname + " entered the world.");
            }
            else if (type == "s2c_world_tick") {
                if (payload.contains("players") && payload["players"].is_array()) {
                    std::lock_guard<std::mutex> pLock(playerMutex);
                    for (const auto& p : payload["players"]) {
                        std::string pid = p.value("player_id", "");
                        if (!pid.empty() && pid != myPlayerId) {
                            RemotePlayer& rp = players[pid];
                            rp.id = pid;
                            if (p.contains("username")) rp.username = p.value("username", "Player");
                            if (p.contains("position") && p["position"].is_object()) {
                                rp.position.x = p["position"].value("x", 0.0f);
                                rp.position.y = p["position"].value("y", p["position"].value("z", 0.0f));
                                rp.position.z = p["position"].value("z", 0.0f);
                                if (p["position"].contains("y") && p["position"].contains("z")) {
                                    rp.position.y = p["position"].value("y", 0.0f);
                                    rp.position.z = p["position"].value("z", 0.0f);
                                }
                            }
                            rp.yaw = p.value("yaw", 0.0f);
                            rp.isJumping = p.value("is_jumping", false);
                        }
                    }
                }
            }
            else if (type == "s2c_player_left") {
                std::string pid = payload.value("player_id", "");
                std::string uname = payload.value("username", "A player");
                {
                    std::lock_guard<std::mutex> pLock(playerMutex);
                    players.erase(pid);
                }
                std::lock_guard<std::mutex> lock(eventMutex);
                announcements.push(uname + " left the world.");
            }
            else if (type == "c2s_move") {
                std::string pid = payload.value("player_id", "");
                if (pid == myPlayerId) return;

                Vector3 pos;
                if (payload.contains("position") && payload["position"].is_object()) {
                    pos.x = payload["position"].value("x", 0.0f);
                    pos.y = payload["position"].value("y", payload["position"].value("z", 0.0f));
                    pos.z = payload["position"].value("z", 0.0f);
                    if (payload["position"].contains("y") && payload["position"].contains("z")) {
                        pos.y = payload["position"].value("y", 0.0f);
                        pos.z = payload["position"].value("z", 0.0f);
                    }
                }
                std::string sSurf = payload.value("surface", "wood");
                std::string sCloth = payload.value("clothing", "regular");

                SurfaceType st = string_to_surface(sSurf);
                ClothingType ct = ClothingType::REGULAR;
                if (sCloth == "armor") ct = ClothingType::ARMOR;
                else if (sCloth == "naked") ct = ClothingType::NAKED;

                {
                    std::lock_guard<std::mutex> pLock(playerMutex);
                    players[pid].position = pos;
                    players[pid].surface = st;
                    players[pid].clothing = ct;
                    players[pid].isJumping = payload.value("is_jumping", false);
                }

                std::lock_guard<std::mutex> lock(eventMutex);
                stepQueue.push({pid, pos, st, ct});
            }
            else if (type == "s2c_echolocation_event") {
                std::string pid = payload.value("player_id", "");
                if (pid == myPlayerId) return;

                Vector3 pos;
                if (payload.contains("position") && payload["position"].is_object()) {
                    pos.x = payload["position"].value("x", 0.0f);
                    pos.y = payload["position"].value("y", payload["position"].value("z", 0.0f));
                    pos.z = payload["position"].value("z", 0.0f);
                }
                float yaw = payload.value("yaw", 0.0f);

                std::lock_guard<std::mutex> lock(eventMutex);
                echoQueue.push({pid, pos, yaw});
            }
            else if (type == "s2c_attack_event") {
                std::string pid = payload.value("player_id", "");
                RemoteAttackEvent ev;
                ev.playerId = pid;
                ev.username = payload.value("username", "Someone");
                ev.targetName = payload.value("target_name", "object");
                std::string sMat = payload.value("material", "wood");
                ev.material = string_to_surface(sMat);
                ev.destroyed = payload.value("destroyed", false);
                if (payload.contains("impact_pos") && payload["impact_pos"].is_object()) {
                    ev.impactPos.x = payload["impact_pos"].value("x", 0.0f);
                    ev.impactPos.y = payload["impact_pos"].value("y", payload["impact_pos"].value("z", 0.0f));
                    ev.impactPos.z = payload["impact_pos"].value("z", 0.0f);
                }

                std::lock_guard<std::mutex> lock(eventMutex);
                attackQueue.push(ev);
                if (ev.destroyed && pid != myPlayerId) {
                    announcements.push(ev.username + " destroyed " + ev.targetName + "!");
                }
            }
            else if (type == "s2c_object_state" || type == "s2c_object_respawned") {
                ObjectStateEvent ev;
                ev.objectId = payload.value("id", -1);
                ev.name = payload.value("name", "Object");
                ev.health = payload.value("health", 100);
                ev.maxHealth = payload.value("max_health", 100);
                ev.isDestroyed = payload.value("is_destroyed", false);

                std::lock_guard<std::mutex> lock(eventMutex);
                objectStateQueue.push(ev);
                if (type == "s2c_object_respawned") {
                    announcements.push(ev.name + " has regenerated.");
                }
            }
            else if (type == "s2c_npc_update") {
                if (payload.contains("npcs") && payload["npcs"].is_array()) {
                    std::lock_guard<std::mutex> nLock(npcMutex);
                    activeNPCs.clear();
                    for (const auto& n : payload["npcs"]) {
                        ActiveNPC an;
                        an.id = n.value("id", "npc");
                        an.name = n.value("name", "NPC");
                        if (n.contains("position") && n["position"].is_object()) {
                            an.position.x = n["position"].value("x", 0.0f);
                            an.position.y = n["position"].value("y", 0.0f);
                            an.position.z = n["position"].value("z", 0.0f);
                        }
                        an.yaw = n.value("yaw", 0.0f);
                        an.soundAsset = n.value("sound_asset", "footsteps/wood_1.wav");
                        an.foleyAsset = n.value("foley_asset", "foley/regular/regular_1.wav");
                        an.clothing = n.value("clothing", "regular");
                        an.dialogue = n.value("dialogue", "Greetings.");
                        an.isMoving = n.value("is_moving", false);
                        activeNPCs.push_back(an);

                        if (an.isMoving) {
                            std::lock_guard<std::mutex> eLock(eventMutex);
                            stepQueue.push({an.id, an.position, SurfaceType::WOOD, ClothingType::REGULAR});
                        }
                    }
                }
            }
            else if (type == "s2c_interact_response") {
                NPCInteractResponse resp;
                resp.speakerName = payload.value("speaker_name", "Speaker");
                resp.dialogue = payload.value("dialogue", "...");
                resp.soundAsset = payload.value("sound_asset", "");

                std::lock_guard<std::mutex> lock(eventMutex);
                npcResponseQueue.push(resp);
                announcements.push(resp.speakerName + " says: " + resp.dialogue);
            }
            else if (type == "s2c_map_data" || type == "s2c_map_updated") {
                if (payload.contains("map") && payload["map"].is_object()) {
                    std::string mTitle = payload.value("title", payload["map"].value("title", "Fake-VR World"));
                    std::cout << "[Network]: Received authoritative map packet: '" << mTitle << "'\n";
                    {
                        std::lock_guard<std::mutex> lock(eventMutex);
                        pendingMapJson = payload["map"];
                        pendingMapTitle = mTitle;
                        hasPendingMap = true;
                        if (type == "s2c_map_updated") {
                            std::string by = payload.value("updated_by", "Server");
                            announcements.push("World map updated by " + by + ".");
                        }
                    }
                }
            }
        } catch (...) {
        }
    }
};

} // namespace fakevr
