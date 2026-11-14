#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
SampleForgeVoice::SampleForgeVoice(const VoiceParams& p)
    : voiceParams(p)
{
    juce::ADSR::Parameters params;
    params.attack = 0.01f;
    params.decay = 0.1f;
    params.sustain = 1.0f;
    params.release = 0.1f;
    adsr.setParameters(params);
}

bool SampleForgeVoice::canPlaySound(juce::SynthesiserSound* sound)
{
    return dynamic_cast<SampleForgeSound*>(sound) != nullptr;
}

void SampleForgeVoice::startNote(int midiNoteNumber, float velocity, juce::SynthesiserSound* sound, int currentPitchWheelPosition)
{
    currentSound = dynamic_cast<SampleForgeSound*>(sound);

    if (currentSound == nullptr || currentSound->data.getNumSamples() == 0)
    {
        clearCurrentNote();
        return;
    }

    // Zones cover the full velocity range by default, but a velocity-switched
    // map narrows this; SynthesiserSound has no velocity hook of its own, so
    // this has to be checked here rather than in appliesToNote().
    int velocityByte = juce::roundToInt(velocity * 127.0f);
    if (velocityByte < currentSound->velLow.load() || velocityByte > currentSound->velHigh.load())
    {
        clearCurrentNote();
        return;
    }

    adsr.setSampleRate(getSampleRate());
    adsr.noteOn();
    filter.reset();
    lastEnvValue = 0.0f;

    if (stretcher != nullptr)
    {
        stretcher->reset();
        stretcherFinalSent = false;
    }

    level = velocity;
    tailRemaining = 0;

    float rootNote = static_cast<float>(currentSound->rootNote.load());
    double sourceLength = static_cast<double>(currentSound->data.getNumSamples());

    float startFrac = currentSound->sampleStart.load();
    float endFrac = currentSound->sampleEnd.load();
    startFrac = juce::jlimit(0.0f, 1.0f, startFrac);
    endFrac = juce::jlimit(startFrac + 0.001f, 1.0f, endFrac);

    startSampleInSource = startFrac * sourceLength;
    endSampleInSource = endFrac * sourceLength;

    bool reversed = currentSound->reverse.load();
    loopDirection = reversed ? -1 : 1;
    sourcePosition = reversed ? endSampleInSource : startSampleInSource;

    double noteRatio = std::pow(2.0, (midiNoteNumber - rootNote) / 12.0);
    double sampleRateRatio = currentSound->sourceSampleRate / getSampleRate();
    basePitchRatio = noteRatio * sampleRateRatio;

    // A note starting under a held bend begins at the bent pitch immediately.
    wheelPosition = currentPitchWheelPosition;
    float range = voiceParams.bendRange != nullptr ? voiceParams.bendRange->load() : 2.0f;
    bendSemitones = (wheelPosition - 8192) / 8192.0 * range;
    pitchRatio = basePitchRatio * std::exp2(bendSemitones / 12.0);
}

void SampleForgeVoice::stopNote(float /*velocity*/, bool allowTailOff)
{
    if (allowTailOff)
    {
        adsr.noteOff();
    }
    else
    {
        clearCurrentNote();
        adsr.reset();
    }
}

void SampleForgeVoice::pitchWheelMoved(int newPitchWheelValue)
{
    // Picked up (and smoothed) at the start of the next render block; the base
    // class splits blocks at MIDI events, so this is effectively sample-accurate.
    wheelPosition = newPitchWheelValue;
}
void SampleForgeVoice::controllerMoved(int, int) {}

void SampleForgeVoice::prepareVoice(double sampleRate, int samplesPerBlock, int numChannels)
{
    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = static_cast<juce::uint32>(samplesPerBlock);
    spec.numChannels = static_cast<juce::uint32>(numChannels);
    filter.prepare(spec);

    // Real-time, R2/Faster engine: far cheaper than R3/Finer, which matters
    // across up to 16 simultaneous voices. setMaxProcessSize() pre-allocates
    // internal buffers for that chunk size so process() never reallocates
    // once playing (see RubberBandStretcher.h's RT-safety notes).
    stretcher = std::make_unique<RubberBand::RubberBandStretcher>(
        static_cast<size_t>(sampleRate), 2,
        RubberBand::RubberBandStretcher::OptionProcessRealTime
            | RubberBand::RubberBandStretcher::OptionEngineFaster);
    stretcher->setMaxProcessSize(static_cast<size_t>(stretchChunkSize));

    int outputCapacity = juce::jmax(samplesPerBlock, 8192);
    for (int ch = 0; ch < 2; ++ch)
    {
        stretchInputScratch[ch].assign(static_cast<size_t>(stretchChunkSize), 0.0f);
        stretchOutputScratch[ch].assign(static_cast<size_t>(outputCapacity), 0.0f);
    }
}

