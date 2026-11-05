#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#include "models.hpp"
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <queue>
#include <iostream>
#include <sstream>

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
        char buffer[4096];
        std::string lineAcc;

        while (connected) {
            int bytesRead = recv(sock, buffer, sizeof(buffer) - 1, 0);
            if (bytesRead <= 0) {
                connected = false;
                break;
            }

            buffer[bytesRead] = '\0';
            lineAcc.append(buffer, bytesRead);

            size_t newlinePos = 0;
            while ((newlinePos = lineAcc.find('\n')) != std::string::npos) {
                std::string line = lineAcc.substr(0, newlinePos);
                lineAcc.erase(0, newlinePos + 1);
                handlePacketLine(line);
            }
        }
    }

    void handlePacketLine(const std::string& line) {
        // Fast lightweight JSON parser for game events
        std::string type = getJsonString(line, "type");

        if (type == "s2c_welcome") {
            myPlayerId = getJsonString(line, "player_id");
            std::cout << "[Network]: Handshake accepted. Assigned Player ID: " << myPlayerId << "\n";
            std::lock_guard<std::mutex> lock(eventMutex);
            announcements.push("Connected to Fake-VR server. Ready for multiplayer.");

            // Parse initial other_players list if any were already in the room
            size_t othersPos = line.find("\"other_players\":[");
            if (othersPos != std::string::npos) {
                size_t start = othersPos + 17;
                size_t end = line.find("]", start);
                if (end != std::string::npos && end > start + 2) {
                    std::string slice = line.substr(start, end - start);
                    size_t pIndex = 0;
                    while ((pIndex = slice.find("\"player_id\":\"", pIndex)) != std::string::npos) {
                        pIndex += 13;
                        size_t pEnd = slice.find("\"", pIndex);
                        if (pEnd == std::string::npos) break;
                        std::string pid = slice.substr(pIndex, pEnd - pIndex);

                        std::string uname = "Player";
                        size_t uPos = slice.find("\"username\":\"", pIndex);
                        if (uPos != std::string::npos && uPos < slice.find("}", pIndex)) {
                            uPos += 12;
                            size_t uEnd = slice.find("\"", uPos);
                            if (uEnd != std::string::npos) uname = slice.substr(uPos, uEnd - uPos);
                        }

                        if (pid != myPlayerId) {
                            std::lock_guard<std::mutex> pLock(playerMutex);
                            players[pid].id = pid;
                            players[pid].username = uname;
                        }
                        pIndex = pEnd;
                    }
                }
            }
        }
        else if (type == "s2c_player_joined") {
            std::string uname = getJsonString(line, "username");
            std::string pid = getJsonString(line, "player_id");
            if (uname.empty()) uname = getJsonString(line, "name");

            if (!pid.empty() && pid != myPlayerId) {
                std::lock_guard<std::mutex> pLock(playerMutex);
                players[pid].id = pid;
                players[pid].username = uname.empty() ? "Player" : uname;
            }

            std::lock_guard<std::mutex> lock(eventMutex);
            announcements.push(uname.empty() ? "Another player joined the world." : (uname + " entered the lounge."));
        }
        else if (type == "s2c_player_left") {
            std::string uname = getJsonString(line, "username");
            std::string pid = getJsonString(line, "player_id");
            {
                std::lock_guard<std::mutex> pLock(playerMutex);
                players.erase(pid);
            }
            std::lock_guard<std::mutex> lock(eventMutex);
            announcements.push(uname.empty() ? "A player departed." : (uname + " left the world."));
        }
        else if (type == "c2s_move") { // Relayed step from another player
            std::string pid = getJsonString(line, "player_id");
            if (pid == myPlayerId) return; // ignore echo of self

            Vector3 pos;
            pos.x = getJsonFloat(line, "x");
            pos.y = getJsonFloat(line, "y");
            pos.z = getJsonFloat(line, "z");
            std::string sSurf = getJsonString(line, "surface");
            std::string sCloth = getJsonString(line, "clothing");

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

            // Enqueue remote step audio trigger
            std::lock_guard<std::mutex> lock(eventMutex);
            stepQueue.push({pid, pos, st, ct});
        }
        else if (type == "s2c_echolocation_event") {
            std::string pid = getJsonString(line, "player_id");
            if (pid == myPlayerId) return;

            Vector3 pos;
            pos.x = getJsonFloat(line, "x");
            pos.y = getJsonFloat(line, "y");
            pos.z = getJsonFloat(line, "z");
            float yaw = getJsonFloat(line, "yaw");

            std::lock_guard<std::mutex> lock(eventMutex);
            echoQueue.push({pid, pos, yaw});
        }
        else if (type == "s2c_attack_event") {
            std::string pid = getJsonString(line, "player_id");
            if (pid == myPlayerId) return;

            RemoteAttackEvent ev;
            ev.playerId = pid;
            ev.username = getJsonString(line, "username");
            ev.targetName = getJsonString(line, "target_name");
            std::string sMat = getJsonString(line, "material");
            ev.material = SurfaceType::WOOD;
            if (sMat == "glass") ev.material = SurfaceType::GLASS;
            else if (sMat == "metal") ev.material = SurfaceType::METAL;
            else if (sMat == "stone") ev.material = SurfaceType::STONE;

            ev.destroyed = (line.find("\"destroyed\":true") != std::string::npos);
            ev.impactPos.x = getJsonFloat(line, "x");
            ev.impactPos.y = getJsonFloat(line, "y");
            ev.impactPos.z = getJsonFloat(line, "z");

            std::lock_guard<std::mutex> lock(eventMutex);
            attackQueue.push(ev);
            if (ev.destroyed) {
                announcements.push(ev.username + " destroyed " + ev.targetName + "!");
            }
        }
    }

    // Ultra-lightweight JSON value extractors for zero external JSON library overhead
    std::string getJsonString(const std::string& json, const std::string& key) {
        std::string pattern = "\"" + key + "\":\"";
        size_t pos = json.find(pattern);
        if (pos == std::string::npos) return "";
        pos += pattern.length();
        size_t endPos = json.find("\"", pos);
        if (endPos == std::string::npos) return "";
        return json.substr(pos, endPos - pos);
    }

    float getJsonFloat(const std::string& json, const std::string& key) {
        std::string pattern = "\"" + key + "\":";
        size_t pos = json.find(pattern);
        if (pos == std::string::npos) return 0.0f;
        pos += pattern.length();
        while (pos < json.length() && (json[pos] == ' ' || json[pos] == '"')) pos++;
        char* endPtr = nullptr;
        float val = std::strtof(&json[pos], &endPtr);
        return val;
    }
};

} // namespace fakevr
