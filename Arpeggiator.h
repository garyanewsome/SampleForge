#pragma once

#include <JuceHeader.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <bitset>
#include <cmath>

//==============================================================================
// A MIDI-level arpeggiator: held notes go in, a stepped stream of notes comes
// out, and the sampler plays that stream like any other MIDI. Everything is
// fixed-size and allocation-free so process() is safe on the audio thread.
namespace arp
{
    // Keep in sync with the "ARP_PATTERN" / "ARP_RATE" choice parameters.
    enum Pattern { Up = 0, Down, UpDown, DownUp, Random, AsPlayed, NumPatterns };

    inline const juce::StringArray& patternNames()
    {
        static const juce::StringArray names { "Up", "Down", "Up/Down", "Down/Up", "Random", "As Played" };
        return names;
    }
    inline const juce::StringArray& rateNames()
    {
        static const juce::StringArray names { "1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32" };
        return names;
    }
    // Length of one step in beats (quarter notes).
    inline double rateBeats(int index)
    {
        static const double beats[] = { 1.0, 0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125 };
        return beats[juce::jlimit(0, 5, index)];
    }

    // Per-step lane. The arp's step counter (1 per arp tick, from the moment the
    // arp starts running) picks entry `counter % numSteps`; it is independent of
    // which pitch is playing. The defaults leave the arp exactly as it was before
    // the lane existed.
    inline constexpr int maxSteps = 16;
    struct Step
    {
        float velocity = 1.0f; // 0..1, multiplies the played velocity (1 = as played)
        int zone = 0;          // 0 = normal key/velocity routing, n = play only zone n (1-based)
        bool reverse = false;  // flip the zone's own Reverse setting for this hit
        bool tie = false;      // hold the previous note into this step instead of retriggering
    };

    // Per-note-on instruction for the sampler (see SampleForgeSynthesiser::noteOn).
    // Only emitted for steps that ask for a zone or a reversal.
    struct NoteTag { int note = 0; int zone = 0; bool reverse = false; };

    struct Settings
    {
        bool on = false;
        int pattern = Up;
        int rate = 3;          // 1/16
        int octaves = 1;       // 1..4
        float gate = 0.6f;     // fraction of a step the note is held
        float swing = 0.0f;    // 0..1: delays every second step by up to half a step
        bool latch = false;    // keep playing after the keys are released
        int numSteps = maxSteps;                // 1..maxSteps: length of the per-step lane
        std::array<Step, maxSteps> steps {};
    };

    class Arpeggiator
    {
    public:
        static constexpr int maxHeld = 32;
        static constexpr int maxSeq = 256;
        static constexpr int displayCap = 32;

        // Read from the UI thread to draw the pattern.
        std::atomic<int> displayLength { 0 };
        std::atomic<int> displayIndex { -1 };
        std::atomic<int> displayNotes[displayCap];

        // Tags for the note-ons in the buffer filled by the latest process() call,
        // in the order they were emitted. Cleared at the start of each call.
        static constexpr int maxTags = 512;
        int numTags() const { return tagCount; }
        const NoteTag& tag(int i) const { return tags[(size_t) i]; }
        std::atomic<int> displayStep { -1 };   // lane position of the step that last sounded

        Arpeggiator() { for (auto& n : displayNotes) n.store(-1); }

        void reset()
        {
            numHeld = 0;
            physical.reset();
            passthrough.reset();
            running = false;
            soundingNote = -1;
            stepAt = gateOffAt = 0.0;
            stepIndex = stepCounter = 0;
            seqLen = 0;
            seqDirty = true;
            wasOn = false;
            wasLatch = false;
            orderCounter = 0;
            displayLength.store(0);
            displayIndex.store(-1);
            displayStep.store(-1);
            tagCount = 0;
        }

