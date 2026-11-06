// Part of MainComponent: third-party plugins (0.17.0). Scanning is in MainComponentBrowser.cpp; the engine side is in Player.cpp and
// src/plugins/PluginHost.cpp.
#include "ui/MainComponent.h"

namespace sls
{
namespace
{
const JucePlugin* asJuce (const HostedPlugin* p) { return dynamic_cast<const JucePlugin*> (p); }
} // namespace

juce::File MainComponent::pluginSettingsFile (const juce::String& name) const
{
    const auto folder = settings != nullptr ? settings->getFile().getParentDirectory()
                                            : juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("SunLoopsStudio");
    folder.createDirectory();
    return folder.getChildFile (name);
}

void MainComponent::initPlugins()
{
    pluginHost->loadKnownList (pluginSettingsFile ("plugins.xml"));
    refreshFoundPlugins();
    if (! foundPlugins.empty())
    {
        pluginScanState = PluginScanState::Done;
        rebuildBrowser();
    }

    // The device (re)starts with the audio stopped: every plugin is prepared for its rate there.
    engine.onDeviceStarting = [host = pluginHost.get()] (double rate) { host->prepareAll (rate, Player::kChunk); };
}

void MainComponent::refreshFoundPlugins()
{
    foundPlugins.clear();
    for (const auto& d : pluginHost->known().getTypes())
    {
        PluginInfo info;
        info.name = d.name;
        info.manufacturer = d.manufacturerName;
        info.path = d.fileOrIdentifier;
        info.identifier = d.createIdentifierString();
        info.format = d.pluginFormatName;
        info.isInstrument = d.isInstrument;
        info.numInputChannels = d.numInputChannels;
        info.numOutputChannels = d.numOutputChannels;
        foundPlugins.push_back (info);
    }
    std::sort (foundPlugins.begin(), foundPlugins.end(),
               [] (const PluginInfo& a, const PluginInfo& b) { return a.name.compareIgnoreCase (b.name) < 0; });
}

bool MainComponent::findPluginDescription (const juce::String& identifier, juce::PluginDescription& out) const
{
    for (const auto& d : pluginHost->known().getTypes())
        if (d.createIdentifierString() == identifier)
        {
            out = d;
            return true;
        }
    return false;
}

std::shared_ptr<JucePlugin> MainComponent::createPlugin (const juce::String& identifier, juce::String& error)
{
    juce::PluginDescription description;
    if (! findPluginDescription (identifier, description))
    {
        error = "This plugin is no longer in the list: scan again (browser > Plugin).";
        return nullptr;
    }
    return pluginHost->create (description, engine.getSampleRate(), Player::kChunk, error);
}

std::shared_ptr<JucePlugin> MainComponent::duplicatePlugin (const HostedPlugin& original)
{
    const auto* source = asJuce (&original);
    if (source == nullptr)
        return nullptr;
    juce::String error;
    auto copy = source->isLoaded() ? pluginHost->create (source->getDescription(), engine.getSampleRate(), Player::kChunk, error)
                                                 : nullptr;
    if (copy == nullptr)
        copy = JucePlugin::placeholder (source->getDescription(), source->getState(), source->isInstrument());
    else
        copy->setState (source->getState());
    return copy;
}

void MainComponent::setPluginInstrument (int channelIndex, const juce::String& identifier)
{
    juce::String error;
    auto plugin = createPlugin (identifier, error);
    if (plugin == nullptr)
    {
        statusLabel.setText ("Plugin not loaded: " + error, juce::dontSendNotification);
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Plugin not loaded", error, "OK", this);
        return;
    }
    if (channelIndex < 0)
    {
        channelIndex = addChannel (Instrument::Plugin, false);
        if (channelIndex < 0)
        {
            statusLabel.setText ("All " + juce::String (kMaxChannels) + " channels are in use", juce::dontSendNotification);
            return;
        }
    }
    auto& channel = channels[static_cast<size_t> (channelIndex)];
    channel.instrument = Instrument::Plugin;
    channel.plugin = plugin;
    channel.name = juce::String (plugin->displayName()).substring (0, 16).toStdString();
    strips[static_cast<size_t> (channelIndex)]->refreshFromChannel();
    selectChannel (channelIndex);
    updateTrackRouting();
    publishProject();
    statusLabel.setText ("Loaded plugin " + juce::String (plugin->displayName()), juce::dontSendNotification);
    openPluginWindow (plugin);
}

void MainComponent::addPluginEffectForChannel (int channelIndex, const juce::String& identifier)
{
    if (channelIndex < 0 || channelIndex >= static_cast<int> (channels.size()))
    {
        statusLabel.setText ("Drop an effect plugin on a channel: it goes into that channel's mixer track", juce::dontSendNotification);
        return;
    }
    const int trackIndex = std::clamp (channels[static_cast<size_t> (channelIndex)].mixerTrack, 0, kMaxTracks - 1);
    auto& slots = tracks[static_cast<size_t> (trackIndex)].slots;
    const auto freeSlot = std::find_if (slots.begin(), slots.end(), [] (const EffectSlot& s) { return s.type == EffectType::None; });
    if (freeSlot == slots.end())
    {
        statusLabel.setText ("Track " + juce::String (trackIndex + 1) + " has no free effect slot", juce::dontSendNotification);
        return;
    }
    juce::String error;
    auto plugin = createPlugin (identifier, error);
    if (plugin == nullptr)
    {
        statusLabel.setText ("Plugin not loaded: " + error, juce::dontSendNotification);
        return;
    }
    EffectSlot slot;
    slot.type = EffectType::Plugin;
    slot.plugin = plugin;
    *freeSlot = slot;
    mixerPanel.refreshTrack (trackIndex);
    publishProject();
    statusLabel.setText (juce::String (plugin->displayName()) + " added to track " + juce::String (trackIndex + 1) + ", slot "
                             + juce::String (static_cast<int> (freeSlot - slots.begin()) + 1),
                         juce::dontSendNotification);
    openPluginWindow (plugin);
}

void MainComponent::openPluginWindow (const std::shared_ptr<HostedPlugin>& plugin)
{
    auto juce = std::dynamic_pointer_cast<JucePlugin> (plugin);
    if (juce == nullptr)
        return;
    for (auto& w : pluginWindows)
        if (w->shows() == plugin.get())
        {
            w->setVisible (true);
            w->toFront (true);
            return;
        }
    auto window = std::make_unique<PluginWindow> (std::move (juce));
    auto* raw = window.get();
    window->onClose = [this, raw]
    {
        // Deferred: the window is still running its own close handler.
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<MainComponent> (this), raw]
        {
            if (safe == nullptr)
                return;
            auto& list = safe->pluginWindows;
            list.erase (std::remove_if (list.begin(), list.end(), [raw] (const std::unique_ptr<PluginWindow>& w) { return w.get() == raw; }), list.end());
        });
    };
    pluginWindows.push_back (std::move (window));
}

