// sun-loops-helios (0.64.0): the helper process that runs Helios's language model for Sun Loops Studio.
//
// The program starts it (juce::ChildProcessCoordinator), tells it which model file to load and sends it whole prompts; the
// helper answers with the text as it is written, piece by piece (src/assistant/HeliosProtocol.h). The model runs here and not
// in the program so that a model that runs out of memory or crashes cannot take the program and an unsaved project with it,
// and so that llama.cpp can be built once per processor (Apple silicon and Intel halves of the Mac app are built separately and
// joined with lipo: llama.cpp cannot be built for both in one go).
//
// Without the program, for trying a model by hand:
//   sun-loops-helios --version
//   sun-loops-helios --run model.gguf "<bos><|turn>user\nCiao!<turn|>\n<|turn>model\n<|channel>thought\n<channel|>"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#if ! defined(_WIN32)
 #include <unistd.h>
#endif

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

#include "llama.h"

#include "assistant/HeliosProtocol.h"

namespace
{
// The longest prefix of `text` that ends on a whole UTF-8 character (a model's pieces can split one).
size_t wholeCharacters (const std::string& text)
{
    // Walk back over at most 3 continuation bytes to the lead byte of the last character.
    size_t i = text.size(), back = 0;
    while (i > 0 && back < 3 && (static_cast<unsigned char> (text[i - 1]) & 0xC0) == 0x80)
    {
        --i;
        ++back;
    }
    if (i == 0)
        return 0; // only continuation bytes so far: wait for more (whatever is left is sent at the end)
    const auto lead = static_cast<unsigned char> (text[i - 1]);
    size_t need = 1;
    if ((lead & 0xE0) == 0xC0)
        need = 2;
    else if ((lead & 0xF0) == 0xE0)
        need = 3;
    else if ((lead & 0xF8) == 0xF0)
        need = 4;
    else
        return text.size(); // not UTF-8 at all: send as it is
    return back + 1 >= need ? text.size() : i - 1;
}

void quietLog (ggml_log_level level, const char* text, void*)
{
    if (level == GGML_LOG_LEVEL_ERROR)
        std::fputs (text, stderr);
}

struct Result
{
    std::string reason = "end"; // end, limit, cancelled, error
    std::string error;
    int promptTokens = 0, reused = 0, generated = 0;
    double seconds = 0.0;
};

// The model and what it has read so far (its memory of the conversation).
class Engine
{
public:
    ~Engine() { unload(); }

    bool load (const std::string& path, int contextTokens, int threads, std::string& error)
    {
        unload();
        auto modelParams = llama_model_default_params();
        modelParams.n_gpu_layers = 999; // everything on the GPU where there is one (Apple silicon); ignored otherwise
        model = llama_model_load_from_file (path.c_str(), modelParams);
        if (model == nullptr)
        {
            error = "the model file could not be read (damaged, incomplete or not a GGUF model)";
            return false;
        }
        auto contextParams = llama_context_default_params();
        contextParams.n_ctx = static_cast<uint32_t> (std::max (2048, contextTokens));
        contextParams.n_batch = 512;
        contextParams.n_ubatch = 512;
        contextParams.n_threads = std::max (1, threads);
        contextParams.n_threads_batch = std::max (1, threads);
        context = llama_init_from_model (model, contextParams);
        if (context == nullptr)
        {
            error = "not enough memory for the model's context";
            unload();
            return false;
        }
        vocab = llama_model_get_vocab (model);
        cached.clear();
        return true;
    }

    void unload()
    {
        if (context != nullptr)
            llama_free (context);
        if (model != nullptr)
            llama_model_free (model);
        context = nullptr;
        model = nullptr;
        vocab = nullptr;
        cached.clear();
    }

    bool loaded() const noexcept { return context != nullptr; }
    int contextSize() const { return context != nullptr ? static_cast<int> (llama_n_ctx (context)) : 0; }

