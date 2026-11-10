#include "ui/PitchEditor.h"

#include <algorithm>
#include <cmath>

#include "audio/AudioEngine.h"
#include "audio/MusicTheory.h"

namespace sls
{
namespace
{
const juce::Colour kBack (0xff15171c), kRowDark (0xff1b1e24), kRowLight (0xff22262d), kCurve (0xffb8bec9), kNote (0xff4fb3a9),
    kNoteSelected (0xffe8943a), kText (0xffd0d4dc);
}

// Notes over the pitch curve: time across, pitch up.
class PitchEditor::View : public juce::Component
{
public:
    explicit View (PitchEditor& o) : owner (o) {}

    void fit()
    {
        lowKey = 127.0;
        highKey = 0.0;
        for (const auto& n : owner.notes)
        {
            lowKey = std::min (lowKey, n.midi + n.shift);
            highKey = std::max (highKey, n.midi + n.shift);
        }
        if (lowKey > highKey)
        {
            lowKey = 48.0;
            highKey = 72.0;
        }
        lowKey = std::floor (lowKey) - 5.0;
        highKey = std::ceil (highKey) + 5.0;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (kBack);
        const double seconds = owner.original != nullptr ? owner.original->numFrames() / owner.original->sampleRate : 1.0;
        const int w = getWidth();
        g.setFont (11.0f);
        for (int key = static_cast<int> (lowKey); key <= static_cast<int> (highKey); ++key)
        {
            const float y0 = keyToY (key + 0.5), y1 = keyToY (key - 0.5);
            const bool black = juce::MidiMessage::isMidiNoteBlack (key);
            g.setColour (black ? kRowDark : kRowLight);
            g.fillRect (0.0f, y0, static_cast<float> (w), y1 - y0);
            if (key % 12 == 0 || y1 - y0 >= 12.0f)
            {
                g.setColour (kText.withAlpha (key % 12 == 0 ? 0.9f : 0.5f));
                g.drawText (juce::String (theory::noteName (key)), 2, static_cast<int> (y0), 36, static_cast<int> (y1 - y0), juce::Justification::centredLeft);
            }
        }

        // The pitch curve as it will sound (each frame moved with its note).
        juce::Path curve;
        bool drawing = false;
        for (const auto& f : owner.frames)
        {
            if (f.hz <= 0.0)
            {
                drawing = false;
                continue;
            }
            double shift = 0.0;
            for (const auto& n : owner.notes)
                if (f.seconds >= n.start && f.seconds <= n.end)
                    shift = n.shift;
            const float x = timeToX (f.seconds, seconds), y = keyToY (f.midi + shift);
            if (! drawing)
                curve.startNewSubPath (x, y);
            else
                curve.lineTo (x, y);
            drawing = true;
        }
        g.setColour (kCurve.withAlpha (0.8f));
        g.strokePath (curve, juce::PathStrokeType (1.2f));

        for (size_t i = 0; i < owner.notes.size(); ++i)
        {
            const auto r = noteBounds (owner.notes[i], seconds);
            const bool sel = owner.selected[i];
            g.setColour ((sel ? kNoteSelected : kNote).withAlpha (0.45f));
            g.fillRoundedRectangle (r, 4.0f);
            g.setColour (sel ? kNoteSelected : kNote);
            g.drawRoundedRectangle (r, 4.0f, 1.2f);
            // where the note was sung, when it has been moved
            if (std::abs (owner.notes[i].shift) > 1.0e-3)
            {
                const float y = keyToY (owner.notes[i].midi);
                g.setColour (kText.withAlpha (0.35f));
                g.drawHorizontalLine (static_cast<int> (y), r.getX(), r.getRight());
            }
            g.setColour (juce::Colours::white);
            const auto& n = owner.notes[i];
            const int cents = static_cast<int> (std::lround (n.centsOff()));
            g.drawText (juce::String (theory::noteName (n.nearestKey())) + (cents == 0 ? juce::String() : (cents > 0 ? " +" : " ") + juce::String (cents)),
                        r.toNearestInt().reduced (3, 0), juce::Justification::centredLeft, true);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const int hit = noteAt (e.position);
        if (hit < 0)
        {
            if (! e.mods.isShiftDown())
                std::fill (owner.selected.begin(), owner.selected.end(), false);
            owner.updateInfo();
            repaint();
            return;
        }
        if (e.mods.isShiftDown() || e.mods.isCommandDown())
            owner.selected[static_cast<size_t> (hit)] = ! owner.selected[static_cast<size_t> (hit)];
        else if (! owner.selected[static_cast<size_t> (hit)])
        {
            std::fill (owner.selected.begin(), owner.selected.end(), false);
            owner.selected[static_cast<size_t> (hit)] = true;
        }
        dragFrom = e.position.y;
        startShifts.clear();
        for (const auto& n : owner.notes)
            startShifts.push_back (n.shift);
        dragging = true;
        owner.updateInfo();
        repaint();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! dragging)
            return;
        const double semis = (dragFrom - e.position.y) / rowHeight();
        const double delta = e.mods.isAltDown() ? semis : std::round (semis);
        for (size_t i = 0; i < owner.notes.size(); ++i)
            if (owner.selected[i])
                owner.notes[i].shift = std::clamp (startShifts[i] + delta, -24.0, 24.0);
        moved = true;
        owner.updateInfo();
        repaint();
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (dragging && moved)
            owner.notesChanged();
        dragging = moved = false;
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        const int hit = noteAt (e.position);
        if (hit < 0)
            return;
        auto& n = owner.notes[static_cast<size_t> (hit)];
        n.shift = std::round (n.midi + n.shift) - n.midi; // in tune, on the key it is nearest to now
        owner.notesChanged();
    }

private:
    double rowHeight() const { return getHeight() / std::max (1.0, highKey - lowKey + 1.0); }
    float keyToY (double key) const { return static_cast<float> ((highKey + 0.5 - key) * rowHeight()); }
    float timeToX (double t, double total) const { return static_cast<float> (40.0 + t / std::max (1.0e-6, total) * (getWidth() - 44)); }
    juce::Rectangle<float> noteBounds (const pitchtrack::Note& n, double total) const
    {
        const float x0 = timeToX (n.start, total), x1 = timeToX (n.end, total);
        const float h = static_cast<float> (std::max (6.0, rowHeight() * 0.8));
        return { x0, keyToY (n.midi + n.shift) - h * 0.5f, std::max (3.0f, x1 - x0), h };
    }
    int noteAt (juce::Point<float> p) const
    {
        const double total = owner.original != nullptr ? owner.original->numFrames() / owner.original->sampleRate : 1.0;
        for (int i = static_cast<int> (owner.notes.size()) - 1; i >= 0; --i)
            if (noteBounds (owner.notes[static_cast<size_t> (i)], total).expanded (0.0f, 3.0f).contains (p))
                return i;
        return -1;
    }

