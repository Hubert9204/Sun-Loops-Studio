#pragma once

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

namespace sls
{
// Asks for one line of text in a small modal window. `onAccepted` gets the text as typed (not trimmed) when the person
// presses the OK button or Return; nothing happens on Cancel or Escape. The window deletes itself when it closes.
// `owner` guards the callback: it is not called if that component has been deleted in the meantime.
inline void promptForText (juce::Component& owner,
                           const juce::String& title,
                           const juce::String& label,
                           const juce::String& initialText,
                           const juce::String& okButtonText,
                           std::function<void (const juce::String&)> onAccepted)
{
    auto* window = new juce::AlertWindow (title, label, juce::MessageBoxIconType::NoIcon);
    window->addTextEditor ("text", initialText);
    window->addButton (okButtonText, 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    window->enterModalState (true,
                             juce::ModalCallbackFunction::create (
                                 [safe = juce::Component::SafePointer<juce::Component> (&owner), window,
                                  accepted = std::move (onAccepted)] (int result)
                                 {
                                     if (safe != nullptr && result == 1 && accepted)
                                         accepted (window->getTextEditorContents ("text"));
                                 }),
                             true);
}
} // namespace sls
