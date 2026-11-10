#include "ui/SampleEditor.h"

#include <algorithm>
#include <cmath>

namespace sls
{
namespace
{
const juce::Colour kBg { 0xff1b1e24 };
const juce::Colour kPanel { 0xff22262e };
const juce::Colour kAccent { 0xffe8943a };
const juce::Colour kWave { 0xff4aa3c8 };
const juce::Colour kGood { 0xff6cc08b };
constexpr int kRuler = 18;
constexpr double kMaxUndoBytes = 600.0 * 1024.0 * 1024.0;
constexpr float kFloorDb = -90.0f;
constexpr int kMaxMarkers = 128;

double bytesOf (const SamplePtr& s) { return s != nullptr ? 8.0 * s->numFrames() : 0.0; }
} // namespace

SampleEditor::SampleEditor (AudioEngine& engineToUse) : engine (engineToUse)
{
    mono.resize (SpectrumAnalyser::kFftSize);
    bins.resize (SpectrumAnalyser::kBins);
    shown.assign (SpectrumAnalyser::kBins, kFloorDb);
    setWantsKeyboardFocus (true);

    for (auto* b : { &playButton, &loopButton, &undoButton, &redoButton, &allButton, &cutButton, &cropButton, &silenceButton, &reverseButton, &invertButton,
                     &gainButton, &normaliseButton, &dcButton, &fadeInButton, &fadeOutButton, &noiseButton, &hitsButton, &partsButton, &clearMarkersButton,
                     &applyButton, &cancelButton, &toSamplerButton })
    {
        b->setMouseClickGrabsKeyboardFocus (false);
        addAndMakeVisible (*b);
    }
    loopButton.setClickingTogglesState (true);

    playButton.setTooltip ("Play the selection, or from the cursor to the end (Space)");
    loopButton.setTooltip ("Repeat the selection");
    cutButton.setTooltip ("Remove the selection: the sound after it moves earlier");
    cropButton.setTooltip ("Keep only the selection");
    silenceButton.setTooltip ("Make the selection silent, keeping the length");
    gainButton.setTooltip ("Change the level of the selection (or of everything if nothing is selected) in dB");
    normaliseButton.setTooltip ("Raise or lower the selection so that its loudest point reaches a level");
    dcButton.setTooltip ("Remove a constant offset (a sound that does not swing around zero)");
    noiseButton.setTooltip ("Select a stretch with only the noise in it first, then press this: the noise is removed from everything");
    hitsButton.setTooltip ("Put a marker at the start of every hit, note or word the program can find");

    fadeShapeBox.addItem ("Linear", 1);
    fadeShapeBox.addItem ("Curve", 2);
    fadeShapeBox.addItem ("S curve", 3);
    fadeShapeBox.setSelectedId (1, juce::dontSendNotification);
    fadeShapeBox.setTooltip ("Shape of the fades");
    addAndMakeVisible (fadeShapeBox);

    sensitivityLabel.setText ("Sensitivity", juce::dontSendNotification);
    sensitivityLabel.setColour (juce::Label::textColourId, juce::Colours::lightgrey);
    addAndMakeVisible (sensitivityLabel);
    sensitivitySlider.setRange (0.0, 1.0, 0.01);
    sensitivitySlider.setValue (0.5, juce::dontSendNotification);
    sensitivitySlider.setSliderStyle (juce::Slider::LinearHorizontal);
    sensitivitySlider.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    sensitivitySlider.setTooltip ("Low: only the clearest hits. High: everything that changes.");
    addAndMakeVisible (sensitivitySlider);

    statusLabel.setColour (juce::Label::textColourId, juce::Colours::lightgrey);
    statusLabel.setMinimumHorizontalScale (0.7f);
    addAndMakeVisible (statusLabel);

    playButton.onClick = [this] { if (engine.auditioner().isPlaying()) stopPlayback(); else play(); };
    undoButton.onClick = [this] { undo(); };
    redoButton.onClick = [this] { redo(); };
    allButton.onClick = [this] { selBegin = 0; selEnd = current != nullptr ? current->numFrames() : 0; repaint(); updateButtons(); };
    cutButton.onClick = [this] { edit ("Cut", [] (const SamplePtr& s, FrameRange r) { return cutRange (s, r); }, true); };
    cropButton.onClick = [this] { edit ("Crop", [] (const SamplePtr& s, FrameRange r) { return cropToRange (s, r); }, true); };
    silenceButton.onClick = [this] { edit ("Silence", [] (const SamplePtr& s, FrameRange r) { return silenceRange (s, r); }); };
    reverseButton.onClick = [this] { edit ("Reverse", [] (const SamplePtr& s, FrameRange r) { return reverseRange (s, r); }); };
    invertButton.onClick = [this] { edit ("Invert phase", [] (const SamplePtr& s, FrameRange r) { return invertPhase (s, r); }); };
    dcButton.onClick = [this] { edit ("Remove DC offset", [] (const SamplePtr& s, FrameRange r) { return removeDcOffset (s, r); }); };
    fadeInButton.onClick = [this]
    {
        const auto shape = static_cast<FadeShape> (fadeShapeBox.getSelectedId() - 1);
        edit ("Fade in", [shape] (const SamplePtr& s, FrameRange r) { return fadeRange (s, r, true, shape); });
    };
    fadeOutButton.onClick = [this]
    {
        const auto shape = static_cast<FadeShape> (fadeShapeBox.getSelectedId() - 1);
        edit ("Fade out", [shape] (const SamplePtr& s, FrameRange r) { return fadeRange (s, r, false, shape); });
    };
    gainButton.onClick = [this]
    {
        ask ("Gain", "Change the level by this many dB (negative = quieter, from -120 to +48).", { "Gain (dB)" }, { "0.0" },
             [this] (const juce::StringArray& v)
             {
                 const double db = v[0].getDoubleValue();
                 edit ("Gain " + juce::String (db, 1) + " dB", [db] (const SamplePtr& s, FrameRange r) { return gainRange (s, r, db); });
             });
    };
    normaliseButton.onClick = [this]
    {
        ask ("Normalise", "The loudest point of the selection (or of everything) is brought to this level. -60 to 0 dB.", { "Peak level (dB)" }, { "-0.3" },
             [this] (const juce::StringArray& v)
             {
                 const double db = v[0].getDoubleValue();
                 edit ("Normalise to " + juce::String (db, 1) + " dB", [db] (const SamplePtr& s, FrameRange r) { return normaliseRange (s, r, db); });
             });
    };
    noiseButton.onClick = [this] { reduceNoiseDialog(); };
    hitsButton.onClick = [this] { findHits(); };
    partsButton.onClick = [this] { sliceByEqualParts(); };
    clearMarkersButton.onClick = [this] { markers.clear(); setStatus ("Markers cleared."); repaint(); updateButtons(); };
    applyButton.onClick = [this] { applyAndClose(); };
    toSamplerButton.setTooltip ("A new sampler channel that plays slice n on key C5 + n, with one note per slice in the current pattern");
    toSamplerButton.onClick = [this]
    {
        if (current == nullptr || markers.empty() || ! onSlicesToSampler)
            return;
        stopPlayback();
        const auto error = onSlicesToSampler (current, markers);
        if (error.isNotEmpty())
        {
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Sample editor", error);
            return;
        }
        if (onClose)
            onClose();
    };
    cancelButton.onClick = [this] { stopPlayback(); if (onClose) onClose(); };

    startTimerHz (30);
}

SampleEditor::~SampleEditor()
{
    stopTimer();
    engine.auditioner().release();
}

void SampleEditor::setSample (SamplePtr sample, bool canSliceIn, const juce::String& applyText)
{
    stopPlayback();
    current = std::move (sample);
    undoStack.clear();
    redoStack.clear();
    markers.clear();
    canSlice = canSliceIn;
    canMark = canSlice || static_cast<bool> (onSlicesToSampler);
    selBegin = selEnd = 0;
    cursor = 0.0;
    applyButton.setButtonText (applyText);
    hitsButton.setEnabled (canMark);
    partsButton.setEnabled (canMark);
    clearMarkersButton.setEnabled (canMark);
    sensitivitySlider.setEnabled (canMark);
    const juce::String why = "Markers are for slicing; this editor was opened without slicing.";
    hitsButton.setTooltip (canMark ? "Put a marker at the start of every hit, note or word the program can find" : why);
    partsButton.setTooltip (canMark ? "Put markers that cut the sample into equal parts" : why);
    clearMarkersButton.setTooltip (canMark ? "Remove all the slice markers" : why);
    sensitivitySlider.setTooltip (canMark ? "Low: only the clearest hits. High: everything that changes." : why);
    toSamplerButton.setVisible (static_cast<bool> (onSlicesToSampler));
    rebuildPeaks();
    fitView();
    setStatus ("Drag on the waveform to select; the wheel zooms, Shift + wheel scrolls; Space plays.");
    updateButtons();
    repaint();
}

// --- state -------------------------------------------------------------------------------------------------------

void SampleEditor::rebuildPeaks()
{
    peaks = {};
    if (current == nullptr)
        return;
    const int n = current->numFrames();
    const int entries = (n + Peaks::kFrames - 1) / Peaks::kFrames;
    for (auto* v : { &peaks.lowL, &peaks.highL, &peaks.lowR, &peaks.highR })
        v->assign (static_cast<size_t> (entries), 0.0f);
    for (int e = 0; e < entries; ++e)
    {
        float loL = 1.0e9f, hiL = -1.0e9f, loR = 1.0e9f, hiR = -1.0e9f;
        for (int i = e * Peaks::kFrames; i < std::min (n, (e + 1) * Peaks::kFrames); ++i)
        {
            const float l = current->left[static_cast<size_t> (i)], r = current->right[static_cast<size_t> (i)];
            loL = std::min (loL, l); hiL = std::max (hiL, l);
            loR = std::min (loR, r); hiR = std::max (hiR, r);
        }
        peaks.lowL[static_cast<size_t> (e)] = loL; peaks.highL[static_cast<size_t> (e)] = hiL;
        peaks.lowR[static_cast<size_t> (e)] = loR; peaks.highR[static_cast<size_t> (e)] = hiR;
    }
}

void SampleEditor::trimUndoMemory()
{
    double total = bytesOf (current);
    for (const auto& s : undoStack)
        total += bytesOf (s);
    for (const auto& s : redoStack)
        total += bytesOf (s);
    while (undoStack.size() > 1 && (total > kMaxUndoBytes || undoStack.size() > 40))
    {
        total -= bytesOf (undoStack.front());
        undoStack.erase (undoStack.begin());
    }
}

void SampleEditor::changeSample (SamplePtr next, const juce::String& message, bool lengthChanged)
{
    stopPlayback();
    undoStack.push_back (current);
    redoStack.clear();
    current = std::move (next);
    trimUndoMemory();
    const int n = current->numFrames();
    selBegin = std::clamp (selBegin, 0, n);
    selEnd = std::clamp (selEnd, 0, n);
    if (lengthChanged)
    {
        selBegin = selEnd = 0; // the old selection no longer points at the same sound
        if (! markers.empty())
        {
            markers.clear();
            setStatus (message + " (the slice markers were removed because the length changed).");
        }
        else
            setStatus (message + ".");
    }
    else
        setStatus (message + ".");
    cursor = std::clamp (cursor, 0.0, static_cast<double> (n));
    rebuildPeaks();
    clampView();
    updateButtons();
    repaint();
}

void SampleEditor::edit (const juce::String& what, const std::function<SamplePtr (const SamplePtr&, FrameRange)>& op, bool changesLength)
{
    if (current == nullptr)
        return;
    const auto range = selectionOrAll();
    if (changesLength && ! hasSelection())
    {
        setStatus (what + " needs a selection: drag on the waveform first.");
        return;
    }
    auto next = op (current, range);
    if (next == nullptr)
    {
        setStatus (what + ": nothing to do here" + (what == "Cut" ? " (you cannot cut everything)." : "."));
        return;
    }
    if (next == current)
    {
        setStatus (what + ": nothing changed (silence?).");
        return;
    }
    changeSample (std::move (next), what + (hasSelection() ? " on the selection" : " on everything"), changesLength);
}

void SampleEditor::undo()
{
    if (undoStack.empty())
        return;
    stopPlayback();
    const bool lengthChanged = undoStack.back()->numFrames() != current->numFrames();
    redoStack.push_back (current);
    current = undoStack.back();
    undoStack.pop_back();
    if (lengthChanged)
    {
        markers.clear();
        selBegin = selEnd = 0;
    }
    rebuildPeaks();
    clampView();
    setStatus ("Undone.");
    updateButtons();
    repaint();
}

void SampleEditor::redo()
{
    if (redoStack.empty())
        return;
    stopPlayback();
    const bool lengthChanged = redoStack.back()->numFrames() != current->numFrames();
    undoStack.push_back (current);
    current = redoStack.back();
    redoStack.pop_back();
    if (lengthChanged)
    {
        markers.clear();
        selBegin = selEnd = 0;
    }
    rebuildPeaks();
    clampView();
    setStatus ("Redone.");
    updateButtons();
    repaint();
}

FrameRange SampleEditor::selectionOrAll() const
{
    if (current == nullptr)
        return {};
    return hasSelection() ? FrameRange { selBegin, selEnd } : FrameRange { 0, current->numFrames() };
}

void SampleEditor::updateButtons()
{
    undoButton.setEnabled (! undoStack.empty());
    redoButton.setEnabled (! redoStack.empty());
    cutButton.setEnabled (hasSelection());
    cropButton.setEnabled (hasSelection());
    loopButton.setEnabled (hasSelection());
    playButton.setButtonText (engine.auditioner().isPlaying() ? "Stop" : "Play");
    clearMarkersButton.setEnabled (canMark && ! markers.empty());
    toSamplerButton.setEnabled (canMark && ! markers.empty());
    if (canSlice && ! markers.empty())
        applyButton.setTooltip ("Saves the edited audio as a new file, puts it on the clip and cuts the clip at the " + juce::String (static_cast<int> (markers.size()))
                                + " markers");
    else
        applyButton.setTooltip ("Saves the edited audio as a new WAV file (the original file is never changed) and uses it");
}

void SampleEditor::setStatus (const juce::String& text)
{
    status = text;
    statusLabel.setText (text, juce::dontSendNotification);
}

void SampleEditor::ask (const juce::String& title, const juce::String& text, const juce::StringArray& names, const juce::StringArray& defaults,
                        std::function<void (const juce::StringArray&)> then)
{
    auto* window = new juce::AlertWindow (title, text, juce::MessageBoxIconType::NoIcon);
    for (int i = 0; i < names.size(); ++i)
        window->addTextEditor ("f" + juce::String (i), defaults[i], names[i]);
    window->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    window->enterModalState (true,
                             juce::ModalCallbackFunction::create (
                                 [window, count = names.size(), then = std::move (then), safe = juce::Component::SafePointer<SampleEditor> (this)] (int result)
                                 {
                                     if (safe == nullptr || result != 1)
                                         return;
                                     juce::StringArray values;
                                     for (int i = 0; i < count; ++i)
                                         values.add (window->getTextEditorContents ("f" + juce::String (i)));
                                     then (values);
                                 }),
                             true);
}

void SampleEditor::reduceNoiseDialog()
{
    if (current == nullptr)
        return;
    if (! hasSelection() || selEnd - selBegin < kNoiseMinProfileFrames)
    {
        setStatus ("Noise reduction: first select a stretch that holds only the noise, at least "
                   + juce::String (kNoiseMinProfileFrames / current->sampleRate, 2) + " s long (before the music starts, for example).");
        return;
    }
    const FrameRange noise { selBegin, selEnd };
    ask ("Reduce noise",
         "The selected stretch is taken as the noise. Everything in the sample that does not rise above it is lowered.\n"
         "A higher reduction removes more noise but can make the sound watery.",
         { "Reduction (dB, 0 to 60)", "Sensitivity (0 to 8, higher keeps less)" }, { "12", "3" },
         [this, noise] (const juce::StringArray& v)
         {
             NoiseReduction nr;
             nr.reductionDb = v[0].getDoubleValue();
             nr.sensitivity = v[1].getDoubleValue();
             juce::MouseCursor::showWaitCursor();
             auto next = reduceNoise (current, noise, { 0, current->numFrames() }, nr);
             juce::MouseCursor::hideWaitCursor();
             if (next == nullptr)
                 setStatus ("Noise reduction needs a longer noise sample.");
             else
                 changeSample (std::move (next), "Noise reduced by " + juce::String (nr.reductionDb, 1) + " dB", false);
         });
}

// --- slicing -----------------------------------------------------------------------------------------------------

void SampleEditor::findHits()
{
    if (current == nullptr || ! canMark)
        return;
    auto found = detectOnsets (current, sensitivitySlider.getValue());
    const double length = current->numFrames() / current->sampleRate;
    found.erase (std::remove_if (found.begin(), found.end(), [length] (double t) { return t < 0.01 || t > length - 0.01; }), found.end());
    if (found.size() > static_cast<size_t> (kMaxMarkers))
        found.resize (kMaxMarkers);
    markers = found;
    setStatus (markers.empty() ? "No hits found: raise the sensitivity, or add markers by double-clicking the waveform."
                               : juce::String (static_cast<int> (markers.size())) + " markers placed. Double-click the waveform to add or remove one; Apply cuts the clip there.");
    updateButtons();
    repaint();
}

void SampleEditor::sliceByEqualParts()
{
    if (current == nullptr || ! canMark)
        return;
    ask ("Equal parts", "Cut the sample into this many equal parts (2 to 128).", { "Parts" }, { "8" },
         [this] (const juce::StringArray& v)
         {
             const int n = std::clamp (v[0].getIntValue(), 2, kMaxMarkers);
             markers = gridSlices (current, n);
             setStatus (juce::String (n) + " equal parts: " + juce::String (static_cast<int> (markers.size())) + " markers placed.");
             updateButtons();
             repaint();
         });
}

void SampleEditor::toggleMarkerAt (double frame, double tolerancePixels)
{
    if (current == nullptr || ! canMark)
        return;
    const double tolerance = tolerancePixels * framesPerPixel / current->sampleRate;
    const double seconds = frame / current->sampleRate;
    for (auto it = markers.begin(); it != markers.end(); ++it)
        if (std::fabs (*it - seconds) <= tolerance)
        {
            markers.erase (it);
            updateButtons();
            repaint();
            return;
        }
    if (markers.size() >= static_cast<size_t> (kMaxMarkers) || seconds < 0.005 || seconds > current->numFrames() / current->sampleRate - 0.005)
        return;
    markers.push_back (seconds);
    std::sort (markers.begin(), markers.end());
    updateButtons();
    repaint();
}

void SampleEditor::applyAndClose()
{
    if (current == nullptr || ! onApply)
        return;
    if (! hasChanges())
    {
        setStatus ("Nothing was changed.");
        return;
    }
    stopPlayback();
    std::vector<double> cuts;
    if (canSlice)
        cuts = markers;
    const auto error = onApply (current, cuts);
    if (error.isNotEmpty())
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Sample editor", error);
        return;
    }
    if (onClose)
        onClose();
}

