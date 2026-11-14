// Tests for 0.43.0: the session log, the crash handler and its reports, the HTTP client, GitHub issues and the update check.
// The network parts talk to a small HTTP server on 127.0.0.1 started here; nothing goes to the internet.
#include <algorithm>
#include <atomic>
#include <chrono>
#include <climits>
#include <csignal>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <cstdio>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <arpa/inet.h>
#include <dlfcn.h>
#if defined(__linux__)
 #include <link.h>
#endif
#include <netinet/in.h>
#include <poll.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include <spdlog/spdlog.h>

#include "support/CrashHandler.h"
#include "support/CrashReport.h"
#include "support/GitHub.h"
#include "support/Http.h"
#include "support/Logging.h"
#include "support/SemVer.h"
#include "support/UpdateChecker.h"

void supportCheck (bool ok, const char* name); // tests/test_core.cpp

namespace fs = std::filesystem;

namespace
{
fs::path freshFolder (const std::string& name)
{
    auto dir = fs::temp_directory_path() / ("sls-support-test-" + std::to_string (::getpid())) / name;
    std::error_code ignored;
    fs::remove_all (dir, ignored);
    fs::create_directories (dir);
    return dir;
}

std::string readAll (const fs::path& file)
{
    std::ifstream in (file, std::ios::binary);
    return { std::istreambuf_iterator<char> (in), std::istreambuf_iterator<char>() };
}

// A one-connection HTTP server on 127.0.0.1: it records the request and sends `response`, or (silent) never answers.
class MiniServer
{
public:
    explicit MiniServer (std::string responseText, bool silentServer = false) : response (std::move (responseText)), silent (silentServer)
    {
        listener = ::socket (AF_INET, SOCK_STREAM, 0);
        int yes = 1;
        ::setsockopt (listener, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof (yes));
        sockaddr_in address {};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl (INADDR_LOOPBACK);
        address.sin_port = 0;
        if (::bind (listener, reinterpret_cast<sockaddr*> (&address), sizeof (address)) != 0 || ::listen (listener, 4) != 0)
            std::cerr << "MiniServer: cannot listen on 127.0.0.1\n";
        socklen_t length = sizeof (address);
        ::getsockname (listener, reinterpret_cast<sockaddr*> (&address), &length);
        port = ntohs (address.sin_port);
        worker = std::thread ([this] { serve(); });
    }

    ~MiniServer()
    {
        stop.store (true);
        if (worker.joinable())
            worker.join();
        ::close (listener);
    }

    std::string base() const { return "http://127.0.0.1:" + std::to_string (port); }
    std::string received()
    {
        const std::lock_guard<std::mutex> guard (lock);
        return request;
    }

    static std::string reply (int status, const std::string& reason, const std::string& body)
    {
        return "HTTP/1.1 " + std::to_string (status) + " " + reason + "\r\nContent-Type: application/json\r\nContent-Length: "
               + std::to_string (body.size()) + "\r\nConnection: close\r\n\r\n" + body;
    }

private:
    void serve()
    {
        pollfd p { listener, POLLIN, 0 };
        while (! stop.load() && ::poll (&p, 1, 50) == 0) {}
        if (stop.load())
            return;
        const int client = ::accept (listener, nullptr, nullptr);
        if (client < 0)
            return;
        std::string text;
        char buffer[4096];
        std::size_t expected = std::string::npos;
        pollfd c { client, POLLIN, 0 };
        while (! stop.load())
        {
            if (::poll (&c, 1, 50) <= 0)
                continue;
            const auto n = ::recv (client, buffer, sizeof (buffer), 0);
            if (n <= 0)
                break;
            text.append (buffer, static_cast<std::size_t> (n));
            const auto headerEnd = text.find ("\r\n\r\n");
            if (headerEnd != std::string::npos && expected == std::string::npos)
            {
                std::size_t length = 0;
                for (const char* name : { "Content-Length: ", "content-length: " })
                    if (const auto at = text.find (name); at != std::string::npos && at < headerEnd)
                        length = static_cast<std::size_t> (std::stoul (text.substr (at + std::strlen (name))));
                expected = headerEnd + 4 + length;
            }
            if (expected != std::string::npos && text.size() >= expected)
                break;
        }
        {
            const std::lock_guard<std::mutex> guard (lock);
            request = text;
        }
        if (! silent)
            ::send (client, response.data(), response.size(), MSG_NOSIGNAL);
        else
            while (! stop.load())
                std::this_thread::sleep_for (std::chrono::milliseconds (20));
        ::close (client);
    }

