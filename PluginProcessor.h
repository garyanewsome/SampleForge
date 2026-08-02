#pragma once

#include <JuceHeader.h>
#include <rubberband/RubberBandStretcher.h>

//==============================================================================
// Holds the decoded sample data plus the key/velocity range and per-zone trim
// that make it a "zone" in the multi-sample map. appliesToNote() is what lets
// juce::Synthesiser route a MIDI note only to zones whose keyLow..keyHigh
// covers it; velocity range is checked in SampleForgeVoice::startNote since
// SynthesiserSound has no velocity hook of its own.
class SampleForgeSound : public juce::SynthesiserSound
{
public:
    SampleForgeSound(juce::AudioBuffer<float> bufferToUse, double sourceRate, juce::String sourceFilePath)
        : data(std::move(bufferToUse)), sourceSampleRate(sourceRate), filePath(std::move(sourceFilePath))
    {
    }

    bool appliesToNote(int midiNoteNumber) override
    {
        // rrSuppressed is set by SampleForgeSynthesiser::noteOn() just before this
        // is checked, to make round-robin/random cycling pick exactly one zone
        // per group per hit instead of layering every zone in the group.
        if (rrSuppressed.load())
            return false;

        return midiNoteNumber >= keyLow.load() && midiNoteNumber <= keyHigh.load();
    }
    bool appliesToChannel(int) override { return true; }

    juce::AudioBuffer<float> data;
    double sourceSampleRate = 44100.0;
    juce::String filePath;

    // Built in loadZoneInternal() once the AudioFormatManager/AudioThumbnailCache
    // are available; draws and caches its own peak data independently of `data`,
    // so the editor never has to rescan samples itself.
    std::unique_ptr<juce::AudioThumbnail> thumbnail;

    std::atomic<int> rootNote { 60 };
    std::atomic<int> keyLow { 0 };
    std::atomic<int> keyHigh { 127 };
    std::atomic<int> velLow { 0 };
    std::atomic<int> velHigh { 127 };
    std::atomic<float> sampleStart { 0.0f };
    std::atomic<float> sampleEnd { 1.0f };
    std::atomic<bool> reverse { false };

    // Per-zone balance/level on top of the global GAIN parameter, so a
    // multisampled instrument's zones can be leveled/placed against each other.
    std::atomic<float> pan { 0.0f };  // -1 (left) .. 0 (centre) .. 1 (right)
    std::atomic<float> gain { 1.0f }; // 0 .. 1, unity by default

    // 0 = not part of a round-robin group (always independently eligible, the
    // pre-existing behaviour). 1..maxRRGroups-1 = grouped with any other zone
    // sharing this id; SampleForgeSynthesiser::noteOn() picks one member of a
    // group to actually sound per hit instead of layering all of them.
    std::atomic<int> rrGroup { 0 };
    std::atomic<bool> rrSuppressed { false };
};

//==============================================================================
// Global (not per-zone) voice settings: envelope, gain, and loop mode apply
// across every zone. Key/velocity range, root note, and trim live on the
// SampleForgeSound the voice is currently playing instead.
struct VoiceParams
{
    std::atomic<float>* attack = nullptr;
    std::atomic<float>* decay = nullptr;
    std::atomic<float>* sustain = nullptr;
    std::atomic<float>* release = nullptr;
    std::atomic<float>* gain = nullptr;
    std::atomic<float>* loopMode = nullptr;
    std::atomic<float>* filterType = nullptr;
    std::atomic<float>* filterCutoff = nullptr;
    std::atomic<float>* filterResonance = nullptr;
    std::atomic<float>* filterEnvAmount = nullptr;

    // Off = the loop's original free-running length (trim End - trim Start).
    // Otherwise the loop wraps/bounces at a tempo-derived length instead
    // (never past the trim End) — see LoopSyncRate.
    std::atomic<float>* loopSyncRate = nullptr;

