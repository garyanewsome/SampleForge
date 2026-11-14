#include "PluginEditor.h"
#include "PluginProcessor.h"

//==============================================================================
namespace
{
    bool isSupportedAudioFile(const juce::File& file)
    {
        static const juce::StringArray extensions { "wav", "aiff", "aif", "flac", "mp3", "ogg" };
        return extensions.contains(file.getFileExtension().substring(1), true);
    }

    void addDroppedFiles(SampleForgeAudioProcessor& processor, const juce::StringArray& files)
    {
        for (auto& path : files)
        {
            juce::File file(path);
            if (file.existsAsFile() && isSupportedAudioFile(file))
                processor.addZoneFromFile(file);
        }
    }
}

//==============================================================================
WaveformDisplay::WaveformDisplay(SampleForgeAudioProcessor& processor)
    : audioProcessor(processor)
{
    audioProcessor.addChangeListener(this);
    attachToSelectedZoneThumbnail();
    setInterceptsMouseClicks(true, false);
}

WaveformDisplay::~WaveformDisplay()
{
    if (auto* old = dynamic_cast<SampleForgeSound*>(observedZone.get()))
        if (old->thumbnail != nullptr)
            old->thumbnail->removeChangeListener(this);

    audioProcessor.removeChangeListener(this);
}

void WaveformDisplay::changeListenerCallback(juce::ChangeBroadcaster* source)
{
    // Either the processor broadcasting a zone add/remove/select (in which
    // case we might need to listen to a different zone's thumbnail now), or
    // the currently-observed thumbnail itself reporting new peak data as it
    // loads in the background — either way, just repaint.
    if (dynamic_cast<juce::AudioThumbnail*>(source) == nullptr)
        attachToSelectedZoneThumbnail();

    repaint();
}

void WaveformDisplay::attachToSelectedZoneThumbnail()
{
    auto* zone = audioProcessor.getZone(audioProcessor.getSelectedZoneIndex());

    if (zone == observedZone.get())
        return;

    if (auto* old = dynamic_cast<SampleForgeSound*>(observedZone.get()))
        if (old->thumbnail != nullptr)
            old->thumbnail->removeChangeListener(this);

    observedZone = zone;

    if (zone != nullptr && zone->thumbnail != nullptr)
        zone->thumbnail->addChangeListener(this);
}

void WaveformDisplay::paint(juce::Graphics& g)
{
    using LAF = SampleForgeLookAndFeel;
    auto bounds = getLocalBounds().toFloat();

    g.setColour(juce::Colours::black);
    g.fillRect(bounds);

    // Faint graticule: centre line and vertical divisions, like a scope.
    g.setColour(LAF::getAccentColour().withAlpha(0.10f));
    g.fillRect(0.0f, bounds.getCentreY() - 0.5f, bounds.getWidth(), 1.0f);
    for (int i = 1; i < 10; ++i)
        g.fillRect(bounds.getWidth() * (float) i / 10.0f - 0.5f, 0.0f, 1.0f, bounds.getHeight());

    auto* zone = audioProcessor.getZone(audioProcessor.getSelectedZoneIndex());

    if (zone == nullptr || zone->thumbnail == nullptr || zone->thumbnail->getTotalLength() <= 0.0)
    {
        LAF::drawGlowText(g, fileDragHover ? "DROP TO ADD ZONE" : "ADD A ZONE TO GET STARTED  /  OR DROP A FILE HERE",
                          getLocalBounds(), juce::Justification::centred, LAF::hudFont(12.0f, 0.2f),
                          LAF::getTextColour().withAlpha(0.7f), 0.3f);
        LAF::drawBrackets(g, bounds.reduced(1.5f), 14.0f, 1.5f, LAF::getAccentColour(), fileDragHover ? 0.6f : 0.0f);
        return;
    }

    const double length = zone->thumbnail->getTotalLength();

    // Peaks: a faint halo copy, then the crisp trace.
    g.setColour(LAF::getAccentColour().withAlpha(0.12f));
    zone->thumbnail->drawChannels(g, getLocalBounds().expanded(0, 4), 0.0, length, 1.0f);
    g.setColour(LAF::getAccentColour());
    zone->thumbnail->drawChannels(g, getLocalBounds(), 0.0, length, 1.0f);

    float startNorm = zone->sampleStart.load();
    float endNorm = zone->sampleEnd.load();
    float startX = startNorm * bounds.getWidth();
    float endX = endNorm * bounds.getWidth();

    // Dim the trimmed-out head/tail.
    g.setColour(juce::Colours::black.withAlpha(0.72f));
    if (startX > 0.0f)
        g.fillRect(bounds.withWidth(startX));
    if (endX < bounds.getWidth())
        g.fillRect(bounds.withLeft(endX));

    // Time ruler along the bottom: a tick every 5 %, a label every 10 %.
    g.setColour(LAF::getDimTextColour().withAlpha(0.8f));
    for (int i = 0; i <= 20; ++i)
    {
        const float x = juce::jlimit(0.5f, bounds.getWidth() - 0.5f, bounds.getWidth() * (float) i / 20.0f);
        const bool major = (i % 2 == 0);
        g.fillRect(x - 0.5f, bounds.getBottom() - (major ? 8.0f : 4.0f), 1.0f, major ? 8.0f : 4.0f);
    }
    g.setFont(LAF::monoFont(9.0f));
    for (int i = 1; i < 10; i += 1)
    {
        const float x = bounds.getWidth() * (float) i / 10.0f;
        g.drawText(juce::String(length * i / 10.0, 2) + "s", juce::Rectangle<float>(x - 22.0f, bounds.getBottom() - 21.0f, 44.0f, 12.0f),
                   juce::Justification::centred, false);
    }

    // Trim lines: hairline with a halo, a small marker and a time read-out.
    auto drawTrim = [&](float x, const juce::String& tag, double seconds, bool isStart)
    {
        g.setColour(juce::Colours::white.withAlpha(0.18f));
        g.fillRect(x - 2.5f, 0.0f, 5.0f, bounds.getHeight());
        g.setColour(juce::Colours::white);
        g.fillRect(x - 0.5f, 0.0f, 1.0f, bounds.getHeight());

        juce::Path marker;
        marker.addTriangle(x - 6.0f, 0.0f, x + 6.0f, 0.0f, x, 9.0f);
        g.fillPath(marker);

        const auto label = juce::Rectangle<float>(isStart ? x + 8.0f : x - 88.0f, 2.0f, 80.0f, 12.0f);
        g.setFont(LAF::monoFont(9.0f));
        g.drawText(tag + " " + juce::String(seconds, 3) + "s", label,
                   isStart ? juce::Justification::centredLeft : juce::Justification::centredRight, false);
    };
    drawTrim(startX, "S", startNorm * length, true);
    drawTrim(endX, "E", endNorm * length, false);

    LAF::drawBrackets(g, bounds.reduced(1.5f), 14.0f, 1.5f, LAF::getAccentColour(), fileDragHover ? 0.6f : 0.0f);
}