    PitchEditor& owner;
    double lowKey = 48.0, highKey = 72.0;
    float dragFrom = 0.0f;
    std::vector<double> startShifts;
    bool dragging = false, moved = false;
};

PitchEditor::PitchEditor (AudioEngine& e) : engine (e), view (std::make_unique<View> (*this))
{
    addAndMakeVisible (*view);
    amount.setSliderStyle (juce::Slider::LinearHorizontal);
    amount.setTextBoxStyle (juce::Slider::TextBoxRight, false, 50, 20);
    amount.setRange (0.0, 100.0, 1.0);
    amount.setValue (100.0, juce::dontSendNotification);
    amount.setTextValueSuffix (" %");
    addAndMakeVisible (amount);
    for (auto* l : { &amountLabel, &info })
    {
        l->setColour (juce::Label::textColourId, kText);
        addAndMakeVisible (*l);
    }

    const auto& list = theory::scales();
    for (size_t i = 0; i < list.size(); ++i)
        scaleBox.addItem (list[i].name, static_cast<int> (i) + 1);
    scaleBox.setSelectedId (1, juce::dontSendNotification);
    for (int k = 0; k < 12; ++k)
        rootBox.addItem (juce::MidiMessage::getMidiNoteName (k, true, false, 4), k + 1);
    rootBox.setSelectedId (1, juce::dontSendNotification);
    addAndMakeVisible (scaleBox);
    addAndMakeVisible (rootBox);

    for (auto* b : { &correctButton, &resetButton, &playButton, &applyButton, &midiButton, &cancelButton })
        addAndMakeVisible (*b);
    correctButton.onClick = [this] { correctSelected (amount.getValue() / 100.0); };
    resetButton.onClick = [this]
    {
        const bool any = std::find (selected.begin(), selected.end(), true) != selected.end();
        for (size_t i = 0; i < notes.size(); ++i)
            if (! any || selected[i])
                notes[i].shift = 0.0;
        notesChanged();
    };
    playButton.onClick = [this] { if (engine.auditioner().isPlaying()) stopPlayback(); else play(); };
    applyButton.onClick = [this]
    {
        stopPlayback();
        if (onApply == nullptr)
            return;
        const auto error = onApply (edited());
        if (error.isNotEmpty())
            info.setText (error, juce::dontSendNotification);
        else if (onClose)
            onClose();
    };
    midiButton.onClick = [this]
    {
        if (onNotesToPattern == nullptr)
            return;
        const auto error = onNotesToPattern (notes);
        info.setText (error.isNotEmpty() ? error : juce::String (static_cast<int> (notes.size())) + " notes written into the pattern",
                      juce::dontSendNotification);
    };
    cancelButton.onClick = [this] { stopPlayback(); if (onClose) onClose(); };
    setSize (900, 520);
}

PitchEditor::~PitchEditor()
{
    engine.auditioner().release();
}

void PitchEditor::setSample (std::shared_ptr<const SampleData> sample)
{
    original = std::move (sample);
    cache = nullptr;
    frames = original != nullptr ? pitchtrack::track (*original) : std::vector<pitchtrack::Frame> {};
    notes = pitchtrack::notesFrom (frames);
    selected.assign (notes.size(), false);
    view->fit();
    updateInfo();
    if (notes.empty())
        info.setText ("No notes were found: the pitch editor needs one voice or instrument at a time (no chords, not too much noise).",
                      juce::dontSendNotification);
}

void PitchEditor::correctSelected (double how)
{
    const bool any = std::find (selected.begin(), selected.end(), true) != selected.end();
    const int type = std::max (0, scaleBox.getSelectedId() - 1);
    bool inScale[12] {};
    const auto mask = type < static_cast<int> (theory::scales().size()) ? theory::scales()[static_cast<size_t> (type)].mask : 0xfff;
    for (int k = 0; k < 12; ++k)
        inScale[k] = ((mask >> k) & 1) != 0;
    const int root = std::max (0, rootBox.getSelectedId() - 1);
    for (size_t i = 0; i < notes.size(); ++i)
        if (! any || selected[i])
            notes[i].shift = pitchtrack::correction (notes[i], how, type == 0 ? nullptr : inScale, root);
    notesChanged();
}

void PitchEditor::notesChanged()
{
    cache = nullptr;
    stopPlayback();
    view->repaint();
    updateInfo();
}

std::shared_ptr<const SampleData> PitchEditor::edited()
{
    if (cache == nullptr)
        cache = pitchtrack::render (original, notes);
    return cache;
}

void PitchEditor::play()
{
    if (original == nullptr)
        return;
    engine.auditioner().play (edited(), 0.0, 0.0, false);
    playButton.setButtonText ("Stop");
}

void PitchEditor::stopPlayback()
{
    engine.auditioner().release();
    playButton.setButtonText ("Play");
}

void PitchEditor::updateInfo()
{
    int count = 0;
    const pitchtrack::Note* one = nullptr;
    for (size_t i = 0; i < notes.size(); ++i)
        if (selected[i])
        {
            ++count;
            one = &notes[i];
        }
    if (count == 1)
    {
        const int sungCents = static_cast<int> (std::lround ((one->midi - std::round (one->midi)) * 100.0));
        info.setText ("Sung " + juce::String (theory::noteName (static_cast<int> (std::lround (one->midi)))) + (sungCents >= 0 ? " +" : " ") + juce::String (sungCents)
                          + " cents, now " + juce::String (theory::noteName (one->nearestKey())) + (one->centsOff() >= 0.0 ? " +" : " ")
                          + juce::String (static_cast<int> (std::lround (one->centsOff()))) + " cents (moved " + juce::String (one->shift, 2) + " semitones)",
                      juce::dontSendNotification);
    }
    else
        info.setText (juce::String (static_cast<int> (notes.size())) + " notes" + (count > 1 ? ", " + juce::String (count) + " selected" : juce::String())
                          + ". Drag a note up or down (Alt: freely), double-click it to tune it, Shift+click to select more.",
                      juce::dontSendNotification);
}

void PitchEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1b1e24));
}