bool SampleForgeVoice::fillStretchedBlock(int numSamples, const StretchFeed& feed){
    const auto& sourceData = currentSound->data;
    int numSourceChannels = sourceData.getNumChannels();
    int sourceNumSamples = sourceData.getNumSamples();

    int produced = 0;

    while (produced < numSamples)
    {
        int avail = stretcher->available();

        if (avail < 0)
        {
            // Fully drained: the note is over.
            for (int ch = 0; ch < 2; ++ch)
                std::fill(stretchOutputScratch[ch].begin() + produced, stretchOutputScratch[ch].begin() + numSamples, 0.0f);
            return false;
        }

        if (avail > 0)
        {
            int toRetrieve = juce::jmin(avail, numSamples - produced);
            float* outPtrs[2] = { stretchOutputScratch[0].data() + produced, stretchOutputScratch[1].data() + produced };
            size_t got = stretcher->retrieve(outPtrs, static_cast<size_t>(toRetrieve));
            produced += juce::jmax(1, static_cast<int>(got)); // never spin on a zero-progress retrieve
            continue;
        }

        // avail == 0: needs more input, unless we already sent the final
        // (post-Start..End) chunk, in which case there's nothing left to
        // feed — bail to silence rather than risk spinning on the audio
        // thread waiting for a drain that (per the documented contract)
        // should already have resolved to -1 above.
        if (stretcherFinalSent)
        {
            for (int ch = 0; ch < 2; ++ch)
                std::fill(stretchOutputScratch[ch].begin() + produced, stretchOutputScratch[ch].begin() + numSamples, 0.0f);
            return false;
        }

        int fed = 0;
        bool exhausted = false;
        const bool looping = feed.mode != static_cast<int>(LoopMode::off) && feed.loopEnd > startSampleInSource;
        const double xf = looping && feed.mode == static_cast<int>(LoopMode::forward) ? feed.crossfade : 0.0;
        const int lastIndex = sourceNumSamples - 1;

        while (fed < stretchChunkSize)
        {
            if (looping)
            {
                if (feed.mode == static_cast<int>(LoopMode::forward))
                {
                    if (loopDirection > 0 && sourcePosition >= feed.loopEnd)
                        sourcePosition = sourcePosition - feed.loopEnd + startSampleInSource + xf;
                    else if (loopDirection < 0 && sourcePosition <= startSampleInSource)
                        sourcePosition = sourcePosition - startSampleInSource + feed.loopEnd - xf;
                }
                else // ping-pong: reflecting keeps the signal continuous
                {
                    for (int guard = 0; guard < 4; ++guard)
                    {
                        if (sourcePosition >= feed.loopEnd)
                        {
                            sourcePosition = feed.loopEnd - (sourcePosition - feed.loopEnd);
                            loopDirection = -1;
                        }
                        else if (sourcePosition <= startSampleInSource)
                        {
                            sourcePosition = startSampleInSource + (startSampleInSource - sourcePosition);
                            loopDirection = 1;
                        }
                        else
                            break;
                    }
                }
            }

            int idx = loopDirection > 0 ? static_cast<int>(sourcePosition) : static_cast<int>(sourcePosition) - 1;

            if (!looping && (idx < static_cast<int>(startSampleInSource) || idx >= static_cast<int>(endSampleInSource) || idx < 0 || idx > lastIndex))
            {
                exhausted = true;
                break;
            }

            idx = juce::jlimit(0, lastIndex, idx);

            // Seamless-loop crossfade: approaching the loop point, blend towards the
            // material that will follow the wrap (see StretchFeed).
            float w = 0.0f;
            int otherIdx = idx;
            if (xf > 0.0 && loopDirection > 0 && sourcePosition >= feed.loopEnd - xf)
            {
                w = static_cast<float>((sourcePosition - (feed.loopEnd - xf)) / xf);
                otherIdx = juce::jlimit(0, lastIndex, static_cast<int>(startSampleInSource + (sourcePosition - (feed.loopEnd - xf))));
            }
            else if (xf > 0.0 && loopDirection < 0 && sourcePosition <= startSampleInSource + xf)
            {
                w = static_cast<float>((startSampleInSource + xf - sourcePosition) / xf);
                otherIdx = juce::jlimit(0, lastIndex, static_cast<int>(feed.loopEnd - xf + (sourcePosition - startSampleInSource)) - 1);
            }
            w = juce::jlimit(0.0f, 1.0f, w);

            for (int ch = 0; ch < 2; ++ch)
            {
                int srcCh = juce::jmin(ch, numSourceChannels - 1);
                float v = sourceData.getSample(srcCh, idx);
                if (w > 0.0f)
                    v = v * (1.0f - w) + sourceData.getSample(srcCh, otherIdx) * w;
                stretchInputScratch[ch][static_cast<size_t>(fed)] = v;
            }
            sourcePosition += static_cast<double>(loopDirection);
            ++fed;
        }

        const float* inPtrs[2] = { stretchInputScratch[0].data(), stretchInputScratch[1].data() };
        stretcher->process(inPtrs, static_cast<size_t>(fed), exhausted);

        if (exhausted)
            stretcherFinalSent = true;
    }

    return true;
}

