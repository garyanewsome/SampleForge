// Headless tests for SampleForge. Build and run:
//   cmake --build cmake-build --target SampleForgeTests
//   cmake-build/SampleForgeTests_artefacts/Release/SampleForgeTests
#include <JuceHeader.h>
#include <cmath>
#include <iostream>

#include "Arpeggiator.h"
#include "PluginEditor.h"
#include "PluginProcessor.h"

namespace {

int failures = 0;
void check(bool ok, const juce::String& what)
{
    std::cout << (ok ? "  PASS  " : "  FAIL  ") << what << std::endl;
    if (!ok) ++failures;
}

juce::File tempDir()
{
    auto d = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("SampleForgeTests");
    d.createDirectory();
    return d;
}

// kind: 0 = sine at `freq`, 1 = constant `freq` (used as a DC level)
juce::File makeWav(const juce::String& name, double rate, int length, int kind, float value)
{
    auto file = tempDir().getChildFile(name);
    file.deleteFile();
    juce::AudioBuffer<float> b(2, length);
    for (int i = 0; i < length; ++i)
    {
        float v = kind == 0 ? 0.5f * (float) std::sin(2.0 * juce::MathConstants<double>::pi * value * i / rate) : value;
        b.setSample(0, i, v);
        b.setSample(1, i, v);
    }
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::FileOutputStream> out(file.createOutputStream());
    std::unique_ptr<juce::AudioFormatWriter> w(wav.createWriterFor(out.get(), rate, 2, 24, {}, 0));
    out.release();
    w->writeFromAudioSampleBuffer(b, 0, length);
    return file;
}

void setParam(SampleForgeAudioProcessor& p, const char* id, float v)
{
    auto* prm = p.apvts.getParameter(id);
    prm->setValueNotifyingHost(prm->convertTo0to1(v));
}

struct Rig
{
    SampleForgeAudioProcessor proc;
    double sr;
    int block;
    Rig(double rate = 48000.0, int b = 256) : sr(rate), block(b)
    {
        proc.setPlayConfigDetails(0, 2, sr, block);
        proc.prepareToPlay(sr, block);
    }
    // Renders `blocks` blocks; `noteOns` are fired at the start of the first block.
    std::vector<float> render(int blocks, const std::vector<std::pair<int, float>>& noteOns = {},
                              const std::vector<std::pair<int, juce::MidiMessage>>& extra = {})
    {
        std::vector<float> out;
        juce::AudioBuffer<float> buf(2, block);
        for (int i = 0; i < blocks; ++i)
        {
            buf.clear();
            juce::MidiBuffer midi;
            for (auto& e : extra)
                if (e.first == i) midi.addEvent(e.second, 0);   // `first` = block index
            if (i == 0)
                for (auto& n : noteOns) midi.addEvent(juce::MidiMessage::noteOn(1, n.first, n.second), 0);
            proc.processBlock(buf, midi);
            for (int s = 0; s < block; ++s) out.push_back(buf.getSample(0, s));
        }
        return out;
    }
};

float peak(const std::vector<float>& v)
{
    float p = 0;
    for (float x : v) p = std::max(p, std::abs(x));
    return p;
}

// Index just past the last sample louder than `thresh`.
int lengthAbove(const std::vector<float>& v, float thresh)
{
    for (int i = (int) v.size() - 1; i >= 0; --i)
        if (std::abs(v[(size_t) i]) > thresh) return i + 1;
    return 0;
}

void pumpMessages(int ms)
{
    auto end = juce::Time::getMillisecondCounter() + (juce::uint32) ms;
    while (juce::Time::getMillisecondCounter() < end)
        juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
}

} // namespace

// --snapshot out.png : renders the editor offscreen with a few demo zones loaded.
int snapshot(const juce::File& png, int page)
{
    auto demo = [](const juce::String& name, double rate, int length, std::function<float(double, double)> fn) {
        auto file = tempDir().getChildFile(name);
        file.deleteFile();
        juce::AudioBuffer<float> b(2, length);
        for (int i = 0; i < length; ++i) { float v = fn(i / rate, (double) i / length); b.setSample(0, i, v); b.setSample(1, i, v); }
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::FileOutputStream> out(file.createOutputStream());
        std::unique_ptr<juce::AudioFormatWriter> w(wav.createWriterFor(out.get(), rate, 2, 24, {}, 0));
        out.release();
        w->writeFromAudioSampleBuffer(b, 0, length);
        return file;
    };
    const double tau = juce::MathConstants<double>::twoPi;
    auto pad = demo("Warm Pad C3.wav", 48000, 48000 * 3, [&](double t, double u) {
        double env = std::sin(juce::MathConstants<double>::pi * u);
        return (float) (0.45 * env * (std::sin(tau * 130.8 * t) + 0.6 * std::sin(tau * 196.0 * t) + 0.35 * std::sin(tau * 261.6 * t + 1.0 * std::sin(tau * 3.0 * t)))); });
    auto pluck = demo("Pluck E2.wav", 48000, 48000 * 2, [&](double t, double) {
        return (float) (0.8 * std::exp(-t * 3.0) * (std::sin(tau * 82.4 * t) + 0.5 * std::sin(tau * 164.8 * t) + 0.25 * std::sin(tau * 247.2 * t))); });
    auto hit = demo("Snare Hit.wav", 44100, 44100, [&](double t, double) {
        return (float) (0.9 * std::exp(-t * 14.0) * (0.6 * std::sin(tau * 190.0 * t) + 0.9 * ((std::fmod(t * 7919.0, 1.0)) - 0.5))); });

    SampleForgeAudioProcessor proc;
    proc.addZoneFromFile(pluck); proc.waitForLoads(); proc.getZone(0)->keyLow = 28; proc.getZone(0)->keyHigh = 47; proc.getZone(0)->rootNote = 40; proc.getZone(0)->rrGroup = 1;
    proc.addZoneFromFile(hit); proc.waitForLoads();   proc.getZone(1)->keyLow = 48; proc.getZone(1)->keyHigh = 59; proc.getZone(1)->rootNote = 54; proc.getZone(1)->velHigh = 90;
    proc.addZoneFromFile(pad); proc.waitForLoads();   proc.getZone(2)->keyLow = 60; proc.getZone(2)->keyHigh = 83; proc.getZone(2)->rootNote = 60; proc.getZone(2)->sampleStart = 0.08f; proc.getZone(2)->sampleEnd = 0.86f;
    proc.setSelectedZoneIndex(2);
    setParam(proc, "SUSTAIN", 0.7f); setParam(proc, "RELEASE", 0.8f); setParam(proc, "FILTER_CUTOFF", 4200.0f);
    setParam(proc, "FILTER_RESONANCE", 1.4f); setParam(proc, "FILTER_ENV_AMOUNT", 1.0f); setParam(proc, "STRETCH", 1.5f); setParam(proc, "LOOP_MODE", 1.0f);
    proc.apvts.state.setProperty("uiPage", page, nullptr);
    if (page == 2)
    {
        setParam(proc, "ARP_ON", 1.0f); setParam(proc, "ARP_PATTERN", 2.0f); setParam(proc, "ARP_RATE", 3.0f);
        setParam(proc, "ARP_OCTAVES", 2.0f); setParam(proc, "ARP_GATE", 0.7f); setParam(proc, "ARP_SWING", 0.3f);
        proc.arpLane[1].velocity = 0.5f; proc.arpLane[2].tie = true; proc.arpLane[3].zone = 2; proc.arpLane[5].reverse = true; proc.arpLane[6].velocity = 0.7f;
        proc.arpNumSteps = 12;
        for (int n : { 60, 64, 67, 71 }) proc.keyboardState.noteOn(1, n, 0.8f);
        proc.setPlayConfigDetails(0, 2, 48000.0, 512);
        proc.prepareToPlay(48000.0, 512);
        juce::AudioBuffer<float> buf(2, 512);
        for (int i = 0; i < 40; ++i) { juce::MidiBuffer m; proc.processBlock(buf, m); }
    }
    else
    {
        proc.keyboardState.noteOn(1, 64, 0.8f);
        proc.keyboardState.noteOn(1, 67, 0.8f);
    }

    std::unique_ptr<juce::AudioProcessorEditor> ed(proc.createEditor());
    ed->setBounds(0, 0, 1280, 690);
    pumpMessages(600);
    auto img = ed->createComponentSnapshot(ed->getLocalBounds(), true, 1.0f);
    png.deleteFile();
    juce::PNGImageFormat fmt;
    std::unique_ptr<juce::FileOutputStream> out(png.createOutputStream());
    fmt.writeImageToStream(img, *out);
    std::cout << "wrote " << png.getFullPathName() << std::endl;
    return 0;
}

