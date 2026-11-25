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
    g.setColour(LAF::getTickColour().withAlpha(0.8f));
    for (int i = 0; i <= 20; ++i)
    {
        const float x = juce::jlimit(0.5f, bounds.getWidth() - 0.5f, bounds.getWidth() * (float) i / 20.0f);
        const bool major = (i % 2 == 0);
        g.fillRect(x - 0.5f, bounds.getBottom() - (major ? 8.0f : 4.0f), 1.0f, major ? 8.0f : 4.0f);
    }
    g.setColour(LAF::getDimTextColour());
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
ArpDisplay::ArpDisplay(SampleForgeAudioProcessor& p) : audioProcessor(p)
{
    startTimerHz(30);
}

ArpDisplay::~ArpDisplay()
{
    stopTimer();
}

void ArpDisplay::timerCallback()
{
    const auto& a = audioProcessor.getArpeggiator();
    const int length = a.displayLength.load(), index = a.displayIndex.load();
    const bool on = audioProcessor.apvts.getRawParameterValue("ARP_ON")->load() > 0.5f;
    const float gate = audioProcessor.apvts.getRawParameterValue("ARP_GATE")->load();
    if (length != lastLength || index != lastIndex || on != lastOn || gate != lastGate)
    {
        lastLength = length; lastIndex = index; lastOn = on; lastGate = gate;
        repaint();
    }
}

void ArpDisplay::paint(juce::Graphics& g)
{
    using LAF = SampleForgeLookAndFeel;
    auto bounds = getLocalBounds().toFloat();
    g.setColour(juce::Colours::black);
    g.fillRect(bounds);

    // Graticule, as on the waveform.
    g.setColour(LAF::getAccentColour().withAlpha(0.08f));
    for (int i = 1; i < 4; ++i)
        g.fillRect(0.0f, bounds.getHeight() * (float) i / 4.0f - 0.5f, bounds.getWidth(), 1.0f);

    const auto& a = audioProcessor.getArpeggiator();
    const int length = a.displayLength.load();
    const int active = a.displayIndex.load();
    const bool on = audioProcessor.apvts.getRawParameterValue("ARP_ON")->load() > 0.5f;

    LAF::drawBrackets(g, bounds.reduced(1.5f), 14.0f, 1.5f, LAF::getAccentColour(), 0.0f);

    if (!on || length == 0)
    {
        LAF::drawGlowText(g, !on ? "ARP OFF" : "HOLD KEYS TO ARPEGGIATE", getLocalBounds(), juce::Justification::centred,
                          LAF::hudFont(12.0f, 0.25f), LAF::getTextColour().withAlpha(0.6f), 0.3f);
        return;
    }

    int lowest = 127, highest = 0;
    for (int i = 0; i < length; ++i)
    {
        const int n = a.displayNotes[i].load();
        lowest = juce::jmin(lowest, n);
        highest = juce::jmax(highest, n);
    }

    auto area = bounds.reduced(22.0f, 20.0f);
    area.removeFromBottom(16.0f); // note names
    const float colW = area.getWidth() / (float) length;
    const float barW = juce::jlimit(3.0f, 14.0f, colW * 0.32f);

    for (int i = 0; i < length; ++i)
    {
        const int note = a.displayNotes[i].load();
        const float norm = highest > lowest ? (float) (note - lowest) / (float) (highest - lowest) : 0.5f;
        const float h = area.getHeight() * (0.18f + 0.82f * norm);
        const float cx = area.getX() + colW * ((float) i + 0.5f);
        const bool lit = (i == active);
        const auto colour = LAF::getAccentColour().withAlpha(lit ? 1.0f : 0.45f);

        if (lit)
        {
            g.setColour(LAF::getAccentColour().withAlpha(0.18f));
            g.fillRect(cx - barW * 0.5f - 4.0f, area.getBottom() - h - 4.0f, barW + 8.0f, h + 4.0f);
        }
        g.setColour(colour);
        g.fillRect(cx - barW * 0.5f, area.getBottom() - h, barW, h);
        g.fillRect(cx - barW * 0.5f - 3.0f, area.getBottom() - h - 1.0f, barW + 6.0f, 1.5f);   // cap

        if (colW >= 26.0f)
        {
            g.setColour(lit ? LAF::getTextColour() : LAF::getDimTextColour());
            g.setFont(LAF::monoFont(9.0f));
            g.drawText(juce::MidiMessage::getMidiNoteName(note, true, true, 3),
                       juce::Rectangle<float>(cx - colW * 0.5f, area.getBottom() + 3.0f, colW, 12.0f), juce::Justification::centred, false);
        }
    }

    g.setColour(LAF::getAccentColour().withAlpha(0.5f));
    g.fillRect(area.getX(), area.getBottom(), area.getWidth(), 1.0f);
}

