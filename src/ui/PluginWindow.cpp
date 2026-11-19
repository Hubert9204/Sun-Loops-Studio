#include "ui/PluginWindow.h"

namespace sls
{
namespace
{
constexpr int kBarHeight = 32;
const juce::Colour kBarColour (0xff23262d), kBarText (0xffd8dbe0), kBarDim (0xff8a9099), kCrashed (0xffe0605a);
} // namespace

// What the window holds: the bar and, under it, the plugin's editor (or a note when there is none here). It follows the editor's size.
class PluginFrame : public juce::Component, private juce::ComponentListener
{
public:
    explicit PluginFrame (PluginWindow& w) : window (w)
    {
        for (auto* b : { &presets, &previous, &next, &undo, &redo, &routing, &process })
            addAndMakeVisible (*b);
        presets.setTooltip ("The plugin's own presets and yours: load, save, step through them with the arrows");
        previous.setTooltip ("The previous preset");
        next.setTooltip ("The next preset");
        undo.setTooltip ("Undo the last change made in the plugin (Ctrl+Z)");
        redo.setTooltip ("Redo it (Ctrl+Y)");
        routing.setTooltip ("Where the plugin's sidechain input comes from, where its other outputs go and which channel its MIDI out plays");
        process.setTooltip ("Run the plugin in a process of its own (a sandbox): if it crashes, the program goes on and the plugin can be reloaded. "
                            "It costs a little more CPU and memory.");
        presets.onClick = [this] { window.showPresetMenu (presets); };
        previous.onClick = [this] { window.stepPreset (-1); };
        next.onClick = [this] { window.stepPreset (1); };
        undo.onClick = [this] { window.undo(); };
        redo.onClick = [this] { window.redo(); };
        routing.onClick = [this] { if (window.onRoutingMenu) window.onRoutingMenu (routing); };
        process.onClick = [this]
        {
            if (window.plugin->sandboxState() == JucePlugin::SandboxState::Crashed && window.onRestart)
                window.onRestart();
            else if (window.onToggleSandbox)
                window.onToggleSandbox();
        };
        state.setColour (juce::Label::textColourId, kBarDim);
        state.setFont (juce::FontOptions (12.0f));
        addAndMakeVisible (state);
        showWindow.onClick = [this] { window.plugin->showSandboxEditor (true); };
        build();
    }

    ~PluginFrame() override
    {
        if (editor != nullptr)
            editor->removeComponentListener (this);
    }

    // The editor (made again when the plugin moved between this process and its sandbox).
    void build()
    {
        if (editor != nullptr)
            editor->removeComponentListener (this);
        editor.reset();
        note.reset();
        removeChildComponent (&showWindow);
        auto* instance = window.plugin->getInstance();
        if (instance != nullptr)
        {
            juce::AudioProcessorEditor* made = instance->hasEditor() ? instance->createEditorIfNeeded() : nullptr;
            if (made == nullptr)
                made = new juce::GenericAudioProcessorEditor (*instance);
            editor.reset (made);
            addAndMakeVisible (*editor);
            editor->addComponentListener (this);
        }
        else
        {
            const bool sandboxed = window.plugin->isSandboxed();
            note = std::make_unique<juce::Label> (juce::String(), sandboxed ? "This plugin runs in a process of its own: its window opens next to this one."
                                                                            : "This plugin could not be loaded (it is not installed here). Its settings are kept in the project.");
            note->setJustificationType (juce::Justification::centred);
            note->setColour (juce::Label::textColourId, kBarText);
            note->setSize (520, sandboxed ? 70 : 90);
            addAndMakeVisible (*note);
            if (sandboxed)
                addAndMakeVisible (showWindow);
        }
        refreshBar();
        fit();
    }

