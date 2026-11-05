#include "ui/PanelWindow.h"

namespace sls
{
namespace
{
const juce::Colour kFrame (0xff3a3e48);
const juce::Colour kFrameActive (0xffe8943a);
const juce::Colour kTitleText (0xffe6e8ec);
const juce::Colour kBody (0xff2a2d34);
} // namespace

void PanelWindow::Constrainer::checkBounds (juce::Rectangle<int>& bounds, const juce::Rectangle<int>& previous,
                                            const juce::Rectangle<int>& limits, bool stretchTop,
                                            bool stretchLeft, bool stretchBottom, bool stretchRight)
{
    ComponentBoundsConstrainer::checkBounds (bounds, previous, limits, stretchTop, stretchLeft,
                                             stretchBottom, stretchRight);
    bounds = bounds.getIntersection (limits); // never leave the workspace
}

PanelWindow::PanelWindow (juce::String panelTitle) : title (std::move (panelTitle))
{
    closeButton.setButtonText (juce::String (juce::CharPointer_UTF8 ("\xc3\x97"))); // multiplication sign
    closeButton.setColour (juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
    closeButton.setColour (juce::TextButton::textColourOffId, kTitleText);
    closeButton.setMouseClickGrabsKeyboardFocus (false);
    closeButton.setTooltip ("Close");
    closeButton.onClick = [this] { setOpen (false); notifyLayoutChanged(); };
    addAndMakeVisible (closeButton);

    constrainer.onStart = [this] { maximised = false; activate(); };
    constrainer.onEnd = [this] { notifyLayoutChanged(); };
    constrainer.setMinimumSize (minWidth, minHeight);
    addAndMakeVisible (resizer); // added last: it sits on top of the content, but only reacts at the edges

    setOpaque (true);
}

PanelWindow::~PanelWindow()
{
    setContent (nullptr);
}

void PanelWindow::setContent (juce::Component* newContent)
{
    if (content != nullptr)
    {
        content->removeMouseListener (&raiseOnClick);
        removeChildComponent (content);
    }

    content = newContent;

    if (content != nullptr)
    {
        addAndMakeVisible (content);
        content->addMouseListener (&raiseOnClick, true);
        resizer.toFront (false);
    }
    resized();
}

void PanelWindow::setMinimumSize (int w, int h)
{
    minWidth = w;
    minHeight = h;
    constrainer.setMinimumSize (w, h);
}

void PanelWindow::setTitle (const juce::String& newTitle)
{
    if (title != newTitle)
    {
        title = newTitle;
        repaint();
    }
}

PanelLayout PanelWindow::currentLayout() const
{
    PanelLayout l;
    l.x = getX();
    l.y = getY();
    l.width = getWidth();
    l.height = getHeight();
    l.visible = isVisible();
    return l;
}

void PanelWindow::applyLayout (const PanelLayout& layout)
{
    auto l = layout;
    if (auto* parent = getParentComponent())
        l = l.clampedTo (parent->getWidth(), parent->getHeight(), minWidth, minHeight);

    maximised = false;
    setBounds (l.x, l.y, l.width, l.height);
    setVisible (l.visible);
}

void PanelWindow::clampToParent()
{
    auto* parent = getParentComponent();
    if (parent == nullptr)
        return;

    if (maximised)
    {
        setBounds (parent->getLocalBounds());
        return;
    }

    const auto l = currentLayout().clampedTo (parent->getWidth(), parent->getHeight(), minWidth, minHeight);
    setBounds (l.x, l.y, l.width, l.height);
}

void PanelWindow::setOpen (bool shouldBeOpen)
{
    setVisible (shouldBeOpen);
    if (! shouldBeOpen)
        setActive (false);
}

void PanelWindow::setActive (bool shouldBeActive)
{
    if (active != shouldBeActive)
    {
        active = shouldBeActive;
        repaint();
    }
}

void PanelWindow::toggleMaximised()
{
    auto* parent = getParentComponent();
    if (parent == nullptr)
        return;

    if (maximised)
    {
        maximised = false;
        setBounds (restoreBounds);
    }
    else
    {
        restoreBounds = getBounds();
        maximised = true;
        setBounds (parent->getLocalBounds());
    }
    notifyLayoutChanged();
}

void PanelWindow::activate()
{
    toFront (false); // reorder only: keyboard focus stays with the main component
    if (onActivated)
        onActivated (*this);
}

void PanelWindow::notifyLayoutChanged()
{
    if (onLayoutChanged)
        onLayoutChanged (*this);
}

juce::Rectangle<int> PanelWindow::titleBar() const
{
    return { kBorder, kBorder, getWidth() - 2 * kBorder, kTitleHeight };
}

void PanelWindow::paint (juce::Graphics& g)
{
    g.fillAll (kBody);

    g.setColour (active ? kFrameActive : kFrame);
    g.drawRect (getLocalBounds(), kBorder);

    const auto bar = titleBar();
    g.setColour (active ? kFrameActive.darker (0.35f) : kFrame);
    g.fillRect (bar);

    g.setColour (kTitleText);
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.drawText (title, bar.withTrimmedLeft (8).withTrimmedRight (kTitleHeight), juce::Justification::centredLeft, true);
}

void PanelWindow::resized()
{
    auto bar = titleBar();
    closeButton.setBounds (bar.removeFromRight (kTitleHeight));
    resizer.setBounds (getLocalBounds());

    if (content != nullptr)
        content->setBounds (getLocalBounds().reduced (kBorder).withTrimmedTop (kTitleHeight));
}

void PanelWindow::mouseDown (const juce::MouseEvent& e)
{
    activate();

    dragging = e.eventComponent == this && titleBar().contains (e.getPosition());
    if (dragging)
    {
        dragStartScreen = e.getScreenPosition();
        dragStartPosition = getPosition();
    }
}

void PanelWindow::mouseDrag (const juce::MouseEvent& e)
{
    auto* parent = getParentComponent();
    if (! dragging || parent == nullptr)
        return;

    if (maximised)
    {
        // Dragging a maximised panel restores it first, like a desktop window.
        maximised = false;
        setSize (restoreBounds.getWidth(), restoreBounds.getHeight());
    }

    const auto target = dragStartPosition + (e.getScreenPosition() - dragStartScreen);
    setTopLeftPosition (std::clamp (target.x, 0, std::max (0, parent->getWidth() - getWidth())),
                        std::clamp (target.y, 0, std::max (0, parent->getHeight() - getHeight())));
}

void PanelWindow::mouseUp (const juce::MouseEvent&)
{
    if (dragging)
    {
        dragging = false;
        notifyLayoutChanged();
    }
}

void PanelWindow::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (e.eventComponent == this && titleBar().contains (e.getPosition()))
        toggleMaximised();
}

// ------------------------------------------------------------------------- Workspace

void Workspace::addPanel (PanelWindow& panel)
{
    panels.push_back (&panel);
    addChildComponent (panel); // hidden until opened
    panel.onActivated = [this] (PanelWindow& p) { activate (&p); };
}

void Workspace::activate (PanelWindow* panel)
{
    activePanel = panel;
    for (auto* p : panels)
        p->setActive (p == panel);
    if (panel != nullptr)
        panel->toFront (false);
    giveFocusToParentIfUnfocused();
}

// Shortcuts (Space, F6/F7/F9) live on the main component, so when a click lands on a part of the
// workspace that cannot take keyboard focus itself, hand the focus up instead of dropping it.
void Workspace::giveFocusToParentIfUnfocused()
{
    if (auto* parent = getParentComponent())
        if (! parent->hasKeyboardFocus (true))
            parent->grabKeyboardFocus();
}

void Workspace::mouseDown (const juce::MouseEvent&)
{
    activate (nullptr); // clicking the empty background deselects the active panel
}

void Workspace::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff22252b));

    g.setColour (juce::Colour (0xff2c3038));
    for (int y = 12; y < getHeight(); y += 24)
        for (int x = 12; x < getWidth(); x += 24)
            g.fillRect (x, y, 2, 2);
}

void Workspace::resized()
{
    for (auto* p : panels)
        p->clampToParent();
}
} // namespace sls