// --- playback and spectrum -----------------------------------------------------------------------------------------

void SampleEditor::play()
{
    if (current == nullptr)
        return;
    const double begin = hasSelection() ? selBegin : cursor;
    const double end = hasSelection() ? selEnd : 0.0;
    engine.auditioner().play (current, begin, end, loopButton.getToggleState() && hasSelection());
    updateButtons();
}

void SampleEditor::stopPlayback()
{
    engine.auditioner().release();
    updateButtons();
}

void SampleEditor::timerCallback()
{
    const bool playing = engine.auditioner().isPlaying();
    if (playing != wasPlaying)
    {
        wasPlaying = playing;
        updateButtons();
    }
    updateSpectrum();
    repaint();
}

void SampleEditor::updateSpectrum()
{
    if (current == nullptr)
        return;
    const int n = current->numFrames();
    double centre = cursor;
    if (engine.auditioner().isPlaying())
        centre = engine.auditioner().positionFrames();
    else if (hasSelection())
        centre = 0.5 * (selBegin + selEnd);
    const int start = std::clamp (static_cast<int> (centre) - SpectrumAnalyser::kFftSize / 2, 0, std::max (0, n - SpectrumAnalyser::kFftSize));
    const int count = std::min (SpectrumAnalyser::kFftSize, n - start);
    for (int i = 0; i < count; ++i)
        mono[static_cast<size_t> (i)] = 0.5f * (current->left[static_cast<size_t> (start + i)] + current->right[static_cast<size_t> (start + i)]);
    analyser->analyse (mono.data(), count, bins.data());
    for (size_t b = 0; b < bins.size(); ++b)
        shown[b] = std::max (bins[b], shown[b] - 3.0f); // falls 90 dB per second at 30 Hz
}