    void refreshBar()
    {
        auto& p = *window.plugin;
        undo.setEnabled (window.canUndo());
        redo.setEnabled (window.canRedo());
        routing.setVisible (p.hasSidechain() || p.extraOutputChannels() > 0 || p.sendsMidi());
        const auto st = p.sandboxState();
        process.setButtonText (st == JucePlugin::SandboxState::Crashed ? "Reload" : p.isSandboxed() ? "Sandbox: on" : "Sandbox: off");
        process.setColour (juce::TextButton::buttonColourId, st == JucePlugin::SandboxState::Crashed ? kCrashed : juce::Colour (0xff353a44));
        process.setEnabled (p.isLoaded() || st == JucePlugin::SandboxState::Crashed);
        state.setText (st == JucePlugin::SandboxState::Crashed ? "The plugin crashed: the program went on. Reload it."
                       : st == JucePlugin::SandboxState::Stalled ? "The plugin does not answer in time"
                       : juce::String(),
                       juce::dontSendNotification);
        state.setColour (juce::Label::textColourId, st == JucePlugin::SandboxState::Crashed ? kCrashed : kBarDim);
        resized();
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xff1b1e24));
        g.setColour (kBarColour);
        g.fillRect (0, 0, getWidth(), kBarHeight);
    }

    void resized() override
    {
        auto bar = getLocalBounds().removeFromTop (kBarHeight).reduced (4, 4);
        presets.setBounds (bar.removeFromLeft (80));
        bar.removeFromLeft (2);
        previous.setBounds (bar.removeFromLeft (26));
        next.setBounds (bar.removeFromLeft (26));
        bar.removeFromLeft (8);
        undo.setBounds (bar.removeFromLeft (50));
        redo.setBounds (bar.removeFromLeft (50));
        bar.removeFromLeft (8);
        if (routing.isVisible())
        {
            routing.setBounds (bar.removeFromLeft (70));
            bar.removeFromLeft (8);
        }
        process.setBounds (bar.removeFromRight (100));
        bar.removeFromRight (6);
        state.setBounds (bar);
        auto body = getLocalBounds().withTrimmedTop (kBarHeight);
        if (editor != nullptr)
            editor->setTopLeftPosition (0, kBarHeight);
        if (note != nullptr)
        {
            auto n = body;
            if (showWindow.isShowing())
                showWindow.setBounds (n.removeFromBottom (36).withSizeKeepingCentre (180, 26));
            note->setBounds (n);
        }
    }

    void fit()
    {
        const int w = std::max (480, editor != nullptr ? editor->getWidth() : note != nullptr ? note->getWidth() : 480);
        const int h = kBarHeight + (editor != nullptr ? editor->getHeight() : note != nullptr ? note->getHeight() + (showWindow.isShowing() ? 40 : 0) : 60);
        setSize (w, h);
    }

    juce::AudioProcessorEditor* currentEditor() const noexcept { return editor.get(); }

private:
    void componentMovedOrResized (juce::Component& c, bool, bool wasResized) override
    {
        if (wasResized && &c == editor.get())
            fit();
    }

    PluginWindow& window;
    juce::TextButton presets { "Presets" }, previous { "<" }, next { ">" }, undo { "Undo" }, redo { "Redo" }, routing { "Routing" },
        process { "Sandbox: off" }, showWindow { "Show its window" };
    juce::Label state;
    std::unique_ptr<juce::AudioProcessorEditor> editor;
    std::unique_ptr<juce::Label> note;
};

PluginWindow::PluginWindow (std::shared_ptr<JucePlugin> pluginToShow, const juce::File& root)
    : juce::DocumentWindow (juce::String (pluginToShow->displayName()), juce::Colour (0xff1b1e24), juce::DocumentWindow::closeButton),
      plugin (std::move (pluginToShow)), presetsRoot (root)
{
    setUsingNativeTitleBar (false);
    currentState = plugin->getState();
    seenChanges = plugin->changeCount();
    auto made = std::make_unique<PluginFrame> (*this);
    frame = made.get();
    setContentOwned (made.release(), true);
    if (auto* e = frame->currentEditor())
        setResizable (e->isResizable(), false);
    setAlwaysOnTop (true);
    centreWithSize (getWidth(), getHeight());
    setVisible (true);
    if (plugin->isSandboxed())
        plugin->showSandboxEditor (true);
    startTimerHz (5);
}