//==============================================================================
ArpLaneEditor::ArpLaneEditor(SampleForgeAudioProcessor& p) : audioProcessor(p)
{
    startTimerHz(30);
}

ArpLaneEditor::~ArpLaneEditor()
{
    stopTimer();
}

void ArpLaneEditor::timerCallback()
{
    juce::uint32 sum = (juce::uint32) audioProcessor.arpNumSteps.load();
    for (auto& s : audioProcessor.arpLane)
        sum = sum * 31u + (juce::uint32) (s.velocity.load() * 1000.0f) + (juce::uint32) s.zone.load() * 7u
              + (s.reverse.load() ? 3u : 0u) + (s.tie.load() ? 5u : 0u);
    const int step = audioProcessor.getArpeggiator().displayStep.load();
    if (step != lastStep || sum != lastSum)
    {
        lastStep = step; lastSum = sum;
        repaint();
    }
}

namespace
{
    constexpr int laneHeader = 16, laneRow = 18;
}

int ArpLaneEditor::columnAt(int x) const
{
    const float colW = (float) getWidth() / (float) arp::maxSteps;
    return juce::jlimit(0, arp::maxSteps - 1, (int) ((float) x / colW));
}

void ArpLaneEditor::setVelocityFromY(int column, int y)
{
    const int top = laneHeader, bottom = getHeight() - 3 * laneRow;
    const float v = 1.0f - (float) (y - top) / (float) juce::jmax(1, bottom - top);
    audioProcessor.arpLane[(size_t) column].velocity = juce::jlimit(0.0f, 1.0f, std::round(v * 100.0f) / 100.0f);
    repaint();
}

void ArpLaneEditor::mouseDown(const juce::MouseEvent& e)
{
    const int col = columnAt(e.x);
    auto& step = audioProcessor.arpLane[(size_t) col];
    const int rowsTop = getHeight() - 3 * laneRow;
    dragColumn = -1;

    if (e.y < laneHeader)
        audioProcessor.arpNumSteps = col + 1;
    else if (e.y < rowsTop)
    {
        dragColumn = col;
        setVelocityFromY(col, e.y);
    }
    else if (e.y < rowsTop + laneRow)
        step.tie = !step.tie.load();
    else if (e.y < rowsTop + 2 * laneRow)
        step.reverse = !step.reverse.load();
    else
    {
        const int n = juce::jmax(1, audioProcessor.getNumZones());
        const int dir = e.mods.isPopupMenu() ? -1 : 1;
        step.zone = ((step.zone.load() + dir) % (n + 1) + (n + 1)) % (n + 1);
    }
    repaint();
}

void ArpLaneEditor::mouseDrag(const juce::MouseEvent& e)
{
    if (dragColumn >= 0)
        setVelocityFromY(dragColumn, e.y);
}