        // Consumes `in` (a block of incoming MIDI) and fills `out`. With the arp
        // off everything passes straight through. hostPpq < 0 means unknown.
        void process(const juce::MidiBuffer& in, juce::MidiBuffer& out, int numSamples, double sampleRate,
                     const Settings& s, double bpm, bool hostPlaying, double hostPpq)
        {
            out.clear();
            tagCount = 0;
            if (numSamples <= 0)
                return;
            if (bpm <= 0.0) bpm = 120.0;
            if (sampleRate <= 0.0) sampleRate = 44100.0;

            auto emit = [&](const juce::MidiMessage& m, double t) {
                out.addEvent(m, juce::jlimit(0, numSamples - 1, (int) std::floor(t)));
            };
            auto noteOffMsg = [&](int note) { return juce::MidiMessage::noteOff(channel, note); };

            const double stepBeats = rateBeats(s.rate);
            const double stepSamples = juce::jmax(1.0, stepBeats * 60.0 / bpm * sampleRate);

            // Settings changes between blocks.
            if (wasOn && !s.on)
            {
                if (soundingNote >= 0) emit(noteOffMsg(soundingNote), 0.0);
                soundingNote = -1;
                running = false;
                numHeld = 0;
                physical.reset();
                seqLen = 0;
                displayLength.store(0);
                displayIndex.store(-1);
            }
            wasOn = s.on;

            if (s.on && wasLatch && !s.latch)
            {
                // Latch released: keep only the keys that are really down.
                int kept = 0;
                for (int i = 0; i < numHeld; ++i)
                    if (physical[(size_t) held[(size_t) i].note])
                        held[(size_t) kept++] = held[(size_t) i];
                numHeld = kept;
                seqDirty = true;
                if (numHeld == 0 && running)
                    stop(0.0, out, numSamples);
            }
            wasLatch = s.latch;

            if (s.pattern != lastPattern || s.octaves != lastOctaves)
            {
                lastPattern = s.pattern;
                lastOctaves = s.octaves;
                seqDirty = true;
            }

            auto it = in.begin();
            const auto end = in.end();

            for (;;)
            {
                const double tInput = it != end ? (double) (*it).samplePosition : 1.0e18;
                const double tStep = running ? stepAt : 1.0e18;
                const double tGate = soundingNote >= 0 ? gateOffAt : 1.0e18;
                const double t = std::min({ tInput, tStep, tGate });
                if (t >= (double) numSamples)
                    break;
                const double now = std::max(0.0, t);

                if (tGate <= t)
                {
                    emit(noteOffMsg(soundingNote), now);
                    soundingNote = -1;
                }
                else if (tInput <= t)
                {
                    const auto m = (*it).getMessage();
                    ++it;
                    channel = m.getChannel() > 0 ? m.getChannel() : channel;
                    handleInput(m, now, s, hostPlaying, hostPpq, stepBeats, stepSamples, bpm, sampleRate, out, numSamples);
                }
                else
                {
                    doStep(now, s, stepSamples, out, numSamples);
                }
            }

            stepAt -= numSamples;
            gateOffAt -= numSamples;
        }

    private:
        struct Held { int note = 0; float vel = 0.0f; unsigned order = 0; };
        struct SeqNote { int note = 0; float vel = 0.0f; };