void WaveformDisplay::mouseDown(const juce::MouseEvent& e)
{
    auto* zone = audioProcessor.getZone(audioProcessor.getSelectedZoneIndex());
    if (zone == nullptr)
        return;

    float width = static_cast<float>(juce::jmax(1, getWidth()));
    float startX = zone->sampleStart.load() * width;
    float endX = zone->sampleEnd.load() * width;

    float distToStart = std::abs(static_cast<float>(e.x) - startX);
    float distToEnd = std::abs(static_cast<float>(e.x) - endX);

    const float grabRadius = 10.0f;
    if (distToStart <= grabRadius || distToEnd <= grabRadius)
    {
        dragging = (distToStart <= distToEnd) ? DragTarget::start : DragTarget::end;
        updateDrag(e.x);
    }
    else
    {
        dragging = DragTarget::none;
    }
}

void WaveformDisplay::mouseDrag(const juce::MouseEvent& e)
{
    if (dragging != DragTarget::none)
        updateDrag(e.x);
}

void WaveformDisplay::mouseUp(const juce::MouseEvent&)
{
    dragging = DragTarget::none;
}

void WaveformDisplay::updateDrag(int x)
{
    auto* zone = audioProcessor.getZone(audioProcessor.getSelectedZoneIndex());
    if (zone == nullptr)
        return;

    float norm = juce::jlimit(0.0f, 1.0f, static_cast<float>(x) / static_cast<float>(juce::jmax(1, getWidth())));
    const float minGap = 0.001f;

    if (dragging == DragTarget::start)
        zone->sampleStart = juce::jmin(norm, zone->sampleEnd.load() - minGap);
    else if (dragging == DragTarget::end)
        zone->sampleEnd = juce::jmax(norm, zone->sampleStart.load() + minGap);

    repaint();
}

bool WaveformDisplay::isInterestedInFileDrag(const juce::StringArray& files)
{
    for (auto& path : files)
        if (isSupportedAudioFile(juce::File(path)))
            return true;
    return false;
}

void WaveformDisplay::fileDragEnter(const juce::StringArray&, int, int)
{
    fileDragHover = true;
    repaint();
}

void WaveformDisplay::fileDragExit(const juce::StringArray&)
{
    fileDragHover = false;
    repaint();
}

void WaveformDisplay::filesDropped(const juce::StringArray& files, int, int)
{
    fileDragHover = false;
    addDroppedFiles(audioProcessor, files);
    repaint();
}