void PitchEditor::resized()
{
    auto area = getLocalBounds().reduced (10);
    auto top = area.removeFromTop (26);
    correctButton.setBounds (top.removeFromLeft (80));
    top.removeFromLeft (8);
    amountLabel.setBounds (top.removeFromLeft (56));
    amount.setBounds (top.removeFromLeft (180));
    top.removeFromLeft (8);
    rootBox.setBounds (top.removeFromLeft (60));
    top.removeFromLeft (4);
    scaleBox.setBounds (top.removeFromLeft (180));
    top.removeFromLeft (8);
    resetButton.setBounds (top.removeFromLeft (70));
    top.removeFromLeft (8);
    playButton.setBounds (top.removeFromLeft (70));

    auto bottom = area.removeFromBottom (26);
    cancelButton.setBounds (bottom.removeFromRight (80));
    bottom.removeFromRight (8);
    applyButton.setBounds (bottom.removeFromRight (190));
    bottom.removeFromRight (8);
    midiButton.setBounds (bottom.removeFromRight (140));
    area.removeFromBottom (6);
    info.setBounds (area.removeFromBottom (22));
    area.removeFromTop (6);
    view->setBounds (area);
}

PitchEditorWindow::PitchEditorWindow (AudioEngine& engine)
    : juce::DocumentWindow ("Pitch editor", juce::Colour (0xff1b1e24), juce::DocumentWindow::closeButton), content (engine)
{
    setUsingNativeTitleBar (false);
    setContentNonOwned (&content, true);
    setResizable (true, false);
    setResizeLimits (600, 360, 3000, 1600);
    centreWithSize (content.getWidth(), content.getHeight());
    setAlwaysOnTop (true);
}
} // namespace sls
