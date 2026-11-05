#pragma once

#include "phonon.h"
#include "phonon_version.h"
#include "models.hpp"

#include <vector>
#include <iostream>
#include <cmath>
#include <cstring>
#include <algorithm>

namespace fakevr {

class SteamAudioSource {
public:
    SteamAudioSource() = default;

    bool init(IPLContext context, IPLAudioSettings* audioSettings, IPLHRTF hrtf, int frameSize) {
        this->context = context;
        this->frameSize = frameSize;
        this->hrtf = hrtf;

        IPLBinauralEffectSettings effectSettings{};
        effectSettings.hrtf = hrtf;
        IPLerror err = iplBinauralEffectCreate(context, audioSettings, &effectSettings, &binauralEffect);
        if (err != IPL_STATUS_SUCCESS) {
            std::cerr << "[SteamAudioSource] Failed to create binaural effect: " << err << "\n";
            return false;
        }

        iplAudioBufferAllocate(context, 1, frameSize, &inBuf);
        iplAudioBufferAllocate(context, 2, frameSize, &outBuf);
        filterState = 0.0f;
        initialized = true;
        return true;
    }

    ~SteamAudioSource() {
        release();
    }

    void release() {
        if (!initialized) return;
        iplAudioBufferFree(context, &inBuf);
        iplAudioBufferFree(context, &outBuf);
        iplBinauralEffectRelease(&binauralEffect);
        initialized = false;
    }

