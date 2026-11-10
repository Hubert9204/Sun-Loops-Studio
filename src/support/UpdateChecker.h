#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

#include "support/GitHub.h"
#include "support/Http.h"

namespace sls::updates
{
// The update check (0.43.0): GET /repos/{owner}/{repo}/releases/latest on a worker thread, with a short timeout, compared with
// the running version by Semantic Versioning. No network, a timeout, a repository without releases or a strange answer are all
// ordinary results, never an exception or a wait on the message or audio thread. Plain C++ (no JUCE), tested against a local
// server.

struct UpdateInfo
{
    std::string version;   // "0.43.0" (the tag without its "v")
    std::string tag;       // "v0.43.0"
    std::string url;       // where to get it: the AppImage for this processor when the release has one, else the release page
    std::string pageUrl;   // the release page (html_url)
    std::string changelog; // the start of the release notes (body), shortened
};

enum class Status
{
    UpdateAvailable, // info holds the new version
    UpToDate,
    NoReleases,      // the repository has no published release yet (404)
    Offline,         // no network, or no answer within the timeout
    Failed,          // an answer that could not be used (message says why)
    Cancelled
};

struct Result
{
    Status status = Status::Failed;
    UpdateInfo info;     // filled whenever a release was read
    std::string message; // for the user, in English
};

// tag_name, html_url, body and the assets of a "latest release" answer. Nothing (and `error`) when the JSON is not usable.
std::optional<UpdateInfo> parseRelease (std::string_view json, std::string* error = nullptr, std::string_view arch = "x86_64");

// The first lines of release notes: Markdown headings and emphasis kept as they are, at most `maxLines` lines and `maxChars`
// characters (cut at a word, with "..." when cut).
std::string shortChangelog (std::string_view body, std::size_t maxChars = 600, std::size_t maxLines = 12);

// What an answer to the request means for `currentVersion`.
Result evaluate (const http::Response& response, std::string_view currentVersion, std::string_view arch = "x86_64");

class UpdateChecker
{
public:
    struct Options
    {
        std::string currentVersion;               // "0.42.0"
        github::Repo repo;
        std::string apiBase = github::kApiBase;
        std::string userAgent;                    // "SunLoopsStudio/0.42.0"
        long timeoutSeconds = 5;
        bool enabled = true;                      // the setting "check for updates": false = start() does nothing
    };

    // Runs a function on the main (message) thread: the result is delivered through it. Without one the callback runs on the
    // worker thread.
    using Dispatcher = std::function<void (std::function<void()>)>;
    using Callback = std::function<void (const Result&)>;

    explicit UpdateChecker (Options options, Dispatcher dispatcher = {});
    ~UpdateChecker(); // cancels a check in progress and waits for its thread (at most about a second); no callback after it

    UpdateChecker (const UpdateChecker&) = delete;
    UpdateChecker& operator= (const UpdateChecker&) = delete;

    // Starts a check. Returns false (and calls nothing) when checking is switched off or a check is already running.
    bool start (Callback callback);
    bool isRunning() const noexcept { return running.load(); }
    void setEnabled (bool shouldCheck) noexcept { enabled.store (shouldCheck); }
    bool isEnabled() const noexcept { return enabled.load(); }

private:
    Options options;
    Dispatcher dispatcher;
    std::atomic<bool> enabled;
    std::atomic<bool> running { false };
    std::atomic<bool> cancel { false };
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);
    std::mutex threadLock;
    std::thread worker;
};
} // namespace sls::updates
