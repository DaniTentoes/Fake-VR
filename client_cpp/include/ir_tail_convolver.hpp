#pragma once

#include "miniaudio.h"
#include <vector>
#include <string>
#include <algorithm>
#include <cmath>
#include <iostream>

namespace fakevr {

class IRTailConvolver {
public:
    IRTailConvolver() = default;

    bool loadIR(const std::string& wavPath, int targetSampleRate = 48000) {
        ma_decoder_config config = ma_decoder_config_init(ma_format_f32, 2, targetSampleRate);
        ma_decoder decoder;
        ma_result result = ma_decoder_init_file(wavPath.c_str(), &config, &decoder);
        if (result != MA_SUCCESS) {
            std::cerr << "[IRTailConvolver] Failed to load IR file: " << wavPath << "\n";
            return false;
        }

        ma_uint64 totalFrames = 0;
        ma_decoder_get_length_in_pcm_frames(&decoder, &totalFrames);
        if (totalFrames == 0) totalFrames = targetSampleRate;

        // Load 0.40s of genuine residential room impulse response
        int maxFrames = int(targetSampleRate * 0.40f);
        int framesToLoad = (std::min)(int(totalFrames), maxFrames);

        std::vector<float> interleaved(framesToLoad * 2);
        ma_uint64 framesRead = 0;
        ma_decoder_read_pcm_frames(&decoder, interleaved.data(), framesToLoad, &framesRead);
        ma_decoder_uninit(&decoder);

        // Subsample IR by factor of 8 for gentle diffuse residential decay
        step = 8;
        irLen = int(framesRead / step);
        irLen = (irLen / 4) * 4;
        irL.resize(irLen);
        irR.resize(irLen);

        float maxPeak = 1e-6f;
        for (int i = 0; i < irLen; ++i) {
            float l = interleaved[(i * step) * 2 + 0];
            float r = interleaved[(i * step) * 2 + 1];
            irL[i] = l;
            irR[i] = r;
            maxPeak = (std::max)(maxPeak, (std::max)(std::abs(l), std::abs(r)));
        }

        // Normalize IR to realistic subtle living room levels
        float norm = 0.22f / maxPeak;
        for (int i = 0; i < irLen; ++i) {
            // Apply gentle natural room high-frequency damping over time
            float progress = (float)i / (float)irLen;
            float damping = 1.0f - 0.35f * progress;
            irL[i] *= norm * damping;
            irR[i] *= norm * damping;
        }

        historySize = irLen * step + 4096;
        history.assign(historySize, 0.0f);
        writeIndex = 0;

        std::cout << "[IRTailConvolver] Loaded residential lounge IR: " << wavPath 
                  << " (" << irLen << " stereo taps across " 
                  << (float)framesRead / targetSampleRate << "s)\n";
        hasIR = true;
        return true;
    }

    void process(const float* monoIn, int numFrames, float wetGain, float* outL, float* outR) {
        if (!hasIR || !monoIn || numFrames <= 0 || wetGain < 0.001f) {
            return;
        }

        for (int f = 0; f < numFrames; ++f) {
            history[writeIndex] = monoIn[f];

            float accL = 0.0f;
            float accR = 0.0f;

            int base = writeIndex;
            for (int k = 0; k < irLen; k += 4) {
                int r0 = base - (k + 0) * step; if (r0 < 0) r0 += historySize;
                int r1 = base - (k + 1) * step; if (r1 < 0) r1 += historySize;
                int r2 = base - (k + 2) * step; if (r2 < 0) r2 += historySize;
                int r3 = base - (k + 3) * step; if (r3 < 0) r3 += historySize;

                accL += history[r0] * irL[k + 0] + history[r1] * irL[k + 1] + history[r2] * irL[k + 2] + history[r3] * irL[k + 3];
                accR += history[r0] * irR[k + 0] + history[r1] * irR[k + 1] + history[r2] * irR[k + 2] + history[r3] * irR[k + 3];
            }

            outL[f] += accL * wetGain;
            outR[f] += accR * wetGain;

            writeIndex++;
            if (writeIndex >= historySize) writeIndex = 0;
        }
    }

    bool isLoaded() const { return hasIR; }

private:
    bool hasIR = false;
    int step = 8;
    int irLen = 0;
    std::vector<float> irL;
    std::vector<float> irR;

    int historySize = 0;
    int writeIndex = 0;
    std::vector<float> history;
};

} // namespace fakevr