//==============================================================================
SampleForgeAudioProcessorEditor::SampleForgeAudioProcessorEditor(SampleForgeAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p), waveformDisplay(p),
      midiKeyboard(p.keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard)
{
    setLookAndFeel(&lookAndFeel);

    audioProcessor.addChangeListener(this);

    addAndMakeVisible(waveformDisplay);

    loadButton.setButtonText("Add Zone...");
    loadButton.onClick = [this]
    {
        fileChooser = std::make_unique<juce::FileChooser>(
            "Select a sample to add as a zone...", juce::File(), "*.wav;*.aiff;*.aif;*.flac;*.mp3;*.ogg");

        auto flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;

        fileChooser->launchAsync(flags, [this](const juce::FileChooser& fc)
        {
            auto file = fc.getResult();
            if (file.existsAsFile())
                audioProcessor.addZoneFromFile(file);
        });
    };
    addAndMakeVisible(loadButton);

    savePresetButton.setButtonText("Save Preset...");
    savePresetButton.onClick = [this]
    {
        fileChooser = std::make_unique<juce::FileChooser>("Save preset...", juce::File(), "*.sfpreset");

        auto flags = juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                   | juce::FileBrowserComponent::warnAboutOverwriting;

        fileChooser->launchAsync(flags, [this](const juce::FileChooser& fc)
        {
            auto file = fc.getResult();
            if (file != juce::File())
            {
                if (!file.hasFileExtension("sfpreset"))
                    file = file.withFileExtension("sfpreset");
                audioProcessor.savePresetToFile(file);
            }
        });
    };
    addAndMakeVisible(savePresetButton);

    loadPresetButton.setButtonText("Load Preset...");
    loadPresetButton.onClick = [this]
    {
        fileChooser = std::make_unique<juce::FileChooser>("Load preset...", juce::File(), "*.sfpreset");

        auto flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;

        fileChooser->launchAsync(flags, [this](const juce::FileChooser& fc)
        {
            auto file = fc.getResult();
            if (file.existsAsFile())
                audioProcessor.loadPresetFromFile(file);
        });
    };
    addAndMakeVisible(loadPresetButton);

    fileNameLabel.setText("No zones loaded", juce::dontSendNotification);
    addAndMakeVisible(fileNameLabel);

    loopModeLabel.setText("Loop", juce::dontSendNotification);
    loopModeLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(loopModeLabel);

    loopModeBox.addItemList({ "Off", "Forward", "Ping-Pong" }, 1);
    loopModeAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        audioProcessor.apvts, "LOOP_MODE", loopModeBox);
    addAndMakeVisible(loopModeBox);

    loopSyncLabel.setText("Sync", juce::dontSendNotification);
    loopSyncLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(loopSyncLabel);

    loopSyncBox.addItemList({ "Off", "1/1", "1/2", "1/4", "1/8", "1/16" }, 1);
    loopSyncAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        audioProcessor.apvts, "LOOP_SYNC", loopSyncBox);
    addAndMakeVisible(loopSyncBox);

    rrOrderLabel.setText("RR", juce::dontSendNotification);
    rrOrderLabel.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(rrOrderLabel);

    rrOrderBox.addItemList({ "Sequential", "Random" }, 1);
    rrOrderAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        audioProcessor.apvts, "RR_ORDER", rrOrderBox);
    addAndMakeVisible(rrOrderBox);

    filterTypeLabel.setText("Filter", juce::dontSendNotification);
    filterTypeLabel.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(filterTypeLabel);

    filterTypeBox.addItemList({ "Low-pass", "High-pass", "Band-pass" }, 1);
    filterTypeAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        audioProcessor.apvts, "FILTER_TYPE", filterTypeBox);
    addAndMakeVisible(filterTypeBox);

    zoneListBox.setRowHeight(22);
    addAndMakeVisible(zoneListBox);

    removeZoneButton.setButtonText("Remove Zone");
    removeZoneButton.setEnabled(false);
    removeZoneButton.onClick = [this]
    {
        audioProcessor.removeZone(audioProcessor.getSelectedZoneIndex());
    };
    addAndMakeVisible(removeZoneButton);

    reverseButton.setButtonText("Reverse");
    reverseButton.setEnabled(false);
    reverseButton.onClick = [this]
    {
        if (auto* zone = audioProcessor.getZone(audioProcessor.getSelectedZoneIndex()))
        {
            zone->reverse = reverseButton.getToggleState();
            zoneListBox.repaint();
        }
    };
    addAndMakeVisible(reverseButton);

    auto noteNameFn = [](double value)
    {
        return juce::MidiMessage::getMidiNoteName(static_cast<int>(value), true, true, 3);
    };

    // Zone detail controls aren't APVTS parameters (each zone has its own
    // values), so they're wired up manually via onValueChange instead of
    // SliderAttachment, and refreshed from the selected zone on every change
    // message (see refreshZoneDetailControls).
    auto setupZoneSlider = [this](juce::Slider& s, juce::Label& l, const juce::String& labelText)
    {
        s.setSliderStyle(juce::Slider::LinearVertical);
        s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 20);
        s.setRange(0.0, 127.0, 1.0);
        s.setEnabled(false);
        addAndMakeVisible(s);

        l.setText(labelText, juce::dontSendNotification);
        l.setJustificationType(juce::Justification::centred);
        addAndMakeVisible(l);
    };

    setupZoneSlider(rootNoteSlider, rootNoteLabel, "Root");
    rootNoteSlider.textFromValueFunction = noteNameFn;
    rootNoteSlider.setNumDecimalPlacesToDisplay(0);
    rootNoteSlider.updateText();
    rootNoteSlider.onValueChange = [this]
    {
        if (auto* zone = audioProcessor.getZone(audioProcessor.getSelectedZoneIndex()))
        {
            zone->rootNote = static_cast<int>(rootNoteSlider.getValue());
            zoneListBox.repaint();
        }
    };

    setupZoneSlider(keyLoSlider, keyLoLabel, "Key Lo");
    keyLoSlider.textFromValueFunction = noteNameFn;
    keyLoSlider.setNumDecimalPlacesToDisplay(0);
    keyLoSlider.updateText();
    keyLoSlider.onValueChange = [this]
    {
        if (auto* zone = audioProcessor.getZone(audioProcessor.getSelectedZoneIndex()))
        {
            int lo = juce::jmin(static_cast<int>(keyLoSlider.getValue()), zone->keyHigh.load());
            zone->keyLow = lo;
            keyLoSlider.setValue(lo, juce::dontSendNotification);
            zoneListBox.repaint();
        }
    };

    setupZoneSlider(keyHiSlider, keyHiLabel, "Key Hi");
    keyHiSlider.textFromValueFunction = noteNameFn;
    keyHiSlider.setNumDecimalPlacesToDisplay(0);
    keyHiSlider.updateText();
    keyHiSlider.onValueChange = [this]
    {
        if (auto* zone = audioProcessor.getZone(audioProcessor.getSelectedZoneIndex()))
        {
            int hi = juce::jmax(static_cast<int>(keyHiSlider.getValue()), zone->keyLow.load());
            zone->keyHigh = hi;
            keyHiSlider.setValue(hi, juce::dontSendNotification);
            zoneListBox.repaint();
        }
    };

    setupZoneSlider(velLoSlider, velLoLabel, "Vel Lo");
    velLoSlider.onValueChange = [this]
    {
        if (auto* zone = audioProcessor.getZone(audioProcessor.getSelectedZoneIndex()))
        {
            int lo = juce::jmin(static_cast<int>(velLoSlider.getValue()), zone->velHigh.load());
            zone->velLow = lo;
            velLoSlider.setValue(lo, juce::dontSendNotification);
            zoneListBox.repaint();
        }
    };

    setupZoneSlider(velHiSlider, velHiLabel, "Vel Hi");
    velHiSlider.onValueChange = [this]
    {
        if (auto* zone = audioProcessor.getZone(audioProcessor.getSelectedZoneIndex()))
        {
            int hi = juce::jmax(static_cast<int>(velHiSlider.getValue()), zone->velLow.load());
            zone->velHigh = hi;
            velHiSlider.setValue(hi, juce::dontSendNotification);
            zoneListBox.repaint();
        }
    };

    setupZoneSlider(zonePanSlider, zonePanLabel, "Pan");
    zonePanSlider.setRange(-1.0, 1.0, 0.01);
    zonePanSlider.textFromValueFunction = [](double v)
    {
        if (std::abs(v) < 0.005) return juce::String("C");
        int pct = juce::roundToInt(std::abs(v) * 100.0);
        return juce::String(pct) + (v < 0.0 ? "L" : "R");
    };
    zonePanSlider.updateText();
    zonePanSlider.onValueChange = [this]
    {
        if (auto* zone = audioProcessor.getZone(audioProcessor.getSelectedZoneIndex()))
            zone->pan = static_cast<float>(zonePanSlider.getValue());
    };

    setupZoneSlider(zoneGainSlider, zoneGainLabel, "Gain");
    zoneGainSlider.setRange(0.0, 1.0, 0.01);
    zoneGainSlider.textFromValueFunction = [](double v) { return juce::String(juce::roundToInt(v * 100.0)) + "%"; };
    zoneGainSlider.valueFromTextFunction = [](const juce::String& text) { return text.getDoubleValue() / 100.0; };
    zoneGainSlider.updateText();
    zoneGainSlider.onValueChange = [this]
    {
        if (auto* zone = audioProcessor.getZone(audioProcessor.getSelectedZoneIndex()))
            zone->gain = static_cast<float>(zoneGainSlider.getValue());
    };

    rrGroupLabel.setText("RR Grp", juce::dontSendNotification);
    addAndMakeVisible(rrGroupLabel);

    rrGroupBox.addItem("None", 1);
    for (int group = 1; group <= 15; ++group)
        rrGroupBox.addItem(juce::String(group), group + 1);
    rrGroupBox.setEnabled(false);
    rrGroupBox.onChange = [this]
    {
        if (auto* zone = audioProcessor.getZone(audioProcessor.getSelectedZoneIndex()))
        {
            zone->rrGroup = rrGroupBox.getSelectedId() - 1;
            zoneListBox.repaint();
        }
    };
    addAndMakeVisible(rrGroupBox);

    // A quick local helper to set up our ADSR/gain sliders elegantly
    auto setupSlider = [this](juce::Slider& s, juce::Label& l, const juce::String& labelText,
                               std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>& att,
                               const juce::String& paramId)
    {
        s.setSliderStyle(juce::Slider::LinearVertical);
        s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 20);
        att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(audioProcessor.apvts, paramId, s);
        addAndMakeVisible(s);

        l.setText(labelText, juce::dontSendNotification);
        l.setJustificationType(juce::Justification::centred);
        addAndMakeVisible(l);
    };

    setupSlider(attackSlider, attackLabel, "Attack", attackAtt, "ATTACK");
    setupSlider(decaySlider, decayLabel, "Decay", decayAtt, "DECAY");
    setupSlider(sustainSlider, sustainLabel, "Sustain", sustainAtt, "SUSTAIN");
    setupSlider(releaseSlider, releaseLabel, "Release", releaseAtt, "RELEASE");
    setupSlider(gainSlider, gainLabel, "Gain", gainAtt, "GAIN");
    setupSlider(cutoffSlider, cutoffLabel, "Cutoff", cutoffAtt, "FILTER_CUTOFF");
    setupSlider(resonanceSlider, resonanceLabel, "Reso", resonanceAtt, "FILTER_RESONANCE");
    setupSlider(filterEnvSlider, filterEnvLabel, "Filt Env", filterEnvAtt, "FILTER_ENV_AMOUNT");
    setupSlider(stretchSlider, stretchLabel, "Stretch", stretchAtt, "STRETCH");
    // Playback's other controls (Loop/Sync) are horizontal label+combo rows;
    // match that instead of the vertical style setupSlider defaults to.
    // Stretch and Bend are vertical faders like the rest (setupSlider's default).

    setupSlider(bendSlider, bendLabel, "Bend", bendAtt, "BEND_RANGE");
    bendSlider.setNumDecimalPlacesToDisplay(0);
    bendSlider.setTextValueSuffix(" st");

    // Raw floats (seconds, 0-1 fraction) read awkwardly on a control surface;
    // show Attack/Decay/Release in milliseconds and Sustain/Gain as a percentage.
    auto secondsToMs = [](double seconds) { return juce::String(juce::roundToInt(seconds * 1000.0)) + " ms"; };
    auto msToSeconds = [](const juce::String& text) { return text.getDoubleValue() / 1000.0; };
    auto fracToPercent = [](double frac) { return juce::String(juce::roundToInt(frac * 100.0)) + "%"; };
    auto percentToFrac = [](const juce::String& text) { return text.getDoubleValue() / 100.0; };

    for (auto* s : { &attackSlider, &decaySlider, &releaseSlider })
    {
        s->textFromValueFunction = secondsToMs;
        s->valueFromTextFunction = msToSeconds;
        s->updateText();
    }

    for (auto* s : { &sustainSlider, &gainSlider })
    {
        s->textFromValueFunction = fracToPercent;
        s->valueFromTextFunction = percentToFrac;
        s->updateText();
    }

    cutoffSlider.textFromValueFunction = [](double hz)
    {
        return hz >= 1000.0 ? juce::String(hz / 1000.0, 1) + " kHz" : juce::String(juce::roundToInt(hz)) + " Hz";
    };
    cutoffSlider.valueFromTextFunction = [](const juce::String& text)
    {
        double v = text.getDoubleValue();
        return text.containsIgnoreCase("k") ? v * 1000.0 : v;
    };
    cutoffSlider.updateText();

    filterEnvSlider.textFromValueFunction = [](double oct)
    {
        return (oct >= 0.0 ? "+" : "") + juce::String(oct, 1) + " oct";
    };
    filterEnvSlider.valueFromTextFunction = [](const juce::String& text) { return text.getDoubleValue(); };
    filterEnvSlider.updateText();

    stretchSlider.textFromValueFunction = [](double ratio) { return juce::String(ratio, 2) + "x"; };
    stretchSlider.valueFromTextFunction = [](const juce::String& text)
    {
        return text.upToFirstOccurrenceOf("x", false, true).getDoubleValue();
    };
    stretchSlider.updateText();

    addAndMakeVisible(midiKeyboard);

    // The processor may already hold zones (reopening the window on a loaded
    // session), so sync the list, file label and zone controls right away
    // instead of waiting for the next change message.
    changeListenerCallback(nullptr);

    setSize(1280, 770);
}

