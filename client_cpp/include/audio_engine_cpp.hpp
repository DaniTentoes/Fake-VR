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
    int spatialChannel = 0;
    int delaySamples = 0;
    int cursor = 0;
    bool finished = false;

    int getNextFrames(int frames, std::vector<float>& outMono) {
        outMono.assign(frames, 0.0f);
        const float* src = buffer ? buffer->samples.data() : syntheticBuffer.data();
        size_t total = buffer ? buffer->samples.size() : syntheticBuffer.size();

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
            if (cursor >= (int)total) finished = true;
            return toCopy;
        }

        int available = int(total - cursor);
        int toCopy = (std::min)(frames, available);
        for (int i = 0; i < toCopy; ++i) {
            outMono[i] = src[cursor + i] * volume;
        }
        cursor += toCopy;
        if (cursor >= (int)total) finished = true;
        return toCopy;
    }
};

class AudioEngineCpp {
public:
    AudioEngineCpp(int sampleRate = 48000, int frameSize = 1024)
        : sampleRate(sampleRate), frameSize(frameSize), spatializer(sampleRate, frameSize), soundBank("assets/sounds") {
        
        // Load residential lounge impulse response (0.4s Little Room from Apple 03 Small Spaces)
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
        std::cout << "[AudioEngineCpp] Real-time physical audio running at " 
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

    void setMap(const WorldMap& m) {
        std::lock_guard<std::mutex> lock(audioMutex);
        currentMap = m;
    }

    // Physical Movement Audio:
    // Triggers both the surface footstep and the character's clothing/body foley
    // with independent randomized sound variations.
    void triggerMovement(const Vector3& pos, SurfaceType surface, ClothingType clothes, bool isLocal) {
        const AudioBuffer* footBuf = soundBank.getFootstep(surface);
        const AudioBuffer* foleyBuf = soundBank.getFoley(clothes);

        std::lock_guard<std::mutex> lock(audioMutex);

        // Clear any lingering footsteps from previous positions
        for (auto& s : activeSounds) {
            if (s.isFootstep) {
                s.finished = true;
            }
        }

        // 1. Direct footstep sound
        if (footBuf && !footBuf->samples.empty()) {
            ActiveSound snd;
            snd.buffer = footBuf;
            snd.position = Vector3(pos.x, pos.y - 1.2f, pos.z + 0.05f);
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
            foleySnd.position = Vector3(pos.x, pos.y - 0.4f, pos.z);
            float foleyVol = 0.60f;
            if (clothes == ClothingType::ARMOR) foleyVol = 1.05f; // Armor is loud and metallic
            else if (clothes == ClothingType::NAKED) foleyVol = 0.35f; // Soft skin friction

            foleySnd.volume = isLocal ? foleyVol : (foleyVol * 0.7f);
            foleySnd.isLocalHead = false;
            foleySnd.isFootstep = true;
            foleySnd.spatialChannel = 8;
            activeSounds.push_back(std::move(foleySnd));
        }

        // 3. Early reflections off nearby walls and furniture objects
        if (footBuf && !footBuf->samples.empty()) {
            float footAngles[4] = {0.0f, 90.0f, 180.0f, 270.0f};
            for (int i = 0; i < 4; ++i) {
                float rad = (listenerYaw + footAngles[i]) * (float)M_PI / 180.0f;
                Vector3 dir(std::sin(rad), 0.0f, std::cos(rad));
                SurfaceType hitMat = SurfaceType::WOOD;
                float refl = 0.60f;
                std::string hitName;
                float dist = currentMap.cast_ray(pos, dir, 10.0f, hitMat, refl, hitName);

                float delaySec = (2.0f * dist) / 343.0f;
                int delaySamp = int(delaySec * sampleRate);

                AcousticMaterialProps props = get_material_acoustics(hitMat);
                float echoVol = (0.65f / (1.0f + 0.28f * dist * dist + 0.15f * dist)) * props.reflectivity;

                if (echoVol > 0.02f) {
                    ActiveSound fRefl;
                    fRefl.buffer = footBuf;
                    fRefl.position = Vector3(pos.x + dir.x * dist, pos.y - 0.3f, pos.z + dir.z * dist);
                    fRefl.volume = echoVol;
                    fRefl.delaySamples = delaySamp;
                    fRefl.isLocalHead = false;
                    fRefl.isFootstep = true;
                    fRefl.spatialChannel = i + 1;
                    activeSounds.push_back(std::move(fRefl));
                }
            }
        }
    }

    // Keep backwards compatibility for any external caller
    void triggerFootstep(const Vector3& pos, SurfaceType surface, bool isLocal) {
        triggerMovement(pos, surface, ClothingType::REGULAR, isLocal);
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

    // Triggers attack impact sound (punch) and material destruction shatter/crash
    void triggerAttackImpact(const Vector3& pos, SurfaceType mat, bool destroyed) {
        std::lock_guard<std::mutex> lock(audioMutex);

        // 1. Physical hit punch impact
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

        // 2. Material destruction sound if object was broken
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
                shatter.volume = 1.3f; // Dramatic destruction crash
                shatter.isLocalHead = false;
                shatter.spatialChannel = 7;
                activeSounds.push_back(std::move(shatter));
            }
        }
    }

