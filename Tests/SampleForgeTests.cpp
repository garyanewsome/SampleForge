// Headless tests for SampleForge. Build and run:
//   cmake --build cmake-build --target SampleForgeTests
//   cmake-build/SampleForgeTests_artefacts/Release/SampleForgeTests
#include <JuceHeader.h>
#include <cmath>
#include <iostream>

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
int snapshot(const juce::File& png)
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
    proc.addZoneFromFile(pluck); proc.getZone(0)->keyLow = 28; proc.getZone(0)->keyHigh = 47; proc.getZone(0)->rootNote = 40; proc.getZone(0)->rrGroup = 1;
    proc.addZoneFromFile(hit);   proc.getZone(1)->keyLow = 48; proc.getZone(1)->keyHigh = 59; proc.getZone(1)->rootNote = 54; proc.getZone(1)->velHigh = 90;
    proc.addZoneFromFile(pad);   proc.getZone(2)->keyLow = 60; proc.getZone(2)->keyHigh = 83; proc.getZone(2)->rootNote = 60; proc.getZone(2)->sampleStart = 0.08f; proc.getZone(2)->sampleEnd = 0.86f;
    proc.setSelectedZoneIndex(2);
    setParam(proc, "SUSTAIN", 0.7f); setParam(proc, "RELEASE", 0.8f); setParam(proc, "FILTER_CUTOFF", 4200.0f);
    setParam(proc, "FILTER_RESONANCE", 1.4f); setParam(proc, "FILTER_ENV_AMOUNT", 1.0f); setParam(proc, "STRETCH", 1.5f); setParam(proc, "LOOP_MODE", 1.0f);
    proc.keyboardState.noteOn(1, 64, 0.8f);
    proc.keyboardState.noteOn(1, 67, 0.8f);

    std::unique_ptr<juce::AudioProcessorEditor> ed(proc.createEditor());
    ed->setBounds(0, 0, 1280, 770);
    pumpMessages(600);
    auto img = ed->createComponentSnapshot(ed->getLocalBounds(), true, 1.0f);
    png.deleteFile();
    juce::PNGImageFormat fmt;
    std::unique_ptr<juce::FileOutputStream> out(png.createOutputStream());
    fmt.writeImageToStream(img, *out);
    std::cout << "wrote " << png.getFullPathName() << std::endl;
    return 0;
}

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;

    if (argc >= 3 && juce::String(argv[1]) == "--snapshot")
        return snapshot(juce::File(argv[2]));

    {
        std::cout << "[1] Velocity layers never steal voices from sounding notes\n";
        auto loud = makeWav("loud.wav", 48000, 96000, 0, 440.0f);
        auto soft = makeWav("soft.wav", 48000, 96000, 0, 330.0f);
        Rig r;
        setParam(r.proc, "LOOP_MODE", 1.0f);
        r.proc.addZoneFromFile(loud);   // matches high velocity, added first
        r.proc.addZoneFromFile(soft);   // matches low velocity, added second
        r.proc.getZone(0)->velLow = 64;
        r.proc.getZone(1)->velHigh = 63;
        std::vector<std::pair<int, float>> notes;
        for (int i = 0; i < 16; ++i) notes.push_back({ 40 + i, 0.8f });
        r.render(4, notes);
        check(r.proc.getActiveVoiceCount() == 16, "16 loud notes keep all 16 voices sounding (got " + juce::String(r.proc.getActiveVoiceCount()) + ")");

        Rig q;
        setParam(q.proc, "LOOP_MODE", 1.0f);
        q.proc.addZoneFromFile(loud);
        q.proc.addZoneFromFile(soft);
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
                r.proc.addZoneFromFile(f);
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
            r.proc.addZoneFromFile(f);
            r.proc.getZone(0)->keyLow = 40;
            r.proc.getZone(0)->rootNote = 50;
            r.proc.getZone(0)->rrGroup = 2;
            r.proc.getStateInformation(saved);
        }
        auto backup = f.withFileExtension("bak");
        f.moveFileTo(backup);
        {
            Rig r;
            r.proc.setStateInformation(saved.getData(), (int) saved.getSize());
            check(r.proc.getNumZones() == 1, "the zone survives the load");
            auto* z = r.proc.getZone(0);
            check(z != nullptr && z->missing, "it is flagged as missing");
            check(z != nullptr && z->keyLow.load() == 40 && z->rootNote.load() == 50 && z->rrGroup.load() == 2, "its settings are intact");
            check(peak(r.render(8, { { 50, 1.0f } })) == 0.0f, "it is silent instead of crashing");
            juce::MemoryBlock again;
            r.proc.getStateInformation(again);
            backup.moveFileTo(f);
            Rig s;
            s.proc.setStateInformation(again.getData(), (int) again.getSize());
            auto* z2 = s.proc.getZone(0);
            check(z2 != nullptr && !z2->missing && z2->keyLow.load() == 40, "re-saved state restores fully once the file is back");
            check(peak(s.render(8, { { 50, 1.0f } })) > 0.01f, "and it plays again");
        }
    }

    {
        std::cout << "[4] One-shots fade out instead of ending on a hard cut\n";
        auto f = makeWav("dc.wav", 48000, 4800, 1, 0.5f);
        Rig r;
        r.proc.addZoneFromFile(f);
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
        r.proc.addZoneFromFile(a);
        r.proc.addZoneFromFile(b);
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
        proc.addZoneFromFile(a);
        proc.addZoneFromFile(b);
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
            r.proc.addZoneFromFile(f);
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
            r.proc.addZoneFromFile(wavA);
            r.proc.getZone(0)->keyLow = 36;
            r.proc.getZone(0)->keyHigh = 48;
            check(r.proc.savePresetToFile(presetFile), "preset saved next to its sample");
        }
        dirA.moveFileTo(dirB);                                   // the whole folder moves
        {
            Rig r;
            check(r.proc.loadPresetFromFile(dirB.getChildFile("kit.sfpreset")), "moved preset loads");
            auto* z = r.proc.getZone(0);
            check(z != nullptr && !z->missing && z->keyLow.load() == 36, "its sample was found beside it");
        }
        // Relink: a missing zone adopts a dropped file of the same name and keeps its settings.
        {
            Rig r;
            r.proc.addZoneFromFile(dirB.getChildFile("kick.wav"));
            r.proc.getZone(0)->keyLow = 30;
            juce::MemoryBlock st; r.proc.getStateInformation(st);
            auto moved = dirB.getChildFile("kick.wav").moveFileTo(tempDir().getChildFile("kick_elsewhere.wav"));
            Rig q;
            q.proc.setStateInformation(st.getData(), (int) st.getSize());
            check(q.proc.getZone(0) != nullptr && q.proc.getZone(0)->missing, "with the file gone the zone is missing");
            auto back = dirB.getChildFile("kick.wav");
            tempDir().getChildFile("kick_elsewhere.wav").moveFileTo(back);
            q.proc.addZoneFromFile(back);
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
            r.proc.addZoneFromFile(tone);
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
            r.proc.addZoneFromFile(bursts);
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
            r.proc.addZoneFromFile(odd);
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
        rv.proc.addZoneFromFile(swell);
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

    std::cout << (failures == 0 ? "\nALL PASSED\n" : "\nFAILURES: " + juce::String(failures) + "\n");
    return failures == 0 ? 0 : 1;
}