// --- view --------------------------------------------------------------------------------------------------------

double SampleEditor::frameAtX (int x) const noexcept
{
    return viewStart + (x - waveArea.getX()) * framesPerPixel;
}

int SampleEditor::xAtFrame (double frame) const noexcept
{
    return waveArea.getX() + static_cast<int> (std::lround ((frame - viewStart) / framesPerPixel));
}

void SampleEditor::clampView()
{
    if (current == nullptr || waveArea.getWidth() <= 0)
        return;
    const double total = current->numFrames();
    const double visible = framesPerPixel * waveArea.getWidth();
    framesPerPixel = std::clamp (framesPerPixel, 0.05, std::max (0.05, total / std::max (1, waveArea.getWidth())));
    viewStart = std::clamp (viewStart, 0.0, std::max (0.0, total - visible));
}

void SampleEditor::fitView()
{
    viewStart = 0.0;
    if (current != nullptr && waveArea.getWidth() > 0)
        framesPerPixel = std::max (0.05, current->numFrames() / static_cast<double> (waveArea.getWidth()));
}

void SampleEditor::zoomTo (double newFramesPerPixel, int anchorX)
{
    const double anchorFrame = frameAtX (anchorX);
    framesPerPixel = newFramesPerPixel;
    viewStart = anchorFrame - (anchorX - waveArea.getX()) * framesPerPixel;
    clampView();
    repaint();
}

