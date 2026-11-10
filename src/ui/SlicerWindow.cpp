#include "ui/SlicerWindow.h"

#include <algorithm>
#include <cmath>

#include "audio/Arrangement.h"
#include "audio/MusicTheory.h"
#include "audio/SampleEdit.h"
#include "audio/Slicer.h"

namespace sls
{
namespace
{
const juce::Colour kBack (0xff1b1e24), kWave (0xff4fb3a9), kMarker (0xffe8943a), kSelected (0xff2f3640), kText (0xffd0d4dc);
}

// The loop, its markers and the selected slice.
class SlicerPanel::Wave : public juce::Component
{
public:
    explicit Wave (SlicerPanel& o) : owner (o) {}

    void setSample (const std::shared_ptr<const SampleData>& s)
    {
        if (s == sample)
            return;
        sample = s;
        peaks.clear();
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xff15171c));
        const auto& st = owner.current;
        if (sample == nullptr || sample->numFrames() < 2)
        {
            g.setColour (kText);
            g.drawText ("No loop", getLocalBounds(), juce::Justification::centred);
            return;
        }
        const int frames = sample->numFrames();
        const int w = getWidth(), h = getHeight();

        // the selected slice's background
        if (owner.selected >= 0 && owner.selected < static_cast<int> (st.starts.size()))
        {
            const int a = frameToX (st.starts[static_cast<size_t> (owner.selected)]);
            const int b = frameToX (owner.selected + 1 < static_cast<int> (st.starts.size()) ? st.starts[static_cast<size_t> (owner.selected) + 1] : frames);
            g.setColour (kSelected);
            g.fillRect (a, 0, std::max (1, b - a), h);
        }

        if (static_cast<int> (peaks.size()) != w)
        {
            peaks.assign (static_cast<size_t> (w), 0.0f);
            for (int x = 0; x < w; ++x)
            {
                const int f0 = static_cast<int> (static_cast<double> (x) / w * frames);
                const int f1 = std::max (f0 + 1, static_cast<int> (static_cast<double> (x + 1) / w * frames));
                float p = 0.0f;
                for (int f = f0; f < std::min (f1, frames); ++f)
                    p = std::max ({ p, std::abs (sample->left[static_cast<size_t> (f)]), std::abs (sample->right[static_cast<size_t> (f)]) });
                peaks[static_cast<size_t> (x)] = std::min (1.0f, p);
            }
        }
        const float mid = static_cast<float> (h) * 0.5f;
        for (int x = 0; x < w; ++x)
        {
            const int slice = slicer::sliceAt (st.starts, xToFrame (x));
            const bool reversed = slice >= 0 && slice < static_cast<int> (st.settings.size()) && st.settings[static_cast<size_t> (slice)].reverse;
            g.setColour (reversed ? kWave.withRotatedHue (0.5f) : kWave);
            const float half = peaks[static_cast<size_t> (x)] * (mid - 14.0f);
            g.drawVerticalLine (x, mid - half, mid + half + 1.0f);
        }

        // markers, with the key each slice plays on
        g.setFont (juce::FontOptions (11.0f));
        for (size_t i = 0; i < st.starts.size(); ++i)
        {
            const int x = frameToX (st.starts[i]);
            g.setColour (static_cast<int> (i) == dragging ? juce::Colours::white : kMarker);
            g.drawVerticalLine (x, 0.0f, static_cast<float> (h));
            g.fillRect (x, 0, 7, 12);
            g.setColour (kText);
            g.drawText (juce::String (theory::noteName (st.firstKey + static_cast<int> (i))), x + 9, 0, 40, 12, juce::Justification::centredLeft, false);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        auto& st = owner.current;
        if (sample == nullptr)
            return;
        const int marker = markerAt (e.x);
        if (e.mods.isPopupMenu())
        {
            if (marker > 0)
            {
                st.starts.erase (st.starts.begin() + marker);
                if (marker < static_cast<int> (st.settings.size()))
                    st.settings.erase (st.settings.begin() + marker);
                owner.select (std::min (owner.selected, static_cast<int> (st.starts.size()) - 1));
                owner.changed (true);
            }
            return;
        }
        if (marker > 0)
        {
            dragging = marker;
            return;
        }
        const int slice = slicer::sliceAt (st.starts, xToFrame (e.x));
        owner.select (slice);
        playing = st.firstKey + slice;
        if (owner.onPlayKey && playing <= 127)
            owner.onPlayKey (playing);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragging > 0)
        {
            slicer::moveStart (owner.current.starts, dragging, xToFrame (e.x), sample->numFrames());
            repaint();
        }
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (dragging > 0)
        {
            dragging = -1;
            owner.changed (true);
        }
        if (playing >= 0 && owner.onStopKey)
            owner.onStopKey (playing);
        playing = -1;
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (sample == nullptr || e.mods.isPopupMenu())
            return;
        auto& st = owner.current;
        const int index = slicer::addStart (st.starts, xToFrame (e.x), sample->numFrames());
        if (index < 0)
            return;
        if (index <= static_cast<int> (st.settings.size())) // the new slice starts with default settings
            st.settings.insert (st.settings.begin() + index, SliceSettings {});
        owner.select (index);
        owner.changed (true);
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        setMouseCursor (markerAt (e.x) > 0 ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
    }