void MainComponent::openChannelPlugin (int channelIndex)
{
    if (channelIndex < 0 || channelIndex >= static_cast<int> (channels.size()))
        return;
    selectChannel (channelIndex);
    const auto& channel = channels[static_cast<size_t> (channelIndex)];
    if (channel.instrument == Instrument::Plugin && channel.plugin != nullptr)
        openPluginWindow (channel.plugin);
}

bool MainComponent::projectUsesPlugin (const HostedPlugin* plugin) const
{
    for (const auto& c : channels)
        if (c.instrument == Instrument::Plugin && c.plugin.get() == plugin)
            return true;
    const auto inSlots = [plugin] (const std::array<EffectSlot, kSlotsPerTrack>& slots)
    {
        return std::any_of (slots.begin(), slots.end(), [plugin] (const EffectSlot& s) { return s.type == EffectType::Plugin && s.plugin.get() == plugin; });
    };
    for (const auto& t : tracks)
        if (inSlots (t.slots))
            return true;
    for (const auto& a : console.aux)
        if (inSlots (a.slots))
            return true;
    return inSlots (masterTrack.slots);
}

bool MainComponent::projectHasPlugins() const
{
    for (const auto& c : channels)
        if (c.instrument == Instrument::Plugin && c.plugin != nullptr)
            return true;
    const auto inSlots = [] (const std::array<EffectSlot, kSlotsPerTrack>& slots)
    {
        return std::any_of (slots.begin(), slots.end(), [] (const EffectSlot& s) { return s.type == EffectType::Plugin && s.plugin != nullptr; });
    };
    return std::any_of (tracks.begin(), tracks.end(), [&] (const MixerTrack& t) { return inSlots (t.slots); })
        || std::any_of (console.aux.begin(), console.aux.end(), [&] (const MixerTrack& t) { return inSlots (t.slots); })
        || inSlots (masterTrack.slots);
}

void MainComponent::closeUnusedPluginWindows()
{
    pluginWindows.erase (std::remove_if (pluginWindows.begin(), pluginWindows.end(),
                                         [this] (const std::unique_ptr<PluginWindow>& w) { return ! projectUsesPlugin (w->shows()); }),
                         pluginWindows.end());
}