juce::String SampleEditor::timeText (double frame) const
{
    const double seconds = current != nullptr ? frame / current->sampleRate : 0.0;
    const int minutes = static_cast<int> (seconds / 60.0);
    return juce::String (minutes) + ":" + juce::String (seconds - minutes * 60.0, 3).paddedLeft ('0', 6);
}

// --- mouse and keys -----------------------------------------------------------------------------------------------

void SampleEditor::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    if (current == nullptr || ! waveArea.expanded (0, kRuler).contains (e.getPosition()))
        return;
    dragAnchorX = e.x;
    dragAnchorFrame = std::clamp (frameAtX (e.x), 0.0, static_cast<double> (current->numFrames()));
    dragging = false;
    if (e.mods.isShiftDown() && (hasSelection() || cursor > 0.0))
    {
        // extend from the cursor or the nearer end of the selection
        const double pivot = hasSelection() ? (std::fabs (dragAnchorFrame - selBegin) < std::fabs (dragAnchorFrame - selEnd) ? selEnd : selBegin) : cursor;
        dragAnchorFrame = pivot;
        dragging = true;
        selBegin = static_cast<int> (std::min (pivot, std::clamp (frameAtX (e.x), 0.0, static_cast<double> (current->numFrames()))));
        selEnd = static_cast<int> (std::max (pivot, std::clamp (frameAtX (e.x), 0.0, static_cast<double> (current->numFrames()))));
        updateButtons();
        repaint();
    }
}

