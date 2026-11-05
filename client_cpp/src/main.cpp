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
    std::string host = "127.0.0.1";
    int port = 7777;
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

static bool g_dialogAccepted = false;
static AppConfig g_cfg;

LRESULT CALLBACK ConnectDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_INITDIALOG: {
            SetWindowTextA(GetDlgItem(hDlg, 101), g_cfg.host.c_str());
            SetWindowTextA(GetDlgItem(hDlg, 102), std::to_string(g_cfg.port).c_str());
            SetWindowTextA(GetDlgItem(hDlg, 103), g_cfg.username.c_str());
            SetFocus(GetDlgItem(hDlg, 103));
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

bool ShowConnectionDialog(HINSTANCE hInstance, HWND hParent) {
    std::vector<WORD> dlgTemplate(1024, 0);
    LPDLGTEMPLATE pDlg = (LPDLGTEMPLATE)dlgTemplate.data();
    pDlg->style = DS_MODALFRAME | DS_CENTER | WS_POPUP | WS_CAPTION | WS_SYSMENU;
    pDlg->cdit = 8;
    pDlg->x = 0; pDlg->y = 0;
    pDlg->cx = 240; pDlg->cy = 130;

    WORD* p = (WORD*)(pDlg + 1);
    *p++ = 0;
    *p++ = 0;
    const wchar_t* title = L"Fake-VR Server Connect";
    wcscpy_s((wchar_t*)p, 64, title);
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
        wcscpy_s((wchar_t*)p, 64, text);
        p += wcslen(text) + 1;
        *p++ = 0;
    };

    addControl(WS_CHILD | WS_VISIBLE | SS_LEFT, 15, 12, 60, 10, 201, 0x0082, L"Server Host:");
    addControl(WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, 80, 10, 145, 12, 101, 0x0081, L"");

    addControl(WS_CHILD | WS_VISIBLE | SS_LEFT, 15, 32, 60, 10, 202, 0x0082, L"Port:");
    addControl(WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL | ES_NUMBER, 80, 30, 145, 12, 102, 0x0081, L"");

    addControl(WS_CHILD | WS_VISIBLE | SS_LEFT, 15, 52, 60, 10, 203, 0x0082, L"Username:");
    addControl(WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, 80, 50, 145, 12, 103, 0x0081, L"");

    addControl(WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, 55, 90, 75, 18, IDOK, 0x0080, L"Enter World");
    addControl(WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 140, 90, 75, 18, IDCANCEL, 0x0080, L"Cancel");

    INT_PTR res = DialogBoxIndirectA(hInstance, pDlg, hParent, (DLGPROC)ConnectDlgProc);
    return (res == IDOK);
}

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
            TextOutA(hdc, 24, 60, "Steam Audio HRTF & Occlusion Active | 3D Spatial Audio", 54);

            SetTextColor(hdc, RGB(200, 210, 225));
            const char* helpText[] = {
                "CONTROLS:",
                "  W, A, S, D        - Walk forward, strafe (1 step = 1 tile)",
                "  Left / Right      - Turn 30 degrees",
                "  Ctrl + Space      - Jump (3 tiles elevation arc)",
                "  Spacebar          - Attack object in front (Server Authoritative)",
                "  O / Shift+O       - Look around at Objects (Shift+O for accessible list)",
                "  N / Shift+N       - Look around at NPCs (Shift+N for accessible list)",
                "  J / Shift+J       - Look around at Structures & Portals (Shift+J for list)",
                "  B                 - Building Tool (Mark Point A and Point B to erect walls)",
                "  E                 - Interact with nearby NPC or structure",
                "  U                 - Active 16-Ray Echolocation Pulse",
                "  T                 - Cycle outfit (Regular Clothes, Naked, Armor)",
                "  P                 - Radar (Nearby Players & NPCs)",
                "  C                 - Status & 3D Coordinates (X, Y, Z)",
                "  Escape / Alt+F4   - Exit",
            };

            int y = 90;
            for (const auto& line : helpText) {
                TextOutA(hdc, 24, y, line, (int)strlen(line));
                y += 20;
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

// Window procedure and data for Accessible Modal List Dialog
struct ListDialogData {
    ScreenReader* sr = nullptr;
    std::string title;
    std::vector<NearbyItemInfo> items;
    HWND hListBox = NULL;
    bool done = false;
};

static WNDPROC g_OriginalListBoxProc = NULL;
static ListDialogData* g_ActiveListDialogData = NULL;

LRESULT CALLBACK AccessibleListBoxSubclassProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_KEYDOWN) {
        if (wParam == VK_ESCAPE || wParam == VK_RETURN) {
            if (g_ActiveListDialogData) {
                g_ActiveListDialogData->done = true;
                if (g_ActiveListDialogData->sr) {
                    g_ActiveListDialogData->sr->speak("Closed.");
                }
            }
            HWND hParent = GetParent(hWnd);
            if (hParent) DestroyWindow(hParent);
            return 0;
        }
    }
    return CallWindowProcA(g_OriginalListBoxProc, hWnd, msg, wParam, lParam);
}

