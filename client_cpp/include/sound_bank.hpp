#pragma once

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#include <vector>
#include <string>
#include <unordered_map>
#include <iostream>
#include <algorithm>
#include <filesystem>
#include <windows.h>
#include "models.hpp"

namespace fs = std::filesystem;

namespace fakevr {

struct AudioBuffer {
    std::vector<float> samples; // 48kHz mono float samples (-1.0 to 1.0)
    uint32_t sampleRate = 48000;
};

inline std::string get_executable_dir() {
    char path[MAX_PATH];
    GetModuleFileNameA(NULL, path, MAX_PATH);
    fs::path p(path);
    return p.parent_path().string();
}

inline std::string resolve_asset_path(const std::string& relPath) {
    if (fs::exists(relPath)) return relPath;

    // Check parent directory (if running from bin/)
    std::string exeDir = get_executable_dir();
    fs::path p2 = fs::path(exeDir) / ".." / relPath;
    if (fs::exists(p2)) return p2.lexically_normal().string();

    fs::path p1 = fs::path(exeDir) / relPath;
    if (fs::exists(p1)) return p1.string();

    return relPath;
}

class SoundBank {
public:
    SoundBank(const std::string& assetRoot = "assets/sounds") {
        std::vector<std::string> candidates = {
            (fs::path(get_executable_dir()) / ".." / assetRoot).lexically_normal().string(),
            (fs::path(get_executable_dir()) / assetRoot).string(),
            assetRoot,
            "../" + assetRoot
        };

        bool loaded = false;
        for (const auto& c : candidates) {
            if (fs::exists(c)) {
                loadDirectory(c);
                std::cout << "[SoundBank]: Indexed " << cache.size() << " sound assets from: " << c << "\n";
                loaded = true;
                break;
            }
        }

        if (!loaded) {
            std::cerr << "[SoundBank]: Warning: Could not locate sound assets directory!\n";
        }
    }

    const AudioBuffer* get(const std::string& key) {
        if (key.empty()) return nullptr;

        std::string lk = toLower(key);
        // Strip common prefixes
        if (lk.rfind("assets/sounds/", 0) == 0) {
            lk = lk.substr(14);
        } else if (lk.rfind("assets/", 0) == 0) {
            lk = lk.substr(7);
        }

        // 1. Direct cache lookup
        auto it = cache.find(lk);
        if (it != cache.end()) {
            return &it->second;
        }

        // 2. Try with .wav extension if omitted
        if (lk.size() < 4 || lk.substr(lk.size() - 4) != ".wav") {
            auto itExt = cache.find(lk + ".wav");
            if (itExt != cache.end()) {
                return &itExt->second;
            }
        }

        // 3. Basename lookup (e.g. "carpet_1.wav" or "carpet_1")
        std::string baseName = lk;
        size_t lastSlash = baseName.find_last_of('/');
        if (lastSlash != std::string::npos) {
            baseName = baseName.substr(lastSlash + 1);
        }
        for (const auto& kv : cache) {
            std::string kBase = kv.first;
            size_t kSlash = kBase.find_last_of('/');
            if (kSlash != std::string::npos) {
                kBase = kBase.substr(kSlash + 1);
            }
            if (kBase == baseName || (baseName.size() >= 3 && kBase.find(baseName) != std::string::npos)) {
                return &kv.second;
            }
        }

        // 4. On-demand dynamic disk load if not previously cached
        std::string resolved = resolve_asset_path("assets/sounds/" + lk);
        if (!fs::exists(resolved)) {
            resolved = resolve_asset_path(lk);
        }
        if (fs::exists(resolved) && !fs::is_directory(resolved)) {
            AudioBuffer buf;
            if (decodeWav(resolved, buf)) {
                cache[lk] = std::move(buf);
                std::cout << "[SoundBank]: Dynamically loaded asset: " << lk << " (" << cache[lk].samples.size() << " samples)\n";
                return &cache[lk];
            }
        }

        // 5. Fallback to default footstep so sound NEVER fails to play
        auto itFallback = cache.find("footsteps/wood_1.wav");
        if (itFallback != cache.end()) {
            return &itFallback->second;
        }

        return nullptr;
    }

