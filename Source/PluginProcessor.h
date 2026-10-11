// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki
#pragma once
#include <array>
#include <atomic>
#include <juce_audio_processors/juce_audio_processors.h>
#include "Dsp/Chain.h"
#include "Meters/MeterBank.h"
#include "Parameters.h"

class BroadcastMasterProcessor : public juce::AudioProcessor,
                                 private juce::AsyncUpdater,
                                 private juce::AudioProcessorValueTreeState::Listener
{
public:
    BroadcastMasterProcessor();
    ~BroadcastMasterProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // Metering, read by the editor.
    bm::MeterBank metersIn, metersOut;
    void resetMeters() noexcept { metersIn.requestReset(); metersOut.requestReset(); }
    double getCurrentSampleRate() const noexcept { return currentSampleRate.load(); }

    // Level at the output of every stage (dBFS), for the gain-staging mini meters.
    std::array<std::atomic<float>, bm::params::kNumStages> stagePeakDb, stageRmsDb;

    // Gain reduction (positive dB) of the multiband compressor and limiter.
    std::atomic<float> gainReductionComp { 0.0f }, gainReductionLimiter { 0.0f };

    // Live readouts for the module panels.
    float mbcBandGr (int band) const noexcept { return chain.mbc.grDb (band); }
    float mbcBandLevel (int band) const noexcept { return chain.mbc.levelDb (band); }
    float levelerGainDb() const noexcept     { return chain.lev.gainDb(); }
    float levelerSpeech() const noexcept     { return chain.lev.speechAmount(); }
    float bypassMatchOffsetDb() const noexcept { return chain.matchOffsetDb(); }

    // True when the host has the second (solo) output connected.
    bool isSoloBusConnected() const noexcept { return auxConnected.load(); }
    int  reportedLatency() const noexcept { return latencyShared.load(); }

    // Reads the EQ from the parameters (also used by the kernel design thread).
    bm::dsp::EqParams readEq() const;

    juce::AudioProcessorValueTreeState apvts;

private:
    class EqDesigner;

    void readParams (bm::dsp::ChainParams& cp) const;
    void handleAsyncUpdate() override;
    void parameterChanged (const juce::String& id, float value) override;
    void applyTargetLink();
    void publishKernels (bm::dsp::KernelPair&& k);
    float raw (const std::string& id) const;

    bm::dsp::Chain chain;
    bm::dsp::ChainParams cp;

    struct Ptrs
    {
        std::array<std::atomic<float>*, bm::params::kNumStages> gain {}, enable {};
        std::atomic<float> *latency {}, *bypass {}, *match {}, *soloOn {}, *monA {}, *monB {}, *monAB {}, *monLevel {}, *toMain {};
        std::atomic<float> *hpFreq {}, *phFreq {}, *phStages {}, *eqTilt {};
        std::array<std::array<std::atomic<float>*, 5>, bm::dsp::EqParams::kBands> eq {}; // on, freq, gain, q, target
        std::atomic<float> *satType {}, *satAmt {}, *satMix {}, *excFreq {}, *excAmt {}, *bassFreq {}, *bassAmt {};
        std::array<std::atomic<float>*, 3> mbX {};
        std::array<std::array<std::atomic<float>*, 6>, bm::dsp::MbcParams::kBands> mb {}; // on, thr, ratio, atk, rel, makeup
        std::atomic<float> *mbKnee {}, *mbMix {};
        std::atomic<float> *lvTarget {}, *lvRange {}, *lvSpeed {}, *lvGate {}, *lvMode {};
        std::atomic<float> *stWidth {}, *stMid {}, *stSide {}, *stFlipL {}, *stFlipR {}, *stMono {}, *stDelay {}, *stExc {}, *stExcF {};
        std::atomic<float> *clThr {}, *clKnee {}, *lmRel {};
    } ptr;

    std::atomic<double> currentSampleRate { 48000.0 };
    std::atomic<bool> auxConnected { false };
    std::atomic<int> latencyShared { 0 };
    int lastLatency = -1;
    std::atomic<bool> targetLinkDirty { false };

    // EQ kernels are designed on a background thread and handed over here.
    std::unique_ptr<EqDesigner> designer;
    juce::SpinLock kernelLock;
    bm::dsp::KernelPair sharedKernels;
    std::atomic<bool> kernelsReady { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BroadcastMasterProcessor)
};
