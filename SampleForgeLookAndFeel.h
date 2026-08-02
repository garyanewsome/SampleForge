#pragma once

#include <JuceHeader.h>

//==============================================================================
// One flat, single-accent look for the whole plugin (Vital/NI-style), in
// place of the earlier hand-painted blue/orange gradient theme. Built on top
// of LookAndFeel_V4's midnight scheme rather than a from-scratch ColourScheme
// so the built-in widget drawing (sliders, buttons, combo boxes) stays
// consistent without overriding every draw method by hand.
class SampleForgeLookAndFeel : public juce::LookAndFeel_V4
{
public:
    SampleForgeLookAndFeel()
    {
        setColourScheme(LookAndFeel_V4::getMidnightColourScheme());

        auto accent = getAccentColour();
        auto panel = getPanelColour();

        setColour(juce::Slider::thumbColourId, accent);
        setColour(juce::Slider::trackColourId, accent);
        setColour(juce::Slider::backgroundColourId, panel);
        setColour(juce::Slider::textBoxTextColourId, getTextColour());
        setColour(juce::Slider::textBoxBackgroundColourId, panel);
        setColour(juce::Slider::textBoxOutlineColourId, panel.brighter(0.2f));

        setColour(juce::TextButton::buttonColourId, panel);
        setColour(juce::TextButton::buttonOnColourId, accent);
        setColour(juce::TextButton::textColourOffId, getTextColour());
        setColour(juce::TextButton::textColourOnId, juce::Colours::black);

        setColour(juce::ComboBox::backgroundColourId, panel);
        setColour(juce::ComboBox::outlineColourId, panel.brighter(0.2f));
        setColour(juce::ComboBox::textColourId, getTextColour());

        setColour(juce::ListBox::backgroundColourId, getWindowBackgroundColour());
        setColour(juce::ListBox::textColourId, getTextColour());

        setColour(juce::Label::textColourId, getTextColour());
    }

    // A single accent color used deliberately everywhere something needs to
    // stand out (waveform, zone selection, controls); everything else stays
    // neutral, which is the core of the "clean modern flat" look.
    static juce::Colour getAccentColour() { return juce::Colour (0xff5b8def); }
    static juce::Colour getPanelColour() { return juce::Colour (0xff24262b); }
    static juce::Colour getWindowBackgroundColour() { return juce::Colour (0xff1b1c20); }
    static juce::Colour getTextColour() { return juce::Colour (0xffe8e9ec); }
};
