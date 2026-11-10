// Part of MainComponent: autosave, recovery after a crash, a backup of the previous version on save, and exporting a
// project together with its samples (0.15.0).
#include "ui/MainComponent.h"

namespace sls
{
namespace
{
constexpr const char* kRunningKey = "session.running";
constexpr const char* kAutosaveOriginalKey = "autosave.original";
constexpr juce::uint32 kAutosaveEveryMs = 60 * 1000;
} // namespace

juce::File MainComponent::autosaveFile() const
{
    return library.root.getChildFile ("Autosave").getChildFile (juce::String ("Autosave") + kProjectExtension);
}

// Called by the timer: once a minute, if there are unsaved changes, the project is written to the Autosave folder.
// The file is deleted when the program closes normally, so finding it at start-up means the program did not.
void MainComponent::autosaveIfNeeded()
{
    const auto now = juce::Time::getMillisecondCounter();
    if (! projectDirty || recording || now - lastAutosaveMs < kAutosaveEveryMs)
        return;
    lastAutosaveMs = now;
    const auto error = writeProjectFile (autosaveFile(), makeDocument());
    if (error.isEmpty() && settings != nullptr)
    {
        settings->setValue (kAutosaveOriginalKey, currentProjectFile.getFullPathName());
        settings->saveIfNeeded();
    }
}

void MainComponent::startSession()
{
    const bool crashed = settings != nullptr && settings->getBoolValue (kRunningKey, false);
    const auto autosave = autosaveFile();
    if (settings != nullptr)
    {
        settings->setValue (kRunningKey, true);
        settings->saveIfNeeded();
    }
    const bool offerRecovery = crashed && autosave.existsAsFile();
    const auto original = settings != nullptr ? settings->getValue (kAutosaveOriginalKey) : juce::String();
    const auto when = autosave.getLastModificationTime().toString (true, true, false, true);
    auto recover = [safe = juce::Component::SafePointer<MainComponent> (this), autosave, original, when, offerRecovery]
    {
        if (safe == nullptr || ! offerRecovery)
            return;
        juce::AlertWindow::showOkCancelBox (
            juce::MessageBoxIconType::QuestionIcon, "Recover your work?",
            "Sun Loops Studio did not close normally last time. A copy of the project"
                + (original.isNotEmpty() ? " \"" + juce::File (original).getFileNameWithoutExtension() + "\"" : juce::String())
                + " was saved automatically at " + when + ".\n\nOpen it?",
            "Open the recovered project", "Not now", safe.getComponent(),
            juce::ModalCallbackFunction::create ([safe, autosave, original] (int result)
            {
                if (safe == nullptr || result == 0)
                    return; // the file stays until the next autosave or a normal exit, so it can still be opened by hand
                auto parsed = readProjectFile (autosave);
                if (! parsed.document.has_value())
                {
                    safe->statusLabel.setText ("The recovered project could not be read: " + juce::String (parsed.error), juce::dontSendNotification);
                    return;
                }
                juce::StringArray problems;
                safe->applyDocument (std::move (*parsed.document), original.isNotEmpty() ? juce::File (original) : juce::File(), problems);
                safe->projectDirty = true; // not saved yet: Ctrl+S writes it where it belongs
                safe->updateWindowTitle();
                safe->statusLabel.setText ("Recovered the autosaved project: save it to keep it", juce::dontSendNotification);
            }));
    };
    // 0.43.0: a crash report from the last session is offered first, then the autosaved project.
    juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<MainComponent> (this), recover]
    {
        if (safe != nullptr && ! safe->offerCrashReport (recover))
            recover();
    });
}

void MainComponent::endSession()
{
    autosaveFile().deleteFile();
    if (settings != nullptr)
    {
        settings->setValue (kRunningKey, false);
        settings->saveIfNeeded();
    }
}

void MainComponent::exportProjectFolder()
{
    library.ensureExists();
    const auto suggested = library.projects.getChildFile (currentProjectFile != juce::File() ? currentProjectFile.getFileNameWithoutExtension() + " (with samples)"
                                                                                               : juce::String ("My Project (with samples)"));
    fileChooser = std::make_unique<juce::FileChooser> ("Export project with samples: name of the new folder", suggested, juce::String());
    fileChooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
                              [safe = juce::Component::SafePointer<MainComponent> (this)] (const juce::FileChooser& chooser)
    {
        if (safe == nullptr || chooser.getResult() == juce::File())
            return;
        const auto folder = chooser.getResult();
        juce::StringArray notes;
        const auto error = exportProjectWithSamples (safe->makeDocument(), folder, folder.getFileName(), notes);
        if (error.isNotEmpty())
        {
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Could not export the project", error);
            return;
        }
        safe->statusLabel.setText ("Exported the project and its samples to " + folder.getFullPathName(), juce::dontSendNotification);
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Project exported",
                                                "The folder " + folder.getFullPathName() + " holds the project and a copy of every sound it uses."
                                                    + " It can be moved to another computer as it is."
                                                    + (notes.isEmpty() ? juce::String() : "\n\n" + notes.joinIntoString ("\n")));
    });
}
} // namespace sls
