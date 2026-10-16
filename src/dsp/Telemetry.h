#pragma once
// Data the audio thread publishes for the UI (lock-free, single writer / single reader).
#include <atomic>
#include <cmath>

namespace chiller::dsp {

constexpr int kUiPoints = 256;           // log-spaced display points, 20 Hz .. 20 kHz
constexpr float kUiMinHz = 20.f, kUiMaxHz = 20000.f;

inline float uiPointHz(int i)
{
    return kUiMinHz * std::pow(kUiMaxHz / kUiMinHz, (float) i / (float) (kUiPoints - 1));
}

struct UiPeak { float hz = 0.f, db = 0.f; };

struct UiFrame {
    float inDb[kUiPoints] {};      // input spectrum (dBFS, 0 dB = full-scale sine)
    float expDb[kUiPoints] {};     // expected curve + threshold (dBFS)
    float keyDb[kUiPoints] {};     // sidechain key spectrum (dBFS), valid when keyActive
    float gr[kUiPoints] {};        // applied gain reduction in dB (positive = cut)
    UiPeak peaks[5] {};            // largest local maxima of gr
    int numPeaks = 0;
    float f0 = 0.f, f0Conf = 0.f;  // detected pitch (Hz) and confidence 0..1
    float harshIn = 0.f, harshOut = 0.f;   // 0..100
    float avgReduction = 0.f;      // mean reduction over 200 Hz .. 16 kHz (dB)
    bool keyActive = false;
    float sampleRate = 48000.f;
    unsigned long long frameCounter = 0;
};

// Triple buffer: the writer fills writeSlot() and calls publish(); the reader gets the newest frame.
template <class T>
class TripleBuffer {
public:
    T& writeSlot() { return buf[back]; }
    void publish() { const int old = middle.exchange(back | 4, std::memory_order_acq_rel); back = old & 3; }
    void read(T& out)
    {
        if (middle.load(std::memory_order_acquire) & 4) { const int old = middle.exchange(front, std::memory_order_acq_rel); front = old & 3; }
        out = buf[front];
    }
private:
    T buf[3] {};
    int back = 0, front = 2;
    std::atomic<int> middle { 1 };
};

} // namespace chiller::dsp