        void handleInput(const juce::MidiMessage& m, double t, const Settings& s, bool hostPlaying, double hostPpq,
                         double stepBeats, double stepSamples, double bpm, double sampleRate,
                         juce::MidiBuffer& out, int numSamples)
        {
            auto emit = [&](const juce::MidiMessage& msg) {
                out.addEvent(msg, juce::jlimit(0, numSamples - 1, (int) std::floor(t)));
            };

            if (m.isNoteOn())
            {
                const int note = m.getNoteNumber();
                if (!s.on)
                {
                    emit(m);
                    passthrough.set((size_t) note);
                    return;
                }

                const bool firstKey = physical.none();
                physical.set((size_t) note);
                if (s.latch && firstKey)
                    numHeld = 0; // a fresh chord after everything was released replaces the latched one
                addHeld(note, m.getFloatVelocity());
                seqDirty = true;

                if (!running)
                {
                    running = true;
                    stepIndex = 0;
                    stepCounter = 0;
                    double offset = 0.0;
                    if (hostPlaying && hostPpq >= 0.0)
                    {
                        // Start on the host's grid so the arp is in time with the song.
                        const double ppqNow = hostPpq + t * bpm / 60.0 / sampleRate;
                        const double k = ppqNow / stepBeats;
                        const double frac = k - std::floor(k);
                        if (frac > 1.0e-6 && frac < 1.0 - 1.0e-6)
                            offset = (1.0 - frac) * stepSamples;
                    }
                    stepAt = t + offset;
                }
            }
            else if (m.isNoteOff())
            {
                const int note = m.getNoteNumber();
                if (passthrough[(size_t) note])
                {
                    emit(m);
                    passthrough.reset((size_t) note);
                    return;
                }
                if (!s.on)
                {
                    emit(m);
                    return;
                }

                physical.reset((size_t) note);
                if (!s.latch)
                {
                    removeHeld(note);
                    seqDirty = true;
                    if (numHeld == 0 && running)
                        stop(t, out, numSamples);
                }
            }
            else if (m.isAllNotesOff() || m.isAllSoundOff())
            {
                emit(m);
                if (soundingNote >= 0) soundingNote = -1;
                numHeld = 0;
                physical.reset();
                passthrough.reset();
                running = false;
                seqDirty = true;
            }
            else
            {
                emit(m);
            }
        }

        void stop(double t, juce::MidiBuffer& out, int numSamples)
        {
            if (soundingNote >= 0)
                out.addEvent(juce::MidiMessage::noteOff(channel, soundingNote), juce::jlimit(0, numSamples - 1, (int) std::floor(t)));
            soundingNote = -1;
            running = false;
            displayIndex.store(-1);
        }

        void doStep(double t, const Settings& s, double stepSamples, juce::MidiBuffer& out, int numSamples)
        {
            if (numHeld == 0)
            {
                stop(t, out, numSamples);
                return;
            }
            if (seqDirty)
                rebuildSequence(s);
            if (seqLen == 0)
            {
                stop(t, out, numSamples);
                return;
            }

            const int pos = juce::jlimit(0, numSamples - 1, (int) std::floor(t));
            const int laneLen = juce::jlimit(1, maxSteps, s.numSteps);
            const int lanePos = stepCounter % laneLen;
            const Step& st = s.steps[(size_t) lanePos];
            const bool nextTied = s.steps[(size_t) ((stepCounter + 1) % laneLen)].tie;
            displayStep.store(lanePos);

            // Swing: every second step starts later, and the step before it is longer.
            const double swing = juce::jlimit(0.0f, 1.0f, s.swing) * 0.5;
            const double nextStepAt = t + stepSamples * (1.0 + ((stepCounter & 1) == 0 ? swing : -swing));
            const double gateLen = juce::jlimit(0.02f, 1.0f, s.gate) * stepSamples;

            if (st.tie && soundingNote >= 0)
            {
                // Tie: the previous note carries on through this step. No new note-on,
                // and the pitch sequence does not advance. It ends at this step's gate
                // (or runs on again if the next step is tied as well).
                gateOffAt = nextTied ? 1.0e18 : t + gateLen;
                stepAt = nextStepAt;
                ++stepCounter;
                return;
            }

            if (soundingNote >= 0)
            {
                out.addEvent(juce::MidiMessage::noteOff(channel, soundingNote), pos);
                soundingNote = -1;
            }

            int index;
            if (s.pattern == Random)
                index = random.nextInt(seqLen);
            else
            {
                index = stepIndex % seqLen;
                stepIndex = (stepIndex + 1) % seqLen;
            }

            const auto& n = seq[(size_t) index];
            float vel = n.vel;
            if (st.velocity < 1.0f)
                vel = juce::jmax(1.0f / 127.0f, vel * juce::jmax(0.0f, st.velocity));
            if ((st.zone > 0 || st.reverse) && tagCount < maxTags)
                tags[(size_t) tagCount++] = { n.note, st.zone, st.reverse };
            out.addEvent(juce::MidiMessage::noteOn(channel, n.note, vel), pos);
            soundingNote = n.note;
            gateOffAt = nextTied ? 1.0e18 : t + gateLen;
            displayIndex.store(index);

            stepAt = nextStepAt;
            ++stepCounter;
        }

