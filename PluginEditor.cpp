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
    if (observedThumbnail != nullptr)
        observedThumbnail->removeChangeListener(this);

    audioProcessor.removeChangeListener(this);
}

void WaveformDisplay::changeListenerCallback(juce::ChangeBroadcaster* source)
{
    // Either the processor broadcasting a zone add/remove/select (in which
    // case we might need to listen to a different zone's thumbnail now), or
    // the currently-observed thumbnail itself reporting new peak data as it
    // loads in the background — either way, just repaint.
    if (source != observedThumbnail)
        attachToSelectedZoneThumbnail();

    repaint();
}

void WaveformDisplay::attachToSelectedZoneThumbnail()
{
    auto* zone = audioProcessor.getZone(audioProcessor.getSelectedZoneIndex());
    auto* newThumbnail = zone != nullptr ? zone->thumbnail.get() : nullptr;

    if (newThumbnail == observedThumbnail)
        return;

    if (observedThumbnail != nullptr)
        observedThumbnail->removeChangeListener(this);

    observedThumbnail = newThumbnail;

    if (observedThumbnail != nullptr)
        observedThumbnail->addChangeListener(this);
}

void WaveformDisplay::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    g.setColour(juce::Colours::black.withAlpha(0.35f));
    g.fillRect(bounds);

    auto* zone = audioProcessor.getZone(audioProcessor.getSelectedZoneIndex());

    if (zone == nullptr || zone->thumbnail == nullptr || zone->thumbnail->getTotalLength() <= 0.0)
    {
        g.setColour(juce::Colours::white.withAlpha(0.4f));
        g.setFont(16.0f);
        g.drawText(fileDragHover ? "Drop to add zone" : "Add a zone to get started (or drop a file here)",
                   bounds, juce::Justification::centred);

        if (fileDragHover)
        {
            g.setColour(SampleForgeLookAndFeel::getAccentColour());
            g.drawRect(bounds, 2.0f);
        }
        return;
    }

    g.setColour(SampleForgeLookAndFeel::getAccentColour());
    zone->thumbnail->drawChannels(g, getLocalBounds(), 0.0, zone->thumbnail->getTotalLength(), 1.0f);

    float startNorm = zone->sampleStart.load();
    float endNorm = zone->sampleEnd.load();

    float startX = startNorm * bounds.getWidth();
    float endX = endNorm * bounds.getWidth();

    // Dim the trimmed-out head/tail regions
    g.setColour(juce::Colours::black.withAlpha(0.6f));
    if (startX > 0.0f)
        g.fillRect(bounds.withWidth(startX));
    if (endX < bounds.getWidth())
        g.fillRect(bounds.withLeft(endX));

    g.setColour(juce::Colours::white);
    g.drawLine(startX, 0.0f, startX, bounds.getHeight(), 2.0f);
    g.drawLine(endX, 0.0f, endX, bounds.getHeight(), 2.0f);

    juce::Path startHandle, endHandle;
    startHandle.addTriangle(startX - 6.0f, 0.0f, startX + 6.0f, 0.0f, startX, 10.0f);
    endHandle.addTriangle(endX - 6.0f, 0.0f, endX + 6.0f, 0.0f, endX, 10.0f);
    g.fillPath(startHandle);
    g.fillPath(endHandle);

    if (fileDragHover)
    {
        g.setColour(SampleForgeLookAndFeel::getAccentColour());
        g.drawRect(bounds, 2.0f);
    }
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
    loopModeLabel.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(loopModeLabel);

    loopModeBox.addItemList({ "Off", "Forward", "Ping-Pong" }, 1);
    loopModeAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        audioProcessor.apvts, "LOOP_MODE", loopModeBox);
    addAndMakeVisible(loopModeBox);

    loopSyncLabel.setText("Sync", juce::dontSendNotification);
    loopSyncLabel.setJustificationType(juce::Justification::centredRight);
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
        s.setSliderStyle(juce::Slider::LinearHorizontal);
        s.setTextBoxStyle(juce::Slider::TextBoxRight, false, 54, 20);
        s.setRange(0.0, 127.0, 1.0);
        s.setEnabled(false);
        addAndMakeVisible(s);

        l.setText(labelText, juce::dontSendNotification);
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
    stretchSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    stretchSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 60, 20);
    stretchLabel.setJustificationType(juce::Justification::centredRight);

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
        g.fillAll(SampleForgeLookAndFeel::getAccentColour().withAlpha(0.25f));

    auto* zone = audioProcessor.getZone(rowNumber);
    if (zone == nullptr)
        return;

    auto noteName = [](int note) { return juce::MidiMessage::getMidiNoteName(note, true, true, 3); };

    juce::String text = juce::File(zone->filePath).getFileNameWithoutExtension()
                       + "  |  " + noteName(zone->keyLow.load()) + "-" + noteName(zone->keyHigh.load())
                       + "  |  vel " + juce::String(zone->velLow.load()) + "-" + juce::String(zone->velHigh.load())
                       + "  |  root " + noteName(zone->rootNote.load())
                       + (zone->reverse.load() ? "  |  REV" : "")
                       + (zone->rrGroup.load() > 0 ? "  |  RR" + juce::String(zone->rrGroup.load()) : "");

    g.setColour(juce::Colours::white);
    g.setFont(13.0f);
    g.drawText(text, 6, 0, width - 10, height, juce::Justification::centredLeft, true);
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
    g.fillAll(SampleForgeLookAndFeel::getWindowBackgroundColour());

    juce::Rectangle<int> titleBar(0, 0, getWidth(), 44);
    g.setColour(SampleForgeLookAndFeel::getPanelColour());
    g.fillRect(titleBar);

    g.setColour(SampleForgeLookAndFeel::getAccentColour());
    g.fillRect(0, 42, getWidth(), 2);

    g.setColour(SampleForgeLookAndFeel::getTextColour());
    g.setFont(22.0f);
    g.drawText("SampleForge", 20, 0, 400, 44, juce::Justification::centredLeft, true);

    drawPanel(g, zonePanelBounds, "Zone");
    drawPanel(g, envelopePanelBounds, "Envelope");
    drawPanel(g, filterPanelBounds, "Filter");
    drawPanel(g, playbackPanelBounds, "Playback");
}