void MainComponent::addPluginSpecs (ProjectDocument& doc, bool withState) const
{
    const auto spec = [withState] (const HostedPlugin& plugin, PluginSpec::Place place, int index, int slot)
    {
        PluginSpec s;
        s.place = place;
        s.index = index;
        s.slot = slot;
        s.instrument = plugin.isInstrument();
        if (const auto* j = asJuce (&plugin))
        {
            s.description = PluginHost::descriptionToText (j->getDescription()).toStdString();
            if (withState)
                s.state = j->getState().toBase64Encoding().toStdString();
            else
                s.state = "@" + juce::String::toHexString (static_cast<juce::pointer_sized_int> (reinterpret_cast<std::uintptr_t> (j))).toStdString();
        }
        return s;
    };
    for (size_t i = 0; i < channels.size(); ++i)
        if (channels[i].instrument == Instrument::Plugin && channels[i].plugin != nullptr)
            doc.pluginSpecs.push_back (spec (*channels[i].plugin, PluginSpec::Place::Channel, static_cast<int> (i), 0));
    const auto addSlots = [&] (const std::array<EffectSlot, kSlotsPerTrack>& slots, PluginSpec::Place place, int index)
    {
        for (size_t s = 0; s < slots.size(); ++s)
            if (slots[s].type == EffectType::Plugin && slots[s].plugin != nullptr)
                doc.pluginSpecs.push_back (spec (*slots[s].plugin, place, index, static_cast<int> (s)));
    };
    for (size_t t = 0; t < tracks.size(); ++t)
        addSlots (tracks[t].slots, PluginSpec::Place::Track, static_cast<int> (t));
    for (size_t a = 0; a < console.aux.size(); ++a)
        addSlots (console.aux[a].slots, PluginSpec::Place::Aux, static_cast<int> (a));
    addSlots (masterTrack.slots, PluginSpec::Place::Master, 0);
}

void MainComponent::loadDocumentPlugins (ProjectDocument& doc, juce::StringArray& problems)
{
    const auto make = [&] (const PluginSpec& spec) -> std::shared_ptr<HostedPlugin>
    {
        juce::PluginDescription description;
        if (! PluginHost::descriptionFromText (juce::String (spec.description), description))
        {
            problems.add ("A plugin description in the file could not be read.");
            return nullptr;
        }
        juce::MemoryBlock state;
        state.fromBase64Encoding (juce::String (spec.state));

        // The scan's own description is preferred (the plugin may have moved since the project was saved).
        juce::PluginDescription known;
        if (findPluginDescription (description.createIdentifierString(), known))
            description = known;
        juce::String error;
        auto plugin = pluginHost->create (description, engine.getSampleRate(), Player::kChunk, error);
        if (plugin == nullptr)
        {
            problems.add ("Plugin not found: " + description.name + " (" + description.pluginFormatName
                          + "). Its settings are kept; install it and open the project again.");
            return JucePlugin::placeholder (description, std::move (state), spec.instrument);
        }
        plugin->setState (state);
        return plugin;
    };

    for (const auto& spec : doc.pluginSpecs)
    {
        auto plugin = make (spec);
        if (plugin == nullptr)
            continue;
        const auto slotIndex = static_cast<size_t> (std::clamp (spec.slot, 0, kSlotsPerTrack - 1));
        switch (spec.place)
        {
            case PluginSpec::Place::Channel:
                if (spec.index >= 0 && spec.index < static_cast<int> (doc.channels.size()))
                    doc.channels[static_cast<size_t> (spec.index)].plugin = std::move (plugin);
                break;
            case PluginSpec::Place::Track:
                if (spec.index >= 0 && spec.index < static_cast<int> (doc.tracks.size()))
                    doc.tracks[static_cast<size_t> (spec.index)].slots[slotIndex].plugin = std::move (plugin);
                break;
            case PluginSpec::Place::Aux:
                if (spec.index >= 0 && spec.index < kAuxBuses)
                    doc.console.aux[static_cast<size_t> (spec.index)].slots[slotIndex].plugin = std::move (plugin);
                break;
            case PluginSpec::Place::Master:
                doc.masterSlots[slotIndex].plugin = std::move (plugin);
                break;
        }
    }

    // A plugin slot whose plugin could not be described at all is emptied rather than left silent and nameless.
    const auto tidy = [] (std::array<EffectSlot, kSlotsPerTrack>& slots)
    {
        for (auto& s : slots)
            if (s.type == EffectType::Plugin && s.plugin == nullptr)
                s = EffectSlot {};
    };
    for (auto& t : doc.tracks)
        tidy (t.slots);
    for (auto& a : doc.console.aux)
        tidy (a.slots);
    tidy (doc.masterSlots);
}

void MainComponent::beginPluginExport (double sampleRate)
{
    if (! projectHasPlugins())
        return;
    engine.setSuspended (true);
    pluginHost->prepareAll (sampleRate, Player::kChunk); // also clears their tails and held notes
}

void MainComponent::endPluginExport()
{
    if (! projectHasPlugins())
    {
        engine.setSuspended (false);
        return;
    }
    pluginHost->prepareAll (engine.getSampleRate(), Player::kChunk);
    engine.setSuspended (false);
}
} // namespace sls
