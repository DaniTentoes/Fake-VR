#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <commctrl.h>

#include "audio_engine_cpp.hpp"
#include "screen_reader.hpp"
#include "models.hpp"
#include "network_client.hpp"

#include <string>
#include <fstream>
#include <sstream>
#include <cmath>

#pragma comment(lib, "comctl32.lib")

using namespace fakevr;

// Connection info configuration
struct AppConfig {
    std::string host = "bore.pub";
    int port = 32827;
    std::string username = "Player";
};

AppConfig load_config() {
    AppConfig cfg;
    std::ifstream f("client_config.json");
    if (f.is_open()) {
        try {
            json j;
            f >> j;
            cfg.host = j.value("host", cfg.host);
            cfg.port = j.value("port", cfg.port);
            cfg.username = j.value("username", cfg.username);
        } catch (...) {}
    }
    return cfg;
}

void save_config(const AppConfig& cfg) {
    try {
        json j;
        j["host"] = cfg.host;
        j["port"] = cfg.port;
        j["username"] = cfg.username;
        std::ofstream f("client_config.json");
        if (f.is_open()) {
            f << j.dump(2);
        }
    } catch (...) {}
}

// Global Game State
static bool g_dialogAccepted = false;
static AppConfig g_cfg;

// Dialog Callback for Connection Prompt
LRESULT CALLBACK ConnectDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_INITDIALOG: {
            SetWindowTextA(GetDlgItem(hDlg, 101), g_cfg.host.c_str());
            SetWindowTextA(GetDlgItem(hDlg, 102), std::to_string(g_cfg.port).c_str());
            SetWindowTextA(GetDlgItem(hDlg, 103), g_cfg.username.c_str());
            SetFocus(GetDlgItem(hDlg, 103)); // Default focus on Username
            return FALSE;
        }
        case WM_COMMAND: {
            int wmId = LOWORD(wParam);
            if (wmId == IDOK) {
                char bufHost[256] = {0};
                char bufPort[64] = {0};
                char bufUser[256] = {0};

                GetWindowTextA(GetDlgItem(hDlg, 101), bufHost, 255);
                GetWindowTextA(GetDlgItem(hDlg, 102), bufPort, 63);
                GetWindowTextA(GetDlgItem(hDlg, 103), bufUser, 255);

                if (strlen(bufHost) > 0) g_cfg.host = bufHost;
                if (strlen(bufPort) > 0) g_cfg.port = std::atoi(bufPort);
                if (strlen(bufUser) > 0) g_cfg.username = bufUser;

                save_config(g_cfg);
                g_dialogAccepted = true;
                EndDialog(hDlg, IDOK);
                return TRUE;
            } else if (wmId == IDCANCEL) {
                g_dialogAccepted = false;
                EndDialog(hDlg, IDCANCEL);
                return TRUE;
            }
            break;
        }
    }
    return FALSE;
}

