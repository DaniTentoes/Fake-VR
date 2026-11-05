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
};

struct RemoteAttackEvent {
    std::string playerId;
    std::string username;
    std::string targetName;
    SurfaceType material;
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
    SurfaceType surface;
    ClothingType clothing;
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

        // Set TCP_NODELAY for instant spatial audio packet delivery
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

        // Send c2s_join
        std::string joinPacket = "{\"type\":\"c2s_join\",\"payload\":{\"username\":\"" + escapeJson(username) + "\",\"room_id\":\"lounge\",\"aura\":\"none\"}}\n";
        sendRaw(joinPacket);

        // Start background receive thread
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

    // Broadcast local player step
    void sendMove(const Vector3& pos, float yaw, SurfaceType surface, ClothingType clothes, bool isMoving) {
        if (!connected) return;
        std::string sName = surface_to_string(surface);
        std::string cName = (clothes == ClothingType::ARMOR) ? "armor" : ((clothes == ClothingType::NAKED) ? "naked" : "regular");

        std::ostringstream ss;
        ss << "{\"type\":\"c2s_move\",\"payload\":{"
           << "\"position\":{\"x\":" << pos.x << ",\"y\":" << pos.y << ",\"z\":" << pos.z << "},"
           << "\"yaw\":" << yaw << ","
           << "\"pitch\":0.0,"
           << "\"is_moving\":" << (isMoving ? "true" : "false") << ","
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

    // Broadcast local player attack
    void sendAttack(const std::string& targetName, SurfaceType mat, bool destroyed, const Vector3& impactPos) {
        if (!connected) return;
        std::string sMat = surface_to_string(mat);
        std::ostringstream ss;
        ss << "{\"type\":\"c2s_attack\",\"payload\":{"
           << "\"target_name\":\"" << escapeJson(targetName) << "\","
           << "\"material\":\"" << sMat << "\","
           << "\"destroyed\":" << (destroyed ? "true" : "false") << ","
           << "\"impact_pos\":{\"x\":" << impactPos.x << ",\"y\":" << impactPos.y << ",\"z\":" << impactPos.z << "}"
           << "}}\n";
        sendRaw(ss.str());
    }

    // Pop events for the main thread audio engine & screen reader
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

private:
    std::string myUsername;
    std::string myPlayerId;
    SOCKET sock = INVALID_SOCKET;
    std::atomic<bool> connected{false};
    std::thread recvThread;

    std::mutex playerMutex;
    std::unordered_map<std::string, RemotePlayer> players;

    std::mutex eventMutex;
    std::queue<RemoteStepEvent> stepQueue;
    std::queue<RemoteAttackEvent> attackQueue;
    std::queue<RemoteEchoEvent> echoQueue;
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
                                rp.position.y = p["position"].value("y", 0.0f);
                                rp.position.z = p["position"].value("z", 0.0f);
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
                        rp.position.y = p["position"].value("y", 0.0f);
                        rp.position.z = p["position"].value("z", 0.0f);
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
                                rp.position.y = p["position"].value("y", 0.0f);
                                rp.position.z = p["position"].value("z", 0.0f);
                            }
                            rp.yaw = p.value("yaw", 0.0f);
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
                    pos.y = payload["position"].value("y", 0.0f);
                    pos.z = payload["position"].value("z", 0.0f);
                }
                std::string sSurf = payload.value("surface", "wood");
                std::string sCloth = payload.value("clothing", "regular");

                SurfaceType st = SurfaceType::WOOD;
                if (sSurf == "stone") st = SurfaceType::STONE;
                else if (sSurf == "carpet") st = SurfaceType::CARPET;
                else if (sSurf == "grass") st = SurfaceType::GRASS;
                else if (sSurf == "tile") st = SurfaceType::TILE;
                else if (sSurf == "metal") st = SurfaceType::METAL;

                ClothingType ct = ClothingType::REGULAR;
                if (sCloth == "armor") ct = ClothingType::ARMOR;
                else if (sCloth == "naked") ct = ClothingType::NAKED;

                {
                    std::lock_guard<std::mutex> pLock(playerMutex);
                    players[pid].position = pos;
                    players[pid].surface = st;
                    players[pid].clothing = ct;
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
                    pos.y = payload["position"].value("y", 0.0f);
                    pos.z = payload["position"].value("z", 0.0f);
                }
                float yaw = payload.value("yaw", 0.0f);

                std::lock_guard<std::mutex> lock(eventMutex);
                echoQueue.push({pid, pos, yaw});
            }
            else if (type == "s2c_attack_event") {
                std::string pid = payload.value("player_id", "");
                if (pid == myPlayerId) return;

                RemoteAttackEvent ev;
                ev.playerId = pid;
                ev.username = payload.value("username", "Someone");
                ev.targetName = payload.value("target_name", "object");
                std::string sMat = payload.value("material", "wood");
                ev.material = SurfaceType::WOOD;
                if (sMat == "glass") ev.material = SurfaceType::GLASS;
                else if (sMat == "metal") ev.material = SurfaceType::METAL;
                else if (sMat == "stone") ev.material = SurfaceType::STONE;

                ev.destroyed = payload.value("destroyed", false);
                if (payload.contains("impact_pos") && payload["impact_pos"].is_object()) {
                    ev.impactPos.x = payload["impact_pos"].value("x", 0.0f);
                    ev.impactPos.y = payload["impact_pos"].value("y", 0.0f);
                    ev.impactPos.z = payload["impact_pos"].value("z", 0.0f);
                }

                std::lock_guard<std::mutex> lock(eventMutex);
                attackQueue.push(ev);
                if (ev.destroyed) {
                    announcements.push(ev.username + " destroyed " + ev.targetName + "!");
                }
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
                            announcements.push("World map was updated in real-time by " + by + ".");
                        }
                    }
                }
            }
        } catch (...) {
            // Non-JSON or malformed packet
        }
    }
};

} // namespace fakevr