void ArpLaneEditor::paint(juce::Graphics& g)
{
    using LAF = SampleForgeLookAndFeel;
    auto bounds = getLocalBounds().toFloat();
    g.setColour(juce::Colours::black);
    g.fillRect(bounds);

    const int numSteps = audioProcessor.arpNumSteps.load();
    const int active = audioProcessor.getArpeggiator().displayStep.load();
    const float colW = bounds.getWidth() / (float) arp::maxSteps;
    const float barTop = (float) laneHeader, barBottom = bounds.getHeight() - 3.0f * laneRow;
    const float rowsTop = barBottom;

    for (int i = 0; i < arp::maxSteps; ++i)
    {
        const auto& s = audioProcessor.arpLane[(size_t) i];
        const bool inLane = i < numSteps;
        const float a = inLane ? 1.0f : 0.25f;
        const float x = colW * (float) i;
        auto cell = juce::Rectangle<float>(x + 2.0f, 0.0f, colW - 4.0f, bounds.getHeight());

        if (i == active)
        {
            g.setColour(LAF::getAccentColour().withAlpha(0.12f));
            g.fillRect(cell);
        }

        g.setFont(LAF::monoFont(9.0f));
        g.setColour((i == active ? LAF::getTextColour() : LAF::getDimTextColour()).withAlpha(a));
        g.drawText(juce::String(i + 1), cell.withHeight((float) laneHeader), juce::Justification::centred, false);

        const float v = s.velocity.load();
        const float h = (barBottom - barTop - 4.0f) * v;
        g.setColour(LAF::getLineColour().withAlpha(a));
        g.fillRect(cell.getX() + 3.0f, barTop, cell.getWidth() - 6.0f, barBottom - barTop - 2.0f);
        g.setColour(LAF::getAccentColour().withAlpha((s.tie.load() ? 0.45f : 0.85f) * a));
        g.fillRect(cell.getX() + 3.0f, barBottom - 2.0f - h, cell.getWidth() - 6.0f, h);

        auto drawCell = [&](int row, const juce::String& text, bool on) {
            auto r = juce::Rectangle<float>(cell.getX() + 3.0f, rowsTop + (float) (row * laneRow) + 1.0f, cell.getWidth() - 6.0f, (float) laneRow - 2.0f);
            g.setColour(on ? LAF::getAccentColour().withAlpha(0.9f * a) : LAF::getLineColour().withAlpha(a));
            on ? g.fillRect(r) : g.drawRect(r, 1.0f);
            g.setColour(on ? juce::Colours::black : LAF::getDimTextColour().withAlpha(a));
            g.setFont(LAF::monoFont(9.0f, on));
            g.drawText(text, r, juce::Justification::centred, false);
        };
        drawCell(0, "T", s.tie.load());
        drawCell(1, "R", s.reverse.load());
        drawCell(2, s.zone.load() > 0 ? juce::String(s.zone.load()) : juce::String("-"), s.zone.load() > 0);
    }

    LAF::drawBrackets(g, bounds.reduced(1.5f), 14.0f, 1.5f, LAF::getAccentColour(), 0.0f);
}