    void resized() override { peaks.clear(); }

private:
    int frameToX (int frame) const
    {
        return sample == nullptr ? 0 : static_cast<int> (static_cast<double> (frame) / sample->numFrames() * getWidth());
    }
    int xToFrame (int x) const
    {
        return sample == nullptr ? 0 : std::clamp (static_cast<int> (static_cast<double> (x) / std::max (1, getWidth()) * sample->numFrames()), 0, sample->numFrames() - 1);
    }
    int markerAt (int x) const // a marker within 4 pixels (never the first one, which stays at 0)
    {
        const auto& starts = owner.current.starts;
        for (size_t i = 1; i < starts.size(); ++i)
            if (std::abs (frameToX (starts[i]) - x) <= 4)
                return static_cast<int> (i);
        return -1;
    }

    SlicerPanel& owner;
    std::shared_ptr<const SampleData> sample;
    std::vector<float> peaks;
    int dragging = -1;
    int playing = -1;
};

SlicerPanel::SlicerPanel() : wave (std::make_unique<Wave> (*this))
{
    addAndMakeVisible (*wave);

    const auto setupSlider = [this] (juce::Slider& s, double lo, double hi, double step, double value)
    {
        s.setSliderStyle (juce::Slider::LinearHorizontal);
        s.setTextBoxStyle (juce::Slider::TextBoxRight, false, 56, 20);
        s.setRange (lo, hi, step);
        s.setValue (value, juce::dontSendNotification);
        addAndMakeVisible (s);
    };
    for (auto* l : { &sensitivityLabel, &tempoLabel, &pitchLabel, &gainLabel, &panLabel, &sliceLabel, &hint })
    {
        l->setColour (juce::Label::textColourId, kText);
        addAndMakeVisible (*l);
    }
    hint.setFont (juce::FontOptions (12.0f));
    hint.setColour (juce::Label::textColourId, kText.withAlpha (0.7f));
    hint.setText ("Click a slice to hear it. Drag a marker to move it, double-click to add one, right-click a marker to delete it.",
                  juce::dontSendNotification);

    setupSlider (sensitivity, 0.0, 1.0, 0.01, 0.5);
    setupSlider (tempo, 60.0, 200.0, 0.01, 120.0);
    tempo.setTextValueSuffix (" BPM");
    tempo.onDragEnd = [this] { current.loopBpm = tempo.getValue(); changed (false); };
    tempo.onValueChange = [this] { if (! tempo.isMouseButtonDown()) { current.loopBpm = tempo.getValue(); changed (false); } };

    gridBox.addItem ("1/4", kTicksPerBeat);
    gridBox.addItem ("1/8", kTicksPerBeat / 2);
    gridBox.addItem ("1/16", kTicksPerBeat / 4);
    gridBox.addItem ("1/32", kTicksPerBeat / 8);
    gridBox.setSelectedId (kTicksPerBeat / 4, juce::dontSendNotification);
    addAndMakeVisible (gridBox);

    for (auto* b : { &bySoundButton, &byGridButton, &equalButton, &resetSliceButton, &writeButton })
        addAndMakeVisible (*b);
    bySoundButton.onClick = [this] { sliceBySound(); };
    byGridButton.onClick = [this] { sliceByGrid(); };
    equalButton.onClick = [this]
    {
        juce::PopupMenu menu;
        for (const int n : { 2, 4, 8, 16, 32, 64 })
            menu.addItem (n, juce::String (n) + " parts");
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&equalButton),
                            [safe = juce::Component::SafePointer<SlicerPanel> (this)] (int n) { if (safe != nullptr && n > 0) safe->sliceEqually (n); });
    };
    writeButton.onClick = [this] { if (onWriteScore) onWriteScore (current); };

    cutButton.setColour (juce::ToggleButton::textColourId, kText);
    cutButton.onClick = [this] { current.cut = cutButton.getToggleState(); changed (false); };
    addAndMakeVisible (cutButton);

    setupSlider (pitch, -24.0, 24.0, 0.01, 0.0);
    pitch.setTextValueSuffix (" st");
    setupSlider (gain, 0.0, 2.0, 0.01, 1.0);
    setupSlider (pan, -1.0, 1.0, 0.01, 0.0);
    pitch.setDoubleClickReturnValue (true, 0.0);
    gain.setDoubleClickReturnValue (true, 1.0);
    pan.setDoubleClickReturnValue (true, 0.0);
    const auto sliceEdit = [this]
    {
        auto& s = settingsOf (selected);
        s.tune = static_cast<float> (pitch.getValue());
        s.gain = static_cast<float> (gain.getValue());
        s.pan = static_cast<float> (pan.getValue());
        s.reverse = reverseButton.getToggleState();
        changed (true);
    };
    for (auto* s : { &pitch, &gain, &pan })
    {
        s->onDragEnd = sliceEdit;
        s->onValueChange = [this, s, sliceEdit] { if (! s->isMouseButtonDown()) sliceEdit(); };
    }
    reverseButton.setColour (juce::ToggleButton::textColourId, kText);
    reverseButton.onClick = sliceEdit;
    addAndMakeVisible (reverseButton);
    resetSliceButton.onClick = [this]
    {
        settingsOf (selected) = {};
        showSlice();
        changed (true);
    };

    setSize (860, 420);
}