LRESULT CALLBACK AccessibleListWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            CREATESTRUCTA* cs = (CREATESTRUCTA*)lParam;
            ListDialogData* data = (ListDialogData*)cs->lpCreateParams;
            SetWindowLongPtrA(hWnd, GWLP_USERDATA, (LONG_PTR)data);
            g_ActiveListDialogData = data;

            RECT rc;
            GetClientRect(hWnd, &rc);
            HWND hList = CreateWindowExA(
                WS_EX_CLIENTEDGE,
                "LISTBOX",
                "",
                WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_TABSTOP | LBS_NOTIFY | LBS_HASSTRINGS | LBS_NOINTEGRALHEIGHT,
                12, 12, rc.right - 24, rc.bottom - 24,
                hWnd,
                (HMENU)1001,
                cs->hInstance,
                NULL
            );
            data->hListBox = hList;

            HFONT hFont = CreateFontA(18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, "Segoe UI");
            SendMessageA(hList, WM_SETFONT, (WPARAM)hFont, TRUE);

            for (const auto& item : data->items) {
                int distTiles = (int)std::round(item.distance);
                std::string line = std::to_string(distTiles) + (distTiles == 1 ? " tile: " : " tiles: ") + 
                                   item.name + " (" + item.clockDirection + ") - " + item.details;
                SendMessageA(hList, LB_ADDSTRING, 0, (LPARAM)line.c_str());
            }

            if (!data->items.empty()) {
                SendMessageA(hList, LB_SETCURSEL, 0, 0);
            }

            // Subclass list box so Escape and Enter close cleanly
            g_OriginalListBoxProc = (WNDPROC)SetWindowLongPtrA(hList, GWLP_WNDPROC, (LONG_PTR)AccessibleListBoxSubclassProc);
            SetFocus(hList);
            return 0;
        }
        case WM_COMMAND: {
            if (LOWORD(wParam) == 1001 && HIWORD(wParam) == LBN_SELCHANGE) {
                ListDialogData* data = (ListDialogData*)GetWindowLongPtrA(hWnd, GWLP_USERDATA);
                if (data && data->hListBox && data->sr) {
                    int sel = (int)SendMessageA(data->hListBox, LB_GETCURSEL, 0, 0);
                    if (sel >= 0 && sel < (int)data->items.size()) {
                        const auto& item = data->items[sel];
                        int distTiles = (int)std::round(item.distance);
                        std::string spoken = item.name + ", " + std::to_string(distTiles) + 
                                             (distTiles == 1 ? " tile " : " tiles ") + 
                                             item.clockDirection + ". " + item.details;
                        data->sr->speak(spoken);
                    }
                }
            }
            return 0;
        }
        case WM_CLOSE: {
            ListDialogData* data = (ListDialogData*)GetWindowLongPtrA(hWnd, GWLP_USERDATA);
            if (data) {
                data->done = true;
                if (data->sr) data->sr->speak("Closed.");
            }
            DestroyWindow(hWnd);
            return 0;
        }
        case WM_DESTROY: {
            ListDialogData* data = (ListDialogData*)GetWindowLongPtrA(hWnd, GWLP_USERDATA);
            if (data) data->done = true;
            g_ActiveListDialogData = nullptr;
            return 0;
        }
    }
    return DefWindowProcA(hWnd, msg, wParam, lParam);
}

