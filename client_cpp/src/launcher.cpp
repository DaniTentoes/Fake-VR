#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wininet.h>
#include <iostream>
#include <string>
#include <fstream>
#include <sstream>

#pragma comment(lib, "wininet.lib")

// Lightweight NVDA TTS invoker for accessibility announcements
typedef unsigned long(__cdecl* NvdaSpeakFn)(const wchar_t*);
static HMODULE hNvda = NULL;
static NvdaSpeakFn nvdaSpeak = NULL;

void initSpeech() {
    hNvda = LoadLibraryW(L"bin\\nvdaControllerClient64.dll");
    if (!hNvda) {
        hNvda = LoadLibraryW(L"nvdaControllerClient64.dll");
    }
    if (hNvda) {
        nvdaSpeak = (NvdaSpeakFn)GetProcAddress(hNvda, "nvdaController_speakText");
    }
}

void speak(const std::string& text) {
    if (nvdaSpeak) {
        int wlen = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, NULL, 0);
        if (wlen > 0) {
            std::wstring wstr(wlen, 0);
            MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, &wstr[0], wlen);
            nvdaSpeak(wstr.c_str());
        }
    }
    std::cout << "[Launcher]: " << text << "\n";
}

std::string httpGet(const std::string& url) {
    HINTERNET hInternet = InternetOpenA("FakeVR-Launcher", INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
    if (!hInternet) return "";

    HINTERNET hUrl = InternetOpenUrlA(hInternet, url.c_str(), NULL, 0, INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE, 0);
    if (!hUrl) {
        InternetCloseHandle(hInternet);
        return "";
    }

    std::string response;
    char buffer[4096];
    DWORD bytesRead = 0;
    while (InternetReadFile(hUrl, buffer, sizeof(buffer), &bytesRead) && bytesRead > 0) {
        response.append(buffer, bytesRead);
    }

    InternetCloseHandle(hUrl);
    InternetCloseHandle(hInternet);
    return response;
}

std::string extractJsonValue(const std::string& json, const std::string& key) {
    std::string pattern = "\"" + key + "\":";
    size_t pos = json.find(pattern);
    if (pos == std::string::npos) return "";
    pos += pattern.length();
    while (pos < json.length() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\"')) pos++;
    size_t end = json.find_first_of("\",}\r\n", pos);
    if (end == std::string::npos) return "";
    return json.substr(pos, end - pos);
}

std::string getLocalVersion() {
    std::ifstream file("version.json");
    if (!file.is_open()) return "0.0.0";
    std::stringstream buffer;
    buffer << file.rdbuf();
    return extractJsonValue(buffer.str(), "version");
}

int main(int argc, char* argv[]) {
    initSpeech();
    std::string localVer = getLocalVersion();
    std::cout << "=========================================================\n";
    std::cout << "                 FAKE-VR AUTO-LAUNCHER                  \n";
    std::cout << "=========================================================\n";
    std::cout << "Local Version: " << localVer << "\n\n";

    // Read update manifest URL from config or use default
    std::string updateUrl = "https://raw.githubusercontent.com/DaniTentoes/Fake-VR/main/version.json";
    std::ifstream cfgFile("update_source.txt");
    if (cfgFile.is_open()) {
        std::getline(cfgFile, updateUrl);
    }

    if (!updateUrl.empty() && updateUrl.find("http") == 0) {
        std::cout << "Checking for updates from: " << updateUrl << "...\n";
        std::string manifest = httpGet(updateUrl);
        if (!manifest.empty()) {
            std::string remoteVer = extractJsonValue(manifest, "version");
            std::string downloadUrl = extractJsonValue(manifest, "download_url");

            if (!remoteVer.empty() && remoteVer != localVer && !downloadUrl.empty()) {
                std::string updateMsg = "New update available: version " + remoteVer + ". Downloading update...";
                speak(updateMsg);

                // Run PowerShell updater to download and expand cleanly
                std::string psCmd = "powershell -Command \""
                                    "Write-Host 'Downloading update package...'; "
                                    "Invoke-WebRequest -Uri '" + downloadUrl + "' -OutFile 'update.zip'; "
                                    "Write-Host 'Extracting update...'; "
                                    "Expand-Archive -Path 'update.zip' -DestinationPath '.' -Force; "
                                    "Remove-Item 'update.zip' -Force; "
                                    "Write-Host 'Update complete!';\"";

                system(psCmd.c_str());
                speak("Update complete. Launching Fake-VR.");
            } else {
                std::cout << "Client is up to date.\n";
            }
        } else {
            std::cout << "Could not reach update server. Launching offline/current version.\n";
        }
    }

    // Launch game executable
    STARTUPINFOA si{};
    PROCESS_INFORMATION pi{};
    si.cb = sizeof(si);

    std::string cmdLine = "bin\\FakeVR_Client.exe";
    for (int i = 1; i < argc; ++i) {
        cmdLine += " ";
        cmdLine += argv[i];
    }

    if (CreateProcessA(NULL, &cmdLine[0], NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    } else {
        std::cerr << "Failed to start bin\\FakeVR_Client.exe (Error: " << GetLastError() << ")\n";
        speak("Failed to start Fake-VR client executable.");
        system("pause");
    }

    if (hNvda) FreeLibrary(hNvda);
    return 0;
}