bool SampleForgeAudioProcessorEditor::isInterestedInFileDrag(const juce::StringArray& files)
{
    for (auto& path : files)
        if (isSupportedAudioFile(juce::File(path)))
            return true;
    return false;
}

void SampleForgeAudioProcessorEditor::filesDropped(const juce::StringArray& files, int, int)
{
    addDroppedFiles(audioProcessor, files);
}

SampleForgeAudioProcessorEditor::~SampleForgeAudioProcessorEditor()
{
    setLookAndFeel(nullptr);
    audioProcessor.removeChangeListener(this);
}

void SampleForgeAudioProcessorEditor::changeListenerCallback(juce::ChangeBroadcaster*)
{
    int numZones = audioProcessor.getNumZones();
    fileNameLabel.setText(numZones > 0
                               ? juce::String(numZones) + " zone(s) — " + audioProcessor.getSelectedZoneFileName()
                               : "No zones loaded",
                           juce::dontSendNotification);

    zoneListBox.updateContent();

    int selectedIndex = audioProcessor.getSelectedZoneIndex();
    if (selectedIndex >= 0)
        zoneListBox.selectRow(selectedIndex);
    else
        zoneListBox.deselectAllRows();
    zoneListBox.repaint();

    removeZoneButton.setEnabled(selectedIndex >= 0);
    reverseButton.setEnabled(selectedIndex >= 0);

    refreshZoneDetailControls();
}