void ShowAccessibleListDialog(HWND parentHwnd, ScreenReader& sr, const std::string& title, const std::vector<NearbyItemInfo>& items) {
    if (items.empty()) {
        sr.speak("No items found nearby.");
        return;
    }

    HINSTANCE hInst = GetModuleHandleA(NULL);
    const char* DLG_CLASS = "FakeVR_AccessibleList_Window";

    static bool classRegistered = false;
    if (!classRegistered) {
        WNDCLASSA wc{};
        wc.lpfnWndProc = AccessibleListWndProc;
        wc.hInstance = hInst;
        wc.lpszClassName = DLG_CLASS;
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        RegisterClassA(&wc);
        classRegistered = true;
    }

    ListDialogData data;
    data.sr = &sr;
    data.title = title;
    data.items = items;
    data.done = false;

    RECT rcParent;
    GetWindowRect(parentHwnd, &rcParent);
    int width = 580;
    int height = 420;
    int x = rcParent.left + ((rcParent.right - rcParent.left) - width) / 2;
    int y = rcParent.top + ((rcParent.bottom - rcParent.top) - height) / 2;

    HWND hWndDlg = CreateWindowExA(
        WS_EX_DLGMODALFRAME | WS_EX_TOPMOST,
        DLG_CLASS,
        title.c_str(),
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        x, y, width, height,
        parentHwnd,
        NULL,
        hInst,
        &data
    );

    if (!hWndDlg) return;

    int firstDist = (int)std::round(items[0].distance);
    std::string intro = title + ". " + std::to_string(items.size()) + (items.size() == 1 ? " item. " : " items. ") +
                        "First: " + items[0].name + ", " + std::to_string(firstDist) + (firstDist == 1 ? " tile " : " tiles ") + 
                        items[0].clockDirection + ". Use Up and Down arrows to explore. Escape to close.";
    sr.speak(intro);

    MSG msg;
    while (!data.done && GetMessageA(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }

    if (IsWindow(hWndDlg)) {
        DestroyWindow(hWndDlg);
    }

    SetFocus(parentHwnd);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow) {
    g_cfg = load_config();

    if (!ShowConnectionDialog(hInstance, NULL)) {
        return 0;
    }

    ScreenReader sr;
    sr.speak("Starting Fake-VR 3D spatial audio client.");

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
        "Fake-VR (3D Spatial Audio Cyberspace)",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 740, 480,
        NULL, NULL, hInstance, NULL
    );

    if (!hWnd) return 0;
    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);

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

    NetworkClient net;
    bool isMultiplayer = net.connectToServer(g_cfg.host, g_cfg.port, g_cfg.username);
    if (isMultiplayer) {
        sr.speak("Connected to server as " + g_cfg.username);
        net.sendMove(playerPos, playerYaw, curSurface, curClothes, false);
    } else {
        sr.speak("Server offline. Running in standalone local mode.");
    }

    std::string startMsg = "Welcome to Fake-VR, " + g_cfg.username + 
                           ". W, A, S, D to walk. 1 step is 1 tile. Ctrl Space to jump. Space to attack. B for building tool. E to interact. C for coordinates.";
    sr.speak(startMsg);

    bool running = true;
    const float stepSize = 1.0f; // Exactly 1 tile = 1.0 meter!
    const float turnStep = 30.0f;
    const DWORD walkStepIntervalMs = 320;
    DWORD lastWalkTime = 0;
    bool wasMoving = false;

    // 3D Jump Controller
    JumpController jumpCtrl;
    jumpCtrl.baseJumpHeight = 3.0f; // Default 3 tiles vertical height!
    jumpCtrl.accessoryMultiplier = 1.0f; // Ready for footwear/accessories

    // In-Game Building Tool State
    bool buildingMode = false;
    bool hasBuildPointA = false;
    Vector3 buildPointA{0.0f, 0.0f, 0.0f};

    bool prevKeyU = false;
    bool prevKeySpace = false;
    bool prevKeyB = false;
    bool prevKeyE = false;
    bool prevKeyO = false;
    bool prevKeyN = false;
    bool prevKeyJ = false;
    bool prevKeyReturn = false;
    bool prevKeyT = false;
    bool prevKeyY = false;
    bool prevKeyP = false;
    bool prevKeyC = false;
    bool prevKeyH = false;
    bool prevKeyLeft = false;
    bool prevKeyRight = false;

    MSG msg{};
    while (running) {
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

        // 3D Jump physics update
        if (jumpCtrl.isJumping) {
            bool landed = false;
            float newZ = jumpCtrl.update((unsigned long long)now, landed);
            playerPos.z = newZ;
            audio.setListener(playerPos, playerYaw);

            if (landed) {
                audio.triggerLand(playerPos);
                sr.speak("Landed.");
                if (isMultiplayer) {
                    net.sendMove(playerPos, playerYaw, curSurface, curClothes, false, false);
                }
            }
        }

        // Offline regeneration fallback
        if (!isMultiplayer) {
            std::vector<std::string> respawned;
            if (world.update_regenerations((unsigned long long)now, respawned)) {
                audio.setMap(world);
                for (const auto& rName : respawned) {
                    sr.speak(rName + " has regenerated.");
                }
            }
        }

        // Multiplayer Incoming Events & State Synchronization
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

            // 1. Process Authoritative Map Updates FIRST
            json newMapJson;
            std::string newMapTitle;
            if (net.pollMapUpdate(newMapJson, newMapTitle)) {
                // Cache destroyed and damaged object states from current world so map reload never rematerializes them
                std::unordered_map<std::string, std::pair<int, bool>> prevObjStates;
                for (const auto& obj : world.objects) {
                    if (obj.isDestroyed || obj.health < obj.maxHealth) {
                        prevObjStates[obj.id] = {obj.health, obj.isDestroyed};
                        prevObjStates[obj.name] = {obj.health, obj.isDestroyed};
                    }
                }

                if (world.load_from_json(newMapJson)) {
                    // Reapply preserved damaged/destroyed states
                    for (auto& obj : world.objects) {
                        auto it = prevObjStates.find(obj.id);
                        if (it == prevObjStates.end()) it = prevObjStates.find(obj.name);
                        if (it != prevObjStates.end()) {
                            obj.health = it->second.first;
                            obj.isDestroyed = it->second.second;
                            obj.solidObstacle = !obj.isDestroyed;
                        }
                    }

                    audio.setMap(world);
                    curSurface = world.get_surface_at(playerPos);
                    curRegion = world.get_region_at(playerPos);
                    curNearbyObj = world.get_nearby_object(playerPos);
                    audio.updateAmbienceForPosition(playerPos);

                    bool collided = false;
                    std::string hitObs;
                    world.try_move(playerPos, playerPos, collided, hitObs);
                    if (collided) {
                        playerPos = world.spawn_point;
                        audio.setListener(playerPos, playerYaw);
                    }

                    sr.speak("World map updated live: " + newMapTitle + ".");
                }
            }

            // 2. Process Authoritative Object State updates ON TOP of the loaded map
            ObjectStateEvent objEv;
            while (net.pollObjectState(objEv)) {
                for (auto& o : world.objects) {
                    bool match = false;
                    if (!objEv.objectId.empty() && o.id == objEv.objectId) match = true;
                    else if (objEv.legacyId >= 0 && o.idIndex == objEv.legacyId) match = true;
                    else if (o.name == objEv.name) match = true;

                    if (match) {
                        o.health = objEv.health;
                        o.maxHealth = objEv.maxHealth;
                        o.isDestroyed = objEv.isDestroyed;
                        o.solidObstacle = !objEv.isDestroyed;
                        break;
                    }
                }
                audio.setMap(world);
            }

            // Sync authoritative NPCs from network to world
            auto netNpcs = net.getActiveNPCs();
            if (!netNpcs.empty()) {
                world.active_npcs = netNpcs;
            }
        }

        // Announce nearby NPCs on initial arrival / detection
        static bool announcedJoinNpcs = false;
        if (!announcedJoinNpcs && !world.active_npcs.empty()) {
            announcedJoinNpcs = true;
            auto nearby = world.get_nearby_npcs(playerPos, playerYaw, world.active_npcs, 25.0f);
            if (!nearby.empty()) {
                std::string npcAnn = "In area: ";
                for (size_t i = 0; i < nearby.size(); ++i) {
                    int d = (int)std::round(nearby[i].distance);
                    npcAnn += nearby[i].name + " at " + std::to_string(d) + " tiles " + nearby[i].clockDirection + (i + 1 < nearby.size() ? ", " : ".");
                }
                sr.speak(npcAnn);
            }
        }

        // Periodic 3D audible presence rustle/breathing for nearby stationary NPCs (every 3.5s)
        static double lastNpcPresenceTime = 0.0;
        if (now - lastNpcPresenceTime >= 3.5) {
            lastNpcPresenceTime = now;
            for (const auto& npc : world.active_npcs) {
                float d = playerPos.distance_xy(npc.position);
                if (d <= 20.0f) {
                    SurfaceType nSurf = world.get_surface_at(npc.position);
                    ClothingType nCloth = ClothingType::REGULAR;
                    if (npc.clothing == "armor") nCloth = ClothingType::ARMOR;
                    else if (npc.clothing == "naked") nCloth = ClothingType::NAKED;
                    audio.triggerMovement(npc.position, nSurf, nCloth, false);
                }
            }
        }

        if (GetForegroundWindow() == hWnd) {
            if (GetAsyncKeyState(VK_ESCAPE) & 0x8000) {
                if (buildingMode) {
                    buildingMode = false;
                    hasBuildPointA = false;
                    sr.speak("Building tool mode exited.");
                } else {
                    running = false;
                    sr.speak("Exiting Fake-VR.");
                    break;
                }
            }

            bool keyLeft = (GetAsyncKeyState(VK_LEFT) & 0x8000) != 0;
            if (keyLeft && !prevKeyLeft) {
                playerYaw = std::fmod(playerYaw - turnStep + 360.0f, 360.0f);
                audio.setListener(playerPos, playerYaw);
                if (isMultiplayer) net.sendMove(playerPos, playerYaw, curSurface, curClothes, false, jumpCtrl.isJumping);
                sr.speak("Facing " + std::to_string(int(playerYaw)) + " degrees.");
            }
            prevKeyLeft = keyLeft;

            bool keyRight = (GetAsyncKeyState(VK_RIGHT) & 0x8000) != 0;
            if (keyRight && !prevKeyRight) {
                playerYaw = std::fmod(playerYaw + turnStep, 360.0f);
                audio.setListener(playerPos, playerYaw);
                if (isMultiplayer) net.sendMove(playerPos, playerYaw, curSurface, curClothes, false, jumpCtrl.isJumping);
                sr.speak("Facing " + std::to_string(int(playerYaw)) + " degrees.");
            }
            prevKeyRight = keyRight;

            // 16-Ray Echolocation
            bool keyU = (GetAsyncKeyState('U') & 0x8000) != 0;
            if (keyU && !prevKeyU) {
                audio.triggerEcholocation(playerPos, playerYaw);
                if (isMultiplayer) net.sendEcholocation(playerPos, playerYaw);
            }
            prevKeyU = keyU;

            // Jump (Ctrl + Space) or Attack (Space alone)
            bool ctrlPressed = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
            bool keySpace = (GetAsyncKeyState(VK_SPACE) & 0x8000) != 0;
            if (keySpace && !prevKeySpace) {
                if (ctrlPressed) {
                    // JUMP! (3 Tiles default vertical height arc)
                    if (!jumpCtrl.isJumping) {
                        jumpCtrl.startJump((unsigned long long)now);
                        audio.triggerJump(playerPos);
                        sr.speak("Jump!");
                        if (isMultiplayer) {
                            net.sendMove(playerPos, playerYaw, curSurface, curClothes, false, true);
                        }
                    }
                } else {
                    // ATTACK OBJECT IN FRONT
                    std::string hitId, hitName;
                    SurfaceType hitMat = SurfaceType::WOOD;
                    bool destroyed = false;
                    int hpLeft = 0;
                    Vector3 impactPos;

                    int bareHandsDamage = 25;
                    if (world.attack_object_in_front(playerPos, playerYaw, bareHandsDamage, hitId, hitName, hitMat, destroyed, hpLeft, impactPos, (unsigned long long)now)) {
                        audio.triggerAttackImpact(impactPos, hitMat, destroyed);
                        audio.setMap(world);

                        if (isMultiplayer) {
                            net.sendAttack(hitId, hitName, hitMat, bareHandsDamage, impactPos);
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
            }
            prevKeySpace = keySpace;

            // Building Tool (Hotkey 'B')
            bool keyB = (GetAsyncKeyState('B') & 0x8000) != 0;
            if (keyB && !prevKeyB) {
                buildingMode = !buildingMode;
                hasBuildPointA = false;
                if (buildingMode) {
                    sr.speak("Building tool enabled. Stand at Point A and press Enter to mark the ground.");
                } else {
                    sr.speak("Building tool disabled.");
                }
            }
            prevKeyB = keyB;

            // Building Tool Confirmation (Hotkey 'Enter')
            bool keyReturn = (GetAsyncKeyState(VK_RETURN) & 0x8000) != 0;
            if (keyReturn && !prevKeyReturn) {
                if (buildingMode) {
                    if (!hasBuildPointA) {
                        buildPointA = playerPos;
                        hasBuildPointA = true;
                        audio.triggerWallBump(playerPos);
                        sr.speak("Point A marked at X " + std::to_string(int(buildPointA.x)) + 
                                 ", Y " + std::to_string(int(buildPointA.y)) + 
                                 ". Now walk to Point B and press Enter to erect the wall.");
                    } else {
                        float dist = buildPointA.distance_xy(playerPos);
                        if (dist < 1.0f) {
                            sr.speak("Point B is too close to Point A. Walk at least 1 tile away.");
                        } else {
                            float x1 = buildPointA.x, y1 = buildPointA.y;
                            float x2 = playerPos.x, y2 = playerPos.y;
                            std::string wallName = "Constructed Wall";
                            
                            // Add locally
                            WallSegment ws{x1, y1, x2, y2, wallName, SurfaceType::WOOD, 0.60f, false, -1};
                            world.walls.push_back(ws);
                            audio.setMap(world);
                            audio.triggerAttackImpact(playerPos, SurfaceType::WOOD, false);

                            if (isMultiplayer) {
                                net.sendBuildWall(x1, y1, x2, y2, SurfaceType::WOOD, wallName);
                            }

                            sr.speak("Wall erected between marked points! " + std::to_string(int(dist)) + " meters long.");
                            hasBuildPointA = false;
                            buildingMode = false;
                        }
                    }
                }
            }
            prevKeyReturn = keyReturn;

            // Interact with NPC or Structure (Hotkey 'E')
            bool keyE = (GetAsyncKeyState('E') & 0x8000) != 0;
            if (keyE && !prevKeyE) {
                auto npcs = world.active_npcs;
                bool foundNPC = false;
                for (const auto& npc : npcs) {
                    float d = playerPos.distance_xy(npc.position);
                    if (d <= 2.5f) {
                        foundNPC = true;
                        sr.speak("Speaking to " + npc.name + ": " + npc.dialogue);
                        audio.triggerAttackImpact(npc.position, SurfaceType::WOOD, false);
                        if (isMultiplayer) {
                            net.sendInteract(npc.id);
                        }
                        break;
                    }
                }
                if (!foundNPC) {
                    // Check travel points
                    const TravelPoint* tp = world.get_triggered_travel_point(playerPos);
                    if (tp) {
                        sr.speak("Entering travel point: " + tp->name);
                        if (isMultiplayer) {
                            net.sendTravel(tp->target_map, tp->target_spawn);
                        }
                    } else {
                        sr.speak("Nothing nearby to interact with.");
                    }
                }
            }
            prevKeyE = keyE;

            bool keyT = (GetAsyncKeyState('T') & 0x8000) != 0;
            if (keyT && !prevKeyT) {
                if (curClothes == ClothingType::REGULAR) curClothes = ClothingType::NAKED;
                else if (curClothes == ClothingType::NAKED) curClothes = ClothingType::ARMOR;
                else curClothes = ClothingType::REGULAR;

                sr.speak("Wearing " + clothing_to_string(curClothes));
                audio.triggerMovement(playerPos, curSurface, curClothes, true);
                if (isMultiplayer) net.sendMove(playerPos, playerYaw, curSurface, curClothes, false, jumpCtrl.isJumping);
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

            bool shiftPressed = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;

            // Hotkey 'O': Look around at Objects (Shift+O for accessible list)
            bool keyO = (GetAsyncKeyState('O') & 0x8000) != 0;
            if (keyO && !prevKeyO) {
                auto nearbyObjs = world.get_nearby_objects(playerPos, playerYaw, 25.0f);
                if (shiftPressed) {
                    ShowAccessibleListDialog(hWnd, sr, "Nearby Objects", nearbyObjs);
                } else {
                    if (nearbyObjs.empty()) {
                        sr.speak("No objects nearby.");
                    } else if (nearbyObjs.size() == 1) {
                        int d = (int)std::round(nearbyObjs[0].distance);
                        sr.speak(nearbyObjs[0].name + ", " + std::to_string(d) + (d == 1 ? " tile " : " tiles ") + nearbyObjs[0].clockDirection + ".");
                    } else {
                        int d = (int)std::round(nearbyObjs[0].distance);
                        sr.speak("Found " + std::to_string(nearbyObjs.size()) + " objects. Nearest: " + nearbyObjs[0].name + " at " + std::to_string(d) + " tiles " + nearbyObjs[0].clockDirection + ". Press Shift O for list.");
                    }
                }
            }
            prevKeyO = keyO;

            // Hotkey 'N': Look around at NPCs (Shift+N for accessible list)
            bool keyN = (GetAsyncKeyState('N') & 0x8000) != 0;
            if (keyN && !prevKeyN) {
                auto nearbyNpcs = world.get_nearby_npcs(playerPos, playerYaw, world.active_npcs, 25.0f);
                if (shiftPressed) {
                    ShowAccessibleListDialog(hWnd, sr, "Nearby NPCs", nearbyNpcs);
                } else {
                    if (nearbyNpcs.empty()) {
                        sr.speak("No NPCs nearby.");
                    } else if (nearbyNpcs.size() == 1) {
                        int d = (int)std::round(nearbyNpcs[0].distance);
                        sr.speak(nearbyNpcs[0].name + ", " + std::to_string(d) + (d == 1 ? " tile " : " tiles ") + nearbyNpcs[0].clockDirection + ".");
                    } else {
                        int d = (int)std::round(nearbyNpcs[0].distance);
                        sr.speak("Found " + std::to_string(nearbyNpcs.size()) + " NPCs. Nearest: " + nearbyNpcs[0].name + " at " + std::to_string(d) + " tiles " + nearbyNpcs[0].clockDirection + ". Press Shift N for list.");
                    }
                }
            }
            prevKeyN = keyN;

            // Hotkey 'J': Look around at Structures & Portals (Shift+J for accessible list)
            bool keyJ = (GetAsyncKeyState('J') & 0x8000) != 0;
            if (keyJ && !prevKeyJ) {
                auto nearbyStructs = world.get_nearby_structures(playerPos, playerYaw, 60.0f);
                if (shiftPressed) {
                    ShowAccessibleListDialog(hWnd, sr, "Nearby Structures & Portals", nearbyStructs);
                } else {
                    if (nearbyStructs.empty()) {
                        sr.speak("No structures or gateways nearby.");
                    } else if (nearbyStructs.size() == 1) {
                        int d = (int)std::round(nearbyStructs[0].distance);
                        sr.speak(nearbyStructs[0].name + ", " + std::to_string(d) + (d == 1 ? " tile " : " tiles ") + nearbyStructs[0].clockDirection + ".");
                    } else {
                        int d = (int)std::round(nearbyStructs[0].distance);
                        sr.speak("Found " + std::to_string(nearbyStructs.size()) + " structures. Nearest: " + nearbyStructs[0].name + " at " + std::to_string(d) + " tiles " + nearbyStructs[0].clockDirection + ". Press Shift J for list.");
                    }
                }
            }
            prevKeyJ = keyJ;

            // Hotkey 'P': Radar (Nearby Players & NPCs)
            bool keyP = (GetAsyncKeyState('P') & 0x8000) != 0;
            if (keyP && !prevKeyP) {
                std::string radar;
                auto remotePlayers = isMultiplayer ? net.getPlayers() : std::vector<RemotePlayer>{};
                auto activeNpcs = world.active_npcs;

                if (remotePlayers.empty() && activeNpcs.empty()) {
                    sr.speak("No players or NPCs nearby.");
                } else {
                    if (!remotePlayers.empty()) {
                        radar += std::to_string(remotePlayers.size()) + (remotePlayers.size() > 1 ? " players: " : " player: ");
                        for (const auto& rp : remotePlayers) {
                            int d = (int)std::round(playerPos.distance_xy(rp.position));
                            std::string dir = calculate_clock_direction(playerYaw, playerPos, rp.position);
                            radar += (rp.username.empty() ? "Player" : rp.username) + " at " + std::to_string(d) + " tiles " + dir + ". ";
                        }
                    }
                    if (!activeNpcs.empty()) {
                        radar += std::to_string(activeNpcs.size()) + (activeNpcs.size() > 1 ? " NPCs: " : " NPC: ");
                        for (const auto& npc : activeNpcs) {
                            int d = (int)std::round(playerPos.distance_xy(npc.position));
                            std::string dir = calculate_clock_direction(playerYaw, playerPos, npc.position);
                            radar += npc.name + " at " + std::to_string(d) + " tiles " + dir + ". ";
                        }
                    }
                    sr.speak(radar);
                }
            }
            prevKeyP = keyP;

            bool keyC = (GetAsyncKeyState('C') & 0x8000) != 0;
            if (keyC && !prevKeyC) {
                std::string regTitle = curRegion ? curRegion->name : "Area";
                std::string msg = regTitle + ". Position: X " + std::to_string(int(playerPos.x)) + 
                                  ", Y " + std::to_string(int(playerPos.y)) + 
                                  ", Height Z " + std::to_string(int(std::round(playerPos.z))) + 
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
                sr.speak("Controls: W, A, S, D to walk. 1 step is 1 tile. Ctrl Space to jump. Space to attack. B to build. E to interact. C for coordinates.");
            }
            prevKeyH = keyH;

            // Movement in 2D horizontal plane (1 step = 1 tile = 1.0m)
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
                // Forward is North (+Y), Strafe Right is East (+X)
                float dx = (std::sin(rad) * fwd + std::cos(rad) * str) * stepSize;
                float dy = (std::cos(rad) * fwd - std::sin(rad) * str) * stepSize;

                Vector3 targetPos(playerPos.x + dx, playerPos.y + dy, playerPos.z);
                bool collided = false;
                std::string hitObstacle;

                // Pass other players as physical 1-tile obstacles
                auto otherPlayers = net.getRemotePlayerObstacles();
                Vector3 newPos = world.try_move(playerPos, targetPos, collided, hitObstacle, otherPlayers);

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
                        net.sendMove(playerPos, playerYaw, curSurface, curClothes, true, jumpCtrl.isJumping);
                    }

                    // Automatic TravelPoint Trigger Check
                    const TravelPoint* tp = world.get_triggered_travel_point(playerPos);
                    if (tp) {
                        sr.speak("Entering " + tp->name);
                        if (isMultiplayer) {
                            net.sendTravel(tp->target_map, tp->target_spawn);
                        }
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