void SampleEditor::mouseDrag (const juce::MouseEvent& e)
{
    if (current == nullptr)
        return;
    if (! dragging && std::abs (e.x - dragAnchorX) < 3)
        return;
    dragging = true;
    const double total = current->numFrames();
    const double f = std::clamp (frameAtX (e.x), 0.0, total);
    selBegin = static_cast<int> (std::min (dragAnchorFrame, f));
    selEnd = static_cast<int> (std::max (dragAnchorFrame, f));
    updateButtons();
    repaint();
}

void SampleEditor::mouseUp (const juce::MouseEvent& e)
{
    if (current == nullptr)
        return;
    if (! dragging && waveArea.expanded (0, kRuler).contains (e.getPosition()))
    {
        cursor = dragAnchorFrame;
        selBegin = selEnd = 0;
        updateButtons();
        repaint();
    }
    dragging = false;
}

void SampleEditor::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (current != nullptr && canMark && waveArea.contains (e.getPosition()))
        toggleMarkerAt (std::clamp (frameAtX (e.x), 0.0, static_cast<double> (current->numFrames())), 6.0);
}

void SampleEditor::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (current == nullptr)
        return;
    if (e.mods.isShiftDown() || std::fabs (wheel.deltaX) > std::fabs (wheel.deltaY))
    {
        const double delta = std::fabs (wheel.deltaX) > std::fabs (wheel.deltaY) ? wheel.deltaX : wheel.deltaY;
        viewStart -= delta * waveArea.getWidth() * framesPerPixel * 0.5;
        clampView();
        repaint();
    }
    else
        zoomTo (framesPerPixel * (wheel.deltaY > 0 ? 1.0 / 1.25 : 1.25), e.x);
}

