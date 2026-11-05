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
    fs::path p1 = fs::path(exeDir) / relPath;
    if (fs::exists(p1)) return p1.string();

    fs::path p2 = fs::path(exeDir) / ".." / relPath;
    if (fs::exists(p2)) return p2.lexically_normal().string();

    return relPath;
}

class SoundBank {
public:
    SoundBank(const std::string& assetRoot = "assets/sounds") {
        std::vector<std::string> candidates = {
            assetRoot,
            "../" + assetRoot,
            (fs::path(get_executable_dir()) / assetRoot).string(),
            (fs::path(get_executable_dir()) / ".." / assetRoot).lexically_normal().string()
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

    const AudioBuffer* get(const std::string& key) const {
        std::string lk = toLower(key);
        auto it = cache.find(lk);
        if (it != cache.end()) {
            return &it->second;
        }
        // Also check if key matches filename directly
        for (const auto& kv : cache) {
            if (kv.first.find(lk) != std::string::npos || lk.find(kv.first) != std::string::npos) {
                return &kv.second;
            }
        }
        return nullptr;
    }

    const AudioBuffer* getFootstep(SurfaceType surface) const {
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

        // Pick one of 4 randomized variations: e.g. "footsteps/wood_2.wav"
        int variant = (std::rand() % 4) + 1;
        std::string varKey = "footsteps/" + base + "_" + std::to_string(variant) + ".wav";
        const AudioBuffer* buf = get(varKey);
        if (buf) return buf;

        // Fallback to non-variant
        return get("footsteps/" + base + ".wav");
    }

    const AudioBuffer* getFoley(ClothingType clothes) const {
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