void SampleForgeAudioProcessorEditor::drawPanel(juce::Graphics& g, juce::Rectangle<int> bounds, const juce::String& title)
{
    if (bounds.isEmpty())
        return;

    auto panelColour = SampleForgeLookAndFeel::getPanelColour();

    g.setColour(panelColour);
    g.fillRoundedRectangle(bounds.toFloat(), 6.0f);
    g.setColour(panelColour.brighter(0.35f));
    g.drawRoundedRectangle(bounds.toFloat().reduced(0.5f), 6.0f, 1.0f);

    g.setColour(SampleForgeLookAndFeel::getTextColour().withAlpha(0.6f));
    g.setFont(juce::Font(juce::FontOptions(13.0f).withStyle("Bold")));
    g.drawText(title, bounds.withHeight(20).withY(bounds.getY() + 2).reduced(10, 0),
               juce::Justification::centredLeft);
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
    zonePanelBounds = area.removeFromTop(230);
    auto zonePanel = zonePanelBounds.reduced(10, 4);
    zonePanel.removeFromTop(panelHeaderHeight);

    auto zoneListArea = zonePanel.removeFromLeft(360);
    auto zoneButtonRow = zoneListArea.removeFromBottom(26);
    removeZoneButton.setBounds(zoneButtonRow.removeFromLeft(zoneButtonRow.getWidth() * 2 / 3));
    reverseButton.setBounds(zoneButtonRow);
    zoneListArea.removeFromBottom(4);
    zoneListBox.setBounds(zoneListArea);

    zonePanel.removeFromLeft(20);
    auto zoneDetailColumn = zonePanel.removeFromLeft(juce::jmin(zonePanel.getWidth(), 560));
    constexpr int numZoneRows = 8;
    int zoneRowHeight = zoneDetailColumn.getHeight() / numZoneRows;
    auto layoutZoneRow = [&](juce::Component& s, juce::Label& l, int index)
    {
        auto row = zoneDetailColumn.withY(zoneDetailColumn.getY() + zoneRowHeight * index).withHeight(zoneRowHeight);
        l.setBounds(row.removeFromLeft(60));
        s.setBounds(row);
    };
    layoutZoneRow(rootNoteSlider, rootNoteLabel, 0);
    layoutZoneRow(keyLoSlider, keyLoLabel, 1);
    layoutZoneRow(keyHiSlider, keyHiLabel, 2);
    layoutZoneRow(velLoSlider, velLoLabel, 3);
    layoutZoneRow(velHiSlider, velHiLabel, 4);
    layoutZoneRow(zonePanSlider, zonePanLabel, 5);
    layoutZoneRow(zoneGainSlider, zoneGainLabel, 6);

    auto rrRow = zoneDetailColumn.withY(zoneDetailColumn.getY() + zoneRowHeight * 7).withHeight(zoneRowHeight);
    rrGroupLabel.setBounds(rrRow.removeFromLeft(60));
    rrGroupBox.setBounds(rrRow.removeFromLeft(90));
    rrRow.removeFromLeft(10);
    rrOrderLabel.setBounds(rrRow.removeFromLeft(30));
    rrOrderBox.setBounds(rrRow.removeFromLeft(110));

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
    auto layoutVerticalSlider = [&](juce::Rectangle<int> content, int width, juce::Slider& s, juce::Label& l, int index)
    {
        auto column = content.withX(content.getX() + width * index).withWidth(width);
        l.setBounds(column.removeFromTop(20));
        s.setBounds(column);
    };
    layoutVerticalSlider(envelopeContent, envelopeSliderWidth, attackSlider, attackLabel, 0);
    layoutVerticalSlider(envelopeContent, envelopeSliderWidth, decaySlider, decayLabel, 1);
    layoutVerticalSlider(envelopeContent, envelopeSliderWidth, sustainSlider, sustainLabel, 2);
    layoutVerticalSlider(envelopeContent, envelopeSliderWidth, releaseSlider, releaseLabel, 3);
    layoutVerticalSlider(envelopeContent, envelopeSliderWidth, gainSlider, gainLabel, 4);

    // Filter: the type selector sits directly above the sliders it governs.
    auto filterContent = filterPanelBounds.reduced(10, 4);
    filterContent.removeFromTop(panelHeaderHeight);
    auto filterTypeRow = filterContent.removeFromTop(26);
    filterTypeLabel.setBounds(filterTypeRow.removeFromLeft(40));
    filterTypeBox.setBounds(filterTypeRow.removeFromLeft(130));
    filterContent.removeFromTop(4);

    constexpr int numFilterSliders = 3;
    int filterSliderWidth = filterContent.getWidth() / numFilterSliders;
    layoutVerticalSlider(filterContent, filterSliderWidth, cutoffSlider, cutoffLabel, 0);
    layoutVerticalSlider(filterContent, filterSliderWidth, resonanceSlider, resonanceLabel, 1);
    layoutVerticalSlider(filterContent, filterSliderWidth, filterEnvSlider, filterEnvLabel, 2);

    // Playback: Loop mode + its Sync rate, stacked.
    auto playbackContent = playbackPanelBounds.reduced(10, 4);
    playbackContent.removeFromTop(panelHeaderHeight);
    auto loopRow = playbackContent.removeFromTop(28);
    loopModeLabel.setBounds(loopRow.removeFromLeft(40));
    loopModeBox.setBounds(loopRow.removeFromLeft(110));
    playbackContent.removeFromTop(6);
    auto syncRow = playbackContent.removeFromTop(28);
    loopSyncLabel.setBounds(syncRow.removeFromLeft(40));
    loopSyncBox.setBounds(syncRow.removeFromLeft(90));

    playbackContent.removeFromTop(10);
    auto stretchRow = playbackContent.removeFromTop(28);
    stretchLabel.setBounds(stretchRow.removeFromLeft(50));
    stretchSlider.setBounds(stretchRow);
}
