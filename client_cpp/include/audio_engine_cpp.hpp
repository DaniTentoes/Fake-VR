#pragma once

#include "miniaudio.h"
#include "steam_audio_cpp.hpp"
#include "sound_bank.hpp"
#include "ir_tail_convolver.hpp"
#include "models.hpp"

#include <mutex>
#include <vector>
#include <memory>
#include <iostream>

namespace fakevr {

struct ActiveSound {
    const AudioBuffer* buffer = nullptr;
    std::vector<float> syntheticBuffer;
    Vector3 position;
    float volume = 1.0f;
    bool isLocalHead = false;
    bool isFootstep = false;
    bool isLooping = false;
    int spatialChannel = 0;
    int delaySamples = 0;
    int cursor = 0;
    bool finished = false;

    int getNextFrames(int frames, std::vector<float>& outMono) {
        outMono.assign(frames, 0.0f);
        const float* src = buffer ? buffer->samples.data() : syntheticBuffer.data();
        size_t total = buffer ? buffer->samples.size() : syntheticBuffer.size();
        if (total == 0) {
            finished = true;
            return 0;
        }

        if (delaySamples > 0) {
            if (delaySamples >= frames) {
                delaySamples -= frames;
                return 0;
            }
            int skip = delaySamples;
            delaySamples = 0;
            int available = int(total - cursor);
            int toCopy = (std::min)(frames - skip, available);
            for (int i = 0; i < toCopy; ++i) {
                outMono[skip + i] = src[cursor + i] * volume;
            }
            cursor += toCopy;
            if (cursor >= (int)total) {
                if (isLooping) cursor = 0;
                else finished = true;
            }
            return toCopy;
        }

        int copied = 0;
        while (copied < frames) {
            int available = int(total - cursor);
            int toCopy = (std::min)(frames - copied, available);
            for (int i = 0; i < toCopy; ++i) {
                outMono[copied + i] = src[cursor + i] * volume;
            }
            copied += toCopy;
            cursor += toCopy;
            if (cursor >= (int)total) {
                if (isLooping) {
                    cursor = 0;
                } else {
                    finished = true;
                    break;
                }
            }
        }
        return copied;
    }
};

class AudioEngineCpp {
public:
    AudioEngineCpp(int sampleRate = 48000, int frameSize = 1024)
        : sampleRate(sampleRate), frameSize(frameSize), spatializer(sampleRate, frameSize), soundBank("assets/sounds") {
        
        std::string irPath = resolve_asset_path("assets/sounds/impulses/lounge_ir_stereo.wav");
        if (!irTail.loadIR(irPath, sampleRate)) {
            std::cerr << "[AudioEngine] Warning: Could not load lounge IR tail from " << irPath << "\n";
        }
    }

    ~AudioEngineCpp() {
        stop();
    }

    bool start() {
        if (!spatializer.isInitialized()) {
            std::cerr << "Steam Audio failed to initialize!\n";
            return false;
        }

        ma_device_config deviceConfig = ma_device_config_init(ma_device_type_playback);
        deviceConfig.playback.format = ma_format_f32;
        deviceConfig.playback.channels = 2;
        deviceConfig.sampleRate = sampleRate;
        deviceConfig.periodSizeInFrames = frameSize;
        deviceConfig.dataCallback = audioCallback;
        deviceConfig.pUserData = this;

        if (ma_device_init(NULL, &deviceConfig, &device) != MA_SUCCESS) {
            std::cerr << "Failed to open WASAPI audio device with miniaudio!\n";
            return false;
        }

        if (ma_device_start(&device) != MA_SUCCESS) {
            std::cerr << "Failed to start audio device playback!\n";
            ma_device_uninit(&device);
            return false;
        }

        running = true;
        std::cout << "[AudioEngineCpp] Physical 3D audio running at " 
                  << sampleRate << "Hz (" << frameSize << " frame WASAPI buffer).\n";
        return true;
    }

