#include "synthkit/SynthProcessor.h"

namespace synthkit
{
SynthProcessor::SynthProcessor (const Identity& id, juce::AudioProcessorValueTreeState::ParameterLayout layout,
                                const std::vector<FactoryPreset>& factoryPresets)
    : juce::AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      state (*this, nullptr, id.name, std::move (layout)),
      identity (id),
      factory (factoryPresets)
{
}

bool SynthProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

int SynthProcessor::getNumPrograms()
{
    return static_cast<int> (factory.size());
}

const juce::String SynthProcessor::getProgramName (int index)
{
    return index >= 0 && index < static_cast<int> (factory.size()) ? factory[static_cast<size_t> (index)].name : juce::String();
}

juce::File SynthProcessor::userPresetFolder() const
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile (identity.name).getChildFile ("Presets");
}

std::vector<PresetEntry> SynthProcessor::presets() const
{
    std::vector<PresetEntry> out;
    for (size_t i = 0; i < factory.size(); ++i)
        out.push_back ({ factory[i].name, factory[i].category, {}, static_cast<int> (i) });
    auto files = userPresetFolder().findChildFiles (juce::File::findFiles, true, juce::String ("*") + identity.presetExtension);
    files.sort();
    for (const auto& f : files)
    {
        juce::String category = "User";
        if (auto xml = juce::parseXML (f))
            category = xml->getStringAttribute ("category", "User");
        out.push_back ({ f.getFileNameWithoutExtension(), category, f, -1 });
    }
    return out;
}

void SynthProcessor::loadPreset (const PresetEntry& preset)
{
    if (preset.factoryIndex >= 0)
        loadFactoryPreset (preset.factoryIndex);
    else
        loadPresetFile (preset.file);
}

void SynthProcessor::applyValues (const std::vector<std::pair<juce::String, float>>& values)
{
    resetExtras();
    for (auto* p : getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p))
            ranged->setValueNotifyingHost (ranged->getDefaultValue());
    for (const auto& [id, value] : values)
    {
        if (applyExtraValue (id, value))
            continue;
        if (auto* p = state.getParameter (id))
            p->setValueNotifyingHost (p->convertTo0to1 (value));
        else
            jassertfalse; // a preset names a parameter that does not exist
    }
}

void SynthProcessor::presetLoaded (const juce::String& name, const juce::String& category)
{
    presetName = name;
    presetCategory = category;
    ++generation;
}

void SynthProcessor::loadFactoryPreset (int index)
{
    if (index < 0 || index >= static_cast<int> (factory.size()))
        return;
    const auto& p = factory[static_cast<size_t> (index)];
    applyValues (p.values);
    currentProgram = index;
    presetLoaded (p.name, p.category);
}

void SynthProcessor::initPatch()
{
    applyValues ({});
    presetLoaded ("Init", "Init");
}

bool SynthProcessor::loadPresetFile (const juce::File& file)
{
    auto xml = juce::parseXML (file);
    if (xml == nullptr || ! xml->hasTagName (identity.presetTag))
        return false;
    auto* params = xml->getChildByName (state.state.getType());
    if (params == nullptr)
        return false;
    state.replaceState (juce::ValueTree::fromXml (*params));
    stateRestored();
    presetLoaded (file.getFileNameWithoutExtension(), xml->getStringAttribute ("category", "User"));
    return true;
}

juce::File SynthProcessor::savePreset (const juce::String& name, const juce::String& category)
{
    const auto clean = juce::File::createLegalFileName (name.trim());
    if (clean.isEmpty())
        return {};
    auto folder = userPresetFolder();
    folder.createDirectory();
    auto file = folder.getChildFile (clean + identity.presetExtension);
    juce::XmlElement root (identity.presetTag);
    root.setAttribute ("category", category.isEmpty() ? juce::String ("User") : category);
    if (auto params = state.copyState().createXml())
        root.addChildElement (params.release());
    if (! root.writeTo (file))
        return {};
    presetLoaded (clean, category);
    return file;
}

void SynthProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto copy = state.copyState();
    copy.setProperty ("presetName", presetName, nullptr);
    copy.setProperty ("presetCategory", presetCategory, nullptr);
    if (auto xml = copy.createXml())
        copyXmlToBinary (*xml, destData);
}

void SynthProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (state.state.getType()))
        {
            auto tree = juce::ValueTree::fromXml (*xml);
            state.replaceState (tree);
            stateRestored();
            presetLoaded (tree.getProperty ("presetName", "Init").toString(), tree.getProperty ("presetCategory", "Init").toString());
        }
}
} // namespace synthkit
