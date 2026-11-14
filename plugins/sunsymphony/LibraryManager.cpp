#include "sunsymphony/LibraryManager.h"

#include <thread>

#include <juce_cryptography/juce_cryptography.h>

#include "SunSymphonyData.h"
#include "support/Http.h"

namespace sunsymphony
{
namespace
{
constexpr const char* kMarker = ".sunsymphony-installed";
constexpr int kParallel = 6; // files downloaded at once (they are small: the time goes in starting each one)

class Worker : public juce::Thread
{
public:
    explicit Worker (std::function<void()> f) : juce::Thread ("SunSymphony library"), fn (std::move (f)) {}
    void run() override { fn(); }

private:
    std::function<void()> fn;
};

// The repository path, each part percent-encoded ("Cello Section/susvib_A#2.wav" -> "Cello%20Section/susvib_A%232.wav").
juce::String encodePath (const juce::String& path)
{
    juce::StringArray parts;
    for (const auto& p : juce::StringArray::fromTokens (path, "/", ""))
        parts.add (juce::String (sls::http::urlEncode (p.toStdString())));
    return parts.joinIntoString ("/");
}
} // namespace

LibraryManager::LibraryManager()
{
    loadCatalog (juce::String::fromUTF8 (SunSymphonyData::catalog_json, SunSymphonyData::catalog_jsonSize));
}

LibraryManager::~LibraryManager()
{
    cancel();
    if (worker != nullptr)
        worker->stopThread (20000);
}

bool LibraryManager::loadCatalog (const juce::String& json)
{
    catalog.clear();
    const auto root = juce::JSON::parse (json);
    const auto* sources = root.getProperty ("sources", {}).getDynamicObject();
    if (sources == nullptr)
        return false;
    juce::StringArray credits;
    for (const auto& s : sources->getProperties())
        credits.add (s.value.getProperty ("name", "").toString() + " - " + s.value.getProperty ("license", "").toString());
    credit = credits.joinIntoString ("; ");
    if (const auto* list = root.getProperty ("instruments", {}).getArray())
        for (const auto& v : *list)
        {
            Item item;
            item.id = v.getProperty ("id", "").toString();
            item.name = v.getProperty ("name", "").toString();
            item.family = v.getProperty ("family", "").toString();
            item.essential = static_cast<bool> (v.getProperty ("essential", false));
            if (const auto* arts = v.getProperty ("articulations", {}).getArray())
                for (const auto& a : *arts)
                    item.articulations.push_back ({ a.getProperty ("name", "").toString(), a.getProperty ("file", "").toString(),
                                                    a.getProperty ("sfz", "").toString() });
            if (const auto* files = v.getProperty ("files", {}).getArray())
                for (const auto& f : *files)
                {
                    SampleFile file;
                    const auto source = f.getProperty ("source", "").toString();
                    file.url = sources->getProperty (source).getProperty ("base", "").toString() + encodePath (f.getProperty ("path", "").toString());
                    file.dest = f.getProperty ("dest", "").toString();
                    file.sha256 = f.getProperty ("sha256", "").toString();
                    file.size = static_cast<juce::int64> (f.getProperty ("size", 0));
                    // Never outside the instrument's own folder, whatever the catalog says; HTTPS only (plain HTTP: the tests' local server).
                    if (file.dest.isEmpty() || file.dest.contains ("..") || file.dest.startsWithChar ('/') || ! (file.url.startsWith ("https://") || file.url.startsWith ("http://127.0.0.1:")))
                        continue;
                    item.bytes += file.size;
                    item.files.push_back (std::move (file));
                }
            if (item.id.isNotEmpty() && ! item.articulations.empty())
                catalog.push_back (std::move (item));
        }
    return ! catalog.empty();
}

const LibraryManager::Item* LibraryManager::find (const juce::String& id) const
{
    for (const auto& i : catalog)
        if (i.id == id)
            return &i;
    return nullptr;
}

juce::File LibraryManager::folder() const
{
    if (customFolder != juce::File())
        return customFolder;
    return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Sun Loops Studio").getChildFile ("Orchestral");
}

juce::File LibraryManager::instrumentFolder (const Item& item) const
{
    return folder().getChildFile (juce::File::createLegalFileName (item.family)).getChildFile (juce::File::createLegalFileName (item.name));
}

juce::File LibraryManager::sfzFile (const Item& item, const Articulation& a) const
{
    return instrumentFolder (item).getChildFile (juce::File::createLegalFileName (a.file));
}

bool LibraryManager::isInstalled (const Item& item) const
{
    return instrumentFolder (item).getChildFile (kMarker).existsAsFile();
}

LibraryManager::Progress LibraryManager::progress() const
{
    std::lock_guard<std::mutex> g (lock);
    auto p = state;
    p.done = bytesDone.load();
    p.total = bytesTotal.load();
    return p;
}

bool LibraryManager::download (const juce::StringArray& ids)
{
    if (worker != nullptr && worker->isThreadRunning())
        return false;
    std::vector<Item> todo;
    juce::int64 total = 0;
    for (const auto& item : catalog)
        if (ids.contains (item.id) && ! isInstalled (item))
        {
            todo.push_back (item);
            total += item.bytes;
        }
    if (todo.empty())
        return false;
    worker.reset(); // (the previous one has finished)
    stop = false;
    bytesDone = 0;
    bytesTotal = total;
    {
        std::lock_guard<std::mutex> g (lock);
        state.busy = true;
        state.message = {};
        state.current = todo.front().name;
    }
    worker = std::make_unique<Worker> ([this, todo] { run (todo); });
    worker->startThread (juce::Thread::Priority::low);
    return true;
}

void LibraryManager::cancel()
{
    stop = true;
}

bool LibraryManager::remove (const juce::String& id)
{
    const auto* item = find (id);
    if (item == nullptr || progress().busy)
        return false;
    const auto dir = instrumentFolder (*item);
    const bool ok = ! dir.exists() || dir.deleteRecursively();
    std::lock_guard<std::mutex> g (lock);
    ++state.generation;
    state.message = ok ? item->name + " removed." : "Could not remove " + dir.getFullPathName();
    return ok;
}

bool LibraryManager::fetchFile (const SampleFile& file, const juce::File& dest, juce::String& error)
{
    if (dest.existsAsFile() && dest.getSize() == file.size)
        return true; // kept only after its SHA-256 matched
    const auto unverified = dest.getSiblingFile (dest.getFileName() + ".unverified");
    for (int attempt = 0; attempt < 3 && ! stop; ++attempt)
    {
        if (! unverified.existsAsFile())
        {
            std::uint64_t last = 0;
            sls::http::Download request;
            request.url = file.url.toStdString();
            request.destination = unverified.getFullPathName().toStdString();
            request.cancel = &stop;
            request.progress = [this, &last] (std::uint64_t done, std::uint64_t)
            {
                bytesDone += static_cast<juce::int64> (done) - static_cast<juce::int64> (last);
                last = done;
            };
            const auto result = sls::http::download (request);
            bytesDone -= static_cast<juce::int64> (last); // counted again below, once, when the file is kept
            if (result.cancelled || stop)
                return false;
            if (! result.ok())
            {
                error = result.offline ? juce::String ("No connection to GitHub (raw.githubusercontent.com). Check the network and try again.")
                                       : juce::String (result.error) + " (" + file.url + ")";
                continue;
            }
        }
        const bool sizeOk = unverified.getSize() == file.size;
        if (sizeOk && juce::SHA256 (unverified).toHexString() == file.sha256 && unverified.moveFileTo (dest))
        {
            bytesDone += file.size;
            return true;
        }
        unverified.deleteFile();
        error = "A file arrived damaged and was downloaded again: " + dest.getFileName();
    }
    if (error.isEmpty())
        error = "Could not download " + dest.getFileName();
    return false;
}

void LibraryManager::finishInstrument (const Item& item)
{
    const auto dir = instrumentFolder (item);
    for (const auto& a : item.articulations)
        sfzFile (item, a).replaceWithText (a.sfz);
    dir.getChildFile ("CREDITS.txt").replaceWithText (item.name + " for SunSymphony (Sun Loops Studio).\n\nSamples: " + credit
                                                      + "\nCC0 1.0: https://creativecommons.org/publicdomain/zero/1.0/\n"
                                                        "Mapping (the SFZ files): Sun Loops Studio, tools/orchestra/catalog.py.\n");
    dir.getChildFile (kMarker).replaceWithText (juce::String (item.files.size()) + " files\n");
}

void LibraryManager::run (std::vector<Item> todo)
{
    int installed = 0;
    juce::String failure;
    // What is already on disk (an earlier, interrupted download) counts as done.
    for (const auto& item : todo)
        for (const auto& f : item.files)
        {
            const auto dest = instrumentFolder (item).getChildFile (f.dest);
            if (dest.existsAsFile() && dest.getSize() == f.size)
                bytesDone += f.size;
        }
    for (const auto& item : todo)
    {
        if (stop)
            break;
        {
            std::lock_guard<std::mutex> g (lock);
            state.current = item.name;
        }
        const auto dir = instrumentFolder (item);
        std::atomic<size_t> next { 0 };
        std::atomic<int> failed { 0 };
        std::mutex errorLock;
        juce::String error;
        auto fetchSome = [&]
        {
            for (size_t i = next++; i < item.files.size() && ! stop; i = next++)
            {
                const auto& f = item.files[i];
                const auto dest = dir.getChildFile (f.dest);
                if (dest.existsAsFile() && dest.getSize() == f.size)
                    continue;
                dest.getParentDirectory().createDirectory();
                juce::String why;
                if (! fetchFile (f, dest, why))
                {
                    ++failed;
                    std::lock_guard<std::mutex> g (errorLock);
                    if (error.isEmpty())
                        error = why;
                }
            }
        };
        std::vector<std::thread> threads;
        for (int t = 0; t < kParallel; ++t)
            threads.emplace_back (fetchSome);
        for (auto& t : threads)
            t.join();
        if (stop)
            break;
        if (failed > 0)
        {
            failure = item.name + ": " + error;
            continue; // the next instrument may still work; this one stays "not installed" (Download again continues it)
        }
        finishInstrument (item);
        ++installed;
        {
            std::lock_guard<std::mutex> g (lock);
            ++state.generation;
        }
    }
    std::lock_guard<std::mutex> g (lock);
    state.busy = false;
    state.current = {};
    if (stop)
        state.message = "Download stopped. Download again to continue where it stopped.";
    else if (failure.isNotEmpty())
        state.message = failure;
    else
        state.message = installed == 1 ? juce::String ("1 instrument installed.") : juce::String (installed) + " instruments installed.";
}
} // namespace sunsymphony