    void stop() {
        if (running) {
            ma_device_uninit(&device);
            running = false;
        }
    }

    void setListener(const Vector3& pos, float yaw) {
        std::lock_guard<std::mutex> lock(audioMutex);
        listenerPos = pos;
        listenerYaw = yaw;
    }

    void setAura(AuraType a) {
        std::lock_guard<std::mutex> lock(audioMutex);
        currentAura = a;
    }

    void setMap(const WorldMap& m) {
        std::lock_guard<std::mutex> lock(audioMutex);
        currentMap = m;
        refreshEmittingObjects();
    }

    void updateAmbienceForPosition(const Vector3& pos) {
        std::lock_guard<std::mutex> lock(audioMutex);
        const RoomRegion* r = currentMap.get_region_at(pos);
        std::string neededAsset = "";
        float neededVol = 0.40f;

        if (r) {
            if (!r->ambienceAsset.empty()) {
                neededAsset = r->ambienceAsset;
            } else if (r->ambienceCategory == "outdoor" || r->isOutside) {
                neededAsset = "ambiences/outdoor/wind_field.wav";
            } else {
                neededAsset = "ambiences/indoor/room_tone.wav";
            }
            neededVol = r->ambienceVolume;
        } else {
            neededAsset = "ambiences/outdoor/wind_field.wav";
        }

        if (neededAsset != currentAmbienceFile) {
            currentAmbienceFile = neededAsset;
            const AudioBuffer* buf = soundBank.get(neededAsset);
            if (!buf) {
                buf = soundBank.get("environment/resonance_ambient.wav");
            }

            if (buf && !buf->samples.empty()) {
                ambienceSound.buffer = buf;
                ambienceSound.volume = neededVol;
                ambienceSound.isLocalHead = true;
                ambienceSound.isLooping = true;
                ambienceSound.cursor = 0;
                ambienceSound.finished = false;
                hasAmbience = true;
            } else {
                hasAmbience = false;
            }
        }
    }

    void refreshEmittingObjects() {
        for (auto& s : activeSounds) {
            if (s.isLooping && !s.isLocalHead) {
                s.finished = true;
            }
        }

        int ch = 10;
        for (const auto& obj : currentMap.objects) {
            if (obj.isDestroyed || obj.soundAsset.empty()) continue;
            const AudioBuffer* buf = soundBank.get(obj.soundAsset);
            if (!buf) {
                buf = soundBank.get("objects/" + obj.soundAsset);
                if (!buf) buf = soundBank.get("auras/" + obj.soundAsset);
            }

            if (buf && !buf->samples.empty()) {
                ActiveSound emitter;
                emitter.buffer = buf;
                emitter.position = obj.get_center();
                emitter.volume = obj.soundVolume;
                emitter.isLocalHead = false;
                emitter.isLooping = true;
                emitter.cursor = 0;
                emitter.finished = false;
                emitter.spatialChannel = (ch++ % 12) + 10; // channels 10-21
                activeSounds.push_back(std::move(emitter));
            }
        }
    }