void SampleForgeVoice::renderNextBlock(juce::AudioBuffer<float>& outputBuffer, int startSample, int numSamples)
{
    if (!isVoiceActive() || currentSound == nullptr)
        return;

    // Re-read live so dragging the trim handles while a note sustains
    // truncates (or re-bounds the loop) immediately rather than waiting for the next note.
    float startFrac = currentSound->sampleStart.load();
    float endFrac = currentSound->sampleEnd.load();
    double sourceLength = static_cast<double>(currentSound->data.getNumSamples());
    startSampleInSource = juce::jlimit(0.0, sourceLength, static_cast<double>(startFrac) * sourceLength);
    endSampleInSource = juce::jlimit(0.0, sourceLength, static_cast<double>(endFrac) * sourceLength);

    auto loopMode = static_cast<LoopMode>(voiceParams.loopMode != nullptr
                                              ? static_cast<int>(voiceParams.loopMode->load())
                                              : 0);

    juce::ADSR::Parameters adsrParams;
    adsrParams.attack = voiceParams.attack != nullptr ? voiceParams.attack->load() : 0.01f;
    adsrParams.decay = voiceParams.decay != nullptr ? voiceParams.decay->load() : 0.1f;
    adsrParams.sustain = voiceParams.sustain != nullptr ? voiceParams.sustain->load() : 1.0f;
    adsrParams.release = voiceParams.release != nullptr ? voiceParams.release->load() : 0.1f;
    adsr.setParameters(adsrParams);

    float gain = voiceParams.gain != nullptr ? voiceParams.gain->load() : 0.8f;
    float zoneGain = currentSound->gain.load();

    // Filter coefficients are set once per block (using the envelope value
    // carried over from the end of the previous block) rather than per
    // sample — sample-accurate envelope-to-cutoff tracking isn't worth
    // recomputing filter coefficients on every single sample for.
    auto filterType = static_cast<FilterType>(voiceParams.filterType != nullptr
                                                   ? static_cast<int>(voiceParams.filterType->load())
                                                   : 0);
    float baseCutoff = voiceParams.filterCutoff != nullptr ? voiceParams.filterCutoff->load() : 20000.0f;
    float resonance = voiceParams.filterResonance != nullptr ? voiceParams.filterResonance->load() : 0.707f;
    float envAmountOctaves = voiceParams.filterEnvAmount != nullptr ? voiceParams.filterEnvAmount->load() : 0.0f;

    float modulatedCutoff = baseCutoff * std::pow(2.0f, envAmountOctaves * lastEnvValue);
    modulatedCutoff = juce::jlimit(20.0f, 20000.0f, modulatedCutoff);

    filter.setType(filterType == FilterType::lowpass ? juce::dsp::StateVariableTPTFilterType::lowpass
                  : filterType == FilterType::highpass ? juce::dsp::StateVariableTPTFilterType::highpass
                                                        : juce::dsp::StateVariableTPTFilterType::bandpass);
    filter.setCutoffFrequency(modulatedCutoff);
    filter.setResonance(juce::jlimit(0.1f, 20.0f, resonance));

    const auto& sourceData = currentSound->data;
    int numSourceChannels = sourceData.getNumChannels();
    int numOutputChannels = outputBuffer.getNumChannels();

    // Equal-power pan law; only meaningful once there's more than one output
    // channel to place the zone's signal between.
    float leftGain = 1.0f;
    float rightGain = 1.0f;
    if (numOutputChannels > 1)
    {
        float pan = juce::jlimit(-1.0f, 1.0f, currentSound->pan.load());
        float panAngle = (pan + 1.0f) * juce::MathConstants<float>::pi / 4.0f;
        leftGain = std::cos(panAngle);
        rightGain = std::sin(panAngle);
    }

    // Time-stretch works with every loop mode and in reverse: the stretcher is
    // simply fed a source stream that already follows the zone's loop/reverse
    // behaviour (see fillStretchedBlock).
    float stretchRatio = voiceParams.stretchRatio != nullptr ? voiceParams.stretchRatio->load() : 1.0f;
    bool useStretch = stretcher != nullptr && std::abs(stretchRatio - 1.0f) > 0.001f;

    // Smoothly follow the pitch wheel. Per block here; per sample in the plain
    // resampling path below.
    const float bendRange = voiceParams.bendRange != nullptr ? voiceParams.bendRange->load() : 2.0f;
    const double bendTarget = (wheelPosition - 8192) / 8192.0 * bendRange;
    const double bendCoef = 1.0 - std::exp(-1.0 / (0.005 * getSampleRate()));
    if (useStretch && bendSemitones != bendTarget)
    {
        bendSemitones += (bendTarget - bendSemitones) * (1.0 - std::exp(-numSamples / (0.005 * getSampleRate())));
        if (std::abs(bendTarget - bendSemitones) < 1.0e-4) bendSemitones = bendTarget;
        pitchRatio = basePitchRatio * std::exp2(bendSemitones / 12.0);
    }

    // Loop length: the trim region, or a tempo-derived length when Loop Sync is
    // on. Tempo sync never extends past the trim End and only affects looping modes.
    double rawTrimLength = endSampleInSource - startSampleInSource;
    double loopEnd = endSampleInSource;

    int loopSyncIndex = voiceParams.loopSyncRate != nullptr ? static_cast<int>(voiceParams.loopSyncRate->load()) : 0;
    if (loopSyncIndex > 0 && loopMode != LoopMode::off && rawTrimLength > 0.0)
    {
        static constexpr double noteFactors[] = { 0.0, 4.0, 2.0, 1.0, 0.5, 0.25 }; // whole, 1/2, 1/4, 1/8, 1/16
        double bpm = voiceParams.tempoBpm != nullptr ? static_cast<double>(voiceParams.tempoBpm->load()) : 120.0;
        if (bpm <= 0.0)
            bpm = 120.0;

        double syncedSeconds = noteFactors[juce::jlimit(0, 5, loopSyncIndex)] * (60.0 / bpm);
        // Resampling: source advances pitchRatio per output sample. Stretching:
        // pitch is decoupled, so a loop of L source samples lasts
        // L * stretch / sourceRate seconds, whatever note is played.
        double syncedSourceSamples = useStretch
                                         ? syncedSeconds * currentSound->sourceSampleRate / static_cast<double>(stretchRatio)
                                         : syncedSeconds * pitchRatio * getSampleRate();

        if (syncedSourceSamples > 0.0)
            loopEnd = startSampleInSource + juce::jmin(rawTrimLength, syncedSourceSamples);
    }

    double loopLength = loopEnd - startSampleInSource;

    if (useStretch)
    {
        // The stretcher is fed source samples but runs at the host rate, so its
        // time ratio has to include the rate difference or a 44.1 kHz file in a
        // 48 kHz session would stretch to the wrong length. (pitchRatio already
        // folds the same rate difference in.)
        double rateRatio = getSampleRate() / currentSound->sourceSampleRate;
        stretcher->setPitchScale(pitchRatio);
        stretcher->setTimeRatio(static_cast<double>(stretchRatio) * rateRatio);

        StretchFeed feed;
        feed.mode = static_cast<int>(loopMode);
        feed.loopEnd = loopEnd;
        feed.crossfade = juce::jmin(0.005 * currentSound->sourceSampleRate, 0.25 * loopLength);

        bool stillPlaying = fillStretchedBlock(numSamples, feed);

        for (int sample = 0; sample < numSamples; ++sample)
        {
            if (!adsr.isActive())
            {
                clearCurrentNote();
                break;
            }

            float env = adsr.getNextSample();
            lastEnvValue = env;

            for (int channel = 0; channel < numOutputChannels; ++channel)
            {
                int stretchChannel = juce::jmin(channel, 1);
                float raw = stretchOutputScratch[stretchChannel][static_cast<size_t>(sample)];
                float filtered = filter.processSample(channel, raw);
                float channelPanGain = (channel == 0) ? leftGain : rightGain;

                outputBuffer.addSample(channel, startSample + sample,
                                        filtered * level * env * gain * zoneGain * channelPanGain);
            }
        }

        if (!stillPlaying)
            clearCurrentNote();

        return;
    }

    // 4-point, 3rd-order Lagrange interpolation (Niemitalo/musicdsp.org) for
    // smooth pitched playback; positions are clamped to the sample data.
    const int lastIndex = sourceData.getNumSamples() - 1;
    auto lagrange = [lastIndex](const float* d, double pos)
    {
        pos = juce::jlimit(0.0, static_cast<double>(lastIndex), pos); // hold the edge sample rather than extrapolate past it
        int idx0 = juce::jlimit(0, lastIndex, static_cast<int>(pos));
        int idxM1 = juce::jmax(idx0 - 1, 0);
        int idx1 = juce::jmin(idx0 + 1, lastIndex);
        int idx2 = juce::jmin(idx0 + 2, lastIndex);
        float frac = static_cast<float>(pos - idx0);
        float yM1 = d[idxM1], y0 = d[idx0], y1 = d[idx1], y2 = d[idx2];
        float c1 = y1 - (1.0f / 3.0f) * yM1 - 0.5f * y0 - (1.0f / 6.0f) * y2;
        float c2 = 0.5f * (yM1 + y1) - y0;
        float c3 = (1.0f / 6.0f) * (y2 - yM1) + 0.5f * (y0 - y1);
        return ((c3 * frac + c2) * frac + c1) * frac + y0;
    };

    for (int sample = 0; sample < numSamples; ++sample)
    {
        if (loopMode == LoopMode::off)
        {
            bool reachedEnd = loopDirection > 0 ? (sourcePosition >= endSampleInSource)
                                                 : (sourcePosition <= startSampleInSource);
            if (reachedEnd || !adsr.isActive())
            {
                clearCurrentNote();
                break;
            }
        }
        else if (loopMode == LoopMode::forward)
        {
            if (loopLength > 0.0)
            {
                const bool wrapsForward = loopDirection > 0 ? sourcePosition >= loopEnd
                                                            : sourcePosition <= startSampleInSource;
                if (wrapsForward)
                {
                    tailPosition = sourcePosition;
                    tailTotal = juce::jmax(1, static_cast<int>(0.003 * getSampleRate()));
                    tailRemaining = tailTotal;
                }

                if (loopDirection > 0)
                {
                    while (sourcePosition >= loopEnd)
                        sourcePosition -= loopLength;
                }
                else
                {
                    while (sourcePosition <= startSampleInSource)
                        sourcePosition += loopLength;
                }
            }

            if (!adsr.isActive())
            {
                clearCurrentNote();
                break;
            }
        }
        else // LoopMode::pingPong
        {
            // Guard against a handful of reflections per sample rather than looping
            // unboundedly, in case the loop is shorter than one pitch-shifted step.
            for (int guard = 0; loopLength > 0.0 && guard < 4; ++guard)
            {
                if (sourcePosition >= loopEnd)
                {
                    sourcePosition = loopEnd - (sourcePosition - loopEnd);
                    loopDirection = -1;
                }
                else if (sourcePosition <= startSampleInSource)
                {
                    sourcePosition = startSampleInSource + (startSampleInSource - sourcePosition);
                    loopDirection = 1;
                }
                else
                {
                    break;
                }
            }

            if (!adsr.isActive())
            {
                clearCurrentNote();
                break;
            }
        }

        float env = adsr.getNextSample();
        lastEnvValue = env;

        if (bendSemitones != bendTarget)
        {
            bendSemitones += (bendTarget - bendSemitones) * bendCoef;
            if (std::abs(bendTarget - bendSemitones) < 1.0e-4)
                bendSemitones = bendTarget;
            pitchRatio = basePitchRatio * std::exp2(bendSemitones / 12.0);
        }

        // Short fade out over the last ~2 ms before the trim End (or Start when
        // reversed), so a one-shot never ends on a hard cut. Looping modes never
        // reach their end, so they are left alone.
        float endFade = 1.0f;
        if (loopMode == LoopMode::off)
        {
            double remaining = loopDirection > 0 ? endSampleInSource - sourcePosition
                                                  : sourcePosition - startSampleInSource;
            double fadeOutSamples = 0.002 * getSampleRate();
            double remainingOut = remaining / juce::jmax(pitchRatio, 1.0e-6);
            if (remainingOut < fadeOutSamples)
                endFade = static_cast<float>(juce::jmax(0.0, remainingOut / fadeOutSamples));
        }

        for (int channel = 0; channel < numOutputChannels; ++channel)
        {
            int sourceChannel = juce::jmin(channel, numSourceChannels - 1);
            const float* channelData = sourceData.getReadPointer(sourceChannel);

            float interpolated = lagrange(channelData, sourcePosition);
            if (tailRemaining > 0)
            {
                float t = static_cast<float>(tailRemaining) / static_cast<float>(tailTotal);
                interpolated = interpolated * (1.0f - t) + lagrange(channelData, tailPosition) * t;
            }
            float filtered = filter.processSample(channel, interpolated);

            float channelPanGain = (channel == 0) ? leftGain : rightGain;
            outputBuffer.addSample(channel, startSample + sample,
                                    filtered * level * env * endFade * gain * zoneGain * channelPanGain);
        }

        if (tailRemaining > 0)
        {
            tailPosition += pitchRatio * loopDirection;
            --tailRemaining;
        }

        sourcePosition += pitchRatio * loopDirection;
    }
}