int SampleForgeAudioProcessorEditor::getNumRows()
{
    return audioProcessor.getNumZones();
}

void SampleForgeAudioProcessorEditor::paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected)
{
    if (rowIsSelected)
    {
        g.fillAll(SampleForgeLookAndFeel::getAccentColour().withAlpha(0.10f));
        g.setColour(SampleForgeLookAndFeel::getAccentColour());
        g.fillRect(0, 0, 2, height);
    }

    auto* zone = audioProcessor.getZone(rowNumber);
    if (zone == nullptr)
        return;

    auto noteName = [](int note) { return juce::MidiMessage::getMidiNoteName(note, true, true, 3); };

    juce::String text = juce::String(zone->missing ? "[MISSING] " : "") + juce::File(zone->filePath).getFileNameWithoutExtension()
                       + "  |  " + noteName(zone->keyLow.load()) + "-" + noteName(zone->keyHigh.load())
                       + "  |  vel " + juce::String(zone->velLow.load()) + "-" + juce::String(zone->velHigh.load())
                       + "  |  root " + noteName(zone->rootNote.load())
                       + (zone->reverse.load() ? "  |  REV" : "")
                       + (zone->rrGroup.load() > 0 ? "  |  RR" + juce::String(zone->rrGroup.load()) : "");

    SampleForgeLookAndFeel::drawGlowText(g, text, juce::Rectangle<int>(8, 0, width - 12, height),
                                         juce::Justification::centredLeft, SampleForgeLookAndFeel::monoFont(12.0f),
                                         zone->missing ? SampleForgeLookAndFeel::getWarningColour()
                                                       : SampleForgeLookAndFeel::getTextColour(),
                                         0.0f);
}