    // Physical Movement Audio (Footsteps + Foley) with 8-ray early reflections
    void triggerMovement(const Vector3& pos, SurfaceType surface, ClothingType clothes, bool isLocal) {
        const AudioBuffer* footBuf = soundBank.getFootstep(surface);
        const AudioBuffer* foleyBuf = soundBank.getFoley(clothes);

        std::lock_guard<std::mutex> lock(audioMutex);

        for (auto& s : activeSounds) {
            if (s.isFootstep) {
                s.finished = true;
            }
        }

        // 1. Direct footstep sound (ground level)
        if (footBuf && !footBuf->samples.empty()) {
            ActiveSound snd;
            snd.buffer = footBuf;
            snd.position = Vector3(pos.x, pos.y, pos.z - 1.0f);
            snd.volume = isLocal ? 1.0f : 0.70f;
            snd.isLocalHead = false;
            snd.isFootstep = true;
            snd.spatialChannel = 0;
            activeSounds.push_back(std::move(snd));
        }

        // 2. Character Clothing / Body Foley Sound (torso level)
        if (foleyBuf && !foleyBuf->samples.empty()) {
            ActiveSound foleySnd;
            foleySnd.buffer = foleyBuf;
            foleySnd.position = Vector3(pos.x, pos.y, pos.z - 0.3f);
            float foleyVol = 0.60f;
            if (clothes == ClothingType::ARMOR) foleyVol = 1.05f;
            else if (clothes == ClothingType::NAKED) foleyVol = 0.35f;

            foleySnd.volume = isLocal ? foleyVol : (foleyVol * 0.7f);
            foleySnd.isLocalHead = false;
            foleySnd.isFootstep = true;
            foleySnd.spatialChannel = 1;
            activeSounds.push_back(std::move(foleySnd));
        }

        // 3. Acoustic Aura (Local Player Only)
        if (isLocal && currentAura != AuraType::NONE) {
            std::string auraKey = "auras/vinyl.wav";
            if (currentAura == AuraType::CHIME) auraKey = "auras/chime.wav";
            else if (currentAura == AuraType::HUM) auraKey = "auras/hum.wav";
            else if (currentAura == AuraType::SILK) auraKey = "auras/silk.wav";

            const AudioBuffer* auraBuf = soundBank.get(auraKey);
            if (auraBuf && !auraBuf->samples.empty()) {
                ActiveSound auraSnd;
                auraSnd.buffer = auraBuf;
                auraSnd.position = Vector3(pos.x, pos.y, pos.z);
                auraSnd.volume = 0.75f;
                auraSnd.isLocalHead = false;
                auraSnd.isFootstep = true;
                auraSnd.spatialChannel = 2;
                activeSounds.push_back(std::move(auraSnd));
            }
        }

        // 4. Early reflections off nearby walls and objects (Local Player Only)
        if (isLocal && footBuf && !footBuf->samples.empty()) {
            float footAngles[8] = {0.0f, 45.0f, 90.0f, 135.0f, 180.0f, 225.0f, 270.0f, 315.0f};
            for (int i = 0; i < 8; ++i) {
                float rad = (listenerYaw + footAngles[i]) * (float)M_PI / 180.0f;
                Vector3 dir(std::sin(rad), std::cos(rad), 0.0f);
                SurfaceType hitMat = SurfaceType::WOOD;
                float refl = 0.60f;
                std::string hitName;
                float dist = currentMap.cast_ray(pos, dir, 15.0f, hitMat, refl, hitName);

                float delaySec = (2.0f * dist) / 343.0f;
                int delaySamp = int(delaySec * sampleRate);

                AcousticMaterialProps props = get_material_acoustics(hitMat);
                float echoVol = (0.60f / (1.0f + 0.28f * dist * dist + 0.12f * dist)) * props.reflectivity;

                if (echoVol > 0.02f) {
                    ActiveSound fRefl;
                    fRefl.buffer = footBuf;
                    fRefl.position = Vector3(pos.x + dir.x * dist, pos.y + dir.y * dist, pos.z);
                    fRefl.volume = echoVol;
                    fRefl.delaySamples = delaySamp;
                    fRefl.isLocalHead = false;
                    fRefl.isFootstep = true;
                    fRefl.spatialChannel = (i % 6) + 3; // Channels 3-8
                    activeSounds.push_back(std::move(fRefl));
                }
            }
        }
    }