bool SampleEditor::keyPressed (const juce::KeyPress& key)
{
    const auto mods = key.getModifiers();
    if (key == juce::KeyPress::spaceKey)
    {
        if (engine.auditioner().isPlaying()) stopPlayback(); else play();
        return true;
    }
    if (mods.isCommandDown())
    {
        const auto letter = juce::CharacterFunctions::toUpperCase (static_cast<juce::juce_wchar> (key.getKeyCode()));
        if (letter == 'Z') { if (mods.isShiftDown()) redo(); else undo(); return true; }
        if (letter == 'Y') { redo(); return true; }
        if (letter == 'A') { allButton.triggerClick(); return true; }
        return false;
    }
    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
    {
        if (hasSelection())
            cutButton.triggerClick();
        return true;
    }
    if (key == juce::KeyPress::homeKey) { fitView(); repaint(); return true; }
    return false;
}

// --- layout and painting ------------------------------------------------------------------------------------------

void SampleEditor::resized()
{
    auto area = getLocalBounds().reduced (8);
    auto row = [&area] (int height) { auto r = area.removeFromTop (height); area.removeFromTop (6); return r; };
    auto place = [] (juce::Rectangle<int>& r, juce::Component& c, int width) { c.setBounds (r.removeFromLeft (width)); r.removeFromLeft (6); };

    auto r1 = row (28);
    place (r1, playButton, 70); place (r1, loopButton, 60); place (r1, undoButton, 64); place (r1, redoButton, 64); place (r1, allButton, 90);
    auto r2 = row (28);
    place (r2, cutButton, 54); place (r2, cropButton, 58); place (r2, silenceButton, 74); place (r2, reverseButton, 74); place (r2, invertButton, 64);
    place (r2, gainButton, 70); place (r2, normaliseButton, 100); place (r2, dcButton, 96); place (r2, fadeInButton, 72); place (r2, fadeOutButton, 78);
    place (r2, fadeShapeBox, 90); place (r2, noiseButton, 140);
    auto r3 = row (28);
    place (r3, sensitivityLabel, 76); place (r3, sensitivitySlider, 170); place (r3, hitsButton, 90); place (r3, partsButton, 120); place (r3, clearMarkersButton, 120); place (r3, toSamplerButton, 200);

    auto bottom = area.removeFromBottom (30);
    area.removeFromBottom (6);
    cancelButton.setBounds (bottom.removeFromRight (90));
    bottom.removeFromRight (6);
    applyButton.setBounds (bottom.removeFromRight (240));
    bottom.removeFromRight (10);
    statusLabel.setBounds (bottom);

    spectrumArea = area.removeFromBottom (120);
    area.removeFromBottom (6);
    rulerArea = area.removeFromTop (kRuler);
    waveArea = area;
    if (current != nullptr && std::fabs (viewStart) < 1.0e-9 && framesPerPixel >= current->numFrames() / static_cast<double> (std::max (1, waveArea.getWidth())) * 0.999)
        fitView();
    clampView();
}