//==============================================================================
void SampleForgeSynthesiser::noteOn(int midiChannel, int midiNoteNumber, float velocity)
{
    int velocityByte = juce::roundToInt(velocity * 127.0f);

    // Reset every zone's suppression flags first, so a decision made for some
    // earlier note never lingers and silences an unrelated future note. Zones
    // outside this note's velocity range are switched off here, before the base
    // class allocates voices, so they can never take or steal one.
    for (int i = 0; i < getNumSounds(); ++i)
        if (auto* zone = dynamic_cast<SampleForgeSound*>(getSound(i).get()))
        {
            zone->rrSuppressed = false;
            zone->velSuppressed = velocityByte < zone->velLow.load() || velocityByte > zone->velHigh.load();
        }

    // Collect, per round-robin group, the zones that actually match this
    // note+velocity (group 0 means "no group": always independently eligible).
    SampleForgeSound* groupMembers[maxRRGroups][maxGroupMembers];
    int groupCounts[maxRRGroups] = {};

    for (int i = 0; i < getNumSounds(); ++i)
    {
        auto* zone = dynamic_cast<SampleForgeSound*>(getSound(i).get());
        if (zone == nullptr)
            continue;

        int group = zone->rrGroup.load();
        if (group <= 0 || group >= maxRRGroups)
            continue;

        bool keyMatches = midiNoteNumber >= zone->keyLow.load() && midiNoteNumber <= zone->keyHigh.load();
        if (!keyMatches || zone->velSuppressed.load())
            continue;

        int& count = groupCounts[group];
        if (count < maxGroupMembers)
            groupMembers[group][count++] = zone;
    }

    bool randomOrder = rrOrderParam != nullptr && static_cast<int>(rrOrderParam->load()) == static_cast<int>(RRMode::random);

    for (int group = 1; group < maxRRGroups; ++group)
    {
        int count = groupCounts[group];
        if (count <= 1)
            continue; // nothing to disambiguate between

        int winner = randomOrder ? random.nextInt(count) : (rrNextIndex[group]++ % count);

        for (int i = 0; i < count; ++i)
            if (i != winner)
                groupMembers[group][i]->rrSuppressed = true;
    }

    Synthesiser::noteOn(midiChannel, midiNoteNumber, velocity);
}