    // Direct NPC Sound (e.g. wander footstep or voice, playing the sound specified at creation)
    void triggerNPCSound(const Vector3& pos, const std::string& soundAsset, float volume = 0.75f) {
        if (soundAsset.empty()) return;

        // Standard hearing range check: skip sounds further than 25 tiles
        float dx = pos.x - listenerPos.x;
        float dy = pos.y - listenerPos.y;
        float distSq = dx * dx + dy * dy;
        if (distSq > 25.0f * 25.0f) return;

        const AudioBuffer* buf = soundBank.get(soundAsset);
        if (!buf || buf->samples.empty()) {
            buf = soundBank.getFootstep(currentMap.get_surface_at(pos));
        }
        if (!buf || buf->samples.empty()) return;

        // Ensure non-zero offset if position coincides with listener so Steam Audio HRTF calculates a valid direction vector
        Vector3 soundPos = pos;
        if (std::abs(dx) < 0.2f && std::abs(dy) < 0.2f) {
            float yawRad = listenerYaw * (float)M_PI / 180.0f;
            soundPos.x += std::sin(yawRad) * 0.8f;
            soundPos.y += std::cos(yawRad) * 0.8f;
        }

        ActiveSound snd;
        snd.buffer = buf;
        snd.position = soundPos;
        snd.volume = (std::max)(0.15f, (std::min)(1.0f, volume));
        snd.isLocalHead = false;
        snd.isFootstep = true;

        static int npcChannelIndex = 8;
        snd.spatialChannel = (npcChannelIndex++ % 8) + 8; // Dedicated NPC channels 8-15

        std::lock_guard<std::mutex> lock(audioMutex);
        if (activeSounds.size() < 24) {
            activeSounds.push_back(std::move(snd));
        }
    }

    void triggerJump(const Vector3& pos) {
        const AudioBuffer* buf = soundBank.get("movement/jump.wav");
        if (!buf) buf = soundBank.get("environment/door_transition.wav");

        ActiveSound snd;
        snd.buffer = buf;
        snd.position = pos;
        snd.volume = 0.90f;
        snd.isLocalHead = false;
        snd.spatialChannel = 0;

        std::lock_guard<std::mutex> lock(audioMutex);
        activeSounds.push_back(std::move(snd));
    }

    void triggerLand(const Vector3& pos) {
        const AudioBuffer* buf = soundBank.get("movement/land.wav");
        if (!buf) buf = soundBank.get("environment/wall_bump.wav");

        ActiveSound snd;
        snd.buffer = buf;
        snd.position = pos;
        snd.volume = 1.0f;
        snd.isLocalHead = false;
        snd.spatialChannel = 0;

        std::lock_guard<std::mutex> lock(audioMutex);
        activeSounds.push_back(std::move(snd));
    }

    void triggerWallBump(const Vector3& pos) {
        const AudioBuffer* buf = soundBank.get("environment/wall_bump.wav");
        ActiveSound snd;
        snd.buffer = buf;
        snd.position = pos;
        snd.volume = 0.85f;
        snd.isLocalHead = false;
        snd.spatialChannel = 5;

        std::lock_guard<std::mutex> lock(audioMutex);
        activeSounds.push_back(std::move(snd));
    }

    void triggerAttackImpact(const Vector3& pos, SurfaceType mat, bool destroyed) {
        std::lock_guard<std::mutex> lock(audioMutex);

        const AudioBuffer* punchBuf = soundBank.get("environment/punch_impact.wav");
        if (punchBuf) {
            ActiveSound punch;
            punch.buffer = punchBuf;
            punch.position = pos;
            punch.volume = 1.0f;
            punch.isLocalHead = false;
            punch.spatialChannel = 6;
            activeSounds.push_back(std::move(punch));
        }

        if (destroyed) {
            const AudioBuffer* breakBuf = nullptr;
            switch (mat) {
                case SurfaceType::GLASS:
                    breakBuf = soundBank.get("environment/destroy_glass.wav");
                    break;
                case SurfaceType::METAL:
                    breakBuf = soundBank.get("environment/destroy_metal.wav");
                    break;
                case SurfaceType::STONE:
                    breakBuf = soundBank.get("environment/destroy_stone.wav");
                    break;
                case SurfaceType::WOOD:
                case SurfaceType::CARPET:
                case SurfaceType::LEATHER_FABRIC:
                default:
                    breakBuf = soundBank.get("environment/destroy_wood.wav");
                    break;
            }

            if (breakBuf) {
                ActiveSound shatter;
                shatter.buffer = breakBuf;
                shatter.position = pos;
                shatter.volume = 1.3f;
                shatter.isLocalHead = false;
                shatter.spatialChannel = 7;
                activeSounds.push_back(std::move(shatter));
            }
        }
    }