    void spatialize(
        const float* monoIn,
        int numFrames,
        const Vector3& listenerPos,
        float listenerYawDeg,
        const Vector3& sourcePos,
        float* leftOut,
        float* rightOut,
        float volume = 1.0f,
        float occlusionTransmission = 1.0f,
        float occlusionMuffle = 0.0f
    ) {
        // Standard hearing range limit: 25 tiles (25.0 meters)
        const float MAX_HEARING_DISTANCE = 25.0f;

        float dx = sourcePos.x - listenerPos.x;
        float dy = sourcePos.y - listenerPos.y; // North/South horizontal
        float dz = sourcePos.z - listenerPos.z; // Vertical altitude
        float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (dist >= MAX_HEARING_DISTANCE) {
            return; // Completely out of hearing range
        }
        if (dist < 1e-4f) dist = 0.1f;

        // Smooth distance fade approaching 25 tiles to eliminate sudden cutoffs
        float rangeFade = 1.0f;
        if (dist > 18.0f) {
            rangeFade = (MAX_HEARING_DISTANCE - dist) / (MAX_HEARING_DISTANCE - 18.0f);
            rangeFade = (std::max)(0.0f, (std::min)(1.0f, rangeFade));
        }

        float yawRad = listenerYawDeg * (float)M_PI / 180.0f;
        float cosY = std::cos(yawRad);
        float sinY = std::sin(yawRad);

        // Transform into listener's local coordinate frame:
        // +X = Right, +Y = Forward (Ahead), +Z = Up
        float localX = dx * cosY - dy * sinY;
        float localAhead = dx * sinY + dy * cosY;

        // In Steam Audio local frame: +X = Right, +Y = Up, -Z = Ahead
        IPLVector3 dir{};
        dir.x = localX / dist;
        dir.y = dz / dist;
        dir.z = -localAhead / dist;

        // Realistic inverse-distance amplitude falloff
        float att = 1.0f / (1.0f + 0.35f * dist + 0.02f * dist * dist);
        att *= (volume * occlusionTransmission * rangeFade);

        if (att < 0.0005f) return;

        int processFrames = (std::min)(numFrames, frameSize);

        // Apply low-pass frequency damping based on material occlusion
        float lpfAlpha = (std::max)(0.0f, (std::min)(0.88f, occlusionMuffle));
        for (int i = 0; i < processFrames; ++i) {
            filterState = lpfAlpha * filterState + (1.0f - lpfAlpha) * monoIn[i];
            inBuf.data[0][i] = filterState;
        }
        if (processFrames < frameSize) {
            std::memset(inBuf.data[0] + processFrames, 0, (frameSize - processFrames) * sizeof(float));
        }

        if (!initialized) {
            for (int i = 0; i < processFrames; ++i) {
                leftOut[i] += inBuf.data[0][i] * 0.5f * att;
                rightOut[i] += inBuf.data[0][i] * 0.5f * att;
            }
            return;
        }

        IPLBinauralEffectParams params{};
        params.direction = dir;
        params.interpolation = IPL_HRTFINTERPOLATION_BILINEAR;
        params.spatialBlend = 1.0f;
        params.hrtf = hrtf;

        iplBinauralEffectApply(binauralEffect, &params, &inBuf, &outBuf);

        const float* outL = outBuf.data[0];
        const float* outR = outBuf.data[1];

        for (int i = 0; i < processFrames; ++i) {
            leftOut[i] += outL[i] * att;
            rightOut[i] += outR[i] * att;
        }
    }

private:
    IPLContext context = nullptr;
    IPLHRTF hrtf = nullptr;
    IPLBinauralEffect binauralEffect = nullptr;
    IPLAudioBuffer inBuf{};
    IPLAudioBuffer outBuf{};
    float filterState = 0.0f;
    int frameSize = 1024;
    bool initialized = false;
};

class SteamAudioSystem {
public:
    SteamAudioSystem(int sampleRate = 48000, int frameSize = 1024)
        : sampleRate(sampleRate), frameSize(frameSize) {
        
        IPLContextSettings ctxSettings{};
        ctxSettings.version = STEAMAUDIO_VERSION;
        ctxSettings.simdLevel = IPL_SIMDLEVEL_AVX2;
        IPLerror err = iplContextCreate(&ctxSettings, &context);
        if (err != IPL_STATUS_SUCCESS) {
            std::cerr << "[SteamAudioSystem] iplContextCreate failed: " << err << "\n";
            return;
        }

        audioSettings.samplingRate = sampleRate;
        audioSettings.frameSize = frameSize;

        IPLHRTFSettings hrtfSettings{};
        hrtfSettings.type = IPL_HRTFTYPE_DEFAULT;
        hrtfSettings.volume = 1.0f;
        err = iplHRTFCreate(context, &audioSettings, &hrtfSettings, &hrtf);
        if (err != IPL_STATUS_SUCCESS) {
            std::cerr << "[SteamAudioSystem] iplHRTFCreate failed: " << err << "\n";
            return;
        }

        // 24 independent Steam Audio binaural effect units
        pool.resize(24);
        for (auto& src : pool) {
            src.init(context, &audioSettings, hrtf, frameSize);
        }

        initialized = true;
        std::cout << "[SteamAudioSystem] Steam Audio initialized with " << pool.size() << " binaural spatializers.\n";
    }

    ~SteamAudioSystem() {
        if (!initialized) return;
        for (auto& src : pool) {
            src.release();
        }
        iplHRTFRelease(&hrtf);
        iplContextRelease(&context);
        initialized = false;
    }

    bool isInitialized() const { return initialized; }

    void spatializeChannel(
        int channelIndex,
        const float* monoIn,
        int numFrames,
        const Vector3& listenerPos,
        float listenerYawDeg,
        const Vector3& sourcePos,
        float* leftOut,
        float* rightOut,
        float volume = 1.0f,
        float occlusionTransmission = 1.0f,
        float occlusionMuffle = 0.0f
    ) {
        int idx = (std::abs(channelIndex)) % pool.size();
        pool[idx].spatialize(
            monoIn, numFrames, listenerPos, listenerYawDeg, sourcePos,
            leftOut, rightOut, volume, occlusionTransmission, occlusionMuffle
        );
    }

private:
    int sampleRate;
    int frameSize;
    bool initialized = false;

    IPLContext context = nullptr;
    IPLAudioSettings audioSettings{};
    IPLHRTF hrtf = nullptr;
    std::vector<SteamAudioSource> pool;
};

} // namespace fakevr
