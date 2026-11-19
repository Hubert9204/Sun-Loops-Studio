#if defined(_WIN32)
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #include <windows.h>
#else
 #include <unistd.h>
#endif

#include "plugins/PluginSandbox.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <limits>
#include <thread>

#include "plugins/PluginHost.h"

namespace sls
{
namespace
{
using namespace sandbox;

juce::int64 currentProcessId()
{
#if defined(_WIN32)
    return static_cast<juce::int64> (GetCurrentProcessId());
#else
    return static_cast<juce::int64> (getpid());
#endif
}

// Whether the process that started this worker still runs (POSIX: a worker whose parent died gets another parent).
class ParentWatch
{
public:
    explicit ParentWatch (juce::int64 pid)
    {
#if defined(_WIN32)
        handle = OpenProcess (SYNCHRONIZE, FALSE, static_cast<DWORD> (pid));
#else
        parent = pid;
#endif
    }
    ~ParentWatch()
    {
#if defined(_WIN32)
        if (handle != nullptr)
            CloseHandle (handle);
#endif
    }
    bool alive() const
    {
#if defined(_WIN32)
        return handle == nullptr || WaitForSingleObject (handle, 0) != WAIT_OBJECT_0;
#else
        return parent <= 0 || static_cast<juce::int64> (getppid()) == parent;
#endif
    }

private:
#if defined(_WIN32)
    HANDLE handle = nullptr;
#else
    juce::int64 parent = 0;
#endif
};

double nowSeconds() { return juce::Time::getMillisecondCounterHiRes() * 0.001; }

void putText (Shared& s, const juce::String& text)
{
    const auto utf8 = text.toRawUTF8();
    const auto bytes = std::min (std::strlen (utf8), kMailboxBytes);
    std::memcpy (s.mailbox, utf8, bytes);
    s.size = bytes;
}

juce::String getText (const Shared& s)
{
    return juce::String::fromUTF8 (s.mailbox, static_cast<int> (std::min<std::uint64_t> (s.size, kMailboxBytes)));
}
} // namespace

// ------------------------------------------------------------------------- the program's side

SandboxClient::~SandboxClient()
{
    if (shared != nullptr && ! crashed() && child.isRunning())
        request (Quit, 500);
    if (child.isRunning())
        child.kill();
    map.reset();
    file.deleteFile();
}

std::unique_ptr<SandboxClient> SandboxClient::start (const juce::File& executable, const juce::PluginDescription& description, double rate, int block,
                                                     juce::String& error)
{
    std::unique_ptr<SandboxClient> c (new SandboxClient());
    c->file = juce::File::getSpecialLocation (juce::File::tempDirectory)
                  .getChildFile ("sls-sandbox-" + juce::String::toHexString (juce::Random::getSystemRandom().nextInt64()) + ".shm");
    {
        juce::FileOutputStream out (c->file);
        if (! out.openedOk() || ! out.setPosition (static_cast<juce::int64> (sizeof (Shared)) - 1) || ! out.writeByte (0))
        {
            error = "The plugin's sandbox could not make its shared file in " + c->file.getParentDirectory().getFullPathName();
            return nullptr;
        }
    }
    c->map = std::make_unique<juce::MemoryMappedFile> (c->file, juce::MemoryMappedFile::readWrite, false);
    if (c->map->getData() == nullptr || c->map->getSize() < sizeof (Shared))
    {
        error = "The plugin's sandbox could not map its shared file.";
        return nullptr;
    }
    c->shared = new (c->map->getData()) Shared;
    c->shared->magic = kMagic;
    c->shared->version = kVersion;

    if (! c->child.start (juce::StringArray { executable.getFullPathName(), "--plugin-sandbox", c->file.getFullPathName(),
                                              juce::String (currentProcessId()) },
                          0))
    {
        error = "The plugin's sandbox (" + executable.getFileName() + ") could not be started.";
        return nullptr;
    }
    const double until = nowSeconds() + 20.0;
    while (c->shared->ready.load (std::memory_order_acquire) == 0)
    {
        if (! c->child.isRunning() || nowSeconds() > until)
        {
            error = "The plugin's sandbox did not start.";
            return nullptr;
        }
        juce::Thread::sleep (5);
    }

    std::unique_ptr<juce::XmlElement> xml (description.createXml());
    putText (*c->shared, xml != nullptr ? xml->toString() : juce::String());
    c->shared->rate = rate;
    c->shared->block = std::clamp (block, 1, kMaxBlock);
    if (! c->request (Load, 60000) || c->shared->result != 1)
    {
        error = c->crashed() ? "The plugin crashed while it was loading in its sandbox." : getText (*c->shared);
        if (error.isEmpty())
            error = "The plugin " + description.name + " could not be loaded in its sandbox.";
        return nullptr;
    }
    c->name = getText (*c->shared);
    if (c->request (ParameterInfo, 10000))
        if (auto* list = juce::JSON::parse (getText (*c->shared)).getArray())
            for (const auto& p : *list)
                c->params.emplace_back (p.getProperty ("name", "").toString().toStdString(), static_cast<float> (p.getProperty ("default", 0.0)));
    c->lastState = c->getState();
    return c;
}

bool SandboxClient::request (Command command, int timeoutMs)
{
    if (shared == nullptr || crashed())
        return false;
    shared->command = command;
    shared->result = 0;
    shared->requestSerial.store (++serial, std::memory_order_release);
    const double until = nowSeconds() + timeoutMs * 0.001;
    int polls = 0;
    while (shared->doneSerial.load (std::memory_order_acquire) != serial)
    {
        if (++polls % 20 == 0 && ! child.isRunning())
        {
            gone.store (true, std::memory_order_release);
            return false;
        }
        if (nowSeconds() > until)
            return false;
        juce::Thread::sleep (1);
    }
    return true;
}

bool SandboxClient::checkAlive()
{
    if (! crashed() && ! child.isRunning())
        gone.store (true, std::memory_order_release);
    return ! crashed();
}

void SandboxClient::setParameter (int index, float value) noexcept
{
    if (index < 0 || index >= kMaxParams)
        return;
    for (int i = 0; i < numPending; ++i)
        if (pendingParams[static_cast<size_t> (i)].index == index)
        {
            pendingParams[static_cast<size_t> (i)].value = value;
            return;
        }
    if (numPending < kMaxEvents)
        pendingParams[static_cast<size_t> (numPending++)] = { index, value };
}

float SandboxClient::parameterValue (int index) const noexcept
{
    return index >= 0 && index < kMaxParams ? shared->paramValues[static_cast<size_t> (index)].load (std::memory_order_relaxed) : 0.0f;
}

void SandboxClient::process (float* left, float* right, int numSamples, const PluginMidiEvent* events, int numEvents, PluginIo* io,
                             bool isEffect) noexcept
{
    if (io != nullptr)
        io->numMidiOut = 0;
    numSamples = std::clamp (numSamples, 0, kMaxBlock);
    const auto fallback = [&]
    {
        if (! isEffect) // (an effect lets the sound through: it is already in place)
        {
            std::fill_n (left, numSamples, 0.0f);
            std::fill_n (right, numSamples, 0.0f);
        }
        if (io != nullptr && io->extraOut != nullptr)
            for (int e = 0; e < io->extraChannels; ++e)
                if (io->extraOut[e] != nullptr)
                    std::fill_n (io->extraOut[e], numSamples, 0.0f);
    };
    if (crashed() || numSamples == 0)
    {
        fallback();
        return;
    }
    auto& s = *shared;
    const auto asked = s.audioRequest.load (std::memory_order_relaxed);
    if (late.load (std::memory_order_relaxed))
    {
        if (s.audioDone.load (std::memory_order_acquire) != asked)
        {
            fallback(); // still busy with the chunk that came too late
            return;
        }
        late.store (false, std::memory_order_relaxed);
    }

    s.numSamples = numSamples;
    std::copy_n (left, numSamples, s.audioIn[0].begin());
    std::copy_n (right, numSamples, s.audioIn[1].begin());
    s.hasSide = io != nullptr && io->sideLeft != nullptr ? 1 : 0;
    if (s.hasSide != 0)
    {
        std::copy_n (io->sideLeft, numSamples, s.audioIn[2].begin());
        std::copy_n (io->sideRight != nullptr ? io->sideRight : io->sideLeft, numSamples, s.audioIn[3].begin());
    }
    s.numEvents = std::clamp (numEvents, 0, kMaxEvents);
    std::copy_n (events, s.numEvents, s.events.begin());
    s.numParamChanges = numPending;
    std::copy_n (pendingParams.begin(), numPending, s.paramChanges.begin());
    numPending = 0;
    s.wantExtra = io != nullptr && io->extraOut != nullptr ? std::min (io->extraChannels, kMaxChannels - 2) : 0;
    s.wantMidiOut = io != nullptr && io->midiOut != nullptr ? 1 : 0;
    s.bpm = tBpm;
    s.quarters = tQuarters;
    s.barStart = tBar;
    s.playing = tPlaying ? 1 : 0;
    s.numerator = tNum;
    s.denominator = tDen;

    const auto serialNow = asked + 1;
    s.audioRequest.store (serialNow, std::memory_order_release);
    // Wait for the worker: a few spins, then yielding; at most 50 ms (a plugin that takes longer is skipped until it is done).
    const auto start = juce::Time::getHighResolutionTicks();
    const auto limit = juce::Time::secondsToHighResolutionTicks (0.05);
    int spins = 0;
    while (s.audioDone.load (std::memory_order_acquire) != serialNow)
    {
        if (++spins < 64)
            continue;
        std::this_thread::yield();
        if (juce::Time::getHighResolutionTicks() - start > limit)
        {
            late.store (true, std::memory_order_relaxed);
            fallback();
            return;
        }
    }
    std::copy_n (s.audioOut[0].begin(), numSamples, left);
    std::copy_n (s.audioOut[1].begin(), numSamples, right);
    if (io != nullptr)
    {
        for (int e = 0; e < s.wantExtra; ++e)
            if (io->extraOut[e] != nullptr)
                std::copy_n (s.audioOut[static_cast<size_t> (2 + e)].begin(), numSamples, io->extraOut[e]);
        if (io->midiOut != nullptr)
        {
            io->numMidiOut = std::clamp (s.numMidiOut, 0, std::min (io->maxMidiOut, kMaxEvents));
            std::copy_n (s.midiOut.begin(), io->numMidiOut, io->midiOut);
        }
    }
}

bool SandboxClient::prepare (double rate, int block)
{
    shared->rate = rate;
    shared->block = std::clamp (block, 1, kMaxBlock);
    return request (Prepare, 10000);
}

juce::MemoryBlock SandboxClient::getState()
{
    if (! request (GetState, 10000) || shared->result != 1)
        return lastState;
    juce::MemoryBlock state (shared->mailbox, static_cast<size_t> (std::min<std::uint64_t> (shared->size, kMailboxBytes)));
    lastState = state;
    return state;
}

bool SandboxClient::setState (const juce::MemoryBlock& state)
{
    if (state.getSize() > kMailboxBytes)
        return false;
    lastState = state;
    std::memcpy (shared->mailbox, state.getData(), state.getSize());
    shared->size = state.getSize();
    return request (SetState, 10000) && shared->result == 1;
}

juce::String SandboxClient::parameterText (int index, float value)
{
    shared->intArg = index;
    shared->floatArg = value;
    return request (ParameterText, 1000) ? getText (*shared) : juce::String();
}

juce::StringArray SandboxClient::programNames (int& current)
{
    juce::StringArray names;
    current = -1;
    if (request (Programs, 2000))
    {
        const auto v = juce::JSON::parse (getText (*shared));
        if (auto* list = v.getProperty ("names", {}).getArray())
            for (const auto& n : *list)
                names.add (n.toString());
        current = static_cast<int> (v.getProperty ("current", -1));
    }
    return names;
}

bool SandboxClient::setProgram (int index)
{
    shared->intArg = index;
    return request (SetProgram, 5000) && shared->result == 1;
}

void SandboxClient::showEditor (bool show)
{
    shared->flagArg = show ? 1 : 0;
    request (ShowEditor, 5000);
}

// ------------------------------------------------------------------------- the worker

namespace
{
// The plugin's own window, in the worker. Closing it only hides it (the program decides when the plugin goes).
class SandboxEditorWindow : public juce::DocumentWindow
{
public:
    SandboxEditorWindow (JucePlugin& plugin, std::atomic<std::uint32_t>& openFlag)
        : juce::DocumentWindow (juce::String (plugin.displayName()) + " (sandbox)", juce::Colour (0xff1b1e24), juce::DocumentWindow::closeButton),
          open (openFlag)
    {
        setUsingNativeTitleBar (true);
        if (auto* instance = plugin.getInstance())
        {
            juce::AudioProcessorEditor* editor = instance->hasEditor() ? instance->createEditorIfNeeded() : nullptr;
            if (editor == nullptr)
                editor = new juce::GenericAudioProcessorEditor (*instance);
            setContentOwned (editor, true);
            setResizable (editor->isResizable(), false);
        }
        setAlwaysOnTop (true);
        centreWithSize (getWidth(), getHeight());
    }
    ~SandboxEditorWindow() override { clearContentComponent(); }
    void closeButtonPressed() override
    {
        setVisible (false);
        open.store (0, std::memory_order_relaxed);
    }

private:
    std::atomic<std::uint32_t>& open;
};
} // namespace

struct PluginSandboxWorker::Impl : private juce::Timer, private juce::Thread
{
    Impl (const juce::File& sharedFile, juce::int64 parentPid, std::function<void()> finished)
        : juce::Thread ("Plugin sandbox audio"), parent (parentPid), onFinished (std::move (finished))
    {
        map = std::make_unique<juce::MemoryMappedFile> (sharedFile, juce::MemoryMappedFile::readWrite, false);
        if (map->getData() == nullptr || map->getSize() < sizeof (Shared))
            return;
        shared = static_cast<Shared*> (map->getData());
        if (shared->magic != kMagic || shared->version != kVersion)
        {
            shared = nullptr;
            return;
        }
        handled = shared->requestSerial.load (std::memory_order_acquire);
        startThread (juce::Thread::Priority::highest);
        startTimer (5);
        shared->ready.store (1, std::memory_order_release);
    }