    // Mirrors the processor's live host-tempo reading (see
    // SampleForgeAudioProcessor::processBlock); not an APVTS parameter.
    std::atomic<float>* tempoBpm = nullptr;

    // 1.0 = bypass (the original resampling-based playback path, unchanged).
    // Anything else routes through RubberBandStretcher instead — see
    // SampleForgeVoice::renderNextBlock. Only supported for non-looping,
    // non-reversed playback; see the scope note there.
    std::atomic<float>* stretchRatio = nullptr;
};

// Keep in sync with the "LOOP_MODE" AudioParameterChoice's string array.
enum class LoopMode { off = 0, forward = 1, pingPong = 2 };

// Keep in sync with the "FILTER_TYPE" AudioParameterChoice's string array.
enum class FilterType { lowpass = 0, highpass = 1, bandpass = 2 };

// Keep in sync with the "LOOP_SYNC" AudioParameterChoice's string array.
enum class LoopSyncRate { off = 0, whole = 1, half = 2, quarter = 3, eighth = 4, sixteenth = 5 };

//==============================================================================
class SampleForgeVoice : public juce::SynthesiserVoice
{
public:
    SampleForgeVoice(const VoiceParams& params);

    bool canPlaySound(juce::SynthesiserSound* sound) override;
    void startNote(int midiNoteNumber, float velocity, juce::SynthesiserSound* sound, int currentPitchWheelPosition) override;
    void stopNote(float velocity, bool allowTailOff) override;
    void pitchWheelMoved(int newPitchWheelValue) override;
    void controllerMoved(int controllerNumber, int newControllerValue) override;

    // Real-Time Safe audio render block (no heap allocs, sys calls, or locks)
    void renderNextBlock(juce::AudioBuffer<float>& outputBuffer, int startSample, int numSamples) override;

    // Called once from the processor's prepareToPlay(), since SynthesiserVoice
    // has no prepare hook of its own for the juce::dsp filter or the
    // RubberBandStretcher to latch onto.
    void prepareVoice(double sampleRate, int samplesPerBlock, int numChannels);

private:
    // Pulls `numSamples` of time/pitch-stretched audio into stretchOutputScratch,
    // feeding more source material into `stretcher` in fixed-size chunks as
    // needed. Returns false once the source (Start..End) is exhausted and the
    // stretcher has fully drained (available() == -1) — the note is over.
    bool fillStretchedBlock(int numSamples);

    SampleForgeSound* currentSound = nullptr;
    double sourcePosition = 0.0;
    double pitchRatio = 1.0;
    double startSampleInSource = 0.0;
    double endSampleInSource = 0.0;
    int loopDirection = 1;
    float level = 0.0f;
    float lastEnvValue = 0.0f;
    juce::ADSR adsr;
    juce::dsp::StateVariableTPTFilter<float> filter;

    // Only engaged when Stretch != 1x (see renderNextBlock); the common case
    // (Stretch == 1x) uses the original resampling path unchanged, at its
    // original CPU cost. Feeding is capped at stretchChunkSize per
    // process() call — matching setMaxProcessSize() — which per Rubber
    // Band's own documentation is what keeps it allocation-free on the
    // audio thread.
    std::unique_ptr<RubberBand::RubberBandStretcher> stretcher;
    std::vector<float> stretchInputScratch[2];
    std::vector<float> stretchOutputScratch[2];
    bool stretcherFinalSent = false;
    static constexpr int stretchChunkSize = 1024;

    VoiceParams voiceParams;
};

// Keep in sync with the "RR_ORDER" AudioParameterChoice's string array.
enum class RRMode { sequential = 0, random = 1 };

