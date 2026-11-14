// The SunRiff plugin's entry point (VST3 and Standalone): the electric guitar with its amp-style window.
#include "sunriff/RiffEditor.h"

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    sunriff::RiffProcessor::editorFactory() = [] (sunriff::RiffProcessor& p) -> juce::AudioProcessorEditor* { return new sunriff::RiffEditor (p); };
    return new sunriff::RiffProcessor();
}