    ~Impl() override
    {
        stopTimer();
        stopThread (2000);
        editor.reset();
        plugin.reset();
    }

    bool ok() const noexcept { return shared != nullptr; }

    void finish()
    {
        stopTimer();
        live.store (nullptr, std::memory_order_release);
        if (onFinished)
            juce::MessageManager::callAsync (onFinished);
    }

    // Message thread: the mailbox, the reports, the parent.
    void timerCallback() override
    {
        shared->childHeartbeat.fetch_add (1, std::memory_order_relaxed);
        if (++ticks % 40 == 0 && ! parent.alive())
        {
            finish();
            return;
        }
        if (const auto serial = shared->requestSerial.load (std::memory_order_acquire); serial != handled)
        {
            handled = serial;
            const bool quit = shared->command == Quit;
            handle (static_cast<Command> (shared->command));
            shared->doneSerial.store (serial, std::memory_order_release);
            if (quit)
            {
                finish();
                return;
            }
        }
        if (plugin != nullptr)
        {
            shared->changes.store (plugin->changeCount(), std::memory_order_release);
            shared->lastTouched.store (plugin->lastTouchedParameter(), std::memory_order_relaxed);
            if (ticks % 20 == 0)
                for (int i = 0; i < std::min (plugin->parameterCount(), kMaxParams); ++i)
                    shared->paramValues[static_cast<size_t> (i)].store (plugin->parameterValue (i), std::memory_order_relaxed);
        }
    }