    std::string response;
    bool silent;
    int listener = -1;
    int port = 0;
    std::atomic<bool> stop { false };
    std::mutex lock;
    std::string request;
    std::thread worker;
};

// Runs `crash` in a child process with the handler installed; returns the signal that ended the child (0 if none).
int crashInChild (const fs::path& dir, const std::function<void()>& crash)
{
    std::cout.flush(); // or the child flushes the parent's buffered output a second time when it exits
    std::fflush (nullptr);
    const pid_t child = ::fork();
    if (child == 0)
    {
        rlimit noCore { 0, 0 };
        ::setrlimit (RLIMIT_CORE, &noCore);
        sls::crash::install ({ dir, "9.8.7" });
        sls::crash::setContext ({ { "audio_driver", "ALSA" }, { "audio_device", "Test Card" }, { "sample_rate", "48000" }, { "buffer_size", "256" } });
        crash();
        ::_exit (0);
    }
    int status = 0;
    ::waitpid (child, &status, 0);
    return WIFSIGNALED (status) ? WTERMSIG (status) : 0;
}

const char* kSampleCrash = R"(SLS-CRASH 1
version=0.42.0
signal=11
signame=SIGSEGV
code=1
address=0x0
time=1789000000
thread=other
thread_name=Audio Thread
os=Nobara Linux 41 (KDE Plasma)
kernel=Linux 6.11.5
arch=x86_64
audio_driver=JACK
audio_device=Scarlett 2i2 | USB
sample_rate=48000
buffer_size=128
frames:
/home/hubert/Apps/sun-loops-studio(_ZN3sls5crash12_GLOBAL__N_18onSignalEiP9siginfo_tPv+0x2c1) [0x55d0c2a4e1a0]
/lib64/libc.so.6(+0x3e730) [0x7f0b1c23e730]
/home/hubert/Apps/sun-loops-studio(_ZN3sls6Player6renderERN4juce11AudioBufferIfEEi+0x1a) [0x55d0c2a10011]
/home/hubert/Apps/sun-loops-studio(+0x12345) [0x55d0c2a12345]
[0x7f0b1c000000]
end
)";
} // namespace

// Functions that crash, in the program's namespace so their names are exported (as the program's are) and appear in traces.
namespace sls::crashtest
{
[[gnu::noinline]] void writeThroughNull (std::uintptr_t where)
{
    *reinterpret_cast<volatile int*> (where) = 42; // NOLINT(performance-no-int-to-ptr): the crash is the point
}
[[gnu::noinline]] void throwUncaught (int code)
{
    if (code >= 0)
        throw std::runtime_error ("test exception " + std::to_string (code));
}
[[gnu::noinline]] int recurseForever (int depth)
{
    volatile char pad[1024];
    pad[0] = static_cast<char> (depth);
    if (depth == INT_MIN)
        return 0;
    return recurseForever (depth + 1) + pad[0];
}
} // namespace sls::crashtest