void SampleForgeAudioProcessorEditor::listBoxItemClicked(int row, const juce::MouseEvent&)
{
    audioProcessor.setSelectedZoneIndex(row);
}

void SampleForgeAudioProcessorEditor::refreshZoneDetailControls()
{
    auto* zone = audioProcessor.getZone(audioProcessor.getSelectedZoneIndex());
    bool hasZone = zone != nullptr;

    rootNoteSlider.setEnabled(hasZone);
    keyLoSlider.setEnabled(hasZone);
    keyHiSlider.setEnabled(hasZone);
    velLoSlider.setEnabled(hasZone);
    velHiSlider.setEnabled(hasZone);
    zonePanSlider.setEnabled(hasZone);
    zoneGainSlider.setEnabled(hasZone);
    rrGroupBox.setEnabled(hasZone);

    if (hasZone)
    {
        rootNoteSlider.setValue(zone->rootNote.load(), juce::dontSendNotification);
        keyLoSlider.setValue(zone->keyLow.load(), juce::dontSendNotification);
        keyHiSlider.setValue(zone->keyHigh.load(), juce::dontSendNotification);
        velLoSlider.setValue(zone->velLow.load(), juce::dontSendNotification);
        velHiSlider.setValue(zone->velHigh.load(), juce::dontSendNotification);
        zonePanSlider.setValue(zone->pan.load(), juce::dontSendNotification);
        zoneGainSlider.setValue(zone->gain.load(), juce::dontSendNotification);
        rrGroupBox.setSelectedId(zone->rrGroup.load() + 1, juce::dontSendNotification);
        reverseButton.setToggleState(zone->reverse.load(), juce::dontSendNotification);
    }
}

