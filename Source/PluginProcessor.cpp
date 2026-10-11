// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki
#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace bm::params;

// Designs the linear-phase EQ kernels whenever the EQ settings change, away from the audio thread.
class BroadcastMasterProcessor::EqDesigner : public juce::Thread
{
public:
    explicit EqDesigner (BroadcastMasterProcessor& o) : juce::Thread ("BroadcastMaster EQ designer"), owner (o), fft (12) {}

    void run() override
    {
        while (! threadShouldExit())
        {
            wait (12);
            if (threadShouldExit()) break;

            const double fs = owner.currentSampleRate.load();
            const bm::dsp::EqParams p = owner.readEq();
            if (fs == lastFs && p == last) continue;

            bm::dsp::KernelPair k;
            bm::dsp::designKernels (p, fs, k, fft);
            owner.publishKernels (std::move (k));
            last = p;
            lastFs = fs;
        }
    }

private:
    BroadcastMasterProcessor& owner;
    bm::Fft fft;
    bm::dsp::EqParams last;
    double lastFs = 0.0;
};

BroadcastMasterProcessor::BroadcastMasterProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Solo",   juce::AudioChannelSet::stereo(), false)),
      apvts (*this, nullptr, "BroadcastMaster", createLayout())
{
    auto P = [this] (const std::string& id)
    {
        auto* a = apvts.getRawParameterValue (id);
        jassert (a != nullptr);
        return a;
    };

    for (int i = 0; i < kNumStages; ++i)
    {
        ptr.gain[(size_t) i] = P (kStages[i].id);
        ptr.enable[(size_t) i] = kStages[i].enableId ? P (kStages[i].enableId) : nullptr;
        stagePeakDb[(size_t) i].store (-120.0f);
        stageRmsDb[(size_t) i].store (-120.0f);
    }

    ptr.latency = P (latencyMode); ptr.bypass = P (bypass); ptr.match = P (matchBypass); ptr.soloOn = P (soloOn);
    ptr.monA = P (monA); ptr.monB = P (monB); ptr.monAB = P (monAB); ptr.monLevel = P (monLevel); ptr.toMain = P (soloToMain);

    ptr.hpFreq = P ("hp_freq"); ptr.phFreq = P ("ph_freq"); ptr.phStages = P ("ph_stages"); ptr.eqTilt = P ("eq_tilt");
    for (int i = 0; i < bm::dsp::EqParams::kBands; ++i)
        for (int k = 0; k < 5; ++k)
        {
            static const char* what[5] { "on", "freq", "gain", "q", "tgt" };
            ptr.eq[(size_t) i][(size_t) k] = P (eqId (i, what[k]));
        }

    ptr.satType = P ("sat_type"); ptr.satAmt = P ("sat_amt"); ptr.satMix = P ("sat_mix");
    ptr.excFreq = P ("exc_freq"); ptr.excAmt = P ("exc_amt"); ptr.bassFreq = P ("bass_freq"); ptr.bassAmt = P ("bass_amt");

    ptr.mbX = { P ("mb_x1"), P ("mb_x2"), P ("mb_x3") };
    for (int b = 0; b < bm::dsp::MbcParams::kBands; ++b)
        for (int k = 0; k < 6; ++k)
        {
            static const char* what[6] { "on", "thr", "ratio", "atk", "rel", "mk" };
            ptr.mb[(size_t) b][(size_t) k] = P (mbId (b, what[k]));
        }
    ptr.mbKnee = P ("mb_knee"); ptr.mbMix = P ("mb_mix");

    ptr.lvTarget = P ("lv_target"); ptr.lvRange = P ("lv_range"); ptr.lvSpeed = P ("lv_speed"); ptr.lvGate = P ("lv_gate"); ptr.lvMode = P ("lv_mode");
    ptr.stWidth = P ("st_width"); ptr.stMid = P ("st_mid"); ptr.stSide = P ("st_side"); ptr.stFlipL = P ("st_flipl");
    ptr.stFlipR = P ("st_flipr"); ptr.stMono = P ("st_mono"); ptr.stDelay = P ("st_delay");
    ptr.stExc = P ("st_exc"); ptr.stExcF = P ("st_exc_f");
    ptr.clThr = P ("cl_thr"); ptr.clKnee = P ("cl_knee"); ptr.lmRel = P ("lm_rel");

    for (auto* id : { loudPreset, customTarget, customCeiling, presetLink })
        apvts.addParameterListener (id, this);

    designer = std::make_unique<EqDesigner> (*this);
    designer->startThread();
}

BroadcastMasterProcessor::~BroadcastMasterProcessor()
{
    for (auto* id : { loudPreset, customTarget, customCeiling, presetLink })
        apvts.removeParameterListener (id, this);
    cancelPendingUpdate();
    designer->stopThread (1000);
}