// Build programmatic dialog template in memory
bool ShowConnectionDialog(HINSTANCE hInstance, HWND hParent) {
    std::vector<WORD> dlgTemplate(1024, 0);
    LPDLGTEMPLATE pDlg = (LPDLGTEMPLATE)dlgTemplate.data();
    pDlg->style = DS_MODALFRAME | DS_CENTER | WS_POPUP | WS_CAPTION | WS_SYSMENU;
    pDlg->cdit = 8;
    pDlg->x = 0; pDlg->y = 0;
    pDlg->cx = 240; pDlg->cy = 130;

    WORD* p = (WORD*)(pDlg + 1);
    *p++ = 0; // Menu
    *p++ = 0; // Class
    const wchar_t* title = L"Fake-VR Server Connect";
    wcscpy((wchar_t*)p, title);
    p += wcslen(title) + 1;

    auto alignDword = [](WORD* ptr) -> WORD* {
        ULONG_PTR ul = (ULONG_PTR)ptr;
        ul += 3;
        ul &= ~3;
        return (WORD*)ul;
    };

    auto addControl = [&](DWORD style, short x, short y, short cx, short cy, WORD id, WORD classId, const wchar_t* text) {
        p = alignDword(p);
        LPDLGITEMTEMPLATE pItem = (LPDLGITEMTEMPLATE)p;
        pItem->style = style;
        pItem->x = x; pItem->y = y; pItem->cx = cx; pItem->cy = cy;
        pItem->id = id;
        p = (WORD*)(pItem + 1);
        *p++ = 0xFFFF;
        *p++ = classId;
        wcscpy((wchar_t*)p, text);
        p += wcslen(text) + 1;
        *p++ = 0;
    };

    // Label & Edit: Host
    addControl(WS_CHILD | WS_VISIBLE | SS_LEFT, 15, 12, 60, 10, 201, 0x0082, L"Server Host:");
    addControl(WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, 80, 10, 145, 12, 101, 0x0081, L"");

    // Label & Edit: Port
    addControl(WS_CHILD | WS_VISIBLE | SS_LEFT, 15, 32, 60, 10, 202, 0x0082, L"Port:");
    addControl(WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL | ES_NUMBER, 80, 30, 145, 12, 102, 0x0081, L"");

    // Label & Edit: Username
    addControl(WS_CHILD | WS_VISIBLE | SS_LEFT, 15, 52, 60, 10, 203, 0x0082, L"Username:");
    addControl(WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, 80, 50, 145, 12, 103, 0x0081, L"");

    // Buttons: Connect (IDOK) & Exit (IDCANCEL)
    addControl(WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, 55, 90, 75, 18, IDOK, 0x0080, L"Enter World");
    addControl(WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 140, 90, 75, 18, IDCANCEL, 0x0080, L"Cancel");

    INT_PTR res = DialogBoxIndirectA(hInstance, pDlg, hParent, (DLGPROC)ConnectDlgProc);
    return (res == IDOK);
}