    void describe()
    {
        shared->latency = plugin->latencySamples();
        shared->sideChannels = plugin->hasSidechain() ? 2 : 0;
        shared->extraOuts = std::min (plugin->extraOutputChannels(), kMaxChannels - 2);
        shared->makesMidi = plugin->sendsMidi() ? 1 : 0;
        shared->isInstrument = plugin->isInstrument() ? 1 : 0;
        shared->numParams = std::min (plugin->parameterCount(), kMaxParams);
    }

    // While the program asks for nothing (it never sends audio during a Prepare), the audio thread is told to keep off the plugin.
    void withPluginIdle (const std::function<void()>& fn)
    {
        live.store (nullptr, std::memory_order_release);
        while (inProcess.load (std::memory_order_acquire))
            std::this_thread::yield();
        fn();
        live.store (plugin.get(), std::memory_order_release);
    }

    void handle (Command command)
    {
        auto& s = *shared;
        s.result = 0;
        switch (command)
        {
            case Load:
            {
                juce::PluginDescription d;
                if (auto xml = juce::parseXML (getText (s)); xml == nullptr || ! d.loadFromXml (*xml))
                {
                    putText (s, "The plugin's description could not be read.");
                    break;
                }
                juce::String error;
                plugin = host.create (d, s.rate, s.block, error);
                if (plugin == nullptr)
                {
                    putText (s, error);
                    break;
                }
                describe();
                live.store (plugin.get(), std::memory_order_release);
                putText (s, juce::String (plugin->displayName()));
                s.result = 1;
                break;
            }
            case Prepare:
                if (plugin != nullptr)
                {
                    withPluginIdle ([&] { plugin->prepare (s.rate, s.block); });
                    describe();
                    s.result = 1;
                }
                break;
            case GetState:
                if (plugin != nullptr)
                {
                    const auto state = plugin->getState();
                    if (state.getSize() <= kMailboxBytes)
                    {
                        std::memcpy (s.mailbox, state.getData(), state.getSize());
                        s.size = state.getSize();
                        s.result = 1;
                    }
                }
                break;
            case SetState:
                if (plugin != nullptr)
                {
                    plugin->setState (juce::MemoryBlock (s.mailbox, static_cast<size_t> (std::min<std::uint64_t> (s.size, kMailboxBytes))));
                    s.result = 1;
                }
                break;
            case ParameterInfo:
            {
                juce::Array<juce::var> list;
                for (int i = 0; plugin != nullptr && i < std::min (plugin->parameterCount(), kMaxParams); ++i)
                {
                    auto* o = new juce::DynamicObject();
                    o->setProperty ("name", juce::String (plugin->parameterName (i)));
                    o->setProperty ("default", plugin->parameterDefault (i));
                    list.add (juce::var (o));
                }
                putText (s, juce::JSON::toString (list, true));
                s.result = 1;
                break;
            }
            case ParameterText:
                putText (s, plugin != nullptr ? juce::String (plugin->parameterText (s.intArg, s.floatArg)) : juce::String());
                s.result = 1;
                break;
            case Programs:
            {
                auto* o = new juce::DynamicObject();
                juce::Array<juce::var> names;
                if (plugin != nullptr)
                    for (const auto& n : plugin->programNames())
                        names.add (n);
                o->setProperty ("names", names);
                o->setProperty ("current", plugin != nullptr ? plugin->currentProgram() : -1);
                putText (s, juce::JSON::toString (juce::var (o), true));
                s.result = 1;
                break;
            }
            case SetProgram:
                s.result = plugin != nullptr && plugin->setProgram (s.intArg) ? 1 : 0;
                break;
            case ShowEditor:
                if (plugin != nullptr && plugin->getInstance() != nullptr)
                {
                    if (s.flagArg != 0)
                    {
                        if (editor == nullptr)
                            editor = std::make_unique<SandboxEditorWindow> (*plugin, s.editorOpen);
                        editor->setVisible (true);
                        editor->toFront (true);
                        s.editorOpen.store (1, std::memory_order_relaxed);
                    }
                    else if (editor != nullptr)
                    {
                        editor->setVisible (false);
                        s.editorOpen.store (0, std::memory_order_relaxed);
                    }
                    s.result = 1;
                }
                break;
            case Quit:
                s.result = 1;
                break;
            case None:
                break;
        }
    }

