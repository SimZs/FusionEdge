#include "spectrumAnalyzer.h"

// Globális példány – a linker itt allokálja, nem a singleton instance()-ban
SpectrumAnalyzer spectrumAnalyzer;

// audio_process_raw_samples weak callback override
// Ez hívódik az Audio.cpp-ből minden audio frame feldolgozásakor
void audio_process_raw_samples(int32_t* outBuff, int16_t validSamples) {
    spectrumAnalyzer.pushSamples(outBuff, validSamples);
}