    // High-Density 16-Ray Omnidirectional Raytracing for Smooth Acoustic Perception (Diagonals included)
    void triggerEcholocation(const Vector3& origin, float yawDeg) {
        const AudioBuffer* clickBuf = soundBank.get("echolocation/click.wav");
        
        ActiveSound snap;
        snap.buffer = clickBuf;
        if (!clickBuf || clickBuf->samples.empty()) {
            snap.syntheticBuffer = generateSyntheticClick();
        }
        snap.position = origin;
        snap.volume = 1.0f;
        snap.isLocalHead = true;
        snap.spatialChannel = 0;

        std::lock_guard<std::mutex> lock(audioMutex);
        activeSounds.push_back(std::move(snap));

        // 16 omnidirectional ray angles (every 22.5 degrees) covering full circle
        const int NUM_RAYS = 16;
        float rayAngles[NUM_RAYS] = {
            0.0f, 22.5f, 45.0f, 67.5f, 90.0f, 112.5f, 135.0f, 157.5f,
            180.0f, 202.5f, 225.0f, 247.5f, 270.0f, 292.5f, 315.0f, 337.5f
        };

        for (int i = 0; i < NUM_RAYS; ++i) {
            float rel = rayAngles[i];
            float rad = (yawDeg + rel) * (float)M_PI / 180.0f;
            Vector3 dir(std::sin(rad), std::cos(rad), 0.0f);

            SurfaceType hitMaterial = SurfaceType::WOOD;
            float reflectivity = 0.60f;
            std::string hitName;
            float dist = currentMap.cast_ray(origin, dir, 25.0f, hitMaterial, reflectivity, hitName);

            if (dist < 0.20f) dist = 0.20f;
            if (dist >= 25.0f) continue; // Out of hearing distance

            // Physical delay: t = 2d / 343 m/s
            float delaySec = (2.0f * dist) / 343.0f;
            int delaySamp = int(delaySec * sampleRate);

            Vector3 hitPos(origin.x + dir.x * dist, origin.y + dir.y * dist, origin.z);
            AcousticMaterialProps matProps = get_material_acoustics(hitMaterial);

            // Smooth angular weighting to prevent sudden popping into the field of hearing
            float vol = (1.0f / (1.0f + 0.16f * dist * dist + 0.08f * dist)) * matProps.reflectivity;
            vol = (std::max)(0.04f, (std::min)(0.90f, vol));

            ActiveSound ret;
            ret.syntheticBuffer = generateMaterialImpulse(matProps, dist);
            ret.position = hitPos;
            ret.volume = vol;
            ret.delaySamples = delaySamp;
            ret.isLocalHead = false;
            ret.spatialChannel = (i % 16) + 1; // Steam Audio Channels 1-16

            activeSounds.push_back(std::move(ret));
        }
    }

private:
    int sampleRate;
    int frameSize;
    bool running = false;
    ma_device device;

    SteamAudioSystem spatializer;
    IRTailConvolver irTail;
    SoundBank soundBank;

    std::mutex audioMutex;
    Vector3 listenerPos;
    float listenerYaw = 0.0f;
    WorldMap currentMap;
    AuraType currentAura = AuraType::NONE;
    std::vector<ActiveSound> activeSounds;

    std::string currentAmbienceFile = "";
    ActiveSound ambienceSound;
    bool hasAmbience = false;

    std::vector<float> monoTemp;
    std::vector<float> reverbSend;
    std::vector<float> leftOut;
    std::vector<float> rightOut;

    static void audioCallback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount) {
        auto* self = static_cast<AudioEngineCpp*>(pDevice->pUserData);
        self->renderAudio(static_cast<float*>(pOutput), frameCount);
    }