//==============================================================================
SampleForgeAudioProcessorEditor::SampleForgeAudioProcessorEditor(SampleForgeAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p), waveformDisplay(p), arpDisplay(p), arpLaneEditor(p),
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

    // Arpeggiator page.
    arpOnButton.setButtonText("Arp On");
    arpOnAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(audioProcessor.apvts, "ARP_ON", arpOnButton);
    addAndMakeVisible(arpOnButton);
    arpLatchButton.setButtonText("Latch");
    arpLatchAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(audioProcessor.apvts, "ARP_LATCH", arpLatchButton);
    addAndMakeVisible(arpLatchButton);

    arpPatternBox.addItemList(arp::patternNames(), 1);
    arpPatternAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(audioProcessor.apvts, "ARP_PATTERN", arpPatternBox);
    arpPatternLabel.setText("Pattern", juce::dontSendNotification);
    arpRateBox.addItemList(arp::rateNames(), 1);
    arpRateAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(audioProcessor.apvts, "ARP_RATE", arpRateBox);
    arpRateLabel.setText("Rate", juce::dontSendNotification);
    for (auto* c : std::initializer_list<juce::Component*> { &arpPatternBox, &arpRateBox, &arpPatternLabel, &arpRateLabel })
        addAndMakeVisible(c);

    setupSlider(arpOctavesSlider, arpOctavesLabel, "Octaves", arpOctavesAtt, "ARP_OCTAVES");
    arpOctavesSlider.setNumDecimalPlacesToDisplay(0);
    setupSlider(arpGateSlider, arpGateLabel, "Gate", arpGateAtt, "ARP_GATE");
    arpGateSlider.textFromValueFunction = fracToPercent;
    arpGateSlider.valueFromTextFunction = [](const juce::String& t) { return t.getDoubleValue() / 100.0; };
    arpGateSlider.updateText();
    setupSlider(arpSwingSlider, arpSwingLabel, "Swing", arpSwingAtt, "ARP_SWING");
    arpSwingSlider.textFromValueFunction = fracToPercent;
    arpSwingSlider.valueFromTextFunction = [](const juce::String& t) { return t.getDoubleValue() / 100.0; };
    arpSwingSlider.updateText();
    addAndMakeVisible(arpDisplay);
    addAndMakeVisible(arpLaneEditor);

    // Tabs and pages.
    auto setupTab = [this](juce::TextButton& b, const juce::String& text, int page)
    {
        b.setButtonText(text);
        b.setClickingTogglesState(true);
        b.setRadioGroupId(7001);
        b.onClick = [this, page] { setPage(page); };
        addAndMakeVisible(b);
    };
    setupTab(tabZone, "Zone", 0);
    setupTab(tabSound, "Sound", 1);
    setupTab(tabArp, "Arp", 2);

    zonePage = { &zoneListBox, &removeZoneButton, &reverseButton,
                 &rootNoteSlider, &keyLoSlider, &keyHiSlider, &velLoSlider, &velHiSlider, &zonePanSlider, &zoneGainSlider,
                 &rootNoteLabel, &keyLoLabel, &keyHiLabel, &velLoLabel, &velHiLabel, &zonePanLabel, &zoneGainLabel,
                 &rrGroupBox, &rrGroupLabel, &rrOrderBox, &rrOrderLabel };
    soundPage = { &attackSlider, &decaySlider, &sustainSlider, &releaseSlider, &gainSlider,
                  &attackLabel, &decayLabel, &sustainLabel, &releaseLabel, &gainLabel,
                  &filterTypeBox, &filterTypeLabel, &cutoffSlider, &resonanceSlider, &filterEnvSlider,
                  &cutoffLabel, &resonanceLabel, &filterEnvLabel,
                  &loopModeBox, &loopModeLabel, &loopSyncBox, &loopSyncLabel,
                  &stretchSlider, &stretchLabel, &bendSlider, &bendLabel };
    arpPage = { &arpOnButton, &arpLatchButton, &arpPatternBox, &arpRateBox, &arpPatternLabel, &arpRateLabel,
                &arpOctavesSlider, &arpGateSlider, &arpSwingSlider, &arpOctavesLabel, &arpGateLabel, &arpSwingLabel,
                &arpDisplay, &arpLaneEditor };

    addAndMakeVisible(midiKeyboard);

    // The processor may already hold zones (reopening the window on a loaded
    // session), so sync the list, file label and zone controls right away
    // instead of waiting for the next change message.
    changeListenerCallback(nullptr);

    setPage(juce::jlimit(0, 2, static_cast<int>(audioProcessor.apvts.state.getProperty("uiPage", 0))));

    setSize(1280, 690);
}

