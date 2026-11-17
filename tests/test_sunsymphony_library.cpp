// SunSymphony's Library manager (0.67.0): the built-in catalog is consistent, and an instrument installs only with intact files.
#include "TestSockets.h" // POSIX or Winsock (0.75.0)

#include <atomic>
#include <cmath>
#include <iostream>
#include <set>
#include <thread>

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_cryptography/juce_cryptography.h>
#include <juce_events/juce_events.h>

#include "orchestra/Instrument.h"
#include "sunsymphony/LibraryManager.h"

void supportCheck (bool ok, const char* name); // tests/test_core.cpp

namespace
{
using sunsymphony::LibraryManager;

void testBuiltInCatalog()
{
    LibraryManager lib;
    const auto& items = lib.items();
    std::set<juce::String> families;
    int essentials = 0;
    bool filesOk = true, sfzOk = true;
    juce::int64 total = 0;
    for (const auto& item : items)
    {
        families.insert (item.family);
        essentials += item.essential ? 1 : 0;
        total += item.bytes;
        std::set<std::string> dests;
        for (const auto& f : item.files)
        {
            filesOk = filesOk && f.size > 0 && f.sha256.length() == 64 && f.url.startsWith ("https://raw.githubusercontent.com/sgossner/")
                      && ! f.url.contains (" ") && dests.insert (f.dest.toStdString()).second;
        }
        // Every articulation's SFZ plays, and every sample it names is one the instrument downloads.
        for (const auto& a : item.articulations)
        {
            int unknown = 0;
            std::string error;
            auto inst = orchestra::Instrument::fromSfz (a.sfz.toStdString(), "t", "t", [&] (const std::string& path) -> std::shared_ptr<const orchestra::SampleBuffer>
            {
                if (dests.count (path) == 0)
                {
                    ++unknown;
                    return nullptr;
                }
                auto b = std::make_shared<orchestra::SampleBuffer>();
                b->left.assign (64, 0.1f);
                return b;
            }, error);
            if (inst == nullptr || unknown > 0)
            {
                std::cout << "  " << item.name << " / " << a.name << ": " << error << " (" << unknown << " unknown samples)\n";
                sfzOk = false;
            }
        }
    }
    supportCheck (items.size() >= 25 && families.size() == 5 && essentials >= 10, "SunSymphony library: the built-in catalog lists the five families");
    supportCheck (filesOk && total > 1000000000LL, "SunSymphony library: every file has its size, SHA-256 and a fixed GitHub address");
    supportCheck (sfzOk, "SunSymphony library: every articulation's SFZ maps only samples the instrument downloads");
}

// A static file server on 127.0.0.1 (one connection after the other): what GitHub is to the plugin, without the network.
class FolderServer
{
public:
    explicit FolderServer (juce::File root) : folder (std::move (root))
    {
        testsockets::start();
        listener = ::socket (AF_INET, SOCK_STREAM, 0);
        testsockets::reuseAddress (listener);
        sockaddr_in address {};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl (INADDR_LOOPBACK);
        ::bind (listener, reinterpret_cast<sockaddr*> (&address), sizeof (address));
        ::listen (listener, 16);
        testsockets::Length length = sizeof (address);
        ::getsockname (listener, reinterpret_cast<sockaddr*> (&address), &length);
        port = ntohs (address.sin_port);
        worker = std::thread ([this] { serve(); });
    }
    ~FolderServer()
    {
        stop = true;
        worker.join();
        testsockets::close (listener);
    }
    juce::String base() const { return "http://127.0.0.1:" + juce::String (port) + "/"; }
    int requests() const { return served.load(); }

private:
    void serve()
    {
        while (! stop)
        {
            testsockets::Poll p { listener, POLLIN, 0 };
            if (testsockets::poll (&p, 1, 50) <= 0)
                continue;
            const auto client = ::accept (listener, nullptr, nullptr);
            if (client == testsockets::kNone)
                continue;
            std::string request;
            char buffer[4096];
            while (request.find ("\r\n\r\n") == std::string::npos)
            {
                const auto n = testsockets::receive (client, buffer, sizeof (buffer));
                if (n <= 0)
                    break;
                request.append (buffer, static_cast<size_t> (n));
            }
            const auto first = juce::String (request).upToFirstOccurrenceOf ("\r\n", false, false);
            const auto path = juce::URL::removeEscapeChars (first.fromFirstOccurrenceOf (" /", false, false).upToFirstOccurrenceOf (" ", false, false));
            juce::MemoryBlock body;
            const bool found = folder.getChildFile (path).loadFileAsData (body);
            const auto header = juce::String (found ? "HTTP/1.1 200 OK" : "HTTP/1.1 404 Not Found") + "\r\nContent-Length: "
                                + juce::String (found ? static_cast<int> (body.getSize()) : 0) + "\r\nConnection: close\r\n\r\n";
            testsockets::sendAll (client, header.toRawUTF8(), header.getNumBytesAsUTF8());
            if (found)
                testsockets::sendAll (client, body.getData(), body.getSize());
            testsockets::close (client);
            ++served;
        }
    }
    juce::File folder;
    testsockets::Handle listener = testsockets::kNone;
    int port = 0;
    std::atomic<bool> stop { false };
    std::atomic<int> served { 0 };
    std::thread worker;
};

juce::String catalogFor (const juce::File& server, const juce::String& base, const juce::String& sha)
{
    return R"({"format":1,"sources":{"test":{"name":"Test source","license":"CC0 1.0","base":")" + base + R"("}},"instruments":[{"id":"cello","name":"Cello Test","family":"Strings","essential":true,)"
             R"("articulations":[{"name":"sustain","file":"sustain.sfz","sfz":"<region> sample=samples/test/C 3.wav key=48\n"}],)"
             R"("files":[{"source":"test","path":"dir/C 3.wav","dest":"samples/test/C 3.wav","size":)"
           + juce::String (server.getChildFile ("dir/C 3.wav").getSize()) + R"(,"sha256":")" + sha + R"("}]}]})";
}

void testInstall()
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    const auto root = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("sls-library-test");
    root.deleteRecursively();
    const auto server = root.getChildFile ("server");
    const auto wav = server.getChildFile ("dir/C 3.wav");
    wav.getParentDirectory().createDirectory();
    {
        juce::WavAudioFormat format;
        juce::AudioBuffer<float> audio (1, 4800);
        for (int i = 0; i < 4800; ++i)
            audio.setSample (0, i, 0.5f * std::sin (6.2831853f * 130.81f * static_cast<float> (i) / 48000.0f));
        std::unique_ptr<juce::AudioFormatWriter> writer (format.createWriterFor (new juce::FileOutputStream (wav), 48000.0, 1, 16, {}, 0));
        writer->writeFromAudioSampleBuffer (audio, 0, 4800);
    }
    const auto sha = juce::SHA256 (wav).toHexString();
    FolderServer http (server);