    Result generate (const std::string& prompt, int maxTokens, float temperature, uint32_t seed, const std::atomic<bool>& cancel,
                     const std::function<void (const std::string&)>& onText)
    {
        Result r;
        const auto start = std::chrono::steady_clock::now();
        const auto finish = [&r, start] { r.seconds = std::chrono::duration<double> (std::chrono::steady_clock::now() - start).count(); };
        if (! loaded())
        {
            r.reason = "error";
            r.error = "no model is loaded";
            return r;
        }

        // The prompt holds the special tokens as text (<bos>, <|turn>...): parse them, add nothing.
        std::vector<llama_token> tokens (prompt.size() + 8);
        int n = llama_tokenize (vocab, prompt.data(), static_cast<int32_t> (prompt.size()), tokens.data(),
                                static_cast<int32_t> (tokens.size()), false, true);
        if (n < 0)
        {
            tokens.resize (static_cast<size_t> (-n));
            n = llama_tokenize (vocab, prompt.data(), static_cast<int32_t> (prompt.size()), tokens.data(),
                                static_cast<int32_t> (tokens.size()), false, true);
        }
        if (n <= 0)
        {
            r.reason = "error";
            r.error = "the question could not be read";
            finish();
            return r;
        }
        tokens.resize (static_cast<size_t> (n));
        r.promptTokens = n;
        const int ctx = contextSize();
        if (n + 16 > ctx)
        {
            r.reason = "error";
            r.error = "the conversation is too long for the model: start a new chat";
            finish();
            return r;
        }
        maxTokens = std::clamp (maxTokens, 1, ctx - n);

        // Keep what is already read (the same start as last time); read only the rest. At least the last token is read again
        // so that the model gives its next-word probabilities.
        size_t same = 0;
        while (same < cached.size() && same < tokens.size() && cached[same] == tokens[same])
            ++same;
        if (same == tokens.size())
            --same;
        auto* memory = llama_get_memory (context);
        if (same < cached.size())
        {
            if (! llama_memory_seq_rm (memory, 0, static_cast<llama_pos> (same), -1))
            {
                llama_memory_clear (memory, true); // a sliding-window memory cannot always drop its end: read everything
                same = 0;
            }
            cached.resize (same);
        }
        r.reused = static_cast<int> (same);

        const int batch = static_cast<int> (llama_n_batch (context));
        for (size_t at = same; at < tokens.size();)
        {
            if (cancel.load())
            {
                r.reason = "cancelled";
                finish();
                return r;
            }
            const int count = std::min (batch, static_cast<int> (tokens.size() - at));
            if (llama_decode (context, llama_batch_get_one (tokens.data() + at, count)) != 0)
            {
                llama_memory_clear (memory, true);
                cached.clear();
                r.reason = "error";
                r.error = "the model could not read the question";
                finish();
                return r;
            }
            cached.insert (cached.end(), tokens.begin() + static_cast<std::ptrdiff_t> (at), tokens.begin() + static_cast<std::ptrdiff_t> (at + static_cast<size_t> (count)));
            at += static_cast<size_t> (count);
        }

        auto chainParams = llama_sampler_chain_default_params();
        auto* sampler = llama_sampler_chain_init (chainParams);
        llama_sampler_chain_add (sampler, llama_sampler_init_penalties (llama_vocab_n_tokens (vocab), 64, 1.1f, 0.0f, 0.0f));
        llama_sampler_chain_add (sampler, llama_sampler_init_top_k (64));
        llama_sampler_chain_add (sampler, llama_sampler_init_top_p (0.95f, 1));
        llama_sampler_chain_add (sampler, llama_sampler_init_min_p (0.05f, 1));
        llama_sampler_chain_add (sampler, llama_sampler_init_temp (temperature));
        llama_sampler_chain_add (sampler, llama_sampler_init_dist (seed));

        std::string pending;
        r.reason = "limit";
        for (int i = 0; i < maxTokens; ++i)
        {
            if (cancel.load())
            {
                r.reason = "cancelled";
                break;
            }
            const llama_token token = llama_sampler_sample (sampler, context, -1);
            if (llama_vocab_is_eog (vocab, token))
            {
                r.reason = "end";
                break;
            }
            char piece[256];
            const int length = llama_token_to_piece (vocab, token, piece, sizeof (piece), 0, false);
            if (length > 0)
            {
                pending.append (piece, static_cast<size_t> (length));
                if (const auto ready = wholeCharacters (pending); ready > 0)
                {
                    onText (pending.substr (0, ready));
                    pending.erase (0, ready);
                }
            }
            llama_token next = token;
            if (llama_decode (context, llama_batch_get_one (&next, 1)) != 0)
            {
                r.reason = "error";
                r.error = "the model stopped (out of memory?)";
                break;
            }
            cached.push_back (token);
            ++r.generated;
        }
        if (! pending.empty())
            onText (pending);
        llama_sampler_free (sampler);
        finish();
        return r;
    }

private:
    llama_model* model = nullptr;
    llama_context* context = nullptr;
    const llama_vocab* vocab = nullptr;
    std::vector<llama_token> cached; // the tokens the context holds, in order
};

juce::var object (std::initializer_list<std::pair<const char*, juce::var>> properties)
{
    auto* o = new juce::DynamicObject();
    for (const auto& [name, value] : properties)
        o->setProperty (name, value);
    return juce::var (o);
}

juce::String text (const std::string& s)
{
    return juce::String::fromUTF8 (s.data(), static_cast<int> (s.size()));
}

class Worker final : public juce::ChildProcessWorker
{
public:
    Worker() : thread ([this] { run(); }) {}