// Window procedure for main game viewport
LRESULT CALLBACK MainWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hWnd, &ps);
            RECT rc;
            GetClientRect(hWnd, &rc);
            HBRUSH bgBrush = CreateSolidBrush(RGB(18, 22, 28));
            FillRect(hdc, &rc, bgBrush);
            DeleteObject(bgBrush);

            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, RGB(220, 225, 235));

            HFONT hFontTitle = CreateFontA(24, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, "Segoe UI");
            HFONT hFontOld = (HFONT)SelectObject(hdc, hFontTitle);

            TextOutA(hdc, 24, 20, "Fake-VR Spatial Audio Cyberspace", 32);

            HFONT hFontBody = CreateFontA(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, "Segoe UI");
            SelectObject(hdc, hFontBody);
            DeleteObject(hFontTitle);

            SetTextColor(hdc, RGB(160, 175, 195));
            TextOutA(hdc, 24, 60, "Connected to: bore.pub:32827 | Binaural 3D Audio Active", 55);

            SetTextColor(hdc, RGB(200, 210, 225));
            const char* helpText[] = {
                "CONTROLS:",
                "  W, A, S, D        - Walk forward, strafe, move diagonally",
                "  Left / Right      - Turn 30 degrees",
                "  Spacebar          - Attack object in front (Destructible)",
                "  U                 - Active 8-Ray Echolocation Pulse",
                "  T                 - Cycle outfit (Regular Clothes, Naked, Armor)",
                "  P                 - Nearby Players Radar",
                "  C                 - Status & Coordinates",
                "  Escape / Alt+F4   - Exit",
            };

            int y = 100;
            for (const auto& line : helpText) {
                TextOutA(hdc, 24, y, line, (int)strlen(line));
                y += 24;
            }

            SelectObject(hdc, hFontOld);
            DeleteObject(hFontBody);
            EndPaint(hWnd, &ps);
            return 0;
        }
        case WM_DESTROY: {
            PostQuitMessage(0);
            return 0;
        }
    }
    return DefWindowProcA(hWnd, msg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow) {
    g_cfg = load_config();

    // 1. Show Accessible Connection Dialog
    if (!ShowConnectionDialog(hInstance, NULL)) {
        return 0;
    }

    ScreenReader sr;
    sr.speak("Starting Fake-VR spatial audio client.");

    // 2. Register and Create Native Main Window
    const char* CLASS_NAME = "FakeVR_Client_Window";
    WNDCLASSA wc{};
    wc.lpfnWndProc = MainWndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClassA(&wc);

    HWND hWnd = CreateWindowExA(
        0,
        CLASS_NAME,
        "Fake-VR (Spatial Audio Cyberspace)",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 700, 460,
        NULL, NULL, hInstance, NULL
    );

    if (!hWnd) return 0;
    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);

    // 3. Audio & World Engine Setup
    AudioEngineCpp audio(48000, 1024);
    WorldMap world;
    if (!world.load_from_json_file("assets/maps/default_world.json")) {
        world = create_realistic_lounge();
    }
    Vector3 playerPos = world.spawn_point;
    float playerYaw = 0.0f;
    SurfaceType curSurface = world.get_surface_at(playerPos);
    ClothingType curClothes = ClothingType::REGULAR;
    AuraType curAura = AuraType::NONE;
    const WorldObject* curNearbyObj = world.get_nearby_object(playerPos);
    const RoomRegion* curRegion = world.get_region_at(playerPos);

    audio.setMap(world);
    audio.setListener(playerPos, playerYaw);
    audio.setAura(curAura);
    audio.updateAmbienceForPosition(playerPos);

    if (!audio.start()) {
        MessageBoxA(hWnd, "Failed to start native audio engine! Check audio output device.", "Audio Error", MB_ICONERROR);
        return 1;
    }

    // 4. Connect to Server
    NetworkClient net;
    bool isMultiplayer = net.connectToServer(g_cfg.host, g_cfg.port, g_cfg.username);
    if (isMultiplayer) {
        sr.speak("Connected to server as " + g_cfg.username);
        net.sendMove(playerPos, playerYaw, curSurface, curClothes, false);
    } else {
        sr.speak("Server offline. Continuing in standalone offline mode.");
    }

    std::string startMsg = "Welcome to Fake-VR, " + g_cfg.username + 
                           ". Press W, A, S, D to walk. U for echolocation. Spacebar to attack. T to toggle clothes. P for players online. C for status. Escape to quit.";
    sr.speak(startMsg);

    // 5. Game Loop & Real-Time Input Pump
    bool running = true;
    const float stepSize = 0.65f;
    const float turnStep = 30.0f;
    const DWORD walkStepIntervalMs = 280;
    DWORD lastWalkTime = 0;
    bool wasMoving = false;

    bool prevKeyU = false;
    bool prevKeySpace = false;
    bool prevKeyT = false;
    bool prevKeyY = false;
    bool prevKeyP = false;
    bool prevKeyC = false;
    bool prevKeyH = false;
    bool prevKeyLeft = false;
    bool prevKeyRight = false;

    MSG msg{};
    while (running) {
        // Dispatch Win32 messages without blocking
        while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                running = false;
                break;
            }
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
        if (!running) break;

        DWORD now = GetTickCount();

        // 1-Minute Regeneration Check
        std::vector<std::string> respawned;
        if (world.update_regenerations((unsigned long long)now, respawned)) {
            audio.setMap(world);
            for (const auto& rName : respawned) {
                sr.speak(rName + " has regenerated.");
            }
        }

        // Multiplayer Incoming Events
        if (isMultiplayer) {
            std::string ann;
            while (!(ann = net.pollAnnouncement()).empty()) {
                sr.speak(ann);
            }

            RemoteStepEvent stepEv;
            while (net.pollStep(stepEv)) {
                audio.triggerMovement(stepEv.position, stepEv.surface, stepEv.clothing, false);
            }

            RemoteAttackEvent atkEv;
            while (net.pollAttack(atkEv)) {
                audio.triggerAttackImpact(atkEv.impactPos, atkEv.material, atkEv.destroyed);
            }

            RemoteEchoEvent echoEv;
            while (net.pollEcho(echoEv)) {
                audio.triggerEcholocation(echoEv.position, echoEv.yaw);
            }

            // Real-time server map synchronization / remote client reset
            json newMapJson;
            std::string newMapTitle;
            if (net.pollMapUpdate(newMapJson, newMapTitle)) {
                if (world.load_from_json(newMapJson)) {
                    // Update engine audio acoustics and obstacles
                    audio.setMap(world);

                    // Re-resolve player's physical placement on the newly pushed world
                    curSurface = world.get_surface_at(playerPos);
                    curRegion = world.get_region_at(playerPos);
                    curNearbyObj = world.get_nearby_object(playerPos);
                    audio.updateAmbienceForPosition(playerPos);

                    // If newly placed structures or walls now overlap player's coordinate, nudge to spawn safely
                    bool collided = false;
                    std::string hitObs;
                    world.try_move(playerPos, playerPos, collided, hitObs);
                    if (collided) {
                        playerPos = world.spawn_point;
                        audio.setListener(playerPos, playerYaw);
                    }

                    // Save local cached copy of default_world.json so restarts match
                    try {
                        std::ofstream fOut("assets/maps/default_world.json");
                        if (fOut.is_open()) {
                            fOut << newMapJson.dump(2);
                        }
                    } catch (...) {}

                    sr.speak("World map updated live by host: " + newMapTitle + ".");
                }
            }
        }

        // Only process hotkeys if this window has focus
        if (GetForegroundWindow() == hWnd) {
            if (GetAsyncKeyState(VK_ESCAPE) & 0x8000) {
                running = false;
                sr.speak("Exiting Fake-VR.");
                break;
            }

            bool keyLeft = (GetAsyncKeyState(VK_LEFT) & 0x8000) != 0;
            if (keyLeft && !prevKeyLeft) {
                playerYaw = std::fmod(playerYaw - turnStep + 360.0f, 360.0f);
                audio.setListener(playerPos, playerYaw);
                if (isMultiplayer) net.sendMove(playerPos, playerYaw, curSurface, curClothes, false);
                sr.speak("Facing " + std::to_string(int(playerYaw)) + " degrees.");
            }
            prevKeyLeft = keyLeft;

            bool keyRight = (GetAsyncKeyState(VK_RIGHT) & 0x8000) != 0;
            if (keyRight && !prevKeyRight) {
                playerYaw = std::fmod(playerYaw + turnStep, 360.0f);
                audio.setListener(playerPos, playerYaw);
                if (isMultiplayer) net.sendMove(playerPos, playerYaw, curSurface, curClothes, false);
                sr.speak("Facing " + std::to_string(int(playerYaw)) + " degrees.");
            }
            prevKeyRight = keyRight;

            bool keyU = (GetAsyncKeyState('U') & 0x8000) != 0;
            if (keyU && !prevKeyU) {
                audio.triggerEcholocation(playerPos, playerYaw);
                if (isMultiplayer) net.sendEcholocation(playerPos, playerYaw);
            }
            prevKeyU = keyU;

            bool keySpace = (GetAsyncKeyState(VK_SPACE) & 0x8000) != 0;
            if (keySpace && !prevKeySpace) {
                std::string hitName;
                SurfaceType hitMat = SurfaceType::WOOD;
                bool destroyed = false;
                int hpLeft = 0;
                Vector3 impactPos;

                int bareHandsDamage = 25;
                if (world.attack_object_in_front(playerPos, playerYaw, bareHandsDamage, hitName, hitMat, destroyed, hpLeft, impactPos, (unsigned long long)now)) {
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
            }
            prevKeySpace = keySpace;

            bool keyT = (GetAsyncKeyState('T') & 0x8000) != 0;
            if (keyT && !prevKeyT) {
                if (curClothes == ClothingType::REGULAR) curClothes = ClothingType::NAKED;
                else if (curClothes == ClothingType::NAKED) curClothes = ClothingType::ARMOR;
                else curClothes = ClothingType::REGULAR;

                sr.speak("Wearing " + clothing_to_string(curClothes));
                audio.triggerMovement(playerPos, curSurface, curClothes, true);
                if (isMultiplayer) net.sendMove(playerPos, playerYaw, curSurface, curClothes, false);
            }
            prevKeyT = keyT;

            bool keyY = (GetAsyncKeyState('Y') & 0x8000) != 0;
            if (keyY && !prevKeyY) {
                if (curAura == AuraType::NONE) curAura = AuraType::VINYL;
                else if (curAura == AuraType::VINYL) curAura = AuraType::CHIME;
                else if (curAura == AuraType::CHIME) curAura = AuraType::HUM;
                else if (curAura == AuraType::HUM) curAura = AuraType::SILK;
                else curAura = AuraType::NONE;

                audio.setAura(curAura);
                sr.speak("Acoustic Aura: " + aura_to_string(curAura));
                audio.triggerMovement(playerPos, curSurface, curClothes, true);
            }
            prevKeyY = keyY;

            bool keyP = (GetAsyncKeyState('P') & 0x8000) != 0;
            if (keyP && !prevKeyP) {
                if (!isMultiplayer) {
                    sr.speak("Offline mode. You are alone.");
                } else {
                    auto remotePlayers = net.getPlayers();
                    if (remotePlayers.empty()) {
                        sr.speak("No other players nearby.");
                    } else {
                        std::string radar = std::to_string(remotePlayers.size()) + (remotePlayers.size() > 1 ? " players online: " : " player online: ");
                        for (const auto& rp : remotePlayers) {
                            float d = playerPos.distance_xz(rp.position);
                            std::string uName = rp.username.empty() ? "Guest" : rp.username;
                            radar += uName + " at " + std::to_string((int)std::round(d)) + " meters. ";
                        }
                        sr.speak(radar);
                    }
                }
            }
            prevKeyP = keyP;

            bool keyC = (GetAsyncKeyState('C') & 0x8000) != 0;
            if (keyC && !prevKeyC) {
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
            }
            prevKeyC = keyC;

            bool keyH = (GetAsyncKeyState('H') & 0x8000) != 0;
            if (keyH && !prevKeyH) {
                sr.speak("Controls: W, A, S, D to walk. Left and Right arrows to turn. T to change clothes. U for echolocation. Spacebar to attack. P for players online. C for coordinates. Escape to quit.");
            }
            prevKeyH = keyH;

            // Multi-Directional Movement
            float fwd = 0.0f;
            float str = 0.0f;

            if (GetAsyncKeyState('W') & 0x8000) fwd += 1.0f;
            if (GetAsyncKeyState('S') & 0x8000) fwd -= 1.0f;
            if (GetAsyncKeyState('D') & 0x8000) str += 1.0f;
            if (GetAsyncKeyState('A') & 0x8000) str -= 1.0f;

            bool isMovingNow = (fwd != 0.0f || str != 0.0f);

            bool stepReady = false;
            if (isMovingNow) {
                if (!wasMoving || (now - lastWalkTime >= walkStepIntervalMs)) {
                    stepReady = true;
                    lastWalkTime = now;
                }
            }
            wasMoving = isMovingNow;

            if (stepReady) {
                float mag = std::sqrt(fwd * fwd + str * str);
                if (mag > 0.0001f) {
                    fwd /= mag;
                    str /= mag;
                }

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

                    if (isMultiplayer) {
                        net.sendMove(playerPos, playerYaw, curSurface, curClothes, true);
                    }

                    if (curRegion != oldRegion && curRegion != nullptr) {
                        audio.updateAmbienceForPosition(playerPos);
                        sr.speak("Entering " + curRegion->name);
                    } else if (curNearbyObj != oldObj && curNearbyObj != nullptr) {
                        sr.speak("Next to " + curNearbyObj->name + ", " + surface_to_string(curNearbyObj->material));
                    } else if (curSurface != oldSurface) {
                        sr.speak(surface_to_string(curSurface) + " floor.");
                    }
                }
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