//==============================================================================
void SampleForgeAudioProcessorEditor::paint(juce::Graphics& g)
{
    using LAF = SampleForgeLookAndFeel;
    g.fillAll(LAF::getWindowBackgroundColour());

    // Header: ring-and-crosshair mark, spaced title, a status tag on the right,
    // and a hairline underneath with end ticks.
    const float rx = 30.0f, ry = 21.0f;
    g.setColour(LAF::getAccentColour().withAlpha(0.16f));
    g.fillEllipse(rx - 12.0f, ry - 12.0f, 24.0f, 24.0f);
    g.setColour(LAF::getAccentColour());
    g.drawEllipse(rx - 9.0f, ry - 9.0f, 18.0f, 18.0f, 1.0f);
    g.fillEllipse(rx - 2.5f, ry - 2.5f, 5.0f, 5.0f);
    for (float a : { 0.0f, 90.0f, 180.0f, 270.0f })
    {
        const float dx = std::cos(juce::degreesToRadians(a)), dy = std::sin(juce::degreesToRadians(a));
        g.drawLine(rx + dx * 11.0f, ry + dy * 11.0f, rx + dx * 15.0f, ry + dy * 15.0f, 1.0f);
    }

    LAF::drawGlowText(g, "S A M P L E F O R G E", juce::Rectangle<int>(54, 0, 420, 42),
                      juce::Justification::centredLeft, LAF::hudFont(19.0f, 0.1f, true), LAF::getTextColour(), 0.8f);
    LAF::drawGlowText(g, "MULTISAMPLE ENGINE  /  16 VOICES  /  RUBBERBAND STRETCH", juce::Rectangle<int>(getWidth() - 520, 0, 500, 42),
                      juce::Justification::centredRight, LAF::hudFont(9.5f, 0.14f), LAF::getDimTextColour(), 0.0f);

    g.setColour(LAF::getAccentColour().withAlpha(0.10f));
    g.fillRect(0, 41, getWidth(), 3);
    g.setColour(LAF::getAccentColour().withAlpha(0.65f));
    g.fillRect(10, 42, getWidth() - 20, 1);
    g.fillRect(10, 39, 1, 6);
    g.fillRect(getWidth() - 11, 39, 1, 6);

    drawPanel(g, zonePanelBounds, "Zone");
    drawPanel(g, envelopePanelBounds, "Envelope");
    drawPanel(g, filterPanelBounds, "Filter");
    drawPanel(g, playbackPanelBounds, "Playback");
}

void SampleForgeAudioProcessorEditor::drawPanel(juce::Graphics& g, juce::Rectangle<int> bounds, const juce::String& title)
{
    using LAF = SampleForgeLookAndFeel;
    if (bounds.isEmpty())
        return;

    auto r = bounds.toFloat().reduced(1.0f);
    g.setColour(LAF::getPanelColour());
    g.fillRect(r);
    LAF::drawBrackets(g, r, 16.0f, 1.4f, LAF::getAccentColour(), 0.10f);

    // Title: a short tick, then spaced small caps, then a fading rule.
    const int ty = bounds.getY() + 12;
    g.setColour(LAF::getAccentColour());
    g.fillRect(bounds.getX() + 12, ty, 10, 1);
    const juce::String text = title.toUpperCase();
    LAF::drawGlowText(g, text, juce::Rectangle<int>(bounds.getX() + 28, bounds.getY() + 4, 200, 16),
                      juce::Justification::centredLeft, LAF::hudFont(10.0f, 0.22f, true), LAF::getTextColour().withAlpha(0.9f), 0.4f);
    const int ruleStart = bounds.getX() + 34 + text.length() * 9;
    g.setGradientFill(juce::ColourGradient(LAF::getAccentColour().withAlpha(0.35f), (float) ruleStart, (float) ty,
                                           LAF::getAccentColour().withAlpha(0.0f), (float) bounds.getRight() - 18.0f, (float) ty, false));
    g.fillRect((float) ruleStart, (float) ty, (float) (bounds.getRight() - 18 - ruleStart), 1.0f);
}