    ~Worker() override
    {
        {
            const std::lock_guard<std::mutex> guard (lock);
            quitting = true;
            cancel.store (true);
        }
        wake.notify_all();
        thread.join();
    }

    // Until the program says "quit", disconnects or is gone (it crashed or was killed: the helper is then an orphan and stops too).
    void waitUntilDisconnected()
    {
#if ! defined(_WIN32)
        const auto parent = ::getppid();
        while (! lost.wait (1000))
            if (::getppid() != parent)
                break;
#else
        lost.wait();
#endif
        cancel.store (true);
    }

    void handleMessageFromCoordinator (const juce::MemoryBlock& message) override
    {
        auto v = juce::JSON::parse (message.toString());
        const auto type = v["type"].toString();
        if (type == "quit")
        {
            cancel.store (true);
            lost.signal();
            return;
        }
        if (type == "cancel")
        {
            std::vector<int> dropped;
            {
                const std::lock_guard<std::mutex> guard (lock);
                cancel.store (true);
                for (auto it = jobs.begin(); it != jobs.end();)
                    if ((*it)["type"].toString() == "generate")
                    {
                        dropped.push_back (static_cast<int> ((*it)["id"]));
                        it = jobs.erase (it);
                    }
                    else
                        ++it;
            }
            for (const int id : dropped)
                send (object ({ { "type", "done" }, { "id", id }, { "reason", "cancelled" }, { "error", "" } }));
            return;
        }
        {
            const std::lock_guard<std::mutex> guard (lock);
            jobs.push_back (v);
        }
        wake.notify_all();
    }

    void handleConnectionLost() override
    {
        cancel.store (true);
        lost.signal();
    }

private:
    void send (const juce::var& message)
    {
        const auto s = juce::JSON::toString (message, true);
        sendMessageToCoordinator (juce::MemoryBlock (s.toRawUTF8(), s.getNumBytesAsUTF8()));
    }