SlicerPanel::~SlicerPanel() = default;

void SlicerPanel::setState (SlicerState s)
{
    current = std::move (s);
    if (current.starts.empty() || current.starts.front() != 0)
        current.starts.insert (current.starts.begin(), 0);
    if (current.loopBpm <= 0.0 && current.whole != nullptr)
        current.loopBpm = slicer::guessLoopBpm (current.whole->numFrames(), current.whole->sampleRate, 120.0);
    tempo.setValue (current.loopBpm > 0.0 ? current.loopBpm : 120.0, juce::dontSendNotification);
    cutButton.setToggleState (current.cut, juce::dontSendNotification);
    wave->setSample (current.whole);
    select (std::clamp (selected, 0, static_cast<int> (current.starts.size()) - 1));
}

SliceSettings& SlicerPanel::settingsOf (int slice)
{
    slice = std::clamp (slice, 0, static_cast<int> (current.starts.size()) - 1);
    if (static_cast<int> (current.settings.size()) <= slice)
        current.settings.resize (static_cast<size_t> (slice) + 1);
    return current.settings[static_cast<size_t> (slice)];
}

void SlicerPanel::select (int slice)
{
    selected = std::clamp (slice, 0, std::max (0, static_cast<int> (current.starts.size()) - 1));
    showSlice();
    wave->repaint();
}

void SlicerPanel::showSlice()
{
    const SliceSettings s = selected < static_cast<int> (current.settings.size()) ? current.settings[static_cast<size_t> (selected)] : SliceSettings {};
    const int key = current.firstKey + selected;
    double seconds = 0.0;
    if (current.whole != nullptr && current.whole->sampleRate > 0.0)
    {
        const int end = selected + 1 < static_cast<int> (current.starts.size()) ? current.starts[static_cast<size_t> (selected) + 1] : current.whole->numFrames();
        seconds = (end - current.starts[static_cast<size_t> (selected)]) / current.whole->sampleRate;
    }
    sliceLabel.setText ("Slice " + juce::String (selected + 1) + " of " + juce::String (static_cast<int> (current.starts.size())) + "  -  key "
                            + (key <= 127 ? juce::String (theory::noteName (key)) : juce::String ("none (past the last key)")) + "  -  "
                            + juce::String (juce::roundToInt (seconds * 1000.0)) + " ms",
                        juce::dontSendNotification);
    pitch.setValue (s.tune, juce::dontSendNotification);
    gain.setValue (s.gain, juce::dontSendNotification);
    pan.setValue (s.pan, juce::dontSendNotification);
    reverseButton.setToggleState (s.reverse, juce::dontSendNotification);
}