//==============================================================================
SampleForgeAudioProcessor::SampleForgeAudioProcessor()
    : juce::AudioProcessor(BusesProperties()
                           .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "Parameters", createParameterLayout())
{
    formatManager.registerBasicFormats();

    VoiceParams voiceParams;
    voiceParams.attack = apvts.getRawParameterValue("ATTACK");
    voiceParams.decay = apvts.getRawParameterValue("DECAY");
    voiceParams.sustain = apvts.getRawParameterValue("SUSTAIN");
    voiceParams.release = apvts.getRawParameterValue("RELEASE");
    voiceParams.gain = apvts.getRawParameterValue("GAIN");
    voiceParams.loopMode = apvts.getRawParameterValue("LOOP_MODE");
    voiceParams.loopSyncRate = apvts.getRawParameterValue("LOOP_SYNC");
    voiceParams.tempoBpm = &currentBpm;
    voiceParams.filterType = apvts.getRawParameterValue("FILTER_TYPE");
    voiceParams.filterCutoff = apvts.getRawParameterValue("FILTER_CUTOFF");
    voiceParams.filterResonance = apvts.getRawParameterValue("FILTER_RESONANCE");
    voiceParams.filterEnvAmount = apvts.getRawParameterValue("FILTER_ENV_AMOUNT");
    voiceParams.stretchRatio = apvts.getRawParameterValue("STRETCH");
    voiceParams.bendRange = apvts.getRawParameterValue("BEND_RANGE");

    synth.rrOrderParam = apvts.getRawParameterValue("RR_ORDER");

    for (int i = 0; i < 16; ++i)
        synth.addVoice(new SampleForgeVoice(voiceParams));
}