bool BroadcastMasterProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.getMainInputChannelSet() != juce::AudioChannelSet::stereo()
        || layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    if (layouts.outputBuses.size() > 1)
    {
        const auto& solo = layouts.getChannelSet (false, 1);
        if (! solo.isDisabled() && solo != juce::AudioChannelSet::stereo())
            return false;
    }
    return true;
}

float BroadcastMasterProcessor::raw (const std::string& id) const
{
    if (auto* a = apvts.getRawParameterValue (id)) return a->load();
    return 0.0f;
}

bm::dsp::EqParams BroadcastMasterProcessor::readEq() const
{
    bm::dsp::EqParams e;
    e.enabled = ptr.enable[bm::dsp::sEq] == nullptr || ptr.enable[bm::dsp::sEq]->load() > 0.5f;
    for (int i = 0; i < bm::dsp::EqParams::kBands; ++i)
    {
        auto& b = e.band[i];
        b.on = ptr.eq[(size_t) i][0]->load() > 0.5f;
        b.freq = ptr.eq[(size_t) i][1]->load();
        b.gainDb = ptr.eq[(size_t) i][2]->load();
        b.q = ptr.eq[(size_t) i][3]->load();
        b.target = (int) std::lround (ptr.eq[(size_t) i][4]->load());
    }
    e.tiltDb = ptr.eqTilt->load();
    return e;
}

void BroadcastMasterProcessor::readParams (bm::dsp::ChainParams& c) const
{
    using namespace bm::dsp;
    for (int s = 0; s < kNumStages; ++s)
    {
        c.gainDb[s] = ptr.gain[(size_t) s]->load();
        c.on[s] = ptr.enable[(size_t) s] == nullptr || ptr.enable[(size_t) s]->load() > 0.5f;
    }

    c.broadcast = ptr.latency->load() > 0.5f;
    c.hpFreq = ptr.hpFreq->load();
    c.phaseFreq = ptr.phFreq->load();
    c.phaseStages = (int) std::lround (ptr.phStages->load());
    c.eq = readEq();

    c.satType = (SatType) std::clamp ((int) std::lround (ptr.satType->load()), 0, 2);
    c.satAmount = ptr.satAmt->load();
    c.satMix = ptr.satMix->load();
    c.excFreq = ptr.excFreq->load(); c.excAmount = ptr.excAmt->load();
    c.bassFreq = ptr.bassFreq->load(); c.bassAmount = ptr.bassAmt->load();

    for (int i = 0; i < 3; ++i) c.mbc.x[i] = ptr.mbX[(size_t) i]->load();
    for (int b = 0; b < MbcParams::kBands; ++b)
    {
        auto& d = c.mbc.band[b];
        const auto& q = ptr.mb[(size_t) b];
        d.on = q[0]->load() > 0.5f; d.thrDb = q[1]->load(); d.ratio = q[2]->load();
        d.attackMs = q[3]->load(); d.releaseMs = q[4]->load(); d.makeupDb = q[5]->load();
    }
    c.mbc.kneeDb = ptr.mbKnee->load();
    c.mbc.mixPct = ptr.mbMix->load();

    c.lev.targetLufs = ptr.lvTarget->load();
    c.lev.rangeDb = ptr.lvRange->load();
    c.lev.speed = (LevSpeed) std::clamp ((int) std::lround (ptr.lvSpeed->load()), 0, 2);
    c.lev.gateLufs = ptr.lvGate->load();
    c.lev.mode = (LevMode) std::clamp ((int) std::lround (ptr.lvMode->load()), 0, 2);

    c.stereo.widthPct = ptr.stWidth->load();
    c.stereo.midGainDb = ptr.stMid->load();
    c.stereo.sideGainDb = ptr.stSide->load();
    c.stereo.flipL = ptr.stFlipL->load() > 0.5f;
    c.stereo.flipR = ptr.stFlipR->load() > 0.5f;
    c.stereo.mono = ptr.stMono->load() > 0.5f;
    c.stereo.delayMs = ptr.stDelay->load();
    c.stereo.excitePct = ptr.stExc->load();
    c.stereo.exciteHz = ptr.stExcF->load();

    c.clipThreshold = ptr.clThr->load();
    c.clipKnee = ptr.clKnee->load();
    c.limRelease = ptr.lmRel->load();

    c.bypass = ptr.bypass->load() > 0.5f;
    c.matchBypass = ptr.match->load() > 0.5f;
    c.soloOn = ptr.soloOn->load() > 0.5f;
    const bool useB = ptr.monAB->load() > 0.5f;
    c.monSrc = std::clamp ((int) std::lround ((useB ? ptr.monB : ptr.monA)->load()), 0, 7);
    c.monLevelDb = ptr.monLevel->load();
}

