#include "assistant/HeliosAssistant.h"

#include <cstring>

#include <spdlog/spdlog.h>

#include "assistant/HeliosProtocol.h"
#include "library/HelpIndex.h"
#include "support/Http.h"

#ifndef SLS_HELIOS_LLM
 #define SLS_HELIOS_LLM 0
#endif

namespace sls
{
namespace
{
juce::var object (std::initializer_list<std::pair<const char*, juce::var>> properties)
{
    auto* o = new juce::DynamicObject();
    for (const auto& [name, value] : properties)
        o->setProperty (name, value);
    return juce::var (o);
}

juce::String fromUtf8 (const std::string& s)
{
    return juce::String::fromUTF8 (s.data(), static_cast<int> (s.size()));
}

juce::String gigabytes (std::uint64_t bytes)
{
    return juce::String (static_cast<double> (bytes) / 1.0e9, 1) + " GB";
}
} // namespace

// The helper process, seen from here. Its messages arrive on the pipe's thread and are passed to the message thread.
class HeliosAssistant::Helper final : public juce::ChildProcessCoordinator
{
public:
    Helper (HeliosAssistant& a, int number) : owner (&a), generation (number) {}
    ~Helper() override
    {
        // JUCE closes the pipe but does not end the process: the helper is told to quit (and quits by itself if the program is gone).
        send (object ({ { "type", "quit" } }));
        killWorkerProcess();
    }

    bool launch()
    {
        // No output streams: the helper's own messages go nowhere (its pipe would fill up if nobody read it).
        return launchWorkerProcess (HeliosAssistant::helperFile(), helios::kWorkerId, 20000, 0);
    }

    bool send (const juce::var& message)
    {
        const auto s = juce::JSON::toString (message, true);
        return sendMessageToWorker (juce::MemoryBlock (s.toRawUTF8(), s.getNumBytesAsUTF8()));
    }

    void handleMessageFromWorker (const juce::MemoryBlock& message) override
    {
        auto v = juce::JSON::parse (message.toString());
        juce::MessageManager::callAsync ([weak = owner, v, g = generation]
        {
            auto* a = weak.get();
            if (a == nullptr || a->helper == nullptr || a->helperGeneration != g)
                return; // a helper that was stopped since
            const auto type = v["type"].toString();
            if (type == "loaded")
                a->helperLoaded (static_cast<bool> (v["ok"]), v["error"].toString(), static_cast<bool> (v["gpu"]));
            else if (type == "token")
                a->helperToken (static_cast<int> (v["id"]), v["text"].toString());
            else if (type == "done")
                a->helperDone (static_cast<int> (v["id"]), v["reason"].toString(), v["error"].toString(), v);
        });
    }