    void run()
    {
        for (;;)
        {
            juce::var job;
            {
                std::unique_lock<std::mutex> guard (lock);
                wake.wait (guard, [this] { return quitting || ! jobs.empty(); });
                if (quitting)
                    return;
                job = jobs.front();
                jobs.pop_front();
                cancel.store (false); // a cancel that came before this job was for the jobs before it
            }
            const auto type = job["type"].toString();
            if (type == "load")
            {
                std::string error;
                const int threads = job.hasProperty ("threads") ? static_cast<int> (job["threads"]) : 4;
                const int contextTokens = job.hasProperty ("context") ? static_cast<int> (job["context"]) : 8192;
                const bool ok = engine.load (job["path"].toString().toStdString(), contextTokens, threads, error);
                send (object ({ { "type", "loaded" }, { "ok", ok }, { "error", text (error) }, { "context", engine.contextSize() },
                                { "gpu", llama_supports_gpu_offload() } }));
            }
            else if (type == "generate")
            {
                const int id = static_cast<int> (job["id"]);
                const auto prompt = job["prompt"].toString().toStdString();
                const int maxTokens = job.hasProperty ("maxTokens") ? static_cast<int> (job["maxTokens"]) : 768;
                const float temperature = job.hasProperty ("temperature") ? static_cast<float> (static_cast<double> (job["temperature"])) : 0.7f;
                const auto seed = job.hasProperty ("seed") ? static_cast<uint32_t> (static_cast<juce::int64> (job["seed"])) : LLAMA_DEFAULT_SEED;
                const auto r = engine.generate (prompt, maxTokens, temperature, seed, cancel, [this, id] (const std::string& piece)
                {
                    send (object ({ { "type", "token" }, { "id", id }, { "text", text (piece) } }));
                });
                send (object ({ { "type", "done" }, { "id", id }, { "reason", text (r.reason) }, { "error", text (r.error) },
                                { "promptTokens", r.promptTokens }, { "reused", r.reused }, { "generated", r.generated },
                                { "seconds", r.seconds } }));
            }
        }
    }

    Engine engine;
    std::mutex lock;
    std::condition_variable wake;
    std::deque<juce::var> jobs;
    bool quitting = false;
    std::atomic<bool> cancel { false };
    juce::WaitableEvent lost;
    std::thread thread; // last: it starts in the constructor and uses the members above
};

// --run: one prompt from the command line, the answer to stdout (for trying a model without the program).
int runOnce (const std::string& path, const std::string& prompt)
{
    Engine engine;
    std::string error;
    const int threads = std::max (1, juce::SystemStats::getNumPhysicalCpus());
    if (! engine.load (path, 8192, threads, error))
    {
        std::fprintf (stderr, "sun-loops-helios: %s\n", error.c_str());
        return 1;
    }
    std::atomic<bool> cancel { false };
    const auto r = engine.generate (prompt, 512, 0.7f, 1234, cancel, [] (const std::string& piece)
    {
        std::fwrite (piece.data(), 1, piece.size(), stdout);
        std::fflush (stdout);
    });
    std::fprintf (stderr, "\n[%s%s%s; %d prompt tokens, %d generated, %.1f s]\n", r.reason.c_str(), r.error.empty() ? "" : ": ",
                  r.error.c_str(), r.promptTokens, r.generated, r.seconds);
    return r.reason == "error" ? 1 : 0;
}
} // namespace

int main (int argc, char* argv[])
{
    if (argc > 1 && std::string (argv[1]) == "--version")
    {
        std::printf ("sun-loops-helios, protocol %d\n%s\n", sls::helios::kProtocolVersion, llama_print_system_info());
        return 0;
    }
    llama_log_set (quietLog, nullptr);
    llama_backend_init();
    int status = 0;
    if (argc > 3 && std::string (argv[1]) == "--run")
        status = runOnce (argv[2], argv[3]);
    else
    {
        juce::StringArray args;
        for (int i = 1; i < argc; ++i)
            args.add (juce::String::fromUTF8 (argv[i]));
        Worker worker;
        if (worker.initialiseFromCommandLine (args.joinIntoString (" "), sls::helios::kWorkerId, 20000))
            worker.waitUntilDisconnected();
        else
        {
            std::fprintf (stderr, "sun-loops-helios: this helper is started by Sun Loops Studio (try --version or --run)\n");
            status = 2;
        }
    }
    llama_backend_free();
    return status;
}