    auto waitFor = [] (LibraryManager& lib)
    {
        for (int i = 0; i < 400 && lib.progress().busy; ++i)
            juce::Thread::sleep (25);
        return lib.progress();
    };

    {
        LibraryManager lib;
        lib.setFolder (root.getChildFile ("library"));
        supportCheck (lib.loadCatalog (catalogFor (server, http.base(), juce::String::repeatedString ("0", 64))), "SunSymphony library: a catalog is read");
        const auto& item = lib.items().front();
        const bool started = lib.download ({ "cello" });
        const auto p = waitFor (lib);
        supportCheck (started && ! p.busy && ! lib.isInstalled (item) && p.message.contains ("damaged")
                          && ! lib.instrumentFolder (item).getChildFile ("samples/test/C 3.wav").exists(),
                      "SunSymphony library: a file whose SHA-256 does not match is not kept, and the instrument is not installed");
    }
    {
        LibraryManager lib;
        lib.setFolder (root.getChildFile ("library"));
        lib.loadCatalog (catalogFor (server, http.base(), sha));
        const auto& item = lib.items().front();
        const auto before = lib.progress().generation;
        const bool started = lib.download ({ "cello" });
        const auto p = waitFor (lib);
        const auto sfz = lib.sfzFile (item, item.articulations.front());
        if (! lib.isInstalled (item))
            std::cout << "  (" << p.message << ")\n";
        supportCheck (started && lib.isInstalled (item) && p.generation == before + 1 && p.done == p.total && sfz.existsAsFile()
                          && lib.instrumentFolder (item).getChildFile ("CREDITS.txt").loadFileAsString().contains ("CC0"),
                      "SunSymphony library: a verified download installs the instrument: its samples, SFZ files and credits");
        supportCheck (! lib.download ({ "cello" }), "SunSymphony library: an installed instrument is not downloaded again");
        supportCheck (lib.remove ("cello") && ! lib.isInstalled (item) && ! lib.instrumentFolder (item).exists(),
                      "SunSymphony library: Remove deletes the instrument's folder");
    }
    root.deleteRecursively();
}
} // namespace

void testSunSymphonyLibrary()
{
    std::cout << "SunSymphony library manager\n";
    testBuiltInCatalog();
    testInstall();
}