//==============================================================================
// juce::Synthesiser's default noteOn() triggers every sound whose
// appliesToNote()/appliesToChannel() match, which is exactly what velocity
// switching wants (each candidate self-rejects by velocity in startNote) but
// wrong for round-robin: zones sharing a group should alternate, not layer.
// Overriding noteOn() is what lets this hook in at the right moment — it's
// called once per actual note-on event at the correct sample-accurate offset
// by the base class's own MIDI dispatch, unlike anything done once per block.
class SampleForgeSynthesiser : public juce::Synthesiser
{
public:
    void noteOn(int midiChannel, int midiNoteNumber, float velocity) override;

    std::atomic<float>* rrOrderParam = nullptr;

private:
    static constexpr int maxRRGroups = 16;
    static constexpr int maxGroupMembers = 16;

    // Fixed-size, no heap allocation: noteOn() runs on the audio thread.
    int rrNextIndex[maxRRGroups] = {};
    juce::Random random;
};

//==============================================================================
class SampleForgeAudioProcessor  : public juce::AudioProcessor,
                                    public juce::ChangeBroadcaster
{
public:
    //==============================================================================
    SampleForgeAudioProcessor();
    ~SampleForgeAudioProcessor() override;

    //==============================================================================
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;

    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    //==============================================================================
    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    //==============================================================================
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String& newName) override;

    //==============================================================================
    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // Writes/reads the entire instrument (every zone plus the global
    // envelope/loop/filter settings) as a standalone file, reusing the exact
    // same XML the host session state uses — a preset is just that XML
    // living in its own file instead of embedded in a DAW project.
    bool savePresetToFile(const juce::File& file);
    bool loadPresetFromFile(const juce::File& file);

    // Decodes an audio file on the message thread and adds it as a new zone
    // (full key/velocity range by default), selecting it. juce::Synthesiser
    // guards addSound()/removeSound() with its own internal lock, so this is
    // safe to call while the audio thread is mid-renderNextBlock.
    void addZoneFromFile(const juce::File& file);

    // Removes a zone by index. Any voice currently playing it keeps it alive
    // via the base class's ref-counted currentlyPlayingSound until it finishes.
    void removeZone(int index);

    int getNumZones() const;
    SampleForgeSound* getZone(int index) const;

    int getSelectedZoneIndex() const { return selectedZoneIndex; }
    void setSelectedZoneIndex(int index);

    juce::String getSelectedZoneFileName() const;

    // ValueTreeState for atomic parameter access (Thread-Safe & Real-Time Safe)
    juce::AudioProcessorValueTreeState apvts;

    // Tracks on-screen piano clicks
    juce::MidiKeyboardState keyboardState;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // Shared by addZoneFromFile() and the state-restore path used by both
    // setStateInformation() and loadPresetFromFile(), which supply the saved
    // key/vel/root/trim/reverse/pan/gain/rrGroup instead of these defaults.
    bool loadZoneInternal(const juce::File& file, int keyLow, int keyHigh, int velLow, int velHigh,
                          int rootNote, float sampleStart, float sampleEnd,
                          bool reverse = false, float pan = 0.0f, float gain = 1.0f, int rrGroup = 0);

    // Shared by getStateInformation()/savePresetToFile() and by
    // setStateInformation()/loadPresetFromFile() respectively — a "preset"
    // is just this same state XML written to/read from a standalone file
    // instead of embedded in the host's session.
    std::unique_ptr<juce::XmlElement> buildStateXml();
    void restoreStateFromXml(const juce::XmlElement& xmlState);

    // Declared before `synth` on purpose: members are destroyed in reverse
    // declaration order, and each zone's AudioThumbnail (owned via `synth`'s
    // sounds) references these, so they must outlive `synth`'s teardown.
    juce::AudioFormatManager formatManager;
    juce::AudioThumbnailCache thumbnailCache { 8 };

    SampleForgeSynthesiser synth;

    int selectedZoneIndex = -1;

    // Updated from getPlayHead() at the top of processBlock(); read live by
    // voices for tempo-synced looping. Holds its last value (default 120)
    // when the host/standalone doesn't report a tempo.
    std::atomic<float> currentBpm { 120.0f };

    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SampleForgeAudioProcessor)
};
