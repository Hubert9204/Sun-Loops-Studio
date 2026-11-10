// Part of MainComponent: Helios (0.40.0), the offline help: the manual searched on this computer, and commands it can run.
#include "ui/MainComponent.h"

#include "HelpData.h"

namespace sls
{
const HelpIndex& MainComponent::helpIndex()
{
    if (help == nullptr)
    {
        help = std::make_unique<HelpIndex>();
        help->add (std::string (HelpData::userguide_md, static_cast<size_t> (HelpData::userguide_mdSize)), "en");
        help->add (std::string (HelpData::userguide_it_md, static_cast<size_t> (HelpData::userguide_it_mdSize)), "it");
    }
    return *help;
}

std::vector<HeliosCommand> MainComponent::heliosCommands()
{
    const auto safe = juce::Component::SafePointer<MainComponent> (this);
    const auto run = [safe] (std::function<void (MainComponent&)> f)
    {
        return [safe, f = std::move (f)]
        {
            if (safe != nullptr)
                f (*safe);
        };
    };
    return {
        { "Open or close the Mixer (F9)", "mixer insert track effects mischiatore tracce effetti", run ([] (MainComponent& m) { m.togglePanel (m.mixerPanelWindow); }) },
        { "Open or close the Playlist (F5)", "playlist song arrangement brano arrangiamento", run ([] (MainComponent& m) { m.togglePanel (m.playlistPanel); }) },
        { "Open or close the Channel Rack (F6)", "channel rack channels step sequencer canali", run ([] (MainComponent& m) { m.togglePanel (m.rackPanel); }) },
        { "Open or close the Piano Roll (F7)", "piano roll notes note melodia", run ([] (MainComponent& m) { m.togglePanel (m.pianoPanel); }) },
        { "Show or hide the Browser (F8)", "browser sounds samples suoni campioni", run ([] (MainComponent& m) { m.setBrowserOpen (! m.browserOpen); }) },
        { "Play or stop (Space)", "play stop start suona ferma avvia", run ([] (MainComponent& m) { m.setPlaying (! m.engine.isPlaying()); }) },
        { "Record audio", "record recording microphone registra registrazione microfono", run ([] (MainComponent& m) { m.toggleRecording(); }) },
        { "Audio and MIDI devices", "audio midi device sound card settings scheda audio impostazioni dispositivo latency latenza", run ([] (MainComponent& m) { m.openAudioSettings(); }) },
        { "Meters (loudness, spectrum)", "meters lufs loudness spectrum analyser misuratori volume spettro", run ([] (MainComponent& m) { m.toggleMeters(); }) },
        { "Metronome on or off", "metronome click metronomo", run ([] (MainComponent& m) { m.metronomeOn = ! m.metronomeOn; m.applyMetronome(); }) },
        { "Loop Starter: make a loop", "loop starter genre idea beat inizia genere", run ([] (MainComponent& m) { m.openLoopStarter(); }) },
        { "Slice a loop (Slicer)", "slicer slice slicex loop taglia fette", run ([] (MainComponent& m) { m.chooseLoopForSlicer(); }) },
        { "New project", "new project nuovo progetto", run ([] (MainComponent& m) { m.newProject(); }) },
        { "Open a project", "open project load apri progetto carica", run ([] (MainComponent& m) { m.openProject(); }) },
        { "Save the project", "save project salva progetto", run ([] (MainComponent& m) { m.saveProject(); }) },
        { "Open an FL Studio project (.flp)", "flp fl studio import open apri importa", run ([] (MainComponent& m) { m.openFlpProject(); }) },
        { "Export as FL Studio project (.flp)", "flp fl studio export esporta", run ([] (MainComponent& m) { m.exportFlpProject(); }) },
        { "Export audio (the File menu)", "export audio wav mp3 flac ogg render esporta", run ([] (MainComponent& m) { m.showFileMenu(); }) },
        { "Import audio, video or MIDI", "import audio video midi file importa", run ([] (MainComponent& m) { m.chooseMediaToImport(); }) },
        { "Undo (Ctrl+Z)", "undo annulla", run ([] (MainComponent& m) { m.undoEdit(); }) },
        { "Redo (Ctrl+Y)", "redo ripeti", run ([] (MainComponent& m) { m.redoEdit(); }) },
        { "Restore the panel layout", "reset layout panels windows ripristina pannelli finestre", run ([] (MainComponent& m) { m.resetLayout(); }) },
    };
}

void MainComponent::openHelios (const juce::String& question)
{
    if (heliosWindow == nullptr)
    {
        heliosWindow = std::make_unique<HeliosWindow>();
        heliosWindow->onClose = [safe = juce::Component::SafePointer<MainComponent> (this)]
        {
            juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe->heliosWindow.reset(); });
        };
        heliosWindow->panel().setIndex (&helpIndex());
        heliosWindow->panel().setCommands (heliosCommands());
    }
    heliosWindow->setVisible (true);
    heliosWindow->toFront (true);
    heliosWindow->panel().ask (question);
    // The window takes the keyboard only once the window manager has shown it.
    juce::Timer::callAfterDelay (150, [safe = juce::Component::SafePointer<HeliosWindow> (heliosWindow.get())]
    {
        if (safe != nullptr)
            safe->panel().focusBox();
    });
}
} // namespace sls
