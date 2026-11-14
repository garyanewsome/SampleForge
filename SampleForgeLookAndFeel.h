#pragma once

#include <JuceHeader.h>

//==============================================================================
// HUD look: hairline white on black, like a heads-up display. Thin strokes,
// corner brackets instead of boxes, small letter-spaced mono labels, scale
// ticks and ring-and-crosshair thumbs. The only other colour is a warm red
// reserved for "something is wrong" (missing samples).
//==============================================================================
class SampleForgeLookAndFeel : public juce::LookAndFeel_V4
{
public:
    static juce::Colour getWindowBackgroundColour() { return juce::Colour(0xff020203); }
    static juce::Colour getPanelColour()            { return juce::Colour(0xff060608); }
    static juce::Colour getLineColour()             { return juce::Colour(0xff2a2c32); }
    static juce::Colour getAccentColour()           { return juce::Colour(0xfff2f5ff); } // glowing white
    static juce::Colour getTextColour()             { return juce::Colour(0xfff2f5ff); }
    static juce::Colour getDimTextColour()          { return juce::Colour(0xffadb2c2); }
    static juce::Colour getWarningColour()          { return juce::Colour(0xffff7a7a); }

    static juce::Font uiFont(float height, bool bold = false)
    {
        return juce::Font(juce::FontOptions("Helvetica Neue", height, bold ? juce::Font::bold : juce::Font::plain));
    }
    static juce::Font monoFont(float height, bool bold = false)
    {
        return juce::Font(juce::FontOptions("Menlo", height, bold ? juce::Font::bold : juce::Font::plain));
    }
    // Small, letter-spaced mono face used for all labels.
    static juce::Font hudFont(float height, float spacing = 0.12f, bool bold = false)
    {
        return juce::Font(juce::FontOptions("Menlo", height, bold ? juce::Font::bold : juce::Font::plain)
                              .withKerningFactor(spacing));
    }

    // Text with an optional faint halo (two light rings). Keep `glow` low for
    // body text so it stays crisp.
    static void drawGlowText(juce::Graphics& g, const juce::String& text, juce::Rectangle<int> area,
                             juce::Justification justification, const juce::Font& font,
                             juce::Colour colour = getTextColour(), float glow = 1.0f)
    {
        g.setFont(font);
        if (glow > 0.0f)
        {
            struct Ring { int d; float alpha; };
            for (auto ring : { Ring { 2, 0.045f }, Ring { 1, 0.10f } })
            {
                g.setColour(colour.withAlpha(ring.alpha * glow));
                for (int dx = -1; dx <= 1; ++dx)
                    for (int dy = -1; dy <= 1; ++dy)
                        if (dx != 0 || dy != 0)
                            g.drawText(text, area.translated(dx * ring.d, dy * ring.d), justification, true);
            }
        }
        g.setColour(colour);
        g.drawText(text, area, justification, true);
    }

    // Four corner marks (and an optional faint connecting hairline), with a soft halo.
    static void drawBrackets(juce::Graphics& g, juce::Rectangle<float> r, float len, float thickness,
                             juce::Colour colour = getAccentColour(), float hairline = 0.10f)
    {
        if (hairline > 0.0f)
        {
            g.setColour(colour.withAlpha(hairline));
            g.drawRect(r, 1.0f);
        }
        juce::Path p;
        auto corner = [&](float x, float y, float dx, float dy) {
            p.startNewSubPath(x + dx * len, y);
            p.lineTo(x, y);
            p.lineTo(x, y + dy * len);
        };
        corner(r.getX(), r.getY(), 1.0f, 1.0f);
        corner(r.getRight(), r.getY(), -1.0f, 1.0f);
        corner(r.getX(), r.getBottom(), 1.0f, -1.0f);
        corner(r.getRight(), r.getBottom(), -1.0f, -1.0f);
        g.setColour(colour.withAlpha(0.14f));
        g.strokePath(p, juce::PathStrokeType(thickness + 3.0f));
        g.setColour(colour.withAlpha(0.9f));
        g.strokePath(p, juce::PathStrokeType(thickness));
    }

    SampleForgeLookAndFeel()
    {
        const auto white = getTextColour();

        setColour(juce::ResizableWindow::backgroundColourId, getWindowBackgroundColour());

        setColour(juce::Label::textColourId, white);
        setColour(juce::Slider::textBoxTextColourId, white);
        setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::black);
        setColour(juce::Slider::textBoxOutlineColourId, getLineColour().brighter(0.2f));
        setColour(juce::Slider::textBoxHighlightColourId, white.withAlpha(0.25f));
        setColour(juce::Slider::thumbColourId, white);
        setColour(juce::Slider::trackColourId, white);
        setColour(juce::Slider::backgroundColourId, getLineColour());