PluginWindow::~PluginWindow()
{
    stopTimer();
    if (plugin->isSandboxed())
        plugin->showSandboxEditor (false);
    clearContentComponent(); // the editor goes before the plugin it shows
}

void PluginWindow::refreshBar()
{
    if (frame != nullptr)
        frame->refreshBar();
}

// The plugin's state is kept after a change, once nothing has changed for a moment (a knob turned is one step, not a hundred).
void PluginWindow::timerCallback()
{
    static constexpr int kQuietTicks = 2; // 0.4 s
    const auto now = plugin->changeCount();
    if (now != seenChanges)
    {
        seenChanges = now;
        quietTicks = 0;
    }
    else if (quietTicks >= 0 && ++quietTicks >= kQuietTicks)
    {
        quietTicks = -1;
        auto state = plugin->getState();
        if (settling)
        {
            settling = false;
            currentState = std::move (state); // (what the plugin reports after a state was put back is that state)
        }
        else if (state != currentState && state.getSize() > 0)
        {
            undoStates.push_back (std::move (currentState));
            if (undoStates.size() > static_cast<size_t> (kMaxUndo))
                undoStates.erase (undoStates.begin());
            redoStates.clear();
            currentState = std::move (state);
            refreshBar();
        }
    }
    static int ticks = 0;
    if (++ticks % 5 == 0 || plugin->sandboxState() == JucePlugin::SandboxState::Crashed)
        refreshBar(); // (the sandbox's state)
}

void PluginWindow::restore (const juce::MemoryBlock& state)
{
    settling = true;
    quietTicks = 0;
    plugin->setState (state);
    seenChanges = plugin->changeCount();
    if (onStateRestored)
        onStateRestored();
    refreshBar();
}

void PluginWindow::undo()
{
    if (undoStates.empty())
        return;
    redoStates.push_back (currentState);
    currentState = std::move (undoStates.back());
    undoStates.pop_back();
    restore (currentState);
}

void PluginWindow::redo()
{
    if (redoStates.empty())
        return;
    undoStates.push_back (currentState);
    currentState = std::move (redoStates.back());
    redoStates.pop_back();
    restore (currentState);
}

bool PluginWindow::keyPressed (const juce::KeyPress& key)
{
    const auto mods = key.getModifiers();
    if (mods.isCommandDown() && (key.getKeyCode() == 'Z' || key.getKeyCode() == 'z'))
    {
        mods.isShiftDown() ? redo() : undo();
        return true;
    }
    if (mods.isCommandDown() && (key.getKeyCode() == 'Y' || key.getKeyCode() == 'y'))
    {
        redo();
        return true;
    }
    return juce::DocumentWindow::keyPressed (key);
}

juce::File PluginWindow::presetsFolder (const juce::File& root, const juce::PluginDescription& d) { return PluginHost::presetsFolder (root, d); }
juce::Result PluginWindow::savePreset (const JucePlugin& p, const juce::File& file) { return PluginHost::savePreset (p, file); }
juce::Result PluginWindow::loadPreset (JucePlugin& p, const juce::File& file) { return PluginHost::loadPreset (p, file); }