    const AudioBuffer* getFootstep(SurfaceType surface) {
        std::string base = "wood";
        switch (surface) {
            case SurfaceType::WOOD: base = "wood"; break;
            case SurfaceType::STONE: base = "stone"; break;
            case SurfaceType::CARPET: base = "carpet"; break;
            case SurfaceType::METAL: base = "metal"; break;
            case SurfaceType::GRASS: base = "grass"; break;
            case SurfaceType::TILE: base = "tile"; break;
            case SurfaceType::GLASS: base = "tile"; break;
            case SurfaceType::LEATHER_FABRIC: base = "carpet"; break;
            default: base = "wood"; break;
        }

        // Check if randomized variations exist (e.g. "footsteps/wood_1.wav" ... "wood_4.wav")
        std::vector<std::string> availableVariants;
        for (int i = 1; i <= 8; ++i) {
            std::string candidate = "footsteps/" + base + "_" + std::to_string(i) + ".wav";
            if (cache.find(candidate) != cache.end()) {
                availableVariants.push_back(candidate);
            }
        }

        if (!availableVariants.empty()) {
            int idx = std::rand() % availableVariants.size();
            return get(availableVariants[idx]);
        }

        // Fallback to single base sound: e.g. "footsteps/wood.wav"
        return get("footsteps/" + base + ".wav");
    }

    const AudioBuffer* getFoley(ClothingType clothes) {
        std::string folder = "regular";
        std::string prefix = "regular";
        switch (clothes) {
            case ClothingType::REGULAR:
                folder = "regular";
                prefix = "regular";
                break;
            case ClothingType::NAKED:
                folder = "naked";
                prefix = "naked";
                break;
            case ClothingType::ARMOR:
                folder = "armor";
                prefix = "armor";
                break;
        }

        int variant = (std::rand() % 4) + 1;
        std::string key = "foley/" + folder + "/" + prefix + "_" + std::to_string(variant) + ".wav";
        return get(key);
    }

private:
    std::unordered_map<std::string, AudioBuffer> cache;

    static std::string toLower(std::string s) {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c){ return (char)std::tolower(c); });
        std::replace(s.begin(), s.end(), '\\', '/');
        return s;
    }

    void loadDirectory(const std::string& root) {
        if (!fs::exists(root)) return;
        for (const auto& entry : fs::recursive_directory_iterator(root)) {
            if (entry.is_regular_file() && entry.path().extension() == ".wav") {
                std::string rel = fs::relative(entry.path(), root).string();
                std::string key = toLower(rel);
                if (cache.find(key) == cache.end()) {
                    AudioBuffer buf;
                    if (decodeWav(entry.path().string(), buf)) {
                        cache[key] = std::move(buf);
                    }
                }
            }
        }
    }

    bool decodeWav(const std::string& filePath, AudioBuffer& outBuf) {
        ma_decoder_config config = ma_decoder_config_init(ma_format_f32, 1, 48000);
        ma_decoder decoder;
        if (ma_decoder_init_file(filePath.c_str(), &config, &decoder) != MA_SUCCESS) {
            return false;
        }

        ma_uint64 totalFrames = 0;
        ma_decoder_get_length_in_pcm_frames(&decoder, &totalFrames);
        if (totalFrames == 0) totalFrames = 48000 * 5; // fallback 5 sec max

        outBuf.samples.resize(totalFrames);
        ma_uint64 framesRead = 0;
        ma_decoder_read_pcm_frames(&decoder, outBuf.samples.data(), totalFrames, &framesRead);
        outBuf.samples.resize(framesRead);
        outBuf.sampleRate = 48000;

        ma_decoder_uninit(&decoder);
        return !outBuf.samples.empty();
    }
};

} // namespace fakevr