void SampleForgeAudioProcessorEditor::resized()
{
    constexpr int panelHeaderHeight = 22;
    constexpr int panelGap = 10;

    auto area = getLocalBounds();

    midiKeyboard.setBounds(area.removeFromBottom(80));

    area.removeFromTop(44); // reserve room for the title drawn in paint()

    // Just file/session actions — everything else now lives in its own panel
    // below, next to the controls it actually affects.
    auto topBar = area.removeFromTop(36);
    loadButton.setBounds(topBar.removeFromLeft(140).reduced(4));
    savePresetButton.setBounds(topBar.removeFromLeft(120).reduced(4));
    loadPresetButton.setBounds(topBar.removeFromLeft(120).reduced(4));
    fileNameLabel.setBounds(topBar.reduced(4));

    waveformDisplay.setBounds(area.removeFromTop(160).reduced(10, 6));

    area.reduce(10, 6);

    //==========================================================================
    // Zone panel: zone list + per-zone controls, including RR Group right
    // next to RR Order since the two only make sense together.
    auto layoutVerticalSlider = [&](juce::Rectangle<int> content, int width, juce::Slider& sl, juce::Label& l, int index)
    {
        auto column = content.withX(content.getX() + width * index).withWidth(width);
        l.setBounds(column.removeFromTop(20));
        sl.setBounds(column);
    };

    zonePanelBounds = area.removeFromTop(224);
    area.removeFromTop(6); // breathing room between the brackets of the two rows
    auto zonePanel = zonePanelBounds.reduced(10, 4);
    zonePanel.removeFromTop(panelHeaderHeight);

    // Left: the zone list with its Remove button underneath.
    auto zoneListArea = zonePanel.removeFromLeft(360);
    removeZoneButton.setBounds(zoneListArea.removeFromBottom(26));
    zoneListArea.removeFromBottom(4);
    zoneListBox.setBounds(zoneListArea);
    zonePanel.removeFromLeft(14);

    // Right: Reverse and the round-robin pair, stacked.
    auto rrColumn = zonePanel.removeFromRight(210);
    zonePanel.removeFromRight(10);
    rrColumn.removeFromTop(24);
    reverseButton.setBounds(rrColumn.removeFromTop(28));
    rrColumn.removeFromTop(14);
    auto rrGroupRow = rrColumn.removeFromTop(28);
    rrGroupLabel.setBounds(rrGroupRow.removeFromLeft(66));
    rrGroupBox.setBounds(rrGroupRow.removeFromLeft(110));
    rrColumn.removeFromTop(10);
    auto rrOrderRow = rrColumn.removeFromTop(28);
    rrOrderLabel.setBounds(rrOrderRow.removeFromLeft(66));
    rrOrderBox.setBounds(rrOrderRow.removeFromLeft(110));

    // Middle: one vertical fader per zone property.
    constexpr int numZoneFaders = 7;
    const int zoneFaderWidth = zonePanel.getWidth() / numZoneFaders;
    layoutVerticalSlider(zonePanel, zoneFaderWidth, rootNoteSlider, rootNoteLabel, 0);
    layoutVerticalSlider(zonePanel, zoneFaderWidth, keyLoSlider, keyLoLabel, 1);
    layoutVerticalSlider(zonePanel, zoneFaderWidth, keyHiSlider, keyHiLabel, 2);
    layoutVerticalSlider(zonePanel, zoneFaderWidth, velLoSlider, velLoLabel, 3);
    layoutVerticalSlider(zonePanel, zoneFaderWidth, velHiSlider, velHiLabel, 4);
    layoutVerticalSlider(zonePanel, zoneFaderWidth, zonePanSlider, zonePanLabel, 5);
    layoutVerticalSlider(zonePanel, zoneFaderWidth, zoneGainSlider, zoneGainLabel, 6);

    //==========================================================================
    // Second row: Envelope / Filter / Playback panels side by side.
    auto secondRow = area;
    envelopePanelBounds = secondRow.removeFromLeft(560);
    secondRow.removeFromLeft(panelGap);
    filterPanelBounds = secondRow.removeFromLeft(380);
    secondRow.removeFromLeft(panelGap);
    playbackPanelBounds = secondRow;

    // Envelope: Attack/Decay/Sustain/Release/Gain, all vertical sliders.
    auto envelopeContent = envelopePanelBounds.reduced(10, 4);
    envelopeContent.removeFromTop(panelHeaderHeight);
    constexpr int numEnvelopeSliders = 5;
    int envelopeSliderWidth = envelopeContent.getWidth() / numEnvelopeSliders;
    layoutVerticalSlider(envelopeContent, envelopeSliderWidth, attackSlider, attackLabel, 0);
    layoutVerticalSlider(envelopeContent, envelopeSliderWidth, decaySlider, decayLabel, 1);
    layoutVerticalSlider(envelopeContent, envelopeSliderWidth, sustainSlider, sustainLabel, 2);
    layoutVerticalSlider(envelopeContent, envelopeSliderWidth, releaseSlider, releaseLabel, 3);
    layoutVerticalSlider(envelopeContent, envelopeSliderWidth, gainSlider, gainLabel, 4);

    // Filter: the type selector sits directly above the sliders it governs.
    auto filterContent = filterPanelBounds.reduced(10, 4);
    filterContent.removeFromTop(panelHeaderHeight);
    auto filterTypeRow = filterContent.removeFromTop(26);
    filterTypeLabel.setBounds(filterTypeRow.removeFromLeft(58));
    filterTypeBox.setBounds(filterTypeRow.removeFromLeft(130));
    filterContent.removeFromTop(4);

    constexpr int numFilterSliders = 3;
    int filterSliderWidth = filterContent.getWidth() / numFilterSliders;
    layoutVerticalSlider(filterContent, filterSliderWidth, cutoffSlider, cutoffLabel, 0);
    layoutVerticalSlider(filterContent, filterSliderWidth, resonanceSlider, resonanceLabel, 1);
    layoutVerticalSlider(filterContent, filterSliderWidth, filterEnvSlider, filterEnvLabel, 2);

    // Playback: Loop mode and its Sync rate stacked on the left, Stretch and
    // Bend as vertical faders on the right.
    auto playbackContent = playbackPanelBounds.reduced(10, 4);
    playbackContent.removeFromTop(panelHeaderHeight);
    auto playbackCombos = playbackContent.removeFromLeft(124);
    playbackContent.removeFromLeft(6);
    playbackCombos.removeFromTop(6);
    loopModeLabel.setBounds(playbackCombos.removeFromTop(18));
    loopModeBox.setBounds(playbackCombos.removeFromTop(28));
    playbackCombos.removeFromTop(16);
    loopSyncLabel.setBounds(playbackCombos.removeFromTop(18));
    loopSyncBox.setBounds(playbackCombos.removeFromTop(28));

    const int playbackFaderWidth = playbackContent.getWidth() / 2;
    layoutVerticalSlider(playbackContent, playbackFaderWidth, stretchSlider, stretchLabel, 0);
    layoutVerticalSlider(playbackContent, playbackFaderWidth, bendSlider, bendLabel, 1);
}