    // The audio thread: one chunk per request, at once. Between bursts it yields, then sleeps a little (a worker that waits costs
    // almost nothing; the first chunk after a pause waits a fraction of a millisecond more).
    void run() override
    {
        auto seen = shared->audioRequest.load (std::memory_order_acquire);
        double lastWork = nowSeconds();
        while (! threadShouldExit())
        {
            const auto asked = shared->audioRequest.load (std::memory_order_acquire);
            if (asked != seen)
            {
                seen = asked;
                processChunk();
                shared->audioDone.store (asked, std::memory_order_release);
                lastWork = nowSeconds();
                continue;
            }
            if (nowSeconds() - lastWork < 0.02)
                std::this_thread::yield();
            else
                std::this_thread::sleep_for (std::chrono::microseconds (200));
        }
    }

    void processChunk() noexcept
    {
        auto& s = *shared;
        const int n = std::clamp (s.numSamples, 0, kMaxBlock);
        s.numMidiOut = 0;
        inProcess.store (true, std::memory_order_release);
        auto* p = live.load (std::memory_order_acquire);
        if (p == nullptr)
        {
            inProcess.store (false, std::memory_order_release);
            const bool effect = s.isInstrument == 0;
            std::copy_n (s.audioIn[0].begin(), n, s.audioOut[0].begin());
            std::copy_n (s.audioIn[1].begin(), n, s.audioOut[1].begin());
            if (! effect)
            {
                std::fill_n (s.audioOut[0].begin(), n, 0.0f);
                std::fill_n (s.audioOut[1].begin(), n, 0.0f);
            }
            return;
        }
        for (int i = 0; i < std::clamp (s.numParamChanges, 0, kMaxEvents); ++i)
            p->setParameter (s.paramChanges[static_cast<size_t> (i)].index, s.paramChanges[static_cast<size_t> (i)].value);
        p->setMeter (s.numerator, s.denominator, s.barStart);
        p->setTransport (s.bpm, s.quarters, s.playing != 0);
        std::copy_n (s.audioIn[0].begin(), n, s.audioOut[0].begin());
        std::copy_n (s.audioIn[1].begin(), n, s.audioOut[1].begin());
        PluginIo io;
        std::array<float*, kMaxChannels> extra {};
        if (s.hasSide != 0)
        {
            io.sideLeft = s.audioIn[2].data();
            io.sideRight = s.audioIn[3].data();
        }
        if (s.wantExtra > 0)
        {
            for (int e = 0; e < std::min (s.wantExtra, kMaxChannels - 2); ++e)
                extra[static_cast<size_t> (e)] = s.audioOut[static_cast<size_t> (2 + e)].data();
            io.extraOut = extra.data();
            io.extraChannels = std::min (s.wantExtra, kMaxChannels - 2);
        }
        if (s.wantMidiOut != 0)
        {
            io.midiOut = s.midiOut.data();
            io.maxMidiOut = kMaxEvents;
        }
        p->process (s.audioOut[0].data(), s.audioOut[1].data(), n, s.events.data(), std::clamp (s.numEvents, 0, kMaxEvents), &io);
        s.numMidiOut = io.numMidiOut;
        inProcess.store (false, std::memory_order_release);
    }

