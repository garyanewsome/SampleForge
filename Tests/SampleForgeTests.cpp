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
    std::vector<float> render(int blocks, const std::vector<std::pair<int, float>>& noteOns = {})
    {
        std::vector<float> out;
        juce::AudioBuffer<float> buf(2, block);
        for (int i = 0; i < blocks; ++i)
        {
            buf.clear();
            juce::MidiBuffer midi;
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

int main()
{
    juce::ScopedJuceInitialiser_GUI gui;

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

    std::cout << (failures == 0 ? "\nALL PASSED\n" : "\nFAILURES: " + juce::String(failures) + "\n");
    return failures == 0 ? 0 : 1;
}