        void addHeld(int note, float vel)
        {
            for (int i = 0; i < numHeld; ++i)
                if (held[(size_t) i].note == note)
                {
                    held[(size_t) i].vel = vel;
                    return;
                }
            if (numHeld < maxHeld)
                held[(size_t) numHeld++] = { note, vel, orderCounter++ };
        }

        void removeHeld(int note)
        {
            for (int i = 0; i < numHeld; ++i)
                if (held[(size_t) i].note == note)
                {
                    for (int j = i; j < numHeld - 1; ++j)
                        held[(size_t) j] = held[(size_t) j + 1];
                    --numHeld;
                    return;
                }
        }

        void rebuildSequence(const Settings& s)
        {
            seqDirty = false;

            std::array<Held, maxHeld> base;
            for (int i = 0; i < numHeld; ++i) base[(size_t) i] = held[(size_t) i];
            if (s.pattern == AsPlayed)
                std::sort(base.begin(), base.begin() + numHeld, [](const Held& a, const Held& b) { return a.order < b.order; });
            else
                std::sort(base.begin(), base.begin() + numHeld, [](const Held& a, const Held& b) { return a.note < b.note; });

            // The chord repeated up through the octave range.
            std::array<SeqNote, maxSeq> lst;
            int n = 0;
            for (int o = 0; o < juce::jlimit(1, 4, s.octaves); ++o)
                for (int i = 0; i < numHeld; ++i)
                {
                    const int note = base[(size_t) i].note + 12 * o;
                    if (note <= 127 && n < maxSeq)
                        lst[(size_t) n++] = { note, base[(size_t) i].vel };
                }

            seqLen = 0;
            auto push = [&](const SeqNote& x) { if (seqLen < maxSeq) seq[(size_t) seqLen++] = x; };
            switch (s.pattern)
            {
                case Down:
                    for (int i = n - 1; i >= 0; --i) push(lst[(size_t) i]);
                    break;
                case UpDown:
                    for (int i = 0; i < n; ++i) push(lst[(size_t) i]);
                    for (int i = n - 2; i >= 1; --i) push(lst[(size_t) i]);
                    break;
                case DownUp:
                    for (int i = n - 1; i >= 0; --i) push(lst[(size_t) i]);
                    for (int i = 1; i <= n - 2; ++i) push(lst[(size_t) i]);
                    break;
                default: // Up, Random and As Played all use the list in order
                    for (int i = 0; i < n; ++i) push(lst[(size_t) i]);
                    break;
            }

            if (seqLen > 0)
                stepIndex %= seqLen;
            else
                stepIndex = 0;

            const int shown = juce::jmin(seqLen, displayCap);
            for (int i = 0; i < displayCap; ++i)
                displayNotes[i].store(i < shown ? seq[(size_t) i].note : -1);
            displayLength.store(shown);
        }

        std::array<Held, maxHeld> held;
        int numHeld = 0;
        unsigned orderCounter = 0;
        std::bitset<128> physical, passthrough;

        std::array<SeqNote, maxSeq> seq;
        int seqLen = 0;
        bool seqDirty = true;
        int stepIndex = 0, stepCounter = 0;

        bool running = false;
        double stepAt = 0.0;
        int soundingNote = -1;
        double gateOffAt = 0.0;
        int channel = 1;

        std::array<NoteTag, maxTags> tags;
        int tagCount = 0;

        bool wasOn = false, wasLatch = false;
        int lastPattern = -1, lastOctaves = -1;
        juce::Random random { 12345 };
    };
}
