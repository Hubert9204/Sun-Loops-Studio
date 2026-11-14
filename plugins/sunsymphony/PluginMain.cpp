// The SunSymphony plugin's entry point (VST3 and Standalone): the orchestral sampler with its editor.
#include "sunsymphony/SunSymphonyEditor.h"

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    sunsymphony::SunSymphonyProcessor::editorFactory() = [] (sunsymphony::SunSymphonyProcessor& p) -> juce::AudioProcessorEditor* { return new sunsymphony::SunSymphonyEditor (p); };
    return new sunsymphony::SunSymphonyProcessor();
}