    void handleConnectionLost() override
    {
        juce::MessageManager::callAsync ([weak = owner, g = generation]
        {
            if (auto* a = weak.get(); a != nullptr && a->helper != nullptr && a->helperGeneration == g)
                a->helperLost();
        });
    }

private:
    juce::WeakReference<HeliosAssistant> owner; // made on the message thread; copied (thread-safe) by the pipe's thread
    int generation;                             // which helper this is (a stopped one's late messages are ignored)
};

HeliosAssistant::HeliosAssistant (const HelpIndex& manual) : help (manual)
{
    refreshModelState();
}

HeliosAssistant::~HeliosAssistant()
{
    cancelDownloading.store (true);
    if (downloader.joinable())
        downloader.join();
    helper.reset();
    masterReference.clear();
}

juce::File HeliosAssistant::helperFile()
{
    return juce::File::getSpecialLocation (juce::File::currentExecutableFile)
        .getSiblingFile (juce::String ("sun-loops-helios") + (juce::SystemStats::getOperatingSystemType() & juce::SystemStats::Windows ? ".exe" : ""));
}

juce::File HeliosAssistant::modelFolder()
{
#if JUCE_MAC
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Application Support/Sun Loops Studio/Helios");
#elif JUCE_WINDOWS
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Sun Loops Studio/Helios");
#else
    // Big files: ~/.local/share (XDG_DATA_HOME), not ~/.config where the settings are.
    const auto xdg = juce::SystemStats::getEnvironmentVariable ("XDG_DATA_HOME", {});
    const auto base = xdg.isNotEmpty() && juce::File::isAbsolutePath (xdg) ? juce::File (xdg)
                                                                          : juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile (".local/share");
    return base.getChildFile ("sun-loops-studio/helios");
#endif
}

juce::File HeliosAssistant::modelFile() const
{
    const auto* m = helios::findModel (model.toStdString());
    return m != nullptr ? modelFolder().getChildFile (fromUtf8 (m->file)) : juce::File();
}

bool HeliosAssistant::modelPresent() const
{
    const auto f = modelFile();
    return f != juce::File() && f.existsAsFile() && f.getSize() > 0;
}

void HeliosAssistant::refreshModelState()
{
    if (! SLS_HELIOS_LLM || ! helperFile().existsAsFile())
        setState (State::unavailable);
    else if (currentState == State::downloading)
        return;
    else if (! modelPresent())
        setState (State::noModel);
    else if (helper == nullptr)
        setState (State::idle);
}

void HeliosAssistant::setState (State s, const juce::String& errorText)
{
    currentState = s;
    lastError = errorText;
    if (errorText.isNotEmpty())
        spdlog::warn ("Helios: {}", errorText.toStdString());
    sendChangeMessage();
}

juce::String HeliosAssistant::status() const
{
    const auto* m = helios::findModel (model.toStdString());
    const auto name = m != nullptr ? fromUtf8 (m->name) : juce::String ("?");
    switch (currentState)
    {
        case State::unavailable: return "LLM mode is not part of this build of Sun Loops Studio (the helper sun-loops-helios is missing).";
        case State::noModel:     return name + " is not on this computer yet: press Download (" + gigabytes (m != nullptr ? m->bytes : 0)
                                        + ", once). Nothing leaves your computer: the model runs here.";
        case State::downloading: return "Downloading " + name + ": " + juce::String (juce::roundToInt (progress * 100.0)) + "%  ("
                                        + gigabytes (downloadedBytes) + (downloadTotal > 0 ? " of " + gigabytes (downloadTotal) : juce::String()) + ")";
        case State::idle:        return name + " is ready to start: ask something.";
        case State::loading:     return "Starting " + name + "... (on an older computer this takes a minute)";
        case State::ready:       return "Ready - " + name + (onGpu ? ", on the graphics chip" : ", on the processor") + ", offline.";
        case State::answering:   return "Helios is writing... (Stop to interrupt)";
        case State::failed:      return "Problem: " + lastError;
    }
    return {};
}

void HeliosAssistant::chooseModel (const juce::String& id)
{
    if (id == model || helios::findModel (id.toStdString()) == nullptr || currentState == State::downloading)
        return;
    stop();
    model = id;
    refreshModelState();
}

void HeliosAssistant::download()
{
    const auto* m = helios::findModel (model.toStdString());
    if (m == nullptr || currentState == State::downloading || currentState == State::unavailable)
        return;
    const auto folder = modelFolder();
    if (! folder.createDirectory())
    {
        setState (State::failed, "cannot create the folder " + folder.getFullPathName());
        return;
    }
    const auto partFile = modelFile().getFullPathName() + ".part";
    const auto already = juce::File (partFile).existsAsFile() ? static_cast<std::uint64_t> (juce::File (partFile).getSize()) : 0;
    const auto needed = m->bytes > already ? m->bytes - already : 0;
    if (static_cast<std::uint64_t> (folder.getBytesFreeOnVolume()) < needed + 500'000'000ull)
    {
        setState (State::failed, "not enough free disk space: " + fromUtf8 (m->name) + " needs " + gigabytes (needed)
                                     + " more (and some room to spare); " + gigabytes (static_cast<std::uint64_t> (folder.getBytesFreeOnVolume())) + " are free.");
        return;
    }
    if (downloader.joinable())
        downloader.join();
    cancelDownloading.store (false);
    progress = 0.0;
    downloadedBytes = already;
    downloadTotal = m->bytes;
    setState (State::downloading);
    spdlog::info ("Helios: downloading {} from {}", m->file, m->url);

    http::Download request;
    request.url = m->url;
    request.destination = modelFile().getFullPathName().toStdString();
    request.cancel = &cancelDownloading;
    juce::WeakReference<HeliosAssistant> weak (this);
    request.progress = [weak] (std::uint64_t done, std::uint64_t total)
    {
        juce::MessageManager::callAsync ([weak, done, total]
        {
            if (auto* a = weak.get(); a != nullptr && a->currentState == State::downloading)
            {
                a->downloadedBytes = done;
                a->downloadTotal = total > 0 ? total : a->downloadTotal;
                a->progress = a->downloadTotal > 0 ? std::min (1.0, static_cast<double> (done) / static_cast<double> (a->downloadTotal)) : 0.0;
                a->sendChangeMessage();
            }
        });
    };
    downloader = std::thread ([request, weak]
    {
        const auto result = http::download (request);
        juce::MessageManager::callAsync ([weak, result, file = juce::File (juce::String (request.destination.string()))]
        {
            auto* a = weak.get();
            if (a == nullptr)
                return;
            a->currentState = State::noModel; // (so that refreshModelState() looks again)
            if (result.ok())
            {
                // A GGUF file begins with "GGUF": anything else (a web page, a broken file) is not kept.
                juce::FileInputStream in (file);
                char magic[4] {};
                if (! in.openedOk() || in.read (magic, 4) != 4 || std::memcmp (magic, "GGUF", 4) != 0)
                {
                    file.deleteFile();
                    a->setState (State::failed, "the downloaded file is not a model (GGUF); it was deleted, try again");
                    return;
                }
                spdlog::info ("Helios: model downloaded ({} bytes)", result.bytes);
                a->refreshModelState();
            }
            else if (result.cancelled)
                a->refreshModelState();
            else
                a->setState (State::failed, result.offline ? juce::String ("no connection to huggingface.co: try again later (the part already downloaded is kept)")
                                                           : "the download stopped: " + fromUtf8 (result.error) + " (the part already downloaded is kept)");
        });
    });
}

void HeliosAssistant::cancelDownload()
{
    cancelDownloading.store (true);
}

void HeliosAssistant::deleteModel()
{
    if (currentState == State::downloading)
        return;
    stop();
    modelFile().deleteFile();
    juce::File (modelFile().getFullPathName() + ".part").deleteFile();
    refreshModelState();
}

void HeliosAssistant::start()
{
    if (helper != nullptr || (currentState != State::idle && currentState != State::failed) || ! modelPresent() || ! helperFile().existsAsFile())
        return;
    helper = std::make_unique<Helper> (*this, ++helperGeneration);
    if (! helper->launch())
    {
        helper.reset();
        setState (State::failed, "the helper (sun-loops-helios) did not start");
        return;
    }
    // Processor threads for the model: all the cores but one (for the audio), on a 2-core computer both.
    const int cores = juce::SystemStats::getNumPhysicalCpus();
    const int threads = cores >= 4 ? cores - 1 : std::max (1, cores);
    helper->send (object ({ { "type", "load" }, { "path", modelFile().getFullPathName() }, { "context", helios::kContextTokens }, { "threads", threads } }));
    setState (State::loading);
}

void HeliosAssistant::stop()
{
    if (helper == nullptr)
        return;
    helper.reset();
    if (currentState == State::answering && ! shown.empty())
        shown.back().done = true;
    pendingQuestion.clear();
    currentState = State::idle;
    refreshModelState();
    sendChangeMessage();
}

void HeliosAssistant::helperLoaded (bool ok, const juce::String& errorText, bool gpu)
{
    if (currentState != State::loading)
        return;
    if (! ok)
    {
        helper.reset();
        pendingQuestion.clear();
        setState (State::failed, errorText.isNotEmpty() ? errorText : juce::String ("the model could not be loaded"));
        return;
    }
    onGpu = gpu;
    setState (State::ready);
    sendPending();
}

void HeliosAssistant::ask (const juce::String& question)
{
    const auto q = question.trim();
    if (q.isEmpty() || currentState == State::answering || currentState == State::loading || pendingQuestion.isNotEmpty())
        return;
    if (currentState != State::ready && currentState != State::idle && currentState != State::failed)
        return;
    if (! modelPresent())
        return;
    shown.push_back ({ true, q, true });
    pendingQuestion = q;
    if (currentState == State::ready)
        sendPending();
    else
    {
        sendChangeMessage();
        start();
    }
}

void HeliosAssistant::sendPending()
{
    if (pendingQuestion.isEmpty() || helper == nullptr)
        return;
    const auto question = pendingQuestion.toStdString();
    pendingQuestion.clear();
    answerCatchphrase = helios::catchphraseAllowed (std::uniform_real_distribution<double> (0.0, 1.0) (random));
    const auto turn = helios::userTurn (question, helios::excerptsFor (help, question), answerCatchphrase);
    const auto prompt = helios::buildPrompt (history, turn);
    answerText.clear();
    ++answerId;
    history.push_back ({ true, turn }); // taken away again if the answer does not come
    shown.push_back ({ false, {}, false });
    helper->send (object ({ { "type", "generate" }, { "id", answerId }, { "prompt", fromUtf8 (prompt) }, { "maxTokens", helios::kAnswerTokens },
                            { "temperature", 0.7 }, { "seed", static_cast<juce::int64> (random() & 0x7fffffff) } }));
    setState (State::answering);
}

void HeliosAssistant::helperToken (int id, const juce::String& text)
{
    if (id != answerId || currentState != State::answering || shown.empty())
        return;
    answerText += text.toStdString();
    shown.back().text = fromUtf8 (answerText).trimStart();
    sendChangeMessage();
}

void HeliosAssistant::helperDone (int id, const juce::String& reason, const juce::String& errorText, const juce::var& info)
{
    if (id != answerId || currentState != State::answering)
        return;
    const auto tidy = helios::tidyAnswer (answerText, answerCatchphrase);
    if (! shown.empty())
    {
        shown.back().done = true;
        shown.back().text = fromUtf8 (tidy);
    }
    answerInfo = juce::String (static_cast<int> (info["promptTokens"])) + " tokens read (" + juce::String (static_cast<int> (info["reused"]))
                 + " already known), " + juce::String (static_cast<int> (info["generated"])) + " written in "
                 + juce::String (static_cast<double> (info["seconds"]), 1) + " s";
    spdlog::info ("Helios: answer {} ({})", reason.toStdString(), answerInfo.toStdString());
    if ((reason == "end" || reason == "limit") && ! tidy.empty())
        history.push_back ({ false, tidy });
    else if (! history.empty() && history.back().fromUser)
        history.pop_back(); // the question without its answer is not kept for the model
    if (reason == "cancelled" && ! shown.empty() && shown.back().text.isEmpty())
        shown.back().text = "(stopped)";
    if (reason == "error")
    {
        if (! shown.empty() && shown.back().text.isEmpty())
            shown.pop_back();
        setState (State::ready, {});
        lastError = errorText;
        if (errorText.isNotEmpty() && ! shown.empty())
            shown.push_back ({ false, "(" + errorText + ")", true });
        sendChangeMessage();
        return;
    }
    setState (State::ready);
}

void HeliosAssistant::helperLost()
{
    helper.reset();
    pendingQuestion.clear();
    if (! shown.empty() && ! shown.back().done)
        shown.back().done = true;
    if (! history.empty() && history.back().fromUser)
        history.pop_back();
    setState (State::failed, "the model stopped unexpectedly (not enough memory? Try the Light model, or close other programs)");
}

void HeliosAssistant::stopAnswer()
{
    if (currentState == State::answering && helper != nullptr)
        helper->send (object ({ { "type", "cancel" } }));
}

void HeliosAssistant::newChat()
{
    stopAnswer();
    ++answerId; // whatever is still coming is for the old chat
    shown.clear();
    history.clear();
    pendingQuestion.clear();
    if (currentState == State::answering)
        currentState = State::ready;
    sendChangeMessage();
}
} // namespace sls