void SlicerPanel::changed (bool reshaped)
{
    if (current.settings.size() > current.starts.size())
        current.settings.resize (current.starts.size());
    if (reshaped)
        wave->repaint();
    showSlice();
    if (onChanged)
        onChanged (current);
}

void SlicerPanel::sliceBySound()
{
    if (current.whole == nullptr)
        return;
    current.starts = { 0 };
    for (const double t : detectOnsets (current.whole, sensitivity.getValue()))
        if (const int f = static_cast<int> (std::lround (t * current.whole->sampleRate)); f > 1 && current.starts.size() < 128)
            current.starts.push_back (f);
    std::sort (current.starts.begin(), current.starts.end());
    current.starts.erase (std::unique (current.starts.begin(), current.starts.end()), current.starts.end());
    current.settings.clear();
    select (0);
    changed (true);
}

void SlicerPanel::sliceByGrid()
{
    if (current.whole == nullptr)
        return;
    current.loopBpm = tempo.getValue();
    current.starts = slicer::gridStarts (current.whole->numFrames(), current.whole->sampleRate, current.loopBpm, std::max (1, gridBox.getSelectedId()));
    current.settings.clear();
    select (0);
    changed (true);
}

void SlicerPanel::sliceEqually (int parts)
{
    if (current.whole == nullptr || parts < 1)
        return;
    const int frames = current.whole->numFrames();
    current.starts = { 0 };
    for (int i = 1; i < parts; ++i)
        current.starts.push_back (static_cast<int> (static_cast<long long> (frames) * i / parts));
    current.settings.clear();
    select (0);
    changed (true);
}

void SlicerPanel::paint (juce::Graphics& g)
{
    g.fillAll (kBack);
}

void SlicerPanel::resized()
{
    auto area = getLocalBounds().reduced (10);
    auto top = area.removeFromTop (26);
    bySoundButton.setBounds (top.removeFromLeft (110));
    top.removeFromLeft (6);
    sensitivityLabel.setBounds (top.removeFromLeft (70));
    sensitivity.setBounds (top.removeFromLeft (150));
    top.removeFromLeft (12);
    byGridButton.setBounds (top.removeFromLeft (100));
    top.removeFromLeft (4);
    gridBox.setBounds (top.removeFromLeft (64));
    top.removeFromLeft (12);
    equalButton.setBounds (top.removeFromLeft (100));
    top.removeFromLeft (12);
    cutButton.setBounds (top);

    area.removeFromTop (6);
    auto tempoRow = area.removeFromTop (24);
    tempoLabel.setBounds (tempoRow.removeFromLeft (80));
    tempo.setBounds (tempoRow.removeFromLeft (260));
    tempoRow.removeFromLeft (12);
    hint.setBounds (tempoRow);

    area.removeFromTop (6);
    auto bottom = area.removeFromBottom (26);
    writeButton.setBounds (bottom.removeFromRight (230));
    area.removeFromBottom (6);
    auto sliceRow2 = area.removeFromBottom (24);
    auto sliceRow1 = area.removeFromBottom (24);
    area.removeFromBottom (6);
    wave->setBounds (area);

    sliceLabel.setBounds (sliceRow1.removeFromLeft (360));
    reverseButton.setBounds (sliceRow1.removeFromLeft (90));
    resetSliceButton.setBounds (sliceRow1.removeFromLeft (100));
    pitchLabel.setBounds (sliceRow2.removeFromLeft (40));
    pitch.setBounds (sliceRow2.removeFromLeft (230));
    sliceRow2.removeFromLeft (10);
    gainLabel.setBounds (sliceRow2.removeFromLeft (40));
    gain.setBounds (sliceRow2.removeFromLeft (230));
    sliceRow2.removeFromLeft (10);
    panLabel.setBounds (sliceRow2.removeFromLeft (34));
    pan.setBounds (sliceRow2.removeFromLeft (230));
}

SlicerWindow::SlicerWindow()
    : juce::DocumentWindow ("Slicer", juce::Colour (0xff1b1e24), juce::DocumentWindow::closeButton)
{
    setUsingNativeTitleBar (false);
    setContentNonOwned (&content, true);
    setResizable (true, false);
    setResizeLimits (640, 360, 2400, 1400);
    centreWithSize (content.getWidth(), content.getHeight());
    setAlwaysOnTop (true);
}
} // namespace sls
