#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "SampleForgeLookAndFeel.h"

//==============================================================================
// Waveform view with draggable Start/End trim handles for the
// currently selected zone. Peaks are drawn straight from that zone's own
// juce::AudioThumbnail (built once in loadZoneInternal), so switching zones
// or resizing never rescans sample data; a change listener on the thumbnail
// repaints as it fills in progressively for freshly-added zones.
class WaveformDisplay : public juce::Component,
                         private juce::ChangeListener,
                         public juce::FileDragAndDropTarget
{
public:
    explicit WaveformDisplay(SampleForgeAudioProcessor& processor);
    ~WaveformDisplay() override;

    void paint(juce::Graphics&) override;

    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;

    // juce::FileDragAndDropTarget
    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void fileDragEnter(const juce::StringArray& files, int x, int y) override;
    void fileDragExit(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;

private:
    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    void attachToSelectedZoneThumbnail();
    void updateDrag(int x);

    enum class DragTarget { none, start, end };

    SampleForgeAudioProcessor& audioProcessor;
    juce::AudioThumbnail* observedThumbnail = nullptr;
    DragTarget dragging = DragTarget::none;
    bool fileDragHover = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WaveformDisplay)
};

//==============================================================================
class SampleForgeAudioProcessorEditor  : public juce::AudioProcessorEditor,
                                          private juce::ChangeListener,
                                          private juce::ListBoxModel,
                                          public juce::FileDragAndDropTarget
{
public:
    SampleForgeAudioProcessorEditor(SampleForgeAudioProcessor&);
    ~SampleForgeAudioProcessorEditor() override;

    //==============================================================================
    void paint(juce::Graphics&) override;
    void resized() override;

    // juce::FileDragAndDropTarget: catches drops anywhere else in the window
    // that isn't itself a more specific drop target (e.g. WaveformDisplay).
    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;

private:
    void changeListenerCallback(juce::ChangeBroadcaster*) override;

    // juce::ListBoxModel
    int getNumRows() override;
    void paintListBoxItem(int rowNumber, juce::Graphics&, int width, int height, bool rowIsSelected) override;
    void listBoxItemClicked(int row, const juce::MouseEvent&) override;

    // Pulls the selected zone's key/vel/root values into the detail sliders
    // without re-triggering onValueChange (which would write them straight back).
    void refreshZoneDetailControls();

    // Draws a labeled, bordered section background behind a group of
    // controls (Zone/Envelope/Filter/Playback), so related controls read as
    // one unit instead of an undifferentiated row of faders.
    void drawPanel(juce::Graphics&, juce::Rectangle<int> bounds, const juce::String& title);

    SampleForgeAudioProcessor& audioProcessor;

    SampleForgeLookAndFeel lookAndFeel;

    WaveformDisplay waveformDisplay;

    juce::TextButton loadButton;
    juce::TextButton savePresetButton;
    juce::TextButton loadPresetButton;
    juce::Label fileNameLabel;

    juce::ComboBox loopModeBox;
    juce::Label loopModeLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> loopModeAtt;

    juce::ComboBox loopSyncBox;
    juce::Label loopSyncLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> loopSyncAtt;

    juce::ComboBox rrOrderBox;
    juce::Label rrOrderLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> rrOrderAtt;

    juce::ComboBox filterTypeBox;
    juce::Label filterTypeLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> filterTypeAtt;

    // Zone map: list of loaded zones plus per-zone key/vel/root controls for
    // whichever one is selected.
    juce::ListBox zoneListBox { "Zones", this };
    juce::TextButton removeZoneButton;
    juce::ToggleButton reverseButton;

    juce::Slider rootNoteSlider, keyLoSlider, keyHiSlider, velLoSlider, velHiSlider, zonePanSlider, zoneGainSlider;
    juce::Label rootNoteLabel, keyLoLabel, keyHiLabel, velLoLabel, velHiLabel, zonePanLabel, zoneGainLabel;

    // A combo ("None"/"1".../"15") reads far more clearly than a 0-15 slider,
    // and sits right next to RR Order since the two only make sense together.
    juce::ComboBox rrGroupBox;
    juce::Label rrGroupLabel;

    juce::Slider attackSlider, decaySlider, sustainSlider, releaseSlider, gainSlider, cutoffSlider, resonanceSlider, filterEnvSlider;
    juce::Label attackLabel, decayLabel, sustainLabel, releaseLabel, gainLabel, cutoffLabel, resonanceLabel, filterEnvLabel;

    // Lives in the Playback panel, under Loop/Sync — RubberBandStretcher-backed;
    // only takes effect for non-looping, non-reversed zones (see SampleForgeVoice).
    juce::Slider stretchSlider;
    juce::Label stretchLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> stretchAtt;

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attackAtt, decayAtt, sustainAtt, releaseAtt, gainAtt,
        cutoffAtt, resonanceAtt, filterEnvAtt;

    juce::MidiKeyboardComponent midiKeyboard;

    std::unique_ptr<juce::FileChooser> fileChooser;

    // Cached by resized(), drawn by paint() and drawPanel().
    juce::Rectangle<int> zonePanelBounds, envelopePanelBounds, filterPanelBounds, playbackPanelBounds;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SampleForgeAudioProcessorEditor)
};