void SampleForgeAudioProcessorEditor::setPage(int page)
{
    currentPage = page;
    audioProcessor.apvts.state.setProperty("uiPage", page, nullptr);

    tabZone.setToggleState(page == 0, juce::dontSendNotification);
    tabSound.setToggleState(page == 1, juce::dontSendNotification);
    tabArp.setToggleState(page == 2, juce::dontSendNotification);

    for (auto* c : zonePage)  c->setVisible(page == 0);
    for (auto* c : soundPage) c->setVisible(page == 1);
    for (auto* c : arpPage)   c->setVisible(page == 2);
    repaint();
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

    if (currentPage == 0)
        drawPanel(g, zonePanelBounds, "Zone");
    else if (currentPage == 1)
    {
        drawPanel(g, envelopePanelBounds, "Envelope");
        drawPanel(g, filterPanelBounds, "Filter");
        drawPanel(g, playbackPanelBounds, "Playback");
    }
    else
        drawPanel(g, arpPanelBounds, "Arpeggiator");
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
    constexpr int pageHeight = 236;
    constexpr int tabHeight = 28;

    auto area = getLocalBounds();

    midiKeyboard.setBounds(area.removeFromBottom(80));

    area.removeFromTop(44); // reserve room for the title drawn in paint()

    auto topBar = area.removeFromTop(36);
    loadButton.setBounds(topBar.removeFromLeft(140).reduced(4));
    savePresetButton.setBounds(topBar.removeFromLeft(120).reduced(4));
    loadPresetButton.setBounds(topBar.removeFromLeft(120).reduced(4));
    fileNameLabel.setBounds(topBar.reduced(4));

    // Bottom up: the page, a row of tabs above it, and the waveform takes the rest.
    area.reduce(10, 6);
    auto pageBounds = area.removeFromBottom(pageHeight);
    area.removeFromBottom(6);
    auto tabBar = area.removeFromBottom(tabHeight);
    area.removeFromBottom(8);
    waveformDisplay.setBounds(area);

    tabZone.setBounds(tabBar.removeFromLeft(120));
    tabBar.removeFromLeft(8);
    tabSound.setBounds(tabBar.removeFromLeft(120));
    tabBar.removeFromLeft(8);
    tabArp.setBounds(tabBar.removeFromLeft(120));

    auto layoutVerticalSlider = [&](juce::Rectangle<int> content, int width, juce::Slider& sl, juce::Label& l, int index)
    {
        auto column = content.withX(content.getX() + width * index).withWidth(width);
        l.setBounds(column.removeFromTop(20));
        sl.setBounds(column);
    };

    //==========================================================================
    // Zone page: zone list + one vertical fader per zone property, with
    // Reverse and the round-robin pair in a column on the right.
    zonePanelBounds = pageBounds;
    auto zonePanel = zonePanelBounds.reduced(10, 4);
    zonePanel.removeFromTop(panelHeaderHeight);

    auto zoneListArea = zonePanel.removeFromLeft(360);
    removeZoneButton.setBounds(zoneListArea.removeFromBottom(26));
    zoneListArea.removeFromBottom(4);
    zoneListBox.setBounds(zoneListArea);
    zonePanel.removeFromLeft(14);

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
    // Sound page: Envelope / Filter / Playback panels side by side.
    auto soundRow = pageBounds;
    envelopePanelBounds = soundRow.removeFromLeft(560);
    soundRow.removeFromLeft(panelGap);
    filterPanelBounds = soundRow.removeFromLeft(380);
    soundRow.removeFromLeft(panelGap);
    playbackPanelBounds = soundRow;

    auto envelopeContent = envelopePanelBounds.reduced(10, 4);
    envelopeContent.removeFromTop(panelHeaderHeight);
    constexpr int numEnvelopeSliders = 5;
    int envelopeSliderWidth = envelopeContent.getWidth() / numEnvelopeSliders;
    layoutVerticalSlider(envelopeContent, envelopeSliderWidth, attackSlider, attackLabel, 0);
    layoutVerticalSlider(envelopeContent, envelopeSliderWidth, decaySlider, decayLabel, 1);
    layoutVerticalSlider(envelopeContent, envelopeSliderWidth, sustainSlider, sustainLabel, 2);
    layoutVerticalSlider(envelopeContent, envelopeSliderWidth, releaseSlider, releaseLabel, 3);
    layoutVerticalSlider(envelopeContent, envelopeSliderWidth, gainSlider, gainLabel, 4);

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

    //==========================================================================
    // Arp page: switches and menus, three faders, and the live pattern display.
    arpPanelBounds = pageBounds;
    auto arpContent = arpPanelBounds.reduced(10, 4);
    arpContent.removeFromTop(panelHeaderHeight);

    auto arpLeft = arpContent.removeFromLeft(220);
    arpLeft.removeFromTop(6);
    arpOnButton.setBounds(arpLeft.removeFromTop(28));
    arpLeft.removeFromTop(4);
    arpLatchButton.setBounds(arpLeft.removeFromTop(28));
    arpLeft.removeFromTop(12);
    arpPatternLabel.setBounds(arpLeft.removeFromTop(16));
    arpPatternBox.setBounds(arpLeft.removeFromTop(28).removeFromLeft(170));
    arpLeft.removeFromTop(10);
    arpRateLabel.setBounds(arpLeft.removeFromTop(16));
    arpRateBox.setBounds(arpLeft.removeFromTop(28).removeFromLeft(170));

    arpContent.removeFromLeft(10);
    auto arpFaders = arpContent.removeFromLeft(3 * 96);
    layoutVerticalSlider(arpFaders, 96, arpOctavesSlider, arpOctavesLabel, 0);
    layoutVerticalSlider(arpFaders, 96, arpGateSlider, arpGateLabel, 1);
    layoutVerticalSlider(arpFaders, 96, arpSwingSlider, arpSwingLabel, 2);

    arpContent.removeFromLeft(14);
    auto arpRight = arpContent.withTrimmedTop(4);
    arpLaneEditor.setBounds(arpRight.removeFromBottom(juce::jmin(130, arpRight.getHeight() / 2)));
    arpRight.removeFromBottom(6);
    arpDisplay.setBounds(arpRight);
}