    std::unique_ptr<juce::MemoryMappedFile> map;
    Shared* shared = nullptr;
    ParentWatch parent;
    std::function<void()> onFinished;
    PluginHost host;
    std::shared_ptr<JucePlugin> plugin;
    std::atomic<JucePlugin*> live { nullptr };
    std::atomic<bool> inProcess { false };
    std::unique_ptr<SandboxEditorWindow> editor;
    std::uint32_t handled = 0;
    int ticks = 0;
};

bool PluginSandboxWorker::isSandboxCommandLine (const juce::StringArray& arguments)
{
    return arguments.contains ("--plugin-sandbox");
}

std::unique_ptr<PluginSandboxWorker> PluginSandboxWorker::start (const juce::StringArray& arguments, std::function<void()> onFinished)
{
    const int at = arguments.indexOf ("--plugin-sandbox");
    if (at < 0 || at + 1 >= arguments.size())
        return nullptr;
    const juce::File sharedFile (arguments[at + 1].unquoted());
    const auto parentPid = at + 2 < arguments.size() ? arguments[at + 2].getLargeIntValue() : 0;
    std::unique_ptr<PluginSandboxWorker> w (new PluginSandboxWorker());
    w->impl = std::make_unique<Impl> (sharedFile, parentPid, std::move (onFinished));
    if (! w->impl->ok())
        return nullptr;
    return w;
}

PluginSandboxWorker::~PluginSandboxWorker() = default;

int runPluginSandbox (const juce::StringArray& arguments)
{
    juce::ScopedJuceInitialiser_GUI gui;
    auto worker = PluginSandboxWorker::start (arguments, [] { juce::MessageManager::getInstance()->stopDispatchLoop(); });
    if (worker == nullptr)
        return 1;
    juce::MessageManager::getInstance()->runDispatchLoop();
    worker.reset();
    return 0;
}
} // namespace sls