void SampleEditor::paint (juce::Graphics& g)
{
    g.fillAll (kBg);
    g.setColour (kPanel);
    g.fillRect (waveArea);
    g.fillRect (spectrumArea);
    if (current == nullptr || waveArea.getWidth() <= 0)
        return;

    const double sr = current->sampleRate;
    const int n = current->numFrames();

    // ruler: a tick every "nice" number of seconds, at least 70 px apart
    g.setColour (juce::Colour (0xff2c313a));
    g.fillRect (rulerArea);
    const double pxPerSecond = sr / framesPerPixel;
    double step = 0.001;
    for (const double candidate : { 0.001, 0.002, 0.005, 0.01, 0.02, 0.05, 0.1, 0.2, 0.5, 1.0, 2.0, 5.0, 10.0, 20.0, 30.0, 60.0, 120.0, 300.0 })
    {
        step = candidate;
        if (candidate * pxPerSecond >= 70.0)
            break;
    }
    g.setFont (11.0f);
    for (double t = std::ceil (viewStart / sr / step) * step; xAtFrame (t * sr) < waveArea.getRight(); t += step)
    {
        const int x = xAtFrame (t * sr);
        g.setColour (juce::Colours::grey);
        g.drawVerticalLine (x, static_cast<float> (rulerArea.getY() + 10), static_cast<float> (rulerArea.getBottom()));
        g.setColour (juce::Colours::lightgrey);
        g.drawText (timeText (t * sr), x + 3, rulerArea.getY(), 70, 12, juce::Justification::left);
    }

    // waveform: left channel on the upper half, right on the lower
    const int half = waveArea.getHeight() / 2;
    const int midL = waveArea.getY() + half / 2, midR = waveArea.getY() + half + half / 2;
    g.setColour (juce::Colour (0xff343a45));
    g.drawHorizontalLine (midL, static_cast<float> (waveArea.getX()), static_cast<float> (waveArea.getRight()));
    g.drawHorizontalLine (midR, static_cast<float> (waveArea.getX()), static_cast<float> (waveArea.getRight()));
    g.drawHorizontalLine (waveArea.getY() + half, static_cast<float> (waveArea.getX()), static_cast<float> (waveArea.getRight()));
    const float scale = half * 0.45f;
    g.setColour (kWave);
    for (int x = 0; x < waveArea.getWidth(); ++x)
    {
        const double f0 = viewStart + x * framesPerPixel, f1 = f0 + framesPerPixel;
        if (f1 < 0.0 || f0 >= n)
            continue;
        float loL = 1.0e9f, hiL = -1.0e9f, loR = 1.0e9f, hiR = -1.0e9f;
        if (framesPerPixel >= Peaks::kFrames)
        {
            const int e0 = std::clamp (static_cast<int> (f0 / Peaks::kFrames), 0, static_cast<int> (peaks.lowL.size()) - 1);
            const int e1 = std::clamp (static_cast<int> (f1 / Peaks::kFrames), e0, static_cast<int> (peaks.lowL.size()) - 1);
            for (int e = e0; e <= e1; ++e)
            {
                loL = std::min (loL, peaks.lowL[static_cast<size_t> (e)]); hiL = std::max (hiL, peaks.highL[static_cast<size_t> (e)]);
                loR = std::min (loR, peaks.lowR[static_cast<size_t> (e)]); hiR = std::max (hiR, peaks.highR[static_cast<size_t> (e)]);
            }
        }
        else
        {
            const int a = std::clamp (static_cast<int> (f0), 0, n - 1), b = std::clamp (static_cast<int> (std::ceil (f1)), a + 1, n);
            for (int i = a; i < b; ++i)
            {
                const float l = current->left[static_cast<size_t> (i)], r = current->right[static_cast<size_t> (i)];
                loL = std::min (loL, l); hiL = std::max (hiL, l);
                loR = std::min (loR, r); hiR = std::max (hiR, r);
            }
        }
        const int px = waveArea.getX() + x;
        g.drawVerticalLine (px, midL - hiL * scale - 0.5f, midL - loL * scale + 0.5f);
        g.drawVerticalLine (px, midR - hiR * scale - 0.5f, midR - loR * scale + 0.5f);
    }
    // anything beyond full scale (after a boost) is marked in red at the top of the lane
    g.setColour (juce::Colour (0xffd9534f).withAlpha (0.5f));
    g.drawHorizontalLine (midL - static_cast<int> (scale), static_cast<float> (waveArea.getX()), static_cast<float> (waveArea.getRight()));
    g.drawHorizontalLine (midR - static_cast<int> (scale), static_cast<float> (waveArea.getX()), static_cast<float> (waveArea.getRight()));

    g.saveState();
    g.reduceClipRegion (waveArea.expanded (0, kRuler));
    if (hasSelection())
    {
        g.setColour (kAccent.withAlpha (0.22f));
        g.fillRect (juce::Rectangle<int> (xAtFrame (selBegin), waveArea.getY(), std::max (1, xAtFrame (selEnd) - xAtFrame (selBegin)), waveArea.getHeight()));
        g.setColour (kAccent);
        g.drawVerticalLine (xAtFrame (selBegin), static_cast<float> (waveArea.getY()), static_cast<float> (waveArea.getBottom()));
        g.drawVerticalLine (xAtFrame (selEnd), static_cast<float> (waveArea.getY()), static_cast<float> (waveArea.getBottom()));
    }
    else
    {
        g.setColour (juce::Colours::white.withAlpha (0.7f));
        g.drawVerticalLine (xAtFrame (cursor), static_cast<float> (waveArea.getY()), static_cast<float> (waveArea.getBottom()));
    }
    for (size_t i = 0; i < markers.size(); ++i)
    {
        const int x = xAtFrame (markers[i] * sr);
        g.setColour (kGood);
        for (int y = waveArea.getY(); y < waveArea.getBottom(); y += 6)
            g.drawVerticalLine (x, static_cast<float> (y), static_cast<float> (std::min (y + 3, waveArea.getBottom())));
        g.setFont (10.0f);
        g.drawText (juce::String (static_cast<int> (i) + 1), x + 2, waveArea.getY() + 1, 24, 11, juce::Justification::left);
    }
    if (engine.auditioner().isPlaying())
    {
        g.setColour (kAccent);
        g.drawVerticalLine (xAtFrame (engine.auditioner().positionFrames()), static_cast<float> (waveArea.getY()), static_cast<float> (waveArea.getBottom()));
    }
    g.restoreState();

    // information line above the spectrum is the status label; the spectrum itself:
    g.setColour (juce::Colours::lightgrey);
    g.setFont (11.0f);
    const juce::String info = hasSelection() ? "Selection " + timeText (selBegin) + " - " + timeText (selEnd) + "  (" + juce::String ((selEnd - selBegin) / sr, 3) + " s)   "
                                             : "Cursor " + timeText (cursor) + "   ";
    g.drawText (info + "Length " + timeText (n) + "  |  " + juce::String (sr, 0) + " Hz  |  peak " + juce::String (toDb (peakOf (current, { 0, n })), 1) + " dB",
                waveArea.getX(), waveArea.getBottom() - 14, waveArea.getWidth(), 12, juce::Justification::right);

    auto plot = spectrumArea.reduced (30, 14);
    plot.removeFromBottom (12);
    g.setColour (juce::Colour (0xff343a45));
    for (const float db : { 0.0f, -20.0f, -40.0f, -60.0f, -80.0f })
    {
        const float y = plot.getY() + plot.getHeight() * (-db / -kFloorDb);
        g.drawHorizontalLine (static_cast<int> (y), static_cast<float> (plot.getX()), static_cast<float> (plot.getRight()));
        g.setColour (juce::Colours::grey);
        g.drawText (juce::String (static_cast<int> (db)), spectrumArea.getX() + 2, static_cast<int> (y) - 6, 26, 12, juce::Justification::right);
        g.setColour (juce::Colour (0xff343a45));
    }
    const double lowHz = 20.0, highHz = std::min (20000.0, sr * 0.5);
    for (const double hz : { 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0, 20000.0 })
    {
        if (hz >= highHz)
            continue;
        const float x = plot.getX() + static_cast<float> (plot.getWidth() * std::log (hz / lowHz) / std::log (highHz / lowHz));
        g.setColour (juce::Colour (0xff343a45));
        g.drawVerticalLine (static_cast<int> (x), static_cast<float> (plot.getY()), static_cast<float> (plot.getBottom()));
        g.setColour (juce::Colours::grey);
        g.drawText (hz >= 1000.0 ? juce::String (hz / 1000.0, 0) + "k" : juce::String (hz, 0), static_cast<int> (x) - 16, plot.getBottom() + 1, 32, 11, juce::Justification::centred);
    }
    juce::Path path;
    bool started = false;
    for (int b = 1; b < SpectrumAnalyser::kBins; ++b)
    {
        const double hz = SpectrumAnalyser::binFrequency (b, sr);
        if (hz < lowHz || hz > highHz)
            continue;
        const float x = plot.getX() + static_cast<float> (plot.getWidth() * std::log (hz / lowHz) / std::log (highHz / lowHz));
        const float y = plot.getY() + plot.getHeight() * std::clamp (-shown[static_cast<size_t> (b)] / -kFloorDb, 0.0f, 1.0f);
        if (! started)
        {
            path.startNewSubPath (x, static_cast<float> (plot.getBottom()));
            path.lineTo (x, y);
            started = true;
        }
        else
            path.lineTo (x, y);
    }
    if (started)
    {
        juce::Path fill (path);
        fill.lineTo (static_cast<float> (plot.getRight()), static_cast<float> (plot.getBottom()));
        fill.closeSubPath();
        g.setColour (kAccent.withAlpha (0.25f));
        g.fillPath (fill);
        g.setColour (kAccent);
        g.strokePath (path, juce::PathStrokeType (1.2f));
    }
    g.setColour (juce::Colours::lightgrey);
    g.setFont (11.0f);
    g.drawText ("Spectrum at the playhead (while playing), the cursor or the middle of the selection", spectrumArea.getX() + 30, spectrumArea.getY() + 1, 500, 12,
                juce::Justification::left);
}

SampleEditorWindow::SampleEditorWindow (AudioEngine& engine)
    : juce::DocumentWindow ("Sample editor", juce::Colour (0xff1b1e24), juce::DocumentWindow::closeButton), content (engine)
{
    setUsingNativeTitleBar (false);
    setContentNonOwned (&content, false);
    setResizable (true, true);
    setResizeLimits (900, 520, 2400, 1200);
    centreWithSize (1000, 620);
    setAlwaysOnTop (true);
}
} // namespace sls