SampleForgeAudioProcessor::~SampleForgeAudioProcessor()
{
}

juce::AudioProcessorValueTreeState::ParameterLayout SampleForgeAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    // Key/velocity range, root note, and trim are per-zone data on
    // SampleForgeSound now (see addZoneFromFile), not global APVTS parameters.

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("ATTACK", 1), "Attack", 0.001f, 2.0f, 0.01f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("DECAY", 1), "Decay", 0.001f, 2.0f, 0.1f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("SUSTAIN", 1), "Sustain", 0.0f, 1.0f, 1.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("RELEASE", 1), "Release", 0.001f, 4.0f, 0.1f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("GAIN", 1), "Gain", juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.8f));

    // Loops between the SAMPLE_START/SAMPLE_END trim points; indices must match LoopMode.
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID("LOOP_MODE", 1), "Loop",
        juce::StringArray { "Off", "Forward", "Ping-Pong" }, 0));

    // Off (default) keeps the loop's original free-running length; otherwise
    // overrides it with a tempo-derived length. Indices must match LoopSyncRate.
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID("LOOP_SYNC", 1), "Loop Sync",
        juce::StringArray { "Off", "1/1", "1/2", "1/4", "1/8", "1/16" }, 0));

    // Governs how SampleForgeSynthesiser::noteOn() picks a winner within a
    // round-robin group; indices must match RRMode.
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID("RR_ORDER", 1), "RR Order",
        juce::StringArray { "Sequential", "Random" }, 0));

    // Indices must match FilterType. Cutoff defaults fully open and env
    // amount defaults to 0 so the filter is transparent until touched.
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID("FILTER_TYPE", 1), "Filter Type",
        juce::StringArray { "Low-pass", "High-pass", "Band-pass" }, 0));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("FILTER_CUTOFF", 1), "Filter Cutoff",
        juce::NormalisableRange<float>(20.0f, 20000.0f, 1.0f, 0.3f), 20000.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("FILTER_RESONANCE", 1), "Filter Resonance",
        juce::NormalisableRange<float>(0.1f, 10.0f, 0.01f, 0.4f), 0.707f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("FILTER_ENV_AMOUNT", 1), "Filter Env Amount", -4.0f, 4.0f, 0.0f));

    // 1.0 = bypass (unchanged resampling path). Powered by RubberBandStretcher;
    // only applied for non-looping, non-reversed playback — see SampleForgeVoice.
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("STRETCH", 1), "Stretch",
        juce::NormalisableRange<float>(0.25f, 4.0f, 0.001f, 0.4f), 1.0f));

    // Pitch wheel range, semitones each way (standard synth default is 2).
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("BEND_RANGE", 1), "Bend Range",
        juce::NormalisableRange<float>(0.0f, 12.0f, 1.0f), 2.0f));

    return { params.begin(), params.end() };
}