void PluginWindow::showPresetMenu (juce::Component& anchor)
{
    juce::PopupMenu menu;
    const auto names = plugin->programNames();
    const int current = plugin->currentProgram();
    if (names.size() > 1)
    {
        juce::PopupMenu factory;
        for (int i = 0; i < names.size() && i < 999; ++i)
            factory.addItem (1000 + i, names[i].isNotEmpty() ? names[i] : "Program " + juce::String (i + 1), true, i == current);
        menu.addSubMenu ("The plugin's presets (" + juce::String (names.size()) + ")", factory);
    }
    const auto folder = presetsFolder (presetsRoot, plugin->getDescription());
    auto files = folder.findChildFiles (juce::File::findFiles, false, "*.slsplugin");
    files.sort();
    if (! files.isEmpty())
    {
        menu.addSectionHeader ("Your presets");
        for (int i = 0; i < files.size() && i < 500; ++i)
            menu.addItem (5000 + i, files[i].getFileNameWithoutExtension(), true, files[i] == lastPreset);
    }
    menu.addSeparator();
    menu.addItem (1, "Save preset...", plugin->isLoaded());
    menu.addItem (2, "Load a preset file...", plugin->isLoaded());
    menu.addItem (3, "Open the presets folder");
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&anchor),
                        [safe = juce::Component::SafePointer<PluginWindow> (this), files, folder] (int result)
    {
        if (safe == nullptr || result == 0)
            return;
        auto& self = *safe;
        if (result >= 1000 && result < 5000)
        {
            self.plugin->setProgram (result - 1000);
            if (self.onStateRestored)
                self.onStateRestored();
        }
        else if (result >= 5000 && result - 5000 < files.size())
        {
            const auto file = files[result - 5000];
            if (const auto r = loadPreset (*self.plugin, file); r.failed())
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Preset", r.getErrorMessage());
            else
            {
                self.lastPreset = file;
                if (self.onStateRestored)
                    self.onStateRestored();
            }
        }
        else if (result == 1)
        {
            auto* box = new juce::AlertWindow ("Save preset", "A name for these settings of " + self.plugin->getDescription().name + ":",
                                               juce::MessageBoxIconType::NoIcon);
            box->addTextEditor ("name", self.lastPreset != juce::File() ? self.lastPreset.getFileNameWithoutExtension() : juce::String ("My preset"));
            box->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
            box->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
            box->enterModalState (true, juce::ModalCallbackFunction::create ([safe, box, folder] (int ok)
            {
                if (safe == nullptr || ok == 0)
                    return;
                const auto name = juce::File::createLegalFileName (box->getTextEditorContents ("name").trim());
                if (name.isEmpty())
                    return;
                const auto file = folder.getChildFile (name + ".slsplugin");
                if (const auto r = savePreset (*safe->plugin, file); r.failed())
                    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Preset", r.getErrorMessage());
                else
                    safe->lastPreset = file;
            }), true);
        }
        else if (result == 2)
        {
            auto chooser = std::make_shared<juce::FileChooser> ("Load a plugin preset", folder.isDirectory() ? folder : self.presetsRoot, "*.slsplugin");
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                  [safe, chooser] (const juce::FileChooser& fc)
            {
                const auto file = fc.getResult();
                if (safe == nullptr || file == juce::File())
                    return;
                if (const auto r = loadPreset (*safe->plugin, file); r.failed())
                    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Preset", r.getErrorMessage());
                else
                {
                    safe->lastPreset = file;
                    if (safe->onStateRestored)
                        safe->onStateRestored();
                }
            });
        }
        else if (result == 3)
        {
            folder.createDirectory();
            folder.startAsProcess();
        }
    });
}

// The arrows: through the plugin's own presets when it has several, else through the user's presets in its folder.
void PluginWindow::stepPreset (int delta)
{
    const auto names = plugin->programNames();
    if (names.size() > 1)
    {
        const int now = std::max (0, plugin->currentProgram());
        plugin->setProgram ((now + delta + names.size()) % names.size());
    }
    else
    {
        auto files = presetsFolder (presetsRoot, plugin->getDescription()).findChildFiles (juce::File::findFiles, false, "*.slsplugin");
        files.sort();
        if (files.isEmpty())
            return;
        const int at = files.indexOf (lastPreset);
        const auto next = files[at < 0 ? (delta > 0 ? 0 : files.size() - 1) : (at + delta + files.size()) % files.size()];
        if (loadPreset (*plugin, next).wasOk())
            lastPreset = next;
    }
    if (onStateRestored)
        onStateRestored();
}
} // namespace sls