void testSemVerAndUpdates()
{
    std::cout << "Updates: Semantic Versioning, the latest release, the update check (0.43.0)\n";
    using sls::semver::Version;
    const auto v = Version::parse ("v1.2.3-beta.2+build.7");
    supportCheck (v && v->major == 1 && v->minor == 2 && v->patch == 3 && v->preRelease.size() == 2 && v->preRelease[1] == "2" && v->build == "build.7",
           "\"v1.2.3-beta.2+build.7\" is read with its pre-release and build metadata");
    supportCheck (sls::semver::isNewer ("0.2.0", "0.1.0") && sls::semver::isNewer ("v0.43.0", "0.42.0") && ! sls::semver::isNewer ("0.42.0", "0.42.0"),
           "0.2.0 is newer than 0.1.0, v0.43.0 than 0.42.0, a version is not newer than itself");
    supportCheck (sls::semver::isNewer ("0.10.0", "0.9.9") && sls::semver::isNewer ("1.0.0", "0.99.99") && ! sls::semver::isNewer ("0.9.0", "0.10.0"),
           "numbers compare as numbers (0.10.0 after 0.9.9), major first");
    // The precedence example of semver.org, section 11.
    const char* chain[] = { "1.0.0-alpha", "1.0.0-alpha.1", "1.0.0-alpha.beta", "1.0.0-beta", "1.0.0-beta.2", "1.0.0-beta.11", "1.0.0-rc.1", "1.0.0" };
    bool ordered = true;
    for (std::size_t i = 0; i + 1 < std::size (chain); ++i)
        ordered = ordered && sls::semver::compare (chain[i], chain[i + 1]) == -1 && sls::semver::compare (chain[i + 1], chain[i]) == 1;
    supportCheck (ordered, "semver.org's order: alpha < alpha.1 < alpha.beta < beta < beta.2 < beta.11 < rc.1 < release");
    supportCheck (sls::semver::compare ("1.0.0+a", "1.0.0+b") == 0, "build metadata does not count");
    supportCheck (sls::semver::compare ("1.2", "1.2.0") == 0 && sls::semver::compare ("2", "2.0.0") == 0, "\"1.2\" reads as 1.2.0 (tags are often written so)");
    supportCheck (! Version::parse ("01.2.3") && ! Version::parse ("1.2.3.4") && ! Version::parse ("latest") && ! Version::parse ("") && ! Version::parse ("1.2.3-")
               && ! Version::parse ("1.2.3-01") && ! Version::parse ("1.x.3") && ! sls::semver::compare ("nightly", "1.0.0").has_value(),
           "leading zeros, four numbers, words, empty parts and a pre-release 01 are refused");

    const std::string release = R"({"tag_name":"v0.43.0","html_url":"https://github.com/o/r/releases/tag/v0.43.0",
        "body":"\r\n## What's new\r\n- Crash reports\r\n- Update check\r\n","assets":[
        {"name":"Sun-Loops-Studio-0.43.0-aarch64.AppImage","browser_download_url":"https://x/arm"},
        {"name":"Sun-Loops-Studio-0.43.0-x86_64.AppImage","browser_download_url":"https://x/amd64"}]})";
    const auto info = sls::updates::parseRelease (release);
    supportCheck (info && info->version == "0.43.0" && info->tag == "v0.43.0" && info->url == "https://x/amd64"
               && info->pageUrl == "https://github.com/o/r/releases/tag/v0.43.0" && info->changelog == "## What's new\n- Crash reports\n- Update check",
           "a release: version from tag_name, the x86_64 AppImage as the download, the notes without their blank first line");
    {
        const std::string both = R"({"tag_name":"v0.63.0","html_url":"https://page","assets":[
            {"name":"Sun-Loops-Studio-0.63.0-x86_64.AppImage","browser_download_url":"https://x/linux"},
            {"name":"Sun-Loops-Studio-0.63.0-macOS.dmg","browser_download_url":"https://x/mac"}]})";
        const auto onLinux = sls::updates::parseRelease (both, nullptr, "x86_64");
        const auto mac = sls::updates::parseRelease (both, nullptr, "macOS");
        const auto macOnlyLinux = sls::updates::parseRelease (release, nullptr, "macOS");
        supportCheck (onLinux && onLinux->url == "https://x/linux" && mac && mac->url == "https://x/mac" && macOnlyLinux && macOnlyLinux->url == macOnlyLinux->pageUrl,
                      "on macOS the update is the .dmg (a release without one: its page), on Linux the AppImage");
    }
    std::string why;
    supportCheck (! sls::updates::parseRelease ("{\"html_url\":\"x\"}", &why) && why.find ("tag_name") != std::string::npos
               && ! sls::updates::parseRelease ("not json") && ! sls::updates::parseRelease ("{\"tag_name\":\"nightly\",\"html_url\":\"x\"}")
               && ! sls::updates::parseRelease ("[1,2]") && ! sls::updates::parseRelease ("{\"tag_name\":5}"),
           "no tag, not JSON, a tag that is not a version, an array or a number are refused, with the reason");
    const auto noAssets = sls::updates::parseRelease (R"({"tag_name":"1.0.0","html_url":"https://page","body":null})");
    supportCheck (noAssets && noAssets->url == "https://page" && noAssets->changelog.empty(), "without an AppImage the release page is the link; a null body is no notes");
    std::string longNotes;
    for (int i = 0; i < 40; ++i)
        longNotes += "- change number " + std::to_string (i) + " with some words\n";
    const auto shortNotes = sls::updates::shortChangelog (longNotes);
    supportCheck (shortNotes.size() <= 603 && shortNotes.substr (shortNotes.size() - 3) == "..." && std::count (shortNotes.begin(), shortNotes.end(), '\n') <= 11,
           "long notes are cut to 12 lines / 600 characters with \"...\"");

    sls::http::Response ok;
    ok.status = 200;
    ok.body = release;
    supportCheck (sls::updates::evaluate (ok, "0.42.0").status == sls::updates::Status::UpdateAvailable
               && sls::updates::evaluate (ok, "0.43.0").status == sls::updates::Status::UpToDate
               && sls::updates::evaluate (ok, "0.44.0-beta.1").status == sls::updates::Status::UpToDate
               && sls::updates::evaluate (ok, "0.43.0-rc.1").status == sls::updates::Status::UpdateAvailable,
           "0.42.0 sees an update, 0.43.0 and a 0.44 beta do not, a 0.43.0 release candidate does");
    sls::http::Response notFound;
    notFound.status = 404;
    sls::http::Response offline;
    offline.error = "Could not resolve host";
    offline.offline = true;
    sls::http::Response limited;
    limited.status = 403;
    supportCheck (sls::updates::evaluate (notFound, "0.42.0").status == sls::updates::Status::NoReleases
               && sls::updates::evaluate (offline, "0.42.0").status == sls::updates::Status::Offline
               && sls::updates::evaluate (limited, "0.42.0").status == sls::updates::Status::Failed,
           "404 = no releases yet, no network = offline, 403 = the rate limit (a failure, not a crash)");

    // Against a local server: the request GitHub expects, the result through the dispatcher, the timeout, no server at all.
    {
        MiniServer server (MiniServer::reply (200, "OK", release));
        std::vector<std::function<void()>> mainQueue;
        std::mutex queueLock;
        sls::updates::UpdateChecker::Options o;
        o.currentVersion = "0.42.0";
        o.repo = *sls::github::Repo::parse ("Hubert9204/Sun-Loops-Studio");
        o.apiBase = server.base();
        sls::updates::UpdateChecker checker (o, [&] (std::function<void()> f) { const std::lock_guard<std::mutex> g (queueLock); mainQueue.push_back (std::move (f)); });
        std::atomic<bool> delivered { false };
        sls::updates::Result got;
        const auto callerThread = std::this_thread::get_id();
        std::thread::id callbackThread;
        supportCheck (checker.start ([&] (const sls::updates::Result& r) { got = r; callbackThread = std::this_thread::get_id(); delivered = true; }),
               "the check starts on its own thread");
        supportCheck (! checker.start ([] (const sls::updates::Result&) {}), "a second check does not start while one runs");
        const auto until = std::chrono::steady_clock::now() + std::chrono::seconds (5);
        while (! delivered && std::chrono::steady_clock::now() < until)
        {
            std::vector<std::function<void()>> run;
            {
                const std::lock_guard<std::mutex> g (queueLock);
                run.swap (mainQueue);
            }
            for (auto& f : run)
                f(); // what MessageManager::callAsync does in the program
            std::this_thread::sleep_for (std::chrono::milliseconds (5));
        }
        const auto request = server.received();
        supportCheck (request.rfind ("GET /repos/Hubert9204/Sun-Loops-Studio/releases/latest HTTP/1.1", 0) == 0
                   && request.find ("User-Agent: SunLoopsStudio/0.42.0") != std::string::npos
                   && request.find ("Accept: application/vnd.github+json") != std::string::npos && request.find ("Authorization") == std::string::npos,
               "GET /repos/{owner}/{repo}/releases/latest with a User-Agent and GitHub's Accept header, no token");
        // the download this computer gets: the test release has only AppImages, so on a Mac it is the release page (0.63.0)
#if defined(__APPLE__)
        const std::string expectedUrl = "https://github.com/o/r/releases/tag/v0.43.0";
#elif defined(__aarch64__)
        const std::string expectedUrl = "https://x/arm";
#else
        const std::string expectedUrl = "https://x/amd64";
#endif
        supportCheck (delivered && got.status == sls::updates::Status::UpdateAvailable && got.info.version == "0.43.0" && got.info.url == expectedUrl
                   && callbackThread == callerThread,
               "the result (0.43.0 available) arrives through the dispatcher, on the calling (main) thread");
    }
    {
        MiniServer silent ("", true);
        sls::updates::UpdateChecker::Options o;
        o.currentVersion = "0.42.0";
        o.repo = { "o", "r" };
        o.apiBase = silent.base();
        o.timeoutSeconds = 1;
        sls::updates::UpdateChecker checker (o);
        std::atomic<bool> delivered { false };
        sls::updates::Status status {};
        const auto started = std::chrono::steady_clock::now();
        checker.start ([&] (const sls::updates::Result& r) { status = r.status; delivered = true; });
        while (! delivered && std::chrono::steady_clock::now() - started < std::chrono::seconds (5))
            std::this_thread::sleep_for (std::chrono::milliseconds (5));
        const auto took = std::chrono::steady_clock::now() - started;
        supportCheck (delivered && status == sls::updates::Status::Offline && took < std::chrono::milliseconds (2500),
               "a server that never answers: \"offline\" after the timeout (1 s here, 5 s in the program)");
    }
    {
        int freePort = 0;
        {
            MiniServer probe (""); // a port that was free a moment ago and is closed again
            freePort = std::stoi (probe.base().substr (17));
        }
        sls::http::Request r;
        r.url = "http://127.0.0.1:" + std::to_string (freePort) + "/";
        const auto started = std::chrono::steady_clock::now();
        const auto response = sls::http::perform (r);
        supportCheck (response.offline && response.status == 0 && ! response.error.empty() && std::chrono::steady_clock::now() - started < std::chrono::seconds (2),
               "nothing listening: an \"offline\" answer at once, no exception");
    }
    {
        MiniServer silent ("", true);
        sls::updates::UpdateChecker::Options o;
        o.currentVersion = "0.42.0";
        o.repo = { "o", "r" };
        o.apiBase = silent.base();
        o.timeoutSeconds = 30;
        std::atomic<bool> called { false };
        const auto started = std::chrono::steady_clock::now();
        {
            sls::updates::UpdateChecker checker (o);
            checker.start ([&] (const sls::updates::Result&) { called = true; });
            std::this_thread::sleep_for (std::chrono::milliseconds (100));
        } // destroyed while waiting for the answer
        supportCheck (! called && std::chrono::steady_clock::now() - started < std::chrono::milliseconds (2500),
               "closing the program during a check: it is cancelled within about a second and no callback runs");
    }
    {
        sls::updates::UpdateChecker::Options o;
        o.currentVersion = "0.42.0";
        o.repo = { "o", "r" };
        o.enabled = false;
        sls::updates::UpdateChecker checker (o);
        supportCheck (! checker.start ([] (const sls::updates::Result&) {}) && ! checker.isRunning(), "switched off in the settings: no check, no thread");
    }
    supportCheck (sls::http::urlEncode ("a b/ç?&=~") == "a%20b%2F%C3%A7%3F%26%3D~", "URL encoding (UTF-8 bytes, reserved characters)");
}