void BroadcastMasterProcessor::publishKernels (bm::dsp::KernelPair&& k)
{
    {
        const juce::SpinLock::ScopedLockType sl (kernelLock);
        sharedKernels = std::move (k);
    }
    kernelsReady.store (true);
}

void BroadcastMasterProcessor::prepareToPlay (double sampleRate, int)
{
    currentSampleRate.store (sampleRate);
    metersIn.prepare (sampleRate);
    metersOut.prepare (sampleRate);

    chain.prepare (sampleRate);

    // First kernels are designed here so the very first block is already right.
    {
        const auto e = readEq();
        chain.eq.setParams (e);
        bm::dsp::KernelPair k;
        chain.eq.design (k);
        chain.eq.applyKernelsNow (k);
    }

    readParams (cp);
    chain.applyMode (cp.broadcast); // sets the mode, and so the latency, before the host asks for it
    lastLatency = chain.latency();
    latencyShared.store (lastLatency);
    setLatencySamples (lastLatency);
}

void BroadcastMasterProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    if (buffer.getNumChannels() < 2 || n == 0)
        return;

    // new EQ kernels from the design thread (never waits for the lock)
    if (kernelsReady.load())
    {
        const juce::SpinLock::ScopedTryLockType t (kernelLock);
        if (t.isLocked())
        {
            chain.eq.applyKernels (sharedKernels);
            kernelsReady.store (false);
        }
    }

    readParams (cp);

    float* l = buffer.getWritePointer (0);
    float* r = buffer.getWritePointer (1);

    float* auxL = nullptr;
    float* auxR = nullptr;
    bool haveAux = false;
    if (getBusCount (false) > 1)
        if (auto* bus = getBus (false, 1))
            if (bus->isEnabled() && bus->getNumberOfChannels() >= 2)
            {
                auto ab = getBusBuffer (buffer, false, 1);
                auxL = ab.getWritePointer (0);
                auxR = ab.getWritePointer (1);
                haveAux = true;
            }
    auxConnected.store (haveAux, std::memory_order_relaxed);

    chain.process (cp, l, r, n, auxL, auxR, ptr.toMain->load() > 0.5f, &metersIn, &metersOut);

    for (int s = 0; s < kNumStages; ++s)
    {
        stagePeakDb[(size_t) s].store (chain.stagePeak (s), std::memory_order_relaxed);
        stageRmsDb[(size_t) s].store (chain.stageRms (s), std::memory_order_relaxed);
    }

    float grc = 0.0f;
    if (cp.on[bm::dsp::sMbc])
        for (int b = 0; b < bm::dsp::MbcParams::kBands; ++b) grc = std::max (grc, chain.mbc.grDb (b));
    gainReductionComp.store (grc, std::memory_order_relaxed);
    gainReductionLimiter.store (cp.on[bm::dsp::sLim] ? chain.lim.grDb() : 0.0f, std::memory_order_relaxed);

    const int lat = chain.latency();
    if (lat != lastLatency)
    {
        lastLatency = lat;
        latencyShared.store (lat);
        triggerAsyncUpdate(); // the host is told on the message thread
    }
}

void BroadcastMasterProcessor::parameterChanged (const juce::String&, float)
{
    targetLinkDirty.store (true);
    triggerAsyncUpdate();
}

void BroadcastMasterProcessor::handleAsyncUpdate()
{
    if (getLatencySamples() != latencyShared.load())
        setLatencySamples (latencyShared.load());

    if (targetLinkDirty.exchange (false))
        applyTargetLink();
}

// When the link is on, the loudness target drives the leveler target and the limiter ceiling.
void BroadcastMasterProcessor::applyTargetLink()
{
    if (raw (presetLink) < 0.5f) return;

    const int idx = std::clamp ((int) std::lround (raw (loudPreset)), 0, bm::kNumLoudnessPresets - 1);
    const bool custom = idx == bm::kCustomPresetIndex;
    const float target = custom ? raw (customTarget) : bm::kLoudnessPresets[idx].targetLufs;
    const float ceiling = custom ? raw (customCeiling) : bm::kLoudnessPresets[idx].ceilingDbtp;

    auto set = [this] (const char* id, float value)
    {
        if (auto* prm = apvts.getParameter (id))
            prm->setValueNotifyingHost (prm->convertTo0to1 (value));
    };
    set ("lv_target", target);
    set (kStages[kLimiterStage].id, ceiling);
}

juce::AudioProcessorEditor* BroadcastMasterProcessor::createEditor()
{
    return new BroadcastMasterEditor (*this);
}

void BroadcastMasterProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void BroadcastMasterProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new BroadcastMasterProcessor();
}