    void renderAudio(float* outputStereo, int frames) {
        monoTemp.assign(frames, 0.0f);
        reverbSend.assign(frames, 0.0f);
        leftOut.assign(frames, 0.0f);
        rightOut.assign(frames, 0.0f);

        std::unique_lock<std::mutex> lock(audioMutex);
        Vector3 curPos = listenerPos;
        float curYaw = listenerYaw;

        if (hasAmbience) {
            int ambCopied = ambienceSound.getNextFrames(frames, monoTemp);
            if (ambCopied > 0) {
                for (int i = 0; i < frames; ++i) {
                    leftOut[i] += monoTemp[i] * 0.70f;
                    rightOut[i] += monoTemp[i] * 0.70f;
                    reverbSend[i] += monoTemp[i] * 0.20f;
                }
            }
        }

        for (auto it = activeSounds.begin(); it != activeSounds.end(); ) {
            int copied = it->getNextFrames(frames, monoTemp);
            if (copied > 0) {
                if (it->isLocalHead) {
                    for (int i = 0; i < frames; ++i) {
                        leftOut[i] += monoTemp[i] * 0.9f;
                        rightOut[i] += monoTemp[i] * 0.9f;
                        reverbSend[i] += monoTemp[i] * 0.30f;
                    }
                } else {
                    // Material-based Acoustic Occlusion Calculation
                    float trans = 1.0f;
                    float muffle = 0.0f;
                    currentMap.calculate_occlusion(curPos, it->position, trans, muffle);

                    spatializer.spatializeChannel(
                        it->spatialChannel,
                        monoTemp.data(),
                        frames,
                        curPos,
                        curYaw,
                        it->position,
                        leftOut.data(),
                        rightOut.data(),
                        1.0f,
                        trans,
                        muffle
                    );

                    for (int i = 0; i < frames; ++i) {
                        reverbSend[i] += monoTemp[i] * 0.25f * trans;
                    }
                }
            }

            if (it->finished) {
                it = activeSounds.erase(it);
            } else {
                ++it;
            }
        }
        lock.unlock();

        irTail.process(reverbSend.data(), frames, 0.16f, leftOut.data(), rightOut.data());

        for (int i = 0; i < frames; ++i) {
            float l = (std::max)(-0.98f, (std::min)(0.98f, leftOut[i]));
            float r = (std::max)(-0.98f, (std::min)(0.98f, rightOut[i]));
            outputStereo[i * 2 + 0] = l;
            outputStereo[i * 2 + 1] = r;
        }
    }

    std::vector<float> generateSyntheticClick() {
        int length = int(sampleRate * 0.015f);
        std::vector<float> click(length);
        for (int i = 0; i < length; ++i) {
            float t = (float)i / (float)sampleRate;
            float env = std::exp(-t * 300.0f);
            float wave = std::sin(2.0f * (float)M_PI * 2600.0f * t) * env;
            click[i] = wave * 0.90f;
        }
        return click;
    }

    std::vector<float> generateMaterialImpulse(const AcousticMaterialProps& props, float dist) {
        float durSec = 0.030f + (dist * 0.0015f);
        int length = int(sampleRate * durSec);
        std::vector<float> pulse(length);

        float freq = props.resonancePitch / (1.0f + dist * 0.04f);
        float decayRate = 140.0f + props.highFreqDamping * 200.0f;

        for (int i = 0; i < length; ++i) {
            float t = (float)i / (float)sampleRate;
            float env = std::exp(-t * decayRate);
            float wave = std::sin(2.0f * (float)M_PI * freq * t);
            
            if (props.highFreqDamping < 0.20f) {
                wave += 0.35f * std::sin(2.0f * (float)M_PI * (freq * 2.1f) * t) * std::exp(-t * decayRate * 1.6f);
            }

            pulse[i] = wave * env * 0.65f;
        }
        return pulse;
    }
};

} // namespace fakevr
