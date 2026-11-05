#pragma once

#include <windows.h>
#include <string>
#include <iostream>

namespace fakevr {

class ScreenReader {
public:
    ScreenReader() {
        // Look in executable directory or current working directory
        hNvda = LoadLibraryA("nvdaControllerClient64.dll");
        if (!hNvda) {
            hNvda = LoadLibraryA("bin/nvdaControllerClient64.dll");
        }
        if (!hNvda) {
            hNvda = LoadLibraryA("nvdaControllerClient32.dll");
        }
        if (!hNvda) {
            hNvda = LoadLibraryA("bin/nvdaControllerClient32.dll");
        }

        if (hNvda) {
            nvdaSpeak = (NvdaSpeakFn)GetProcAddress(hNvda, "nvdaController_speakText");
            nvdaCancel = (NvdaCancelFn)GetProcAddress(hNvda, "nvdaController_cancelSpeech");
            if (nvdaSpeak) {
                std::cout << "[ScreenReader]: Successfully linked to NVDA Controller Client (SAPI completely disabled).\n";
            }
        } else {
            std::cout << "[ScreenReader]: NVDA Controller Client DLL not active; SAPI disabled per user preference.\n";
        }
    }

    ~ScreenReader() {
        if (hNvda) {
            FreeLibrary(hNvda);
            hNvda = nullptr;
        }
    }

    void speak(const std::string& text, bool interrupt = true) {
        if (text.empty()) return;
        std::cout << "[Speech]: " << text << std::endl;

        // Convert UTF-8 to wide string for NVDA
        int wlen = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, NULL, 0);
        if (wlen <= 0) return;
        std::wstring wtext(wlen, 0);
        MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, &wtext[0], wlen);

        // Send exclusively to NVDA. SAPI is completely removed/disabled.
        if (nvdaSpeak) {
            if (interrupt && nvdaCancel) {
                nvdaCancel();
            }
            nvdaSpeak(wtext.c_str());
        }
    }

private:
    typedef unsigned long (__stdcall *NvdaSpeakFn)(const wchar_t*);
    typedef unsigned long (__stdcall *NvdaCancelFn)(void);

    HMODULE hNvda = nullptr;
    NvdaSpeakFn nvdaSpeak = nullptr;
    NvdaCancelFn nvdaCancel = nullptr;
};

} // namespace fakevr