        setColour(juce::TextButton::buttonColourId, juce::Colours::black);
        setColour(juce::TextButton::buttonOnColourId, white.withAlpha(0.2f));
        setColour(juce::TextButton::textColourOffId, white);
        setColour(juce::TextButton::textColourOnId, white);
        setColour(juce::ToggleButton::textColourId, white);
        setColour(juce::ToggleButton::tickColourId, white);
        setColour(juce::ToggleButton::tickDisabledColourId, getLineColour());

        setColour(juce::ComboBox::backgroundColourId, juce::Colours::black);
        setColour(juce::ComboBox::outlineColourId, getLineColour());
        setColour(juce::ComboBox::textColourId, white);
        setColour(juce::ComboBox::arrowColourId, white);
        setColour(juce::PopupMenu::backgroundColourId, juce::Colour(0xff050507));
        setColour(juce::PopupMenu::textColourId, white);
        setColour(juce::PopupMenu::highlightedBackgroundColourId, white.withAlpha(0.14f));
        setColour(juce::PopupMenu::highlightedTextColourId, white);

        setColour(juce::ListBox::backgroundColourId, juce::Colours::black);
        setColour(juce::ListBox::outlineColourId, getLineColour());
        setColour(juce::ListBox::textColourId, white);

        setColour(juce::MidiKeyboardComponent::whiteNoteColourId, juce::Colour(0xff121317));
        setColour(juce::MidiKeyboardComponent::blackNoteColourId, juce::Colour(0xff000000));
        setColour(juce::MidiKeyboardComponent::keySeparatorLineColourId, getLineColour());
        setColour(juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId, white.withAlpha(0.10f));
        setColour(juce::MidiKeyboardComponent::keyDownOverlayColourId, white.withAlpha(0.75f));
        setColour(juce::MidiKeyboardComponent::textLabelColourId, getDimTextColour());
        setColour(juce::MidiKeyboardComponent::shadowColourId, juce::Colours::transparentBlack);
    }

    //==========================================================================
    juce::Font getLabelFont(juce::Label& l) override
    {
        const bool isSliderText = dynamic_cast<juce::Slider*>(l.getParentComponent()) != nullptr;
        return isSliderText ? monoFont(11.0f) : hudFont(10.5f, 0.14f);
    }

    void drawLabel(juce::Graphics& g, juce::Label& label) override
    {
        g.fillAll(label.findColour(juce::Label::backgroundColourId));

        if (label.isBeingEdited())
            return;

        const bool isSliderText = dynamic_cast<juce::Slider*>(label.getParentComponent()) != nullptr;
        const auto area = getLabelBorderSize(label).subtractedFrom(label.getLocalBounds());
        const float alpha = label.isEnabled() ? 1.0f : 0.4f;

        drawGlowText(g, isSliderText ? label.getText() : label.getText().toUpperCase(), area,
                     label.getJustificationType(), getLabelFont(label),
                     label.findColour(juce::Label::textColourId).withMultipliedAlpha(alpha),
                     isSliderText ? 0.0f : 0.3f);

        if (isSliderText)
        {
            g.setColour(label.findColour(juce::Label::outlineColourId));
            g.drawRect(label.getLocalBounds());
        }
    }

    juce::Font getTextButtonFont(juce::TextButton&, int) override { return hudFont(11.0f, 0.12f, true); }
    juce::Font getComboBoxFont(juce::ComboBox&) override { return hudFont(10.5f, 0.1f); }
    juce::Font getPopupMenuFont() override { return hudFont(11.0f, 0.08f); }

    //==========================================================================
    void drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height,
                          float sliderPos, float, float,
                          juce::Slider::SliderStyle style, juce::Slider& slider) override
    {
        const bool active = slider.isMouseOverOrDragging();
        const float enabledAlpha = slider.isEnabled() ? 1.0f : 0.35f;
        const auto white = getTextColour().withMultipliedAlpha(enabledAlpha);

        if (style == juce::Slider::LinearVertical)
        {
            drawVerticalFader(g, x, y, width, height, sliderPos, slider, active, enabledAlpha);
            return;
        }

        if (style != juce::Slider::LinearHorizontal)
        {
            LookAndFeel_V4::drawLinearSlider(g, x, y, width, height, sliderPos, 0, 0, style, slider);
            return;
        }

        // Hairline track with end ticks, lit up to a ring-and-crosshair thumb.
        const float cy = (float) y + (float) height * 0.5f;
        const float left = (float) x + 7.0f, right = (float) x + (float) width - 7.0f;
        const float pos = juce::jlimit(left, right, sliderPos);

        g.setColour(getLineColour().brighter(0.25f).withMultipliedAlpha(enabledAlpha));
        g.fillRect(left, cy - 0.5f, right - left, 1.0f);
        g.fillRect(left - 0.5f, cy - 3.0f, 1.0f, 6.0f);
        g.fillRect(right - 0.5f, cy - 3.0f, 1.0f, 6.0f);

        g.setColour(white.withAlpha((active ? 0.20f : 0.10f) * enabledAlpha));
        g.fillRect(left, cy - 2.0f, pos - left, 4.0f);
        g.setColour(white);
        g.fillRect(left, cy - 0.75f, pos - left, 1.5f);

        const float r = 5.0f;
        g.setColour(white.withAlpha((active ? 0.28f : 0.14f) * enabledAlpha));
        g.fillEllipse(pos - r - 3.0f, cy - r - 3.0f, (r + 3.0f) * 2.0f, (r + 3.0f) * 2.0f);
        g.setColour(juce::Colours::black);
        g.fillEllipse(pos - r, cy - r, r * 2.0f, r * 2.0f);
        g.setColour(white);
        g.drawEllipse(pos - r, cy - r, r * 2.0f, r * 2.0f, 1.0f);
        g.fillEllipse(pos - 1.2f, cy - 1.2f, 2.4f, 2.4f);
        g.fillRect(pos - 0.5f, cy - r - 4.0f, 1.0f, 3.0f);   // crosshair ticks
        g.fillRect(pos - 0.5f, cy + r + 1.0f, 1.0f, 3.0f);
    }

    void drawVerticalFader(juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                           juce::Slider& slider, bool active, float enabledAlpha)
    {
        const auto white = getTextColour().withMultipliedAlpha(enabledAlpha);
        const float cx = (float) x + (float) width * 0.5f;
        const float capH = 14.0f, capW = juce::jmin(30.0f, (float) width - 8.0f);

        const float top = (float) y + capH * 0.5f;
        const float bottom = (float) y + (float) height - capH * 0.5f;
        const float pos = juce::jlimit(top, bottom, sliderPos);

        // Scale: one tick per detent on stepped controls, otherwise ten divisions.
        int divisions = 10;
        const double interval = slider.getInterval();
        if (interval > 0.0)
        {
            const int steps = juce::roundToInt((slider.getMaximum() - slider.getMinimum()) / interval);
            if (steps >= 1 && steps <= 12)
                divisions = steps;
        }
        const bool stepped = divisions != 10;
        for (int i = 0; i <= divisions; ++i)
        {
            const float ty = bottom - (bottom - top) * (float) i / (float) divisions;
            const bool major = stepped || (i % 5 == 0);
            const float len = major ? 10.0f : 5.0f;
            g.setColour(getDimTextColour().withAlpha(major ? 0.75f : 0.38f));
            g.fillRect(cx - capW * 0.5f - 5.0f - len, ty - 0.5f, len, 1.0f);
            g.fillRect(cx + capW * 0.5f + 5.0f,       ty - 0.5f, len, 1.0f);
        }

        // Hairline rail with end caps, lit up to the cap.
        g.setColour(getLineColour().brighter(0.3f));
        g.fillRect(cx - 0.5f, top - 4.0f, 1.0f, bottom - top + 8.0f);
        g.fillRect(cx - 4.0f, top - 4.5f, 8.0f, 1.0f);
        g.fillRect(cx - 4.0f, bottom + 3.5f, 8.0f, 1.0f);

        g.setColour(white.withAlpha(active ? 0.22f : 0.11f));
        g.fillRect(cx - 2.5f, pos, 5.0f, bottom - pos + 2.0f);
        g.setColour(white);
        g.fillRect(cx - 0.75f, pos, 1.5f, bottom - pos + 2.0f);

        // Slim cap: black body, white edge, lit centre mark.
        const juce::Rectangle<float> cap(cx - capW * 0.5f, pos - capH * 0.5f, capW, capH);
        const float ch = 3.0f;
        juce::Path body;
        body.startNewSubPath(cap.getX() + ch, cap.getY());
        body.lineTo(cap.getRight() - ch, cap.getY());
        body.lineTo(cap.getRight(), cap.getY() + ch);
        body.lineTo(cap.getRight(), cap.getBottom() - ch);
        body.lineTo(cap.getRight() - ch, cap.getBottom());
        body.lineTo(cap.getX() + ch, cap.getBottom());
        body.lineTo(cap.getX(), cap.getBottom() - ch);
        body.lineTo(cap.getX(), cap.getY() + ch);
        body.closeSubPath();

        g.setColour(white.withAlpha(active ? 0.26f : 0.13f));
        g.strokePath(body, juce::PathStrokeType(5.0f));
        g.setColour(juce::Colour(0xff030304));
        g.fillPath(body);
        g.setColour(white);
        g.strokePath(body, juce::PathStrokeType(1.0f));
        g.fillRect(cx - capW * 0.5f + 4.0f, pos - 0.5f, capW - 8.0f, 1.0f);
    }

    //==========================================================================
    void drawButtonBackground(juce::Graphics& g, juce::Button& b, const juce::Colour&,
                              bool highlighted, bool down) override
    {
        auto r = b.getLocalBounds().toFloat().reduced(1.5f);
        const auto white = getTextColour();
        g.setColour(juce::Colours::black);
        g.fillRect(r);
        const bool lit = b.getToggleState();   // a selected tab
        if (lit || highlighted || down)
        {
            g.setColour(white.withAlpha(lit ? 0.16f : (down ? 0.20f : 0.08f)));
            g.fillRect(r);
        }
        drawBrackets(g, r, juce::jmin(7.0f, r.getHeight() * 0.4f), lit ? 1.6f : 1.2f, white.withAlpha(b.isEnabled() ? 1.0f : 0.4f),
                     lit ? 0.7f : (highlighted || down ? 0.55f : 0.30f));
    }

    void drawButtonText(juce::Graphics& g, juce::TextButton& b, bool, bool) override
    {
        drawGlowText(g, b.getButtonText().toUpperCase(), b.getLocalBounds().reduced(4, 0), juce::Justification::centred,
                     getTextButtonFont(b, b.getHeight()), getTextColour().withMultipliedAlpha(b.isEnabled() ? 1.0f : 0.4f), 0.3f);
    }

    void drawToggleButton(juce::Graphics& g, juce::ToggleButton& b, bool highlighted, bool) override
    {
        const float s = juce::jmin(15.0f, (float) b.getHeight() - 8.0f);
        const juce::Rectangle<float> box(5.0f, ((float) b.getHeight() - s) * 0.5f, s, s);
        const auto white = getTextColour().withMultipliedAlpha(b.isEnabled() ? 1.0f : 0.4f);

        g.setColour(juce::Colours::black);
        g.fillRect(box);
        if (b.getToggleState())
        {
            g.setColour(white.withAlpha(0.2f));
            g.drawRect(box.expanded(2.0f), 2.0f);
            g.setColour(white);
            g.fillRect(box.reduced(3.5f));
        }
        g.setColour(highlighted || b.getToggleState() ? white : white.withAlpha(0.6f));
        g.drawRect(box, 1.0f);

        drawGlowText(g, b.getButtonText().toUpperCase(), b.getLocalBounds().withTrimmedLeft((int) s + 14),
                     juce::Justification::centredLeft, hudFont(10.5f, 0.14f), white, 0.3f);
    }

    void drawComboBox(juce::Graphics& g, int width, int height, bool, int, int, int, int,
                      juce::ComboBox& box) override
    {
        auto r = juce::Rectangle<float>(0, 0, (float) width, (float) height).reduced(1.0f);
        g.setColour(juce::Colours::black);
        g.fillRect(r);
        drawBrackets(g, r, 6.0f, 1.0f, getTextColour().withAlpha(box.isEnabled() ? 1.0f : 0.4f),
                     box.isMouseOver() ? 0.5f : 0.28f);

        juce::Path arrow;
        const float cx = (float) width - 13.0f, cy = (float) height * 0.5f;
        arrow.addTriangle(cx - 3.5f, cy - 2.0f, cx + 3.5f, cy - 2.0f, cx, cy + 2.5f);
        g.setColour(getTextColour());
        g.fillPath(arrow);
    }

    void positionComboBoxText(juce::ComboBox& box, juce::Label& label) override
    {
        label.setBounds(9, 1, box.getWidth() - 28, box.getHeight() - 2);
        label.setFont(getComboBoxFont(box));
    }
};