void testCrashReports()
{
    std::cout << "Crash reports: the handler, the report, GitHub issues (0.43.0)\n";
    // Real crashes in a child process.
    {
        const auto dir = freshFolder ("segv");
        const int sig = crashInChild (dir, [] { sls::crashtest::writeThroughNull (static_cast<std::uintptr_t> (::getpid() & 0) + 16); });
        auto report = sls::crash::load (sls::crash::crashFile (dir));
        if (report)
            sls::crash::symbolize (*report, sls::crash::executablePath());
        supportCheck (sig == SIGSEGV, "a write through a null pointer still ends the program with SIGSEGV (the handler does not swallow it)");
        supportCheck (report && report->signalName == "SIGSEGV" && report->signal == SIGSEGV && report->version == "9.8.7" && report->address == "0x10"
                   && report->thread == "main" && report->time > 1700000000,
               "crash_last_session.log: signal, faulting address 0x10, version, thread and time");
        const bool named = report && report->where() == "sls::crashtest::writeThroughNull";
        if (! named && report) // the frames differ between systems: show them
        {
            std::cout << "    crash frame " << report->crashFrame << ", where " << report->where() << "\n";
            for (const auto& f : report->frames)
                std::cout << "    | " << f.raw << "  =>  " << f.symbol << " " << f.offset << "\n";
        }
        supportCheck (named, "the stack trace names the function that crashed (after the handler and libc's frames)");
        supportCheck (report && report->field ("audio_device") == "Test Card" && report->field ("sample_rate") == "48000" && report->field ("arch") != "unknown"
                   && report->field ("os") != "unknown",
               "the audio settings set beforehand and the system (OS, architecture) are in it");
    }
    {
        const auto dir = freshFolder ("abort");
        const int sig = crashInChild (dir, [] { sls::crashtest::throwUncaught (7); });
        auto report = sls::crash::load (sls::crash::crashFile (dir));
        if (report)
            sls::crash::symbolize (*report, sls::crash::executablePath());
        const bool ok = sig == SIGABRT && report && report->signalName == "SIGABRT" && report->exception == "uncaught exception: test exception 7"
                        && report->where() == "sls::crashtest::throwUncaught";
        if (! ok) // the frames differ between systems: show them
        {
            std::cout << "    signal " << sig << ", report " << (report ? "read" : "missing");
            if (report)
            {
                std::cout << ", exception \"" << report->exception << "\", crash frame " << report->crashFrame << ", where " << report->where() << "\n";
                for (const auto& f : report->frames)
                    std::cout << "    | " << f.raw << "  =>  " << f.symbol << " " << f.offset << "\n";
            }
            std::cout << "\n";
        }
        supportCheck (ok, "an uncaught exception: SIGABRT, its message, and the function that threw (std::terminate and abort skipped)");
    }
    {
        const auto dir = freshFolder ("fpe");
        const int sig = crashInChild (dir, [] { std::raise (SIGFPE); });
        auto report = sls::crash::load (sls::crash::crashFile (dir));
        if (report)
            sls::crash::symbolize (*report, sls::crash::executablePath());
        supportCheck (sig == SIGFPE && report && report->signalName == "SIGFPE" && report->description().find ("arithmetic") != std::string::npos,
               "SIGFPE is caught and named");
    }
    {
        const auto dir = freshFolder ("overflow");
        const int sig = crashInChild (dir, [] { static_cast<void> (sls::crashtest::recurseForever (0)); });
        auto report = sls::crash::load (sls::crash::crashFile (dir));
        if (report)
            sls::crash::symbolize (*report, sls::crash::executablePath());
        supportCheck (sig == SIGSEGV && report && report->where() == "sls::crashtest::recurseForever",
               "a stack overflow is caught too (the handler runs on its own stack)");
    }
    {
        const auto dir = freshFolder ("clean");
        const int sig = crashInChild (dir, [] {});
        supportCheck (sig == 0 && ! fs::exists (sls::crash::crashFile (dir)), "a normal exit leaves no crash file");
    }

    // Reading and formatting a report.
    const auto report = sls::crash::parse (kSampleCrash);
    if (! report)
    {
        supportCheck (false, "the sample crash file is read");
        return;
    }
    supportCheck (report && report->frames.size() == 5 && report->crashFrame == 2 && report->where() == "sls::Player::render"
               && report->frames[2].symbol == "sls::Player::render(juce::AudioBuffer<float>&, int)" && report->frames[2].offset == "+0x1a"
               && report->frames[3].offset == "+0x12345" && report->frames[4].address == "0x7f0b1c000000",
           "frames: demangled names, offsets, frames without a name or a module; the crash is the frame after the handler and libc");
    {
        const auto nonPie = sls::crash::parse ("SLS-CRASH 1\nsignal=6\nframes:\n./prog() [0x6b8e7c]\n/lib64/libc.so.6(gsignal+0x1e) [0x7f39e]\nend\n");
        supportCheck (nonPie && nonPie->frames.size() == 2 && nonPie->frames[0].module == "./prog" && nonPie->frames[0].symbol.empty()
                          && nonPie->frames[0].offset.empty() && nonPie->frames[0].address == "0x6b8e7c" && nonPie->frames[1].symbol == "gsignal",
                      "a non-PIE program's frame (\"prog() [0x6b8e7c]\", as on Fedora) is read with its absolute address");
    }
    supportCheck (sls::crash::withoutParameters ("sls::(anonymous namespace)::f(int) const") == "sls::(anonymous namespace)::f"
               && sls::crash::withoutParameters ("std::function<void (int)>::operator()(int) const") == "std::function<void (int)>::operator()"
               && sls::crash::withoutParameters ("plain") == "plain",
           "a function name without its parameters (anonymous namespaces and templates kept)");
    supportCheck (report && sls::crash::issueTitle (*report) == "Crash: SIGSEGV in sls::Player::render (0.42.0)", "the issue title: signal, function, version");
    {
        auto inLibc = *report;
        inLibc.frames.insert (inLibc.frames.begin() + 2, sls::crash::Frame { "/lib64/libc.so.6", "memcpy", "+0x20", "0x7f01", "" });
        inLibc.crashFrame = 2;
        supportCheck (inLibc.signature() == "memcpy, called from sls::Player::render"
                          && sls::crash::issueTitle (inLibc) == "Crash: SIGSEGV in memcpy, called from sls::Player::render (0.42.0)",
                      "a crash inside libc names the program's function that called it");
    }
    const sls::crash::Redaction who { "/home/hubert", "hubert" };
    auto withLog = *report;
    withLog.log = { "[2026-11-10 14:00:00.000] [info] Opened /home/hubert/Music/song.slp", "contains ``` fences" };
    const auto body = sls::crash::issueBody (withLog, who);
    supportCheck (body.find ("| OS | Nobara Linux 41 (KDE Plasma) |") != std::string::npos && body.find ("| Architecture | x86_64 |") != std::string::npos
               && body.find ("| Audio driver | JACK |") != std::string::npos && body.find ("| Sample rate | 48000 Hz |") != std::string::npos
               && body.find ("| Buffer size | 128 samples |") != std::string::npos && body.find ("| Audio device | Scarlett 2i2 \\| USB |") != std::string::npos,
           "the body's system table: OS, architecture, audio driver, device (a | escaped), sample rate, buffer");
    supportCheck (body.find ("### Stack trace\n\n```text\n#0  sls::crash::") != std::string::npos
               && body.find ("#2  sls::Player::render(juce::AudioBuffer<float>&, int) +0x1a  in sun-loops-studio  [0x55d0c2a10011]   <-- crashed here")
                      != std::string::npos,
           "the stack trace in a Markdown code block, the crashing frame marked");
    {
        // 0.63.0: a stack trace as macOS writes it
        const auto mac = sls::crash::parse ("SLS-CRASH 1\nversion=0.63.0\nsignal=11\nsigname=SIGSEGV\nframes:\n"
                                            "0   SunLoopsStudio                      0x0000000104f00010 _ZN3sls5crash12_GLOBAL__N_18onSignalEiP9__siginfoPv + 120\n"
                                            "1   libsystem_platform.dylib            0x000000018c8b2584 _sigtramp + 56\n"
                                            "2   SunLoopsStudio                      0x0000000104f2c8e4 _ZN3sls6Player6renderERN4juce11AudioBufferIfEEi + 26\n"
                                            "3   SunLoopsStudio                      0x0000000104f2c9e4 0x0 + 4378511844\n"
                                            "end\n");
        supportCheck (mac && mac->frames.size() == 4 && mac->frames[2].module == "SunLoopsStudio" && mac->frames[2].offset == "+0x1a"
                          && mac->frames[2].address == "0x0000000104f2c8e4" && mac->where() == "sls::Player::render" && mac->frames[3].symbol.empty(),
                      "a macOS stack trace is read: module, address, function and offset; the crash is in Player::render");
    }
    supportCheck (body.find ("hubert") == std::string::npos && body.find ("~/Music/song.slp") != std::string::npos && body.find ("~/Apps") == std::string::npos
               && body.find ("contains ''' fences") != std::string::npos,
           "the user's name and home folder are removed; ``` in the log cannot break the code block");
    const auto link = sls::github::newIssueLink ({ "Hubert9204", "Sun-Loops-Studio" }, "Crash: x", std::string (20000, 'a') + "\n" + body);
    supportCheck (link.rfind ("https://github.com/Hubert9204/Sun-Loops-Studio/issues/new?labels=crash&title=Crash%3A%20x&body=", 0) == 0 && link.size() <= 7500,
           "without a token: a GitHub \"new issue\" link, the body cut to fit");
#if defined(__linux__) // (an ELF symbol table and glibc's load map: Linux only)
    {
        // The AppImage: the main program appears as "AppRun" in the trace; the recorded program name ties it to the executable.
        // The offset glibc would print: from the load address (l_addr; 0 for a non-PIE executable).
        Dl_info where {};
        link_map* map = nullptr;
        ::dladdr1 (reinterpret_cast<void*> (&sls::crashtest::writeThroughNull), &where, reinterpret_cast<void**> (&map), RTLD_DL_LINKMAP);
        const auto offset = reinterpret_cast<std::uintptr_t> (&sls::crashtest::writeThroughNull) - (map != nullptr ? map->l_addr : 0) + 2;
        char text[32];
        std::snprintf (text, sizeof (text), "+0x%llx", static_cast<unsigned long long> (offset));
        auto appImage = *report; // the sample, read above
        appImage.fields["program"] = "/tmp/.mount_SunLoXYZ/AppRun";
        appImage.frames[3].module = "/tmp/.mount_SunLoXYZ/AppRun";
        appImage.frames[3].offset = text;
        const auto self = fs::read_symlink ("/proc/self/exe");
        const int named = sls::crash::symbolize (appImage, self);
        auto other = appImage;
        other.frames[3].module = "/somewhere/else";
        other.frames[3].symbol.clear();
        const int namedOther = sls::crash::symbolize (other, self);
        supportCheck (named == 1 && appImage.frames[3].symbol == "sls::crashtest::writeThroughNull(unsigned long)" && appImage.frames[3].offset == "+0x2"
                          && namedOther == 0 && sls::crash::issueBody (appImage, who).find ("| program |") == std::string::npos,
                      "a frame of the recorded program (AppRun in the AppImage) is named from the executable's symbol table, another file's is not");
    }
#endif
    supportCheck (! sls::crash::parse ("hello\nsignal=11") && ! sls::crash::parse ("SLS-CRASH 1\nframes:\nend\n"), "a file that is not a crash report is refused");

    // The log goes into the crash file at the next start; after sending, the file is archived.
    {
        const auto dir = freshFolder ("files");
        const auto file = sls::crash::crashFile (dir);
        std::ofstream (file) << kSampleCrash;
        const auto logLines = [&file] { const auto r = sls::crash::load (file); return r ? r->log.size() : std::size_t { 0 }; };
        supportCheck (sls::crash::attachLog (file, { "line one", "line two" }) && logLines() == 2,
               "the previous session's last log lines are added to the crash file");
        supportCheck (sls::crash::attachLog (file, { "other" }) && logLines() == 2, "only once");
        supportCheck (sls::crash::archive (file, "sent") && ! fs::exists (file) && fs::is_directory (dir / "crashes")
                   && std::distance (fs::directory_iterator (dir / "crashes"), fs::directory_iterator()) == 1,
               "after it was sent the crash file moves to crashes/ (so the question is not asked again)");
    }

    // Issues through a local server standing in for api.github.com.
    sls::github::IssueRequest issue;
    issue.repo = { "Hubert9204", "Sun-Loops-Studio" };
    issue.token = "test-token";
    issue.userAgent = "SunLoopsStudio/0.43.0";
    issue.title = "Crash: SIGSEGV in sls::Player::render (0.42.0)";
    issue.body = body;
    issue.labels = { "crash" };
    {
        MiniServer server (MiniServer::reply (201, "Created", R"({"number":17,"html_url":"https://github.com/Hubert9204/Sun-Loops-Studio/issues/17"})"));
        issue.apiBase = server.base();
        const auto result = sls::github::createIssue (issue);
        const auto request = server.received();
        supportCheck (result.created && result.number == 17 && result.url == "https://github.com/Hubert9204/Sun-Loops-Studio/issues/17" && result.status == 201,
               "201 Created: the issue's number and link");
        supportCheck (request.rfind ("POST /repos/Hubert9204/Sun-Loops-Studio/issues HTTP/1.1", 0) == 0
                   && request.find ("Authorization: Bearer test-token") != std::string::npos
                   && request.find ("Content-Type: application/json") != std::string::npos
                   && request.find ("\"title\":\"Crash: SIGSEGV in sls::Player::render (0.42.0)\"") != std::string::npos
                   && request.find ("\"labels\":[\"crash\"]") != std::string::npos && request.find ("```text") != std::string::npos,
               "POST /repos/{owner}/{repo}/issues with the token, JSON title, body (Markdown) and label");
    }
    {
        MiniServer server (MiniServer::reply (401, "Unauthorized", R"({"message":"Bad credentials"})"));
        issue.apiBase = server.base();
        const auto result = sls::github::createIssue (issue);
        supportCheck (! result.created && result.status == 401 && result.error.find ("Bad credentials") != std::string::npos,
               "a refused token: not created, the reason for the user (the crash file stays)");
    }
    {
        auto noToken = issue;
        noToken.token.clear();
        supportCheck (! sls::github::createIssue (noToken).created, "no token: nothing is sent");
    }
    supportCheck (sls::github::Repo::parse ("Hubert9204/Sun-Loops-Studio").has_value() && ! sls::github::Repo::parse ("a/b/c") && ! sls::github::Repo::parse ("/x")
               && ! sls::github::Repo::parse ("a b/c"),
           "owner/repo is checked");
}

void testHeliosDownload()
{
    std::cout << "Downloads to a file, continued after an interruption (0.64.0, Helios's models)\n";
    const auto dir = freshFolder ("download");
    std::string model = "GGUF";
    for (int i = 0; model.size() < 300000; ++i)
        model += static_cast<char> ('a' + i % 26);
    const auto raw = [] (int status, const std::string& reason, const std::string& body, const std::string& extra = {})
    {
        return "HTTP/1.1 " + std::to_string (status) + " " + reason + "\r\nContent-Type: application/octet-stream\r\nContent-Length: "
               + std::to_string (body.size()) + "\r\n" + extra + "Connection: close\r\n\r\n" + body;
    };

    {
        MiniServer server (raw (200, "OK", model));
        std::uint64_t lastDone = 0, lastTotal = 0;
        sls::http::Download d;
        d.url = server.base() + "/model.gguf";
        d.destination = dir / "models" / "model.gguf";
        d.progress = [&] (std::uint64_t done, std::uint64_t total) { lastDone = done; lastTotal = total; };
        const auto r = sls::http::download (d);
        supportCheck (r.ok() && r.status == 200 && r.bytes == model.size() && readAll (d.destination) == model
                          && ! fs::exists (dir / "models" / "model.gguf.part"),
                      "a download: the whole file, in a folder made for it, no .part file left");
        supportCheck (lastDone == model.size() && lastTotal == model.size(), "the progress ends at the file's size");
    }
    {
        MiniServer server (raw (404, "Not Found", "no such file"));
        sls::http::Download d;
        d.url = server.base() + "/missing.gguf";
        d.destination = dir / "missing.gguf";
        const auto r = sls::http::download (d);
        supportCheck (! r.ok() && r.status == 404 && r.error.find ("404") != std::string::npos && ! fs::exists (d.destination),
                      "404: an error, no file (and the error page is not saved as the model)");
        std::error_code ignored;
        fs::remove (dir / "missing.gguf.part", ignored);
    }
    {
        const auto part = dir / "resumed.gguf.part";
        { std::ofstream (part, std::ios::binary) << model.substr (0, 1000); }
        MiniServer server (raw (206, "Partial Content", model.substr (1000),
                                "Content-Range: bytes 1000-" + std::to_string (model.size() - 1) + "/" + std::to_string (model.size()) + "\r\n"));
        sls::http::Download d;
        d.url = server.base() + "/model.gguf";
        d.destination = dir / "resumed.gguf";
        const auto r = sls::http::download (d);
        supportCheck (server.received().find ("Range: bytes=1000-") != std::string::npos, "an interrupted download asks for the rest only");
        supportCheck (r.ok() && readAll (d.destination) == model && ! fs::exists (part), "and the file is whole");
    }
    {
        const auto part = dir / "restarted.gguf.part";
        { std::ofstream (part, std::ios::binary) << "something else entirely"; }
        MiniServer server (raw (200, "OK", model)); // a server that cannot continue sends everything
        sls::http::Download d;
        d.url = server.base() + "/model.gguf";
        d.destination = dir / "restarted.gguf";
        const auto r = sls::http::download (d);
        supportCheck (r.ok() && readAll (d.destination) == model, "a server that sends the whole file again: the .part file is started over");
    }
    {
        MiniServer server ("", true); // never answers
        std::atomic<bool> cancel { false };
        sls::http::Download d;
        d.url = server.base() + "/slow.gguf";
        d.destination = dir / "slow.gguf";
        d.cancel = &cancel;
        std::thread stopper ([&cancel] { std::this_thread::sleep_for (std::chrono::milliseconds (300)); cancel.store (true); });
        const auto started = std::chrono::steady_clock::now();
        const auto r = sls::http::download (d);
        stopper.join();
        supportCheck (r.cancelled && ! r.ok() && std::chrono::steady_clock::now() - started < std::chrono::seconds (5) && ! fs::exists (d.destination),
                      "cancelled: it stops within a second or so, no file");
    }
}

void testSessionLog()
{
    std::cout << "Session log: spdlog, rotating files (0.43.0)\n";
    const auto dir = freshFolder ("log");
    sls::logging::Options o;
    o.directory = dir;
    o.maxFileBytes = 4096;
    o.maxFiles = 2;
    supportCheck (sls::logging::start (o) && sls::logging::currentFile() == dir / "session.log", "the log starts in the given folder");
    for (int i = 0; i < 400; ++i)
        spdlog::info ("line {} of the rotation test with some text to fill it", i);
    spdlog::warn ("last line");
    sls::logging::stop();
    const bool rotated = fs::exists (dir / "session.log") && fs::exists (dir / "session.1.log") && fs::exists (dir / "session.2.log") && ! fs::exists (dir / "session.3.log");
    supportCheck (rotated && fs::file_size (dir / "session.log") <= 4096 && fs::file_size (dir / "session.1.log") <= 4096,
           "full files rotate: session.log, session.1.log, session.2.log, each under the limit (5 MB in the program)");
    // The last line may be alone in a just-rotated file: look at the last line, and for the one before it in either file.
    const auto last = sls::logging::lastLines (dir / "session.log", 1);
    const auto before = readAll (dir / "session.log") + readAll (dir / "session.1.log");
    supportCheck (last.size() == 1 && last[0].find ("[warning]") != std::string::npos && last[0].find ("last line") != std::string::npos
                      && last[0].rfind ("[20", 0) == 0 && before.find ("line 399 of") != std::string::npos,
                  "the last lines, with time stamp and level");
    supportCheck (readAll (dir / "session.1.log").find ("rotation test") != std::string::npos, "the older lines are in session.1.log");
    supportCheck (! sls::logging::start ({ "/proc/no-such-folder" }), "a folder that cannot be written: false, the program goes on (logging to stderr)");
    sls::logging::stop();
    std::error_code ignored;
    if (std::getenv ("SLS_KEEP_TEST_FILES") == nullptr)
        fs::remove_all (fs::temp_directory_path() / ("sls-support-test-" + std::to_string (::getpid())), ignored);
}