// FNV-1a over every note event an arp produces for a fixed scenario. The expected
// values below were captured from the arpeggiator BEFORE the per-step lane existed.
juce::uint64 arpGoldenHash(int scenario)
{
    using namespace arp;
    Settings s; s.on = true;
    s.gate = 0.55f; s.swing = 0.0f; s.octaves = 1; s.pattern = Up; s.rate = 3; s.latch = false;
    int block = 512; double bpm = 120.0;
    struct Ev { int pos; int note; bool on; };
    std::vector<Ev> input { { 0, 64, true }, { 0, 60, true }, { 0, 67, true }, { 5000, 71, true }, { 30000, 64, false }, { 52000, 60, false }, { 60000, 71, false } };
    switch (scenario)
    {
        case 0: break;
        case 1: s.pattern = Down; s.octaves = 2; break;
        case 2: s.pattern = UpDown; s.swing = 0.4f; s.gate = 1.0f; break;
        case 3: s.pattern = DownUp; s.rate = 4; s.octaves = 3; bpm = 97.0; block = 64; break;
        case 4: s.pattern = AsPlayed; s.rate = 1; s.gate = 0.1f; block = 2048; break;
        case 5: s.pattern = arp::Random; s.octaves = 2; break;
        case 6: s.latch = true; s.swing = 1.0f; break;
        default: break;
    }
    Arpeggiator a; a.reset();
    juce::MidiBuffer in, out;
    juce::uint64 h = 1469598103934665603ull;
    auto mix = [&](juce::uint64 v) { h ^= v; h *= 1099511628211ull; };
    for (int start = 0; start < 96000; start += block)
    {
        in.clear();
        for (auto& e : input)
            if (e.pos >= start && e.pos < start + block)
                in.addEvent(e.on ? juce::MidiMessage::noteOn(1, e.note, 0.8f) : juce::MidiMessage::noteOff(1, e.note), e.pos - start);
        a.process(in, out, block, 48000.0, s, bpm, false, -1.0);
        for (const auto meta : out)
        {
            auto m = meta.getMessage();
            mix((juce::uint64) (start + meta.samplePosition));
            mix((juce::uint64) m.getNoteNumber());
            mix((juce::uint64) (m.isNoteOn() ? m.getVelocity() + 1000 : 1));
        }
    }
    return h;
}

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;

    if (argc >= 3 && juce::String(argv[1]) == "--snapshot")
        return snapshot(juce::File(argv[2]), argc >= 4 ? juce::String(argv[3]).getIntValue() : 0);

    {
        std::cout << "[1] Velocity layers never steal voices from sounding notes\n";
        auto loud = makeWav("loud.wav", 48000, 96000, 0, 440.0f);
        auto soft = makeWav("soft.wav", 48000, 96000, 0, 330.0f);
        Rig r;
        setParam(r.proc, "LOOP_MODE", 1.0f);
        r.proc.addZoneFromFile(loud); r.proc.waitForLoads();   // matches high velocity, added first
        r.proc.addZoneFromFile(soft); r.proc.waitForLoads();   // matches low velocity, added second
        r.proc.getZone(0)->velLow = 64;
        r.proc.getZone(1)->velHigh = 63;
        std::vector<std::pair<int, float>> notes;
        for (int i = 0; i < 16; ++i) notes.push_back({ 40 + i, 0.8f });
        r.render(4, notes);
        check(r.proc.getActiveVoiceCount() == 16, "16 loud notes keep all 16 voices sounding (got " + juce::String(r.proc.getActiveVoiceCount()) + ")");

        Rig q;
        setParam(q.proc, "LOOP_MODE", 1.0f);
        q.proc.addZoneFromFile(loud); q.proc.waitForLoads();
        q.proc.addZoneFromFile(soft); q.proc.waitForLoads();
        q.proc.getZone(0)->velLow = 64;
        q.proc.getZone(1)->velHigh = 63;
        auto lo = q.render(4, { { 60, 0.15f } });
        check(q.proc.getActiveVoiceCount() == 1 && peak(lo) > 0.01f, "a soft note plays only the soft layer (one voice)");
    }

    {
        std::cout << "[2] Stretch changes length, not pitch, whatever the file's sample rate\n";
        for (double fileRate : { 44100.0, 48000.0 })
        {
            auto f = makeWav("tone" + juce::String((int) fileRate) + ".wav", fileRate, (int) fileRate, 0, 440.0f);
            auto length = [&](float stretch) {
                Rig r;
                r.proc.addZoneFromFile(f); r.proc.waitForLoads();
                r.proc.getZone(0)->rootNote = 69;
                setParam(r.proc, "STRETCH", stretch);
                auto out = r.render(48000 * 4 / 256, { { 69, 1.0f } });
                return lengthAbove(out, 0.01f) / 48000.0;
            };
            double plain = length(1.0f), twice = length(2.0f), half = length(0.5f);
            check(std::abs(plain - 1.0) < 0.05,
                  juce::String((int) fileRate) + " Hz file, 1x: " + juce::String(plain, 2) + " s (expected 1.0)");
            check(std::abs(twice - 2.0) < 0.05, juce::String((int) fileRate) + " Hz file, 2x: " + juce::String(twice, 2) + " s (expected 2.0)");
            check(std::abs(half - 0.5) < 0.03, juce::String((int) fileRate) + " Hz file, 0.5x: " + juce::String(half, 2) + " s (expected 0.5)");
        }
    }

    {
        std::cout << "[3] A zone whose file has gone is kept, shown as missing, and recovers\n";
        auto f = makeWav("gone.wav", 48000, 24000, 0, 440.0f);
        juce::MemoryBlock saved;
        {
            Rig r;
            r.proc.addZoneFromFile(f); r.proc.waitForLoads();
            r.proc.getZone(0)->keyLow = 40;
            r.proc.getZone(0)->rootNote = 50;
            r.proc.getZone(0)->rrGroup = 2;
            r.proc.getStateInformation(saved);
        }
        auto backup = f.withFileExtension("bak");
        f.moveFileTo(backup);
        {
            Rig r;
            r.proc.setStateInformation(saved.getData(), (int) saved.getSize()); r.proc.waitForLoads();
            check(r.proc.getNumZones() == 1, "the zone survives the load");
            auto* z = r.proc.getZone(0);
            check(z != nullptr && z->missing, "it is flagged as missing");
            check(z != nullptr && z->keyLow.load() == 40 && z->rootNote.load() == 50 && z->rrGroup.load() == 2, "its settings are intact");
            check(peak(r.render(8, { { 50, 1.0f } })) == 0.0f, "it is silent instead of crashing");
            juce::MemoryBlock again;
            r.proc.getStateInformation(again);
            backup.moveFileTo(f);
            Rig s;
            s.proc.setStateInformation(again.getData(), (int) again.getSize()); s.proc.waitForLoads();
            auto* z2 = s.proc.getZone(0);
            check(z2 != nullptr && !z2->missing && z2->keyLow.load() == 40, "re-saved state restores fully once the file is back");
            check(peak(s.render(8, { { 50, 1.0f } })) > 0.01f, "and it plays again");
        }
    }

    {
        std::cout << "[4] One-shots fade out instead of ending on a hard cut\n";
        auto f = makeWav("dc.wav", 48000, 4800, 1, 0.5f);
        Rig r;
        r.proc.addZoneFromFile(f); r.proc.waitForLoads();
        setParam(r.proc, "ATTACK", 0.001f);
        auto out = r.render(40, { { 60, 1.0f } });
        int end = lengthAbove(out, 1.0e-5f);
        float biggestStep = 0;
        for (int i = 1; i < (int) out.size(); ++i) biggestStep = std::max(biggestStep, std::abs(out[(size_t) i] - out[(size_t) i - 1]));
        check(end > 4000 && end < 5200, "the note lasts the length of the sample (" + juce::String(end) + " samples)");
        check(biggestStep < 0.05f, "no sample-to-sample jump above 0.05 (largest " + juce::String(biggestStep, 3) + ")");
    }

    {
        std::cout << "[5] Round-robin still alternates\n";
        auto a = makeWav("rr_a.wav", 48000, 2000, 1, 0.2f);
        auto b = makeWav("rr_b.wav", 48000, 2000, 1, 0.6f);
        Rig r;
        r.proc.addZoneFromFile(a); r.proc.waitForLoads();
        r.proc.addZoneFromFile(b); r.proc.waitForLoads();
        r.proc.getZone(0)->rrGroup = 1;
        r.proc.getZone(1)->rrGroup = 1;
        std::vector<float> peaks;
        for (int i = 0; i < 4; ++i) peaks.push_back(peak(r.render(24, { { 60, 1.0f } })));
        check(peaks[0] < 0.3f && peaks[1] > 0.3f && peaks[2] < 0.3f && peaks[3] > 0.3f,
              "four hits alternate between the two zones");
    }

    {
        std::cout << "[6] Removing a zone with the editor open is safe\n";
        auto a = makeWav("ed_a.wav", 48000, 24000, 0, 440.0f);
        auto b = makeWav("ed_b.wav", 48000, 24000, 0, 330.0f);
        SampleForgeAudioProcessor proc;
        proc.addZoneFromFile(a); proc.waitForLoads();
        proc.addZoneFromFile(b); proc.waitForLoads();
        std::unique_ptr<juce::AudioProcessorEditor> ed(proc.createEditor());
        ed->setBounds(0, 0, 1280, 770);
        pumpMessages(100);
        proc.removeZone(1);
        proc.removeZone(0);
        pumpMessages(100);
        ed.reset();
        check(true, "remove both zones, pump messages, close the editor (run under ASan to be sure)");
    }

    {
        std::cout << "[7] Forward loops wrap without a click\n";
        // A rising ramp, so the loop end (high) and start (low) are far apart.
        auto f = tempDir().getChildFile("ramp.wav");
        {
            f.deleteFile();
            juce::AudioBuffer<float> b(2, 1000);
            for (int i = 0; i < 1000; ++i) { b.setSample(0, i, i / 1000.0f); b.setSample(1, i, i / 1000.0f); }
            juce::WavAudioFormat wav;
            std::unique_ptr<juce::FileOutputStream> out(f.createOutputStream());
            std::unique_ptr<juce::AudioFormatWriter> w(wav.createWriterFor(out.get(), 48000, 2, 24, {}, 0));
            out.release();
            w->writeFromAudioSampleBuffer(b, 0, 1000);
        }
        auto biggestStep = [&](int loopMode) {
            Rig r;
            r.proc.addZoneFromFile(f); r.proc.waitForLoads();
            setParam(r.proc, "LOOP_MODE", (float) loopMode);
            setParam(r.proc, "ATTACK", 0.001f);
            auto out = r.render(60, { { 60, 1.0f } });
            float worst = 0;
            for (int i = 600; i < (int) out.size(); ++i) { float d = std::abs(out[(size_t) i] - out[(size_t) i - 1]); worst = std::max(worst, d); }
            return worst;
        };
        float fwd = biggestStep(1);
        check(fwd < 0.04f, "forward loop: largest step " + juce::String(fwd, 3) + " (a bare wrap jumps by ~1.0)");
        check(biggestStep(2) < 0.04f, "ping-pong loop stays smooth too");
    }

    {
        std::cout << "[8] Presets find their samples after a move; dropping a file relinks a missing zone\n";
        auto dirA = tempDir().getChildFile("presetA"); dirA.deleteRecursively(); dirA.createDirectory();
        auto dirB = tempDir().getChildFile("presetB"); dirB.deleteRecursively();
        auto wavA = makeWav("presetA/kick.wav", 48000, 24000, 0, 110.0f);
        auto presetFile = dirA.getChildFile("kit.sfpreset");
        {
            Rig r;
            r.proc.addZoneFromFile(wavA); r.proc.waitForLoads();
            r.proc.getZone(0)->keyLow = 36;
            r.proc.getZone(0)->keyHigh = 48;
            check(r.proc.savePresetToFile(presetFile), "preset saved next to its sample");
        }
        dirA.moveFileTo(dirB);                                   // the whole folder moves
        {
            Rig r;
            check(r.proc.loadPresetFromFile(dirB.getChildFile("kit.sfpreset")), "moved preset loads"); r.proc.waitForLoads();
            auto* z = r.proc.getZone(0);
            check(z != nullptr && !z->missing && z->keyLow.load() == 36, "its sample was found beside it");
        }
        // Relink: a missing zone adopts a dropped file of the same name and keeps its settings.
        {
            Rig r;
            r.proc.addZoneFromFile(dirB.getChildFile("kick.wav")); r.proc.waitForLoads();
            r.proc.getZone(0)->keyLow = 30;
            juce::MemoryBlock st; r.proc.getStateInformation(st);
            auto moved = dirB.getChildFile("kick.wav").moveFileTo(tempDir().getChildFile("kick_elsewhere.wav"));
            Rig q;
            q.proc.setStateInformation(st.getData(), (int) st.getSize()); q.proc.waitForLoads();
            check(q.proc.getZone(0) != nullptr && q.proc.getZone(0)->missing, "with the file gone the zone is missing");
            auto back = dirB.getChildFile("kick.wav");
            tempDir().getChildFile("kick_elsewhere.wav").moveFileTo(back);
            q.proc.addZoneFromFile(back); q.proc.waitForLoads();
            check(q.proc.getNumZones() == 1 && !q.proc.getZone(0)->missing && q.proc.getZone(0)->keyLow.load() == 30,
                  "dropping the file relinks the same zone with its settings");
            (void) moved;
        }
    }

    {
        std::cout << "[9] Pitch bend\n";
        auto tone = makeWav("bend440.wav", 48000, 48000 * 4, 0, 440.0f);
        auto freqOf = [](const std::vector<float>& v, int from, int to) {
            int crossings = 0;
            for (int i = from + 1; i < to; ++i) if (v[(size_t) i - 1] <= 0.0f && v[(size_t) i] > 0.0f) ++crossings;
            return crossings * 48000.0 / (to - from);
        };
        auto run = [&](float range, int wheelBefore, int wheelAfter) {
            Rig r;
            r.proc.addZoneFromFile(tone); r.proc.waitForLoads();
            r.proc.getZone(0)->rootNote = 69;
            setParam(r.proc, "BEND_RANGE", range);
            std::vector<std::pair<int, juce::MidiMessage>> extra;
            if (wheelBefore >= 0) extra.push_back({ 0, juce::MidiMessage::pitchWheel(1, wheelBefore) });
            if (wheelAfter >= 0) extra.push_back({ 60, juce::MidiMessage::pitchWheel(1, wheelAfter) });
            auto out = r.render(48000 * 2 / 256, { { 69, 1.0f } }, extra);
            return freqOf(out, 48000, 48000 * 2 - 256);
        };
        check(std::abs(run(2.0f, -1, -1) - 440.0) < 2.0, "wheel at centre: 440 Hz");
        double up2 = run(2.0f, 16383, -1);
        check(std::abs(up2 - 440.0 * std::pow(2.0, 2.0 / 12.0)) < 3.0, "wheel up, range 2: " + juce::String(up2, 1) + " Hz (expected 493.9)");
        double up12 = run(12.0f, 16383, -1);
        check(std::abs(up12 - 880.0) < 6.0, "wheel up, range 12: " + juce::String(up12, 1) + " Hz (expected 880)");
        double down = run(2.0f, 0, -1);
        check(std::abs(down - 440.0 * std::pow(2.0, -2.0 / 12.0)) < 3.0, "wheel down, range 2: " + juce::String(down, 1) + " Hz (expected 392)");
        double live = run(2.0f, -1, 16383);
        check(std::abs(live - 493.9) < 3.0, "bending a note already sounding: " + juce::String(live, 1) + " Hz");
        check(run(0.0f, 16383, -1) < 442.0, "range 0 disables the wheel");
    }

    {
        std::cout << "[10] Stretch works while looping and in reverse\n";
        // 1 s of silence with a 50 ms 440 Hz burst every 250 ms.
        auto bursts = tempDir().getChildFile("bursts.wav");
        {
            bursts.deleteFile();
            juce::AudioBuffer<float> b(2, 48000);
            b.clear();
            for (int i = 0; i < 48000; ++i)
                if ((i % 12000) < 2400) { float v = 0.6f * (float) std::sin(2.0 * juce::MathConstants<double>::pi * 440.0 * i / 48000.0); b.setSample(0, i, v); b.setSample(1, i, v); }
            juce::WavAudioFormat wav;
            std::unique_ptr<juce::FileOutputStream> out(bursts.createOutputStream());
            std::unique_ptr<juce::AudioFormatWriter> w(wav.createWriterFor(out.get(), 48000, 2, 24, {}, 0));
            out.release();
            w->writeFromAudioSampleBuffer(b, 0, 48000);
        }
        auto countBursts = [](const std::vector<float>& v) {
            int count = 0; bool on = false; float env = 0;
            for (float x : v) {
                env = std::max(std::abs(x), env * 0.9995f);
                if (!on && env > 0.15f) { on = true; ++count; }
                if (on && env < 0.04f) on = false;
            }
            return count;
        };
        auto bursty = [&](float stretch, int loopMode) {
            Rig r;
            r.proc.addZoneFromFile(bursts); r.proc.waitForLoads();
            r.proc.getZone(0)->rootNote = 69;
            setParam(r.proc, "LOOP_MODE", (float) loopMode);
            setParam(r.proc, "STRETCH", stretch);
            auto out = r.render(48000 * 4 / 256, { { 69, 1.0f } });
            return std::make_pair(countBursts(out), peak(out));
        };
        auto plain = bursty(1.0f, 1), slow = bursty(2.0f, 1);
        check(plain.first >= 15 && plain.first <= 17, "forward loop, no stretch: " + juce::String(plain.first) + " bursts in 4 s (expected 16)");
        check(slow.first >= 7 && slow.first <= 9, "forward loop, stretch 2x: " + juce::String(slow.first) + " bursts in 4 s (expected 8)");
        check(slow.second > 0.1f, "and it is still sounding");
        auto pp = bursty(2.0f, 2);
        check(pp.first >= 6 && pp.second > 0.1f, "ping-pong loop with stretch keeps playing (" + juce::String(pp.first) + " bursts)");

        // Loop seam under stretch: a sine that does not fit the loop evenly.
        auto odd = makeWav("odd.wav", 48000, 22222, 0, 441.37f);
        auto step = [&](bool stretched) -> double {
            Rig r;
            r.proc.addZoneFromFile(odd); r.proc.waitForLoads();
            r.proc.getZone(0)->rootNote = 69;
            setParam(r.proc, "LOOP_MODE", 1.0f);
            setParam(r.proc, "ATTACK", 0.001f);
            setParam(r.proc, "STRETCH", stretched ? 1.5f : 1.0f);
            auto out = r.render(48000 * 3 / 256, { { 69, 1.0f } });
            // Roughness: energy of the second difference relative to the signal. A clean
            // sine gives ~0.003; a click at every loop wrap adds broadband energy.
            double d2 = 0, e = 1e-12;
            for (int i = 24002; i < (int) out.size(); ++i) { double x = out[(size_t) i] - 2.0 * out[(size_t) i - 1] + out[(size_t) i - 2]; d2 += x * x; e += (double) out[(size_t) i] * out[(size_t) i]; }
            return std::sqrt(d2 / e);
        };
        double rough = step(true);
        check(rough < 0.005, "stretched loop stays as clean as a pure sine: roughness " + juce::String(rough, 4) + " (clean sine 0.0033; unfaded seam 0.0085)");

        // Reverse + stretch: a swell played backwards must fade out, and last twice as long.
        auto swell = tempDir().getChildFile("swell.wav");
        {
            swell.deleteFile();
            juce::AudioBuffer<float> b(2, 48000);
            for (int i = 0; i < 48000; ++i) { float v = 0.8f * (i / 48000.0f) * (float) std::sin(2.0 * juce::MathConstants<double>::pi * 440.0 * i / 48000.0); b.setSample(0, i, v); b.setSample(1, i, v); }
            juce::WavAudioFormat wav;
            std::unique_ptr<juce::FileOutputStream> out(swell.createOutputStream());
            std::unique_ptr<juce::AudioFormatWriter> w(wav.createWriterFor(out.get(), 48000, 2, 24, {}, 0));
            out.release();
            w->writeFromAudioSampleBuffer(b, 0, 48000);
        }
        Rig rv;
        rv.proc.addZoneFromFile(swell); rv.proc.waitForLoads();
        rv.proc.getZone(0)->rootNote = 69;
        rv.proc.getZone(0)->reverse = true;
        setParam(rv.proc, "STRETCH", 2.0f);
        auto out = rv.render(48000 * 4 / 256, { { 69, 1.0f } });
        auto rms = [&](int a, int b2) { double e = 0; for (int i = a; i < b2; ++i) e += out[(size_t) i] * out[(size_t) i]; return std::sqrt(e / (b2 - a)); };
        double early = rms(4800, 24000), late = rms(72000, 91000);
        double len = lengthAbove(out, 0.01f) / 48000.0;
        check(early > late * 2.0, "reversed swell fades out (early " + juce::String(early, 3) + " vs late " + juce::String(late, 3) + ")");
        check(std::abs(len - 2.0) < 0.15, "and takes about 2 s: " + juce::String(len, 2) + " s");
    }

    {
        std::cout << "[11] Arpeggiator\n";
        using namespace arp;
        struct Ev { int pos; int note; bool on; };

        // Runs one arpeggiator over `blocks` blocks, with `input` events given as
        // absolute sample positions, and returns every note event it produced.
        auto runArp = [](const Settings& s, const std::vector<Ev>& input, int totalSamples, int block, double bpm = 120.0,
                         bool hostPlaying = false, double ppq = -1.0) {
            Arpeggiator a;
            a.reset();
            std::vector<Ev> result;
            juce::MidiBuffer in, out;
            for (int start = 0; start < totalSamples; start += block)
            {
                in.clear();
                for (auto& e : input)
                    if (e.pos >= start && e.pos < start + block)
                        in.addEvent(e.on ? juce::MidiMessage::noteOn(1, e.note, 0.8f) : juce::MidiMessage::noteOff(1, e.note), e.pos - start);
                a.process(in, out, block, 48000.0, s, bpm, hostPlaying, ppq < 0 ? ppq : ppq + (double) start * bpm / 60.0 / 48000.0);
                for (const auto meta : out)
                {
                    auto m = meta.getMessage();
                    if (m.isNoteOnOrOff()) result.push_back({ start + meta.samplePosition, m.getNoteNumber(), m.isNoteOn() });
                }
            }
            return result;
        };
        auto onsOf = [](const std::vector<Ev>& v) { std::vector<Ev> r; for (auto& e : v) if (e.on) r.push_back(e); return r; };
        auto notes = [&](const std::vector<Ev>& v, int count) {
            std::vector<int> r; for (auto& e : onsOf(v)) if ((int) r.size() < count) r.push_back(e.note); return r; };
        auto chord = std::vector<Ev> { { 0, 64, true }, { 0, 60, true }, { 0, 67, true } };   // pressed E, C, G
        const int step16 = 6000;                                                              // 1/16 at 120 BPM, 48 kHz

        Settings s; s.on = true; s.gate = 0.5f;
        auto up = runArp(s, chord, step16 * 8, 512);
        check(notes(up, 6) == std::vector<int> { 60, 64, 67, 60, 64, 67 }, "Up plays the chord low to high and repeats");
        auto ons = onsOf(up);
        check(ons.size() >= 4 && ons[0].pos == 0 && std::abs(ons[1].pos - step16) <= 1 && std::abs(ons[3].pos - 3 * step16) <= 1,
              "steps land exactly every 1/16 note (" + juce::String(ons.size() > 1 ? ons[1].pos : -1) + ", " + juce::String(ons.size() > 3 ? ons[3].pos : -1) + ")");
        int firstOff = -1; for (auto& e : up) if (!e.on) { firstOff = e.pos; break; }
        check(std::abs(firstOff - step16 / 2) <= 1, "gate 50% releases half a step later (" + juce::String(firstOff) + ")");

        Settings d = s; d.pattern = Down;
        check(notes(runArp(d, chord, step16 * 8, 512), 4) == std::vector<int> { 67, 64, 60, 67 }, "Down plays high to low");
        Settings ud = s; ud.pattern = UpDown;
        check(notes(runArp(ud, chord, step16 * 8, 512), 8) == std::vector<int> { 60, 64, 67, 64, 60, 64, 67, 64 }, "Up/Down turns around without repeating the ends");
        Settings du = s; du.pattern = DownUp;
        check(notes(runArp(du, chord, step16 * 8, 512), 6) == std::vector<int> { 67, 64, 60, 64, 67, 64 }, "Down/Up turns around without repeating the ends");
        Settings ap = s; ap.pattern = AsPlayed;
        check(notes(runArp(ap, chord, step16 * 8, 512), 3) == std::vector<int> { 64, 60, 67 }, "As Played keeps the order the keys were pressed");
        Settings rnd = s; rnd.pattern = arp::Random;
        bool allIn = true; std::set<int> seen;
        for (auto& e : onsOf(runArp(rnd, chord, step16 * 40, 512))) { allIn &= (e.note == 60 || e.note == 64 || e.note == 67); seen.insert(e.note); }
        check(allIn && seen.size() == 3, "Random only picks held notes, and uses all of them");
        Settings oct = s; oct.octaves = 2;
        check(notes(runArp(oct, chord, step16 * 8, 512), 7) == std::vector<int> { 60, 64, 67, 72, 76, 79, 60 }, "2 octaves continues the chord an octave up");

        // Swing: the second step of each pair is late, the next pair starts on time.
        Settings sw = s; sw.swing = 1.0f;
        auto swung = onsOf(runArp(sw, chord, step16 * 6, 512));
        check(swung.size() >= 4 && std::abs(swung[1].pos - (int) (step16 * 1.5)) <= 1 && std::abs(swung[2].pos - 2 * step16) <= 1,
              "full swing pushes every second step half a step late");

        // Tempo and rate: 1/8T at 90 BPM = 60/90 * 1/3 s = 10666.67 samples.
        Settings tri = s; tri.rate = 2;
        auto trip = onsOf(runArp(tri, chord, 48000, 512, 90.0));
        check(trip.size() >= 4 && std::abs(trip[3].pos - (int) std::lround(3 * 10666.6667)) <= 2, "1/8T at 90 BPM steps every 10667 samples");

        // Releasing the keys stops it and sends the note off.
        auto stopInput = chord; for (int n : { 64, 60, 67 }) stopInput.push_back({ step16 + 100, n, false });
        auto stopped = runArp(s, stopInput, step16 * 5, 512);
        check(onsOf(stopped).size() == 2 && !stopped.empty() && !stopped.back().on, "releasing the keys ends the arp with a note-off, no stuck notes");

        // Latch keeps going; a new chord replaces it.
        Settings lt = s; lt.latch = true;
        auto latchIn = stopInput; latchIn.push_back({ step16 * 6, 72, true });
        auto latched = runArp(lt, latchIn, step16 * 9, 512);
        auto latchedOns = onsOf(latched);
        bool oldKeepsGoing = false, newReplaces = true;
        for (auto& e : latchedOns) { if (e.pos > step16 + 200 && e.pos < step16 * 6 && e.note != 72) oldKeepsGoing = true; if (e.pos >= step16 * 7 && e.note != 72) newReplaces = false; }
        check(oldKeepsGoing, "latched notes keep playing after the keys are released");
        check(newReplaces, "pressing a new key after release replaces the latched chord");

        // Buffer size must not change what comes out.
        auto small = runArp(oct, chord, step16 * 10, 64), big = runArp(oct, chord, step16 * 10, 2048);
        bool same = small.size() == big.size();
        for (size_t i = 0; same && i < small.size(); ++i) same = small[i].pos == big[i].pos && small[i].note == big[i].note && small[i].on == big[i].on;
        check(same, "identical output at 64 and 2048 samples per block (" + juce::String((int) small.size()) + " events)");

        // With the host playing, the first step waits for the next 1/16 on the grid.
        auto aligned = onsOf(runArp(s, chord, step16 * 3, 512, 120.0, true, 0.10));
        const double expected = (0.25 - 0.10) * 60.0 / 120.0 * 48000.0;     // 3600 samples
        check(!aligned.empty() && std::abs(aligned[0].pos - (int) expected) <= 2, "with the host playing, the first step starts on the grid (" + juce::String(aligned.empty() ? -1 : aligned[0].pos) + ", expected " + juce::String((int) expected) + ")");

        // Off = pass-through, and a key held before switching the arp on still releases.
        Settings off; off.on = false;
        auto passed = runArp(off, { { 0, 60, true }, { 1000, 60, false } }, 2048, 512);
        check(passed.size() == 2 && passed[0].on && !passed[1].on && passed[1].pos == 1000, "arp off passes notes through untouched");
        {
            Arpeggiator a; a.reset();
            juce::MidiBuffer in, out;
            in.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 0);
            a.process(in, out, 256, 48000.0, off, 120.0, false, -1.0);
            in.clear(); out.clear();
            a.process(in, out, 256, 48000.0, s, 120.0, false, -1.0);   // switched on while 60 is held
            in.clear(); in.addEvent(juce::MidiMessage::noteOff(1, 60), 10);
            a.process(in, out, 256, 48000.0, s, 120.0, false, -1.0);
            bool released = false;
            for (const auto meta : out) released |= meta.getMessage().isNoteOff() && meta.getMessage().getNoteNumber() == 60;
            check(released, "a key held before the arp was switched on still gets its note-off");
        }
    }

    {
        std::cout << "[12] The arpeggiator inside the plugin\n";
        auto hit = makeWav("arp_hit.wav", 48000, 2000, 1, 0.5f);
        auto countHits = [](const std::vector<float>& v, int from, int to) {
            int count = 0; bool on = false;
            for (int i = from; i < to && i < (int) v.size(); ++i) {
                if (!on && std::abs(v[(size_t) i]) > 0.05f) { on = true; ++count; }
                if (on && std::abs(v[(size_t) i]) < 0.005f) on = false;
            }
            return count;
        };
        auto setup = [&](Rig& r) {
            r.proc.addZoneFromFile(hit); r.proc.waitForLoads();
            r.proc.getZone(0)->rootNote = 60;
            setParam(r.proc, "ARP_ON", 1.0f);
            setParam(r.proc, "ARP_RATE", 3.0f);      // 1/16
            setParam(r.proc, "ATTACK", 0.001f);
            setParam(r.proc, "RELEASE", 0.001f);
        };
        {
            Rig r; setup(r);
            auto out = r.render(48000 / 256, { { 60, 1.0f } });
            int hits = countHits(out, 0, 48000);
            check(hits >= 7 && hits <= 9, "one held key plays " + juce::String(hits) + " hits in a second at 1/16 (expected 8)");
        }
        {
            Rig r; setup(r);
            r.proc.setCurrentProgram(0);
            // key down for ~0.4 s, then released; nothing should sound after the last hit decays
            std::vector<float> out;
            auto first = r.render(48000 * 4 / 10 / 256, { { 60, 1.0f } });
            juce::AudioBuffer<float> buf(2, 256); juce::MidiBuffer midi;
            midi.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
            r.proc.processBlock(buf, midi);
            auto rest = r.render(48000 / 256);
            check(peak(first) > 0.1f && peak(rest) < 0.05f, "releasing the key stops the arp (peak after release " + juce::String(peak(rest), 3) + ")");
        }
        {
            Rig r; setup(r);
            setParam(r.proc, "ARP_PATTERN", 2.0f); setParam(r.proc, "ARP_GATE", 0.8f); setParam(r.proc, "ARP_OCTAVES", 3.0f);
            juce::MemoryBlock st; r.proc.getStateInformation(st);
            Rig q;
            q.proc.setStateInformation(st.getData(), (int) st.getSize()); q.proc.waitForLoads();
            check(q.proc.apvts.getRawParameterValue("ARP_PATTERN")->load() == 2.0f
                  && q.proc.apvts.getRawParameterValue("ARP_OCTAVES")->load() == 3.0f
                  && std::abs(q.proc.apvts.getRawParameterValue("ARP_GATE")->load() - 0.8f) < 1e-4f
                  && q.proc.apvts.getRawParameterValue("ARP_ON")->load() > 0.5f,
                  "arp settings are saved with the session");
        }
    }

    {
        std::cout << "[13] Asynchronous sample loading\n";
        auto big = makeWav("async_big.wav", 48000, 48000 * 60, 1, 0.5f);      // 60 s: slow enough to see
        auto small = makeWav("async_small.wav", 48000, 4800, 1, 0.25f);
        auto bad = tempDir().getChildFile("async_not_audio.wav");
        bad.replaceWithText("this is not audio");

        {   // addZoneFromFile returns before decoding finishes, and the audio thread is never blocked
            Rig r;
            r.proc.addZoneFromFile(small); r.proc.waitForLoads();
            r.proc.getZone(0)->rootNote = 60;
            setParam(r.proc, "ATTACK", 0.001f); setParam(r.proc, "RELEASE", 0.001f);
            auto t0 = juce::Time::getMillisecondCounterHiRes();
            r.proc.addZoneFromFile(big);
            double callMs = juce::Time::getMillisecondCounterHiRes() - t0;
            check(callMs < 50.0, "addZoneFromFile returns immediately (" + juce::String(callMs, 1) + " ms)");
            check(r.proc.isLoading() || r.proc.getNumZones() == 2, "a load is pending (or already finished)");
            // keep rendering with the old zone while the big one decodes
            float worstBlockMs = 0; int blocks = 0; bool sounded = false;
            juce::AudioBuffer<float> buf(2, r.block);
            while (r.proc.isLoading() && blocks < 20000)
            {
                buf.clear(); juce::MidiBuffer midi;
                if (blocks % 50 == 0) midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
                auto b0 = juce::Time::getMillisecondCounterHiRes();
                r.proc.processBlock(buf, midi);
                worstBlockMs = std::max<float>(worstBlockMs, (float) (juce::Time::getMillisecondCounterHiRes() - b0));
                if (buf.getMagnitude(0, 0, r.block) > 0.01f) sounded = true;
                ++blocks;
                juce::MessageManager::getInstance()->runDispatchLoopUntil(1);
            }
            check(sounded, "the existing zone keeps sounding while the new one loads");
            check(r.proc.waitForLoads() && r.proc.getNumZones() == 2, "the big file arrives as a second zone");
            check(r.proc.getZone(1)->data.getNumSamples() == 48000 * 60, "decoded in full");
            check(r.proc.getSelectedZoneIndex() == 1, "the new zone is selected");
            std::cout << "        (worst block while loading: " << worstBlockMs << " ms over " << blocks << " blocks)\n";
        }
        {   // undecodable file adds nothing and does not wedge the loader
            Rig r;
            r.proc.addZoneFromFile(bad); r.proc.addZoneFromFile(small);
            check(r.proc.waitForLoads(), "loads finish");
            check(r.proc.getNumZones() == 1, "a bad file adds no zone but the next file still loads");
        }
        {   // repeated loads keep request order
            Rig r;
            r.proc.addZoneFromFile(big); r.proc.addZoneFromFile(small); r.proc.addZoneFromFile(small);
            check(r.proc.waitForLoads() && r.proc.getNumZones() == 3, "three queued loads all land");
            check(r.proc.getZone(0)->data.getNumSamples() == 48000 * 60 && r.proc.getZone(1)->data.getNumSamples() == 4800,
                  "in the order requested");
        }
        {   // state restore is async; old zones play until the new set swaps in; a newer restore wins
            Rig a; a.proc.addZoneFromFile(small); a.proc.waitForLoads(); a.proc.getZone(0)->keyLow = 20;
            juce::MemoryBlock stSmall; a.proc.getStateInformation(stSmall);
            Rig b; b.proc.addZoneFromFile(big); b.proc.addZoneFromFile(big); b.proc.waitForLoads(); b.proc.getZone(0)->keyLow = 30;
            juce::MemoryBlock stBig; b.proc.getStateInformation(stBig);

            Rig r;
            r.proc.addZoneFromFile(small); r.proc.waitForLoads();
            r.proc.setStateInformation(stBig.getData(), (int) stBig.getSize());
            check(r.proc.getNumZones() == 1 && r.proc.getZone(0)->data.getNumSamples() == 4800,
                  "setStateInformation returns with the old zones still in place");
            r.proc.setStateInformation(stSmall.getData(), (int) stSmall.getSize());   // supersedes the big restore
            check(r.proc.waitForLoads(), "restores finish");
            check(r.proc.getNumZones() == 1 && r.proc.getZone(0)->keyLow.load() == 20 && r.proc.getZone(0)->data.getNumSamples() == 4800,
                  "the latest restore wins; the superseded one is dropped");
            // a load requested before a restore must not resurrect after it
            r.proc.addZoneFromFile(small);
            r.proc.setStateInformation(stSmall.getData(), (int) stSmall.getSize());
            r.proc.waitForLoads();
            check(r.proc.getNumZones() == 1, "an add requested before a restore is discarded by it");
        }
        {   // destroying the plugin mid-load is safe and prompt
            auto t0 = juce::Time::getMillisecondCounterHiRes();
            {
                auto* p = new SampleForgeAudioProcessor();
                p->addZoneFromFile(big); p->addZoneFromFile(big); p->addZoneFromFile(big);
                juce::Thread::sleep(20);
                delete p;
            }
            pumpMessages(300);                      // stale completion callbacks must find nothing
            double ms = juce::Time::getMillisecondCounterHiRes() - t0;
            check(ms < 3000.0, "plugin destroyed mid-load without crash or long stall (" + juce::String(ms, 0) + " ms)");
        }
    }

    {
        std::cout << "[14] Arpeggiator per-step lane: velocity, ties, zone, reverse\n";
        using namespace arp;
        struct Ev { int pos; int note; bool on; int vel; };
        auto runArp = [](const Settings& s, const std::vector<Ev>& input, int totalSamples, int block, std::vector<NoteTag>* tags = nullptr) {
            Arpeggiator a; a.reset();
            std::vector<Ev> result;
            juce::MidiBuffer in, out;
            for (int start = 0; start < totalSamples; start += block)
            {
                in.clear();
                for (auto& e : input)
                    if (e.pos >= start && e.pos < start + block)
                        in.addEvent(e.on ? juce::MidiMessage::noteOn(1, e.note, 0.8f) : juce::MidiMessage::noteOff(1, e.note), e.pos - start);
                a.process(in, out, block, 48000.0, s, 120.0, false, -1.0);
                if (tags != nullptr)
                    for (int i = 0; i < a.numTags(); ++i) tags->push_back(a.tag(i));
                for (const auto meta : out)
                {
                    auto m = meta.getMessage();
                    if (m.isNoteOnOrOff()) result.push_back({ start + meta.samplePosition, m.getNoteNumber(), m.isNoteOn(), m.getVelocity() });
                }
            }
            return result;
        };
        auto ons = [](const std::vector<Ev>& v) { std::vector<Ev> r; for (auto& e : v) if (e.on) r.push_back(e); return r; };
        auto offs = [](const std::vector<Ev>& v) { std::vector<Ev> r; for (auto& e : v) if (!e.on) r.push_back(e); return r; };
        const int step16 = 6000;
        const std::vector<Ev> chord { { 0, 60, true, 0 }, { 0, 64, true, 0 }, { 0, 67, true, 0 } };

        // Defaults reproduce the pre-lane output bit for bit (hashes captured from the old engine).
        const juce::uint64 golden[] = { 6359035928952968032ull, 4198607907181958900ull, 7206125683372040994ull, 15298489760740847329ull,
                                        14739403644682878734ull, 15027483251730415700ull, 1153527310852296291ull };
        for (int i = 0; i < 7; ++i)
            check(arpGoldenHash(i) == golden[i], "default lane reproduces the old arp output exactly (scenario " + juce::String(i) + ")");

        Settings s; s.on = true; s.gate = 0.5f;

        // Velocity: scales the played velocity (0.8 -> 102 at 1.0).
        {
            auto t = s; t.numSteps = 2; t.steps[1].velocity = 0.5f;
            auto o = ons(runArp(t, chord, step16 * 4, 512));
            check(o.size() >= 4 && o[0].vel == 102 && o[2].vel == 102, "step velocity 1.0 plays the key velocity as played");
            check(o.size() >= 4 && std::abs(o[1].vel - 51) <= 1 && std::abs(o[3].vel - 51) <= 1, "step velocity 0.5 halves it (" + juce::String(o.size() > 1 ? o[1].vel : -1) + ")");
            auto z = s; z.numSteps = 1; z.steps[0].velocity = 0.0f;
            auto zo = ons(runArp(z, chord, step16 * 2, 512));
            check(!zo.empty() && zo[0].vel >= 1, "velocity 0 still plays a note (never becomes a note-off)");
        }

        // Tie: step 1 holds step 0's note; no retrigger, pitch does not advance.
        {
            auto t = s; t.numSteps = 4; t.steps[1].tie = true;
            auto ev = runArp(t, chord, step16 * 8, 512);
            auto o = ons(ev), f = offs(ev);
            check(o.size() >= 5 && o[0].note == 60 && o[1].note == 64 && o[2].note == 67 && o[3].note == 60,
                  "a tied step plays no note and the pitch sequence does not advance");
            check(o.size() >= 3 && o[0].pos == 0 && std::abs(o[1].pos - 2 * step16) <= 1 && std::abs(o[2].pos - 3 * step16) <= 1,
                  "after a tie the next note lands on its normal grid step");
            check(!f.empty() && f[0].note == 60 && std::abs(f[0].pos - (step16 + step16 / 2)) <= 1,
                  "the held note ends at the tied step's gate (" + juce::String(f.empty() ? -1 : f[0].pos) + ", expected 9000)");
            check(f.size() >= 2 && std::abs(f[1].pos - (2 * step16 + step16 / 2)) <= 1, "untied notes keep their normal gate");
        }
        // Two ties in a row hold across both steps.
        {
            auto t = s; t.numSteps = 4; t.steps[1].tie = true; t.steps[2].tie = true;
            auto ev = runArp(t, chord, step16 * 6, 512);
            auto f = offs(ev);
            check(!f.empty() && f[0].note == 60 && std::abs(f[0].pos - (2 * step16 + step16 / 2)) <= 1, "chained ties hold the note through every tied step");
        }
        // A tie with nothing sounding (first step of the run) just plays a note.
        {
            auto t = s; t.numSteps = 4; t.steps[0].tie = true;
            auto o = ons(runArp(t, chord, step16 * 3, 512));
            check(!o.empty() && o[0].pos == 0 && o[0].note == 60, "a tie on the very first step still sounds");
        }
        // Ties are block-size independent and never leave a stuck note after release.
        {
            auto t = s; t.numSteps = 4; t.steps[1].tie = true; t.steps[3].tie = true;
            auto a = runArp(t, chord, step16 * 9, 64), b = runArp(t, chord, step16 * 9, 2048);
            bool same = a.size() == b.size();
            for (size_t i = 0; same && i < a.size(); ++i) same = a[i].pos == b[i].pos && a[i].note == b[i].note && a[i].on == b[i].on;
            check(same, "tied output is identical at 64 and 2048 samples per block");
            auto in = chord; in.push_back({ step16 + 100, 60, false, 0 }); in.push_back({ step16 + 100, 64, false, 0 }); in.push_back({ step16 + 100, 67, false, 0 });
            auto ev = runArp(t, in, step16 * 4, 512);
            int on = 0; for (auto& e : ev) on += e.on ? 1 : -1;
            check(on == 0 && !ev.empty() && !ev.back().on, "releasing the keys during a tie ends the held note");
        }

        // Tags: zone / reverse requests are emitted per note-on, only when asked for.
        {
            auto t = s; t.numSteps = 3; t.steps[1].zone = 2; t.steps[2].reverse = true;
            std::vector<NoteTag> tags;
            runArp(t, chord, step16 * 3, 512, &tags);
            check(tags.size() == 2 && tags[0].note == 64 && tags[0].zone == 2 && !tags[0].reverse
                  && tags[1].note == 67 && tags[1].zone == 0 && tags[1].reverse, "only steps that ask for a zone or reverse produce tags, with the note they belong to");
            std::vector<NoteTag> none;
            runArp(s, chord, step16 * 3, 512, &none);
            check(none.empty(), "default lane produces no tags");
        }
    }

    {
        std::cout << "[15] Per-step lane inside the plugin\n";
        auto low = makeWav("lane_low.wav", 48000, 4000, 1, 0.2f);
        auto high = makeWav("lane_high.wav", 48000, 4000, 1, 0.6f);
        // A rising ramp: forward it starts near silence, reversed it starts loud.
        auto ramp = tempDir().getChildFile("lane_ramp.wav");
        {
            ramp.deleteFile();
            juce::AudioBuffer<float> b(2, 4800);
            for (int i = 0; i < 4800; ++i) { b.setSample(0, i, 0.8f * i / 4800.0f); b.setSample(1, i, 0.8f * i / 4800.0f); }
            juce::WavAudioFormat wav;
            std::unique_ptr<juce::FileOutputStream> out(ramp.createOutputStream());
            std::unique_ptr<juce::AudioFormatWriter> w(wav.createWriterFor(out.get(), 48000.0, 2, 24, {}, 0));
            out.release();
            w->writeFromAudioSampleBuffer(b, 0, 4800);
        }
        auto setupZones = [&](Rig& r) {
            r.proc.addZoneFromFile(low);  r.proc.getZone(0)->keyLow = 0;  r.proc.getZone(0)->keyHigh = 59;  r.proc.getZone(0)->rootNote = 60;
            r.proc.addZoneFromFile(high); r.proc.getZone(1)->keyLow = 60; r.proc.getZone(1)->keyHigh = 127; r.proc.getZone(1)->rootNote = 60;
            setParam(r.proc, "ARP_ON", 1.0f); setParam(r.proc, "ARP_RATE", 1.0f);
            setParam(r.proc, "ATTACK", 0.001f); setParam(r.proc, "RELEASE", 0.001f);
        };
        auto firstHit = [&](Rig& r) { auto o = r.render(20, { { 60, 1.0f } }); o.resize(2000); return peak(o); };

        { Rig r; setupZones(r); float p = firstHit(r); check(p > 0.3f, "no lane: note 60 plays the zone whose key range holds it (peak " + juce::String(p, 2) + ")"); }
        { Rig r; setupZones(r); r.proc.arpLane[0].zone = 1; float p = firstHit(r);
          check(p > 0.05f && p < 0.3f, "lane zone 1 plays the first zone even though note 60 is outside its key range (peak " + juce::String(p, 2) + ")"); }
        { Rig r; setupZones(r); r.proc.arpLane[0].zone = 2; float p = firstHit(r); check(p > 0.3f, "lane zone 2 plays only the second zone"); }
        { Rig r; setupZones(r); r.proc.arpLane[0].zone = 9; float p = firstHit(r); check(p > 0.3f, "a zone number beyond the zone count falls back to normal routing"); }
        {   // The forced zone must not leak into later hits: step 1 is back to normal routing.
            Rig r; setupZones(r); r.proc.arpNumSteps = 2; r.proc.arpLane[0].zone = 1;
            auto o = r.render(60, { { 60, 1.0f } });   // 1/8 at 120 BPM = 12000 samples a step
            std::vector<float> a(o.begin(), o.begin() + 2000), b(o.begin() + 12000, o.begin() + 14000);
            check(peak(a) < 0.3f && peak(b) > 0.3f, "a step's zone choice does not stick to the next step");
        }

        auto rampRig = [&](Rig& r) {
            r.proc.addZoneFromFile(ramp); r.proc.getZone(0)->rootNote = 60;
            setParam(r.proc, "ARP_ON", 1.0f); setParam(r.proc, "ARP_RATE", 1.0f);
            setParam(r.proc, "ATTACK", 0.001f); setParam(r.proc, "RELEASE", 0.001f);
        };
        auto early = [&](Rig& r) { auto o = r.render(10, { { 60, 1.0f } }); float s = 0; for (int i = 100; i < 300; ++i) s += std::abs(o[(size_t) i]); return s / 200.0f; };
        { Rig r; rampRig(r); float e = early(r); check(e < 0.1f, "forward ramp starts quietly (" + juce::String(e, 3) + ")"); }
        { Rig r; rampRig(r); r.proc.arpLane[0].reverse = true; float e = early(r); check(e > 0.25f, "a reversed step plays the sample backwards (" + juce::String(e, 3) + ")"); }
        { Rig r; rampRig(r); r.proc.getZone(0)->reverse = true; r.proc.arpLane[0].reverse = true; float e = early(r);
          check(e < 0.1f, "a reversed step on an already-reversed zone flips it back to forwards"); }
        { Rig r; rampRig(r); r.proc.getZone(0)->reverse = true; float e = early(r); check(e > 0.25f, "zone Reverse alone is unchanged by the lane"); }

        // State.
        {
            Rig r; setupZones(r);
            juce::MemoryBlock plain; r.proc.getStateInformation(plain);
            auto xml = juce::AudioProcessor::getXmlFromBinary(plain.getData(), (int) plain.getSize());
            check(xml != nullptr && xml->getChildByName("ARPLANE") == nullptr, "an untouched lane adds nothing to the saved state");

            r.proc.arpNumSteps = 5; r.proc.arpLane[2].tie = true; r.proc.arpLane[3].zone = 2; r.proc.arpLane[4].reverse = true; r.proc.arpLane[1].velocity = 0.25f;
            juce::MemoryBlock st; r.proc.getStateInformation(st);
            Rig q; q.proc.setStateInformation(st.getData(), (int) st.getSize());
            check(q.proc.arpNumSteps == 5 && q.proc.arpLane[2].tie && q.proc.arpLane[3].zone == 2 && q.proc.arpLane[4].reverse
                  && std::abs(q.proc.arpLane[1].velocity.load() - 0.25f) < 1e-4f && !q.proc.arpLane[0].tie, "the lane is saved and restored with the session");

            q.proc.setStateInformation(plain.getData(), (int) plain.getSize());
            check(q.proc.arpNumSteps == 16 && !q.proc.arpLane[2].tie && q.proc.arpLane[3].zone == 0 && q.proc.arpLane[1].velocity == 1.0f,
                  "state saved without a lane (older sessions) restores the default lane");
        }
    }

    std::cout << (failures == 0 ? "\nALL PASSED\n" : "\nFAILURES: " + juce::String(failures) + "\n");
    return failures == 0 ? 0 : 1;
}
