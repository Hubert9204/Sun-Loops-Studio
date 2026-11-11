// The HexForge plugin's entry point (VST3 and Standalone): the processor with its hardware-style editor.
#include "hexforge/HexEditor.h"

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    hexforge::HexProcessor::editorFactory() = [] (hexforge::HexProcessor& p) -> juce::AudioProcessorEditor* { return new hexforge::HexEditor (p); };
    return new hexforge::HexProcessor();
}
