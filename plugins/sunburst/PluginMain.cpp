// The SunBurst plugin's entry point (VST3 and Standalone): the processor with its hardware-style editor.
#include "sunburst/SunBurstEditor.h"

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    sunburst::SunBurstProcessor::editorFactory() = [] (sunburst::SunBurstProcessor& p) -> juce::AudioProcessorEditor* { return new sunburst::SunBurstEditor (p); };
    return new sunburst::SunBurstProcessor();
}
