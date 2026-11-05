#pragma once

#include "miniaudio.h"
#include <vector>
#include <string>
#include <algorithm>
#include <iostream>

namespace fakevr {

class IRConvolver {
public:
    IRConvolver() = default;

    bool loadIR(const std::string& wavPath, int targetSampleRate = 48000) {
        ma_decoder_config config = ma_decoder_config_init(ma_format_f32, 2, targetSampleRate);
        ma_decoder decoder;
        ma_result result = ma_decoder_init_file(wavPath.c_str(), &config, &decoder);
        if (result != MA_SUCCESS) {
            std::cerr << "[IRConvolver] Failed to load IR file: " << wavPath << "\n";
            return false;
        }

        ma_uint64 totalFrames = 0;
        ma_decoder_get_length_in_pcm_frames(&decoder, &totalFrames);
        if (totalFrames == 0) totalFrames = targetSampleRate; // fallback 1 sec

        // Limit IR length to max 0.8s (~38400 samples) to ensure zero latency & smooth CPU budget
        int maxFrames = int(targetSampleRate * 0.8f);
        int framesToLoad = (std::min)(int(totalFrames), maxFrames);

        std::vector<float> interleaved(framesToLoad * 2);
        ma_uint64 framesRead = 0;
        ma_decoder_read_pcm_frames(&decoder, interleaved.data(), framesToLoad, &framesRead);
        ma_decoder_uninit(&decoder);

        irL.resize(framesRead);
        irR.resize(framesRead);

        // Subsample IR if needed or decimate by factor of 2 or 4 for fast real-time direct FIR
        // Decimate factor of 2 gives 24kHz resolution, plenty for diffuse reverb tail!
        const int decimate = 2;
        int downsampledLen = int(framesRead / decimate);
        irL_down.resize(downsampledLen);
        irR_down.resize(downsampledLen);

        float maxPeak = 1e-6f;
        for (int i = 0; i < downsampledLen; ++i) {
            float sl = interleaved[i * decimate * 2 + 0];
            float sr = interleaved[i * decimate * 2 + 1];
            irL_down[i] = sl;
            irR_down[i] = sr;
            maxPeak = (std::max)(maxPeak, (std::max)(std::abs(sl), std::abs(sr)));
        }

        // Normalize IR
        float norm = 0.35f / maxPeak;
        for (int i = 0; i < downsampledLen; ++i) {
            irL_down[i] *= norm;
            irR_down[i] *= norm;
        }

        irLength = downsampledLen;
        irStep = decimate;

        // Allocate ring buffers
        historySize = irLength * irStep + 4096;
        history.assign(historySize, 0.0f);
        writeIndex = 0;

        std::cout << "[IRConvolver] Loaded realistic IR tail: " << wavPath 
                  << " (" << irLength << " taps, step=" << irStep << ")\n";
        hasIR = true;
        return true;
    }

    void process(const float* monoIn, int numFrames, float wetGain, float* outL, float* outR) {
        if (!hasIR || irLength <= 0 || !monoIn || numFrames <= 0 || wetGain < 0.001f) {
            return;
        }

        for (int f = 0; f < numFrames; ++f) {
            history[writeIndex] = monoIn[f];

            // Perform direct FIR convolution over downsampled IR taps
            float accL = 0.0f;
            float accR = 0.0f;

            // Sparse tap accumulation for ultra-fast performance
            // Process every tap in ir_down
            int readBase = writeIndex;
            for (int k = 0; k < irLength; ++k) {
                int tapOffset = k * irStep;
                int rIdx = readBase - tapOffset;
                if (rIdx < 0) rIdx += historySize;
                float samp = history[rIdx];
                accL += samp * irL_down[k];
                accR += samp * irR_down[k];
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
    int irLength = 0;
    int irStep = 2;
    std::vector<float> irL;
    std::vector<float> irR;
    std::vector<float> irL_down;
    std::vector<float> irR_down;

    int historySize = 0;
    int writeIndex = 0;
    std::vector<float> history;
};

} // namespace fakevr
