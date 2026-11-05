#pragma once

#include <vector>
#include <cmath>
#include <cstring>
#include <algorithm>

namespace fakevr {

struct ReflectionTap {
    float delaySec;
    float gainL;
    float gainR;
};

class NativeRoomAcoustics {
public:
    NativeRoomAcoustics(int sampleRate = 48000, float maxSeconds = 3.0f)
        : sampleRate(sampleRate), bufferSize(int(sampleRate * maxSeconds)) {
        ringL.assign(bufferSize, 0.0f);
        ringR.assign(bufferSize, 0.0f);

        taps = {
            {0.0143f, 0.42f, 0.26f},
            {0.0211f, 0.28f, 0.44f},
            {0.0337f, 0.35f, 0.28f},
            {0.0451f, 0.22f, 0.32f},
            {0.0593f, 0.20f, 0.16f},
            {0.0761f, 0.16f, 0.20f},
            {0.0983f, 0.12f, 0.14f},
            {0.1247f, 0.09f, 0.11f},
            {0.1583f, 0.07f, 0.08f},
            {0.1991f, 0.04f, 0.05f}
        };
    }

    void process(const float* monoIn, int numFrames, float rt60, float boost, float* outL, float* outR) {
        if (!monoIn || numFrames <= 0) return;

        // Write input into circular buffer
        for (int i = 0; i < numFrames; ++i) {
            int idx = (writePos + i) % bufferSize;
            ringL[idx] = monoIn[i];
            ringR[idx] = monoIn[i];
        }

        // Multi-tap early reflections
        for (const auto& tap : taps) {
            int delaySamples = int(tap.delaySec * sampleRate);
            float att = std::exp(-3.0f * tap.delaySec / (std::max)(0.1f, rt60)) * boost;
            float gL = tap.gainL * att * 0.35f;
            float gR = tap.gainR * att * 0.35f;

            for (int i = 0; i < numFrames; ++i) {
                int readIdx = (writePos + i - delaySamples) % bufferSize;
                if (readIdx < 0) readIdx += bufferSize;
                outL[i] += ringL[readIdx] * gL;
                outR[i] += ringR[readIdx] * gR;
            }
        }

        // Cross-fed circulating feedback
        int fbDelay = int(0.0883f * sampleRate);
        float fbGain = std::pow(10.0f, -3.0f * 0.0883f / (std::max)(0.1f, rt60));
        fbGain = (std::max)(0.05f, (std::min)(0.60f, fbGain));

        for (int i = 0; i < numFrames; ++i) {
            int readIdx = (writePos + i - fbDelay) % bufferSize;
            if (readIdx < 0) readIdx += bufferSize;
            int writeIdx = (writePos + i) % bufferSize;
            ringL[writeIdx] += ringR[readIdx] * fbGain;
            ringR[writeIdx] += ringL[readIdx] * fbGain;
        }

        writePos = (writePos + numFrames) % bufferSize;
    }

private:
    int sampleRate;
    int bufferSize;
    int writePos = 0;
    std::vector<float> ringL;
    std::vector<float> ringR;
    std::vector<ReflectionTap> taps;
};

} // namespace fakevr
