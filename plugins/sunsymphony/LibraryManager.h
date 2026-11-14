#pragma once

#include <atomic>
#include <mutex>

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

// SunSymphony's Library manager (0.67.0): downloads the orchestral instruments from inside the plugin. The catalog is built into
// the plugin (resources/orchestra/catalog.json, made by tools/orchestra/catalog.py): for each instrument the sample files of the
// free CC0 sources (VSCO 2 Community Edition and VCSL, by Versilian Studios) with their size and SHA-256, and the SFZ files that
// map them. The files come straight from GitHub at a fixed commit; each is checked against its SHA-256 before it is kept, an
// interrupted download continues where it stopped, and an instrument counts as installed only when every file is in place.
//
// One manager per process (shared by every SunSymphony in a host), so two instances never download the same file at once.
// Downloads run on their own threads, never on the audio thread; the editor polls progress().
namespace sunsymphony
{
class LibraryManager
{
public:
    struct Articulation
    {
        juce::String name, file, sfz;
    };
    struct SampleFile
    {
        juce::String url, dest, sha256;
        juce::int64 size = 0;
    };
    struct Item
    {
        juce::String id, name, family;
        bool essential = false;
        std::vector<Articulation> articulations;
        std::vector<SampleFile> files;
        juce::int64 bytes = 0;
    };
    struct Progress
    {
        bool busy = false;
        juce::int64 done = 0, total = 0;  // bytes of the instruments being downloaded
        juce::String current;             // the instrument being downloaded
        juce::String message;             // the last outcome ("3 instruments installed", or what went wrong)
        int generation = 0;               // changes whenever an instrument is installed or removed
    };

    LibraryManager();
    ~LibraryManager();

    // The catalog built into the plugin (empty if it could not be read).
    const std::vector<Item>& items() const noexcept { return catalog; }
    const Item* find (const juce::String& id) const;
    juce::String sourcesCredit() const { return credit; }

    juce::File folder() const;                 // ~/Music/Sun Loops Studio/Orchestral
    juce::File instrumentFolder (const Item&) const;
    bool isInstalled (const Item&) const;
    juce::File sfzFile (const Item&, const Articulation&) const;

    // Starts downloading these instruments (already installed ones are skipped). False when a download is already running.
    bool download (const juce::StringArray& ids);
    void cancel();
    // Deletes an installed instrument's folder (not while downloading). False when it could not be removed.
    bool remove (const juce::String& id);

    Progress progress() const;

    // For the tests: a catalog read from text, a folder of one's own.
    bool loadCatalog (const juce::String& json);
    void setFolder (const juce::File& f) { customFolder = f; }

private:
    void run (std::vector<Item> todo);
    bool fetchFile (const SampleFile& file, const juce::File& dest, juce::String& error);
    void finishInstrument (const Item& item);

    std::vector<Item> catalog;
    juce::String credit;
    juce::File customFolder;
    std::unique_ptr<juce::Thread> worker;
    std::atomic<bool> stop { false };
    std::atomic<juce::int64> bytesDone { 0 }, bytesTotal { 0 };
    mutable std::mutex lock;
    Progress state; // (under lock)
};

// The process-wide manager.
using SharedLibrary = juce::SharedResourcePointer<LibraryManager>;
} // namespace sunsymphony