bool SampleForgeAudioProcessor::loadZoneInternal(const juce::File& file, int keyLow, int keyHigh,
                                                  int velLow, int velHigh, int rootNote,
                                                  float sampleStart, float sampleEnd,
                                                  bool reverse, float pan, float gain, int rrGroup,
                                                  bool keepIfMissing)
{
    std::unique_ptr<juce::AudioFormatReader> reader(file.existsAsFile() ? formatManager.createReaderFor(file) : nullptr);

    if (reader == nullptr && !keepIfMissing)
        return false;

    // A restored zone whose file has gone is kept as a silent placeholder, so
    // its settings survive a save and it comes back once the file reappears.
    juce::AudioBuffer<float> newBuffer;
    double sourceRate = 44100.0;
    if (reader != nullptr)
    {
        newBuffer.setSize(static_cast<int>(reader->numChannels), static_cast<int>(reader->lengthInSamples));
        reader->read(&newBuffer, 0, static_cast<int>(reader->lengthInSamples), 0, true, true);
        sourceRate = reader->sampleRate;
    }

    auto* newSound = new SampleForgeSound(std::move(newBuffer), sourceRate, file.getFullPathName());
    newSound->missing = reader == nullptr;
    newSound->keyLow = keyLow;
    newSound->keyHigh = keyHigh;
    newSound->velLow = velLow;
    newSound->velHigh = velHigh;
    newSound->rootNote = rootNote;
    newSound->sampleStart = sampleStart;
    newSound->sampleEnd = sampleEnd;
    newSound->reverse = reverse;
    newSound->pan = pan;
    newSound->gain = gain;
    newSound->rrGroup = rrGroup;

    // Reads the file a second time in the background purely to build/cache
    // peak data for drawing; decoupled from the full in-RAM `data` buffer
    // used for playback, so the editor never has to rescan samples itself.
    newSound->thumbnail = std::make_unique<juce::AudioThumbnail>(128, formatManager, thumbnailCache);
    if (reader != nullptr)
        newSound->thumbnail->setSource(new juce::FileInputSource(file));

    // juce::Synthesiser locks internally around addSound()/removeSound() and
    // renderNextBlock(), so this is safe even if the audio thread is mid-block
    // on another thread.
    synth.addSound(newSound);
    selectedZoneIndex = synth.getNumSounds() - 1;

    return true;
}

void SampleForgeAudioProcessor::addZoneFromFile(const juce::File& file)
{
    for (int i = 0; i < synth.getNumSounds(); ++i)
    {
        auto* zone = getZone(i);
        if (zone != nullptr && zone->missing && juce::File(zone->filePath).getFileName() == file.getFileName())
        {
            // Relink: same settings, new file. The old placeholder is dropped.
            if (loadZoneInternal(file, zone->keyLow.load(), zone->keyHigh.load(), zone->velLow.load(),
                                 zone->velHigh.load(), zone->rootNote.load(), zone->sampleStart.load(),
                                 zone->sampleEnd.load(), zone->reverse.load(), zone->pan.load(),
                                 zone->gain.load(), zone->rrGroup.load()))
            {
                synth.removeSound(i);
                selectedZoneIndex = synth.getNumSounds() - 1;
                sendChangeMessage();
            }
            return;
        }
    }

    if (loadZoneInternal(file, 0, 127, 0, 127, 60, 0.0f, 1.0f))
        sendChangeMessage();
}

void SampleForgeAudioProcessor::removeZone(int index)
{
    if (index < 0 || index >= synth.getNumSounds())
        return;

    synth.removeSound(index);

    if (selectedZoneIndex >= synth.getNumSounds())
        selectedZoneIndex = synth.getNumSounds() - 1;

    sendChangeMessage();
}

int SampleForgeAudioProcessor::getActiveVoiceCount() const
{
    int active = 0;
    for (int i = 0; i < synth.getNumVoices(); ++i)
        if (synth.getVoice(i)->isVoiceActive())
            ++active;
    return active;
}

int SampleForgeAudioProcessor::getNumZones() const
{
    return synth.getNumSounds();
}

SampleForgeSound* SampleForgeAudioProcessor::getZone(int index) const
{
    if (index < 0 || index >= synth.getNumSounds())
        return nullptr;

    return dynamic_cast<SampleForgeSound*>(synth.getSound(index).get());
}

void SampleForgeAudioProcessor::setSelectedZoneIndex(int index)
{
    if (index < -1 || index >= synth.getNumSounds() || index == selectedZoneIndex)
        return;

    selectedZoneIndex = index;
    sendChangeMessage();
}

juce::String SampleForgeAudioProcessor::getSelectedZoneFileName() const
{
    auto* zone = getZone(selectedZoneIndex);
    return zone != nullptr ? juce::File(zone->filePath).getFileName() : juce::String();
}

//==============================================================================
const juce::String SampleForgeAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool SampleForgeAudioProcessor::acceptsMidi() const
{
    return true;
}

bool SampleForgeAudioProcessor::producesMidi() const
{
    return false;
}

bool SampleForgeAudioProcessor::isMidiEffect() const
{
    return false;
}

double SampleForgeAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int SampleForgeAudioProcessor::getNumPrograms()
{
    return 1;
}

int SampleForgeAudioProcessor::getCurrentProgram()
{
    return 0;
}

void SampleForgeAudioProcessor::setCurrentProgram(int /*index*/) {}

const juce::String SampleForgeAudioProcessor::getProgramName(int /*index*/)
{
    return {};
}

void SampleForgeAudioProcessor::changeProgramName(int /*index*/, const juce::String& /*newName*/) {}

//==============================================================================
void SampleForgeAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    synth.setCurrentPlaybackSampleRate(sampleRate);

    // Bus layout only ever allows mono or stereo, so 2 channels covers both.
    for (int i = 0; i < synth.getNumVoices(); ++i)
        if (auto* voice = dynamic_cast<SampleForgeVoice*>(synth.getVoice(i)))
            voice->prepareVoice(sampleRate, samplesPerBlock, 2);
}

void SampleForgeAudioProcessor::releaseResources()
{
}

bool SampleForgeAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    return true;
}

void SampleForgeAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    auto totalNumInputChannels  = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear(i, 0, buffer.getNumSamples());

    // Mirrors the host's tempo for tempo-synced looping; holds its last
    // value when the host/standalone doesn't report one.
    if (auto* playHead = getPlayHead())
        if (auto position = playHead->getPosition())
            if (auto bpm = position->getBpm())
                currentBpm.store(static_cast<float>(*bpm));

    // Merges incoming clicks from the UI keyboard into the buffer stream
    keyboardState.processNextMidiBuffer(midiMessages, 0, buffer.getNumSamples(), true);

    // Render the active synth voices
    synth.renderNextBlock(buffer, midiMessages, 0, buffer.getNumSamples());
}

//==============================================================================
bool SampleForgeAudioProcessor::hasEditor() const
{
    return true;
}

juce::AudioProcessorEditor* SampleForgeAudioProcessor::createEditor()
{
    return new SampleForgeAudioProcessorEditor(*this);
}

//==============================================================================
std::unique_ptr<juce::XmlElement> SampleForgeAudioProcessor::buildStateXml(const juce::File& presetFolder)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());

    auto* zonesXml = xml->createNewChildElement("ZONES");
    for (int i = 0; i < synth.getNumSounds(); ++i)
    {
        if (auto* zone = getZone(i))
        {
            auto* zoneXml = zonesXml->createNewChildElement("ZONE");
            zoneXml->setAttribute("path", zone->filePath);
            if (presetFolder != juce::File())
                zoneXml->setAttribute("relPath", juce::File(zone->filePath).getRelativePathFrom(presetFolder));
            zoneXml->setAttribute("rootNote", zone->rootNote.load());
            zoneXml->setAttribute("keyLow", zone->keyLow.load());
            zoneXml->setAttribute("keyHigh", zone->keyHigh.load());
            zoneXml->setAttribute("velLow", zone->velLow.load());
            zoneXml->setAttribute("velHigh", zone->velHigh.load());
            zoneXml->setAttribute("sampleStart", static_cast<double>(zone->sampleStart.load()));
            zoneXml->setAttribute("sampleEnd", static_cast<double>(zone->sampleEnd.load()));
            zoneXml->setAttribute("reverse", zone->reverse.load());
            zoneXml->setAttribute("pan", static_cast<double>(zone->pan.load()));
            zoneXml->setAttribute("gain", static_cast<double>(zone->gain.load()));
            zoneXml->setAttribute("rrGroup", zone->rrGroup.load());
        }
    }

    return xml;
}

void SampleForgeAudioProcessor::restoreStateFromXml(const juce::XmlElement& xmlState, const juce::File& presetFolder)
{
    apvts.replaceState(juce::ValueTree::fromXml(xmlState));

    synth.clearSounds();
    selectedZoneIndex = -1;

    if (auto* zonesXml = xmlState.getChildByName("ZONES"))
    {
        for (auto* zoneXml = zonesXml->getChildByName("ZONE"); zoneXml != nullptr;
             zoneXml = zoneXml->getNextElementWithTagName("ZONE"))
        {
            juce::File file(zoneXml->getStringAttribute("path"));

            if (!file.existsAsFile() && presetFolder != juce::File())
            {
                auto relative = zoneXml->getStringAttribute("relPath");
                auto beside = relative.isNotEmpty() ? presetFolder.getChildFile(relative) : juce::File();
                auto sameFolder = presetFolder.getChildFile(file.getFileName());
                if (beside.existsAsFile())
                    file = beside;
                else if (sameFolder.existsAsFile())
                    file = sameFolder;
            }

            loadZoneInternal(file,
                              zoneXml->getIntAttribute("keyLow", 0),
                              zoneXml->getIntAttribute("keyHigh", 127),
                              zoneXml->getIntAttribute("velLow", 0),
                              zoneXml->getIntAttribute("velHigh", 127),
                              zoneXml->getIntAttribute("rootNote", 60),
                              static_cast<float>(zoneXml->getDoubleAttribute("sampleStart", 0.0)),
                              static_cast<float>(zoneXml->getDoubleAttribute("sampleEnd", 1.0)),
                              zoneXml->getBoolAttribute("reverse", false),
                              static_cast<float>(zoneXml->getDoubleAttribute("pan", 0.0)),
                              static_cast<float>(zoneXml->getDoubleAttribute("gain", 1.0)),
                              zoneXml->getIntAttribute("rrGroup", 0),
                              true);
        }
    }

    sendChangeMessage();
}

void SampleForgeAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    copyXmlToBinary(*buildStateXml(), destData);
}

void SampleForgeAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));

    if (xmlState == nullptr || !xmlState->hasTagName(apvts.state.getType()))
        return;

    restoreStateFromXml(*xmlState);
}

bool SampleForgeAudioProcessor::savePresetToFile(const juce::File& file)
{
    return buildStateXml(file.getParentDirectory())->writeTo(file);
}

bool SampleForgeAudioProcessor::loadPresetFromFile(const juce::File& file)
{
    auto xmlState = juce::parseXML(file);

    if (xmlState == nullptr || !xmlState->hasTagName(apvts.state.getType()))
        return false;

    restoreStateFromXml(*xmlState, file.getParentDirectory());
    return true;
}

//==============================================================================
// This creates new instances of the plugin
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SampleForgeAudioProcessor();
}