    // High-Resolution 8-Ray Omnidirectional Raytracing for Spacebar Echolocation
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

        float rayAngles[8] = {0.0f, 45.0f, 90.0f, 135.0f, 180.0f, 225.0f, 270.0f, 315.0f};

        for (int i = 0; i < 8; ++i) {
            float rel = rayAngles[i];
            float rad = (yawDeg + rel) * (float)M_PI / 180.0f;
            Vector3 dir(std::sin(rad), 0.0f, std::cos(rad));

            SurfaceType hitMaterial = SurfaceType::WOOD;
            float reflectivity = 0.60f;
            std::string hitName;
            float dist = currentMap.cast_ray(origin, dir, 20.0f, hitMaterial, reflectivity, hitName);

            if (dist < 0.20f) dist = 0.20f;

            // Physical delay: t = 2d / 343 m/s
            float delaySec = (2.0f * dist) / 343.0f;
            int delaySamp = int(delaySec * sampleRate);

            Vector3 hitPos(origin.x + dir.x * dist, origin.y, origin.z + dir.z * dist);

            AcousticMaterialProps matProps = get_material_acoustics(hitMaterial);

            // Realistic amplitude falloff with extended proximity curve
            float vol = (1.10f / (1.0f + 0.18f * dist * dist + 0.10f * dist)) * matProps.reflectivity;
            vol = (std::max)(0.06f, (std::min)(0.95f, vol));

            ActiveSound ret;
            ret.syntheticBuffer = generateMaterialImpulse(matProps, dist);
            ret.position = hitPos;
            ret.volume = vol;
            ret.delaySamples = delaySamp;
            ret.isLocalHead = false;
            ret.spatialChannel = (i + 1); // Channels 1-8

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
    std::vector<ActiveSound> activeSounds;

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

        // Process active sounds & spatial reflections through independent Steam Audio HRTF channels
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
                    spatializer.spatializeChannel(
                        it->spatialChannel,
                        monoTemp.data(),
                        frames,
                        curPos,
                        curYaw,
                        it->position,
                        leftOut.data(),
                        rightOut.data(),
                        1.0f
                    );

                    // Reverb send level for diffuse background tail
                    for (int i = 0; i < frames; ++i) {
                        reverbSend[i] += monoTemp[i] * 0.25f;
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

        // Feed gentle diffuse tail into realistic Apple residential IR (wet gain = 0.16)
        irTail.process(reverbSend.data(), frames, 0.16f, leftOut.data(), rightOut.data());

        // Interleave & soft clip into output buffer
        for (int i = 0; i < frames; ++i) {
            float l = (std::max)(-0.98f, (std::min)(0.98f, leftOut[i]));
            float r = (std::max)(-0.98f, (std::min)(0.98f, rightOut[i]));
            outputStereo[i * 2 + 0] = l;
            outputStereo[i * 2 + 1] = r;
        }
    }

    std::vector<float> generateSyntheticClick() {
        int length = int(sampleRate * 0.015f); // 15ms crisp direct click
        std::vector<float> click(length);
        for (int i = 0; i < length; ++i) {
            float t = (float)i / (float)sampleRate;
            float env = std::exp(-t * 300.0f);
            float wave = std::sin(2.0f * (float)M_PI * 2600.0f * t) * env;
            click[i] = wave * 0.90f;
        }
        return click;
    }

    // Physically models the acoustic reflection waveform based on material properties
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
            
            // Higher harmonic for hard, reflective materials (glass, metal)
            if (props.highFreqDamping < 0.20f) {
                wave += 0.35f * std::sin(2.0f * (float)M_PI * (freq * 2.1f) * t) * std::exp(-t * decayRate * 1.6f);
            }

            pulse[i] = wave * env * 0.65f;
        }
        return pulse;
    }
};

} // namespace fakevr
