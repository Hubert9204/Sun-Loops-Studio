#include "support/UpdateChecker.h"

#include <nlohmann/json.hpp>

#include "support/SemVer.h"

namespace sls::updates
{
namespace
{
std::string stringField (const nlohmann::json& object, const char* key)
{
    const auto it = object.find (key);
    return it != object.end() && it->is_string() ? it->get<std::string>() : std::string();
}

bool endsWith (const std::string& s, const std::string& suffix)
{
    return s.size() >= suffix.size() && s.compare (s.size() - suffix.size(), suffix.size(), suffix) == 0;
}
} // namespace

std::optional<UpdateInfo> parseRelease (std::string_view json, std::string* error, std::string_view arch)
{
    const auto fail = [error] (const std::string& why) -> std::optional<UpdateInfo>
    {
        if (error != nullptr)
            *error = why;
        return std::nullopt;
    };
    const auto root = nlohmann::json::parse (json, nullptr, false);
    if (root.is_discarded() || ! root.is_object())
        return fail ("the answer is not a JSON object");

    UpdateInfo info;
    info.tag = stringField (root, "tag_name");
    if (info.tag.empty())
        return fail ("the release has no tag_name");
    const auto version = semver::Version::parse (info.tag);
    if (! version)
        return fail ("the release's tag \"" + info.tag + "\" is not a version number");
    info.version = version->toString();
    info.pageUrl = stringField (root, "html_url");
    info.url = info.pageUrl;
    info.changelog = shortChangelog (stringField (root, "body"));

    if (const auto assets = root.find ("assets"); assets != root.end() && assets->is_array())
        for (const auto& asset : *assets)
        {
            if (! asset.is_object())
                continue;
            const auto name = stringField (asset, "name");
            const auto link = stringField (asset, "browser_download_url");
            if (! link.empty() && endsWith (name, ".AppImage") && name.find (std::string (arch)) != std::string::npos)
            {
                info.url = link;
                break;
            }
        }
    if (info.url.empty())
        return fail ("the release has no link");
    return info;
}

std::string shortChangelog (std::string_view body, std::size_t maxChars, std::size_t maxLines)
{
    std::string out;
    std::size_t lines = 0;
    std::size_t start = 0;
    bool cut = false;
    while (start <= body.size())
    {
        auto end = body.find ('\n', start);
        if (end == std::string_view::npos)
            end = body.size();
        auto line = std::string (body.substr (start, end - start));
        if (! line.empty() && line.back() == '\r')
            line.pop_back();
        start = end + 1;
        if (out.empty() && line.find_first_not_of (" \t") == std::string::npos)
            continue; // leading blank lines
        if (lines == maxLines)
        {
            cut = true;
            break;
        }
        out += (lines > 0 ? "\n" : "") + line;
        ++lines;
        if (end == body.size())
            break;
    }
    while (! out.empty() && (out.back() == '\n' || out.back() == ' '))
        out.pop_back();
    if (out.size() > maxChars)
    {
        auto at = out.rfind (' ', maxChars);
        if (at == std::string::npos || at < maxChars / 2)
            at = maxChars;
        out.resize (at);
        cut = true;
    }
    if (cut)
        out += "...";
    return out;
}

Result evaluate (const http::Response& response, std::string_view currentVersion, std::string_view arch)
{
    Result r;
    if (response.cancelled)
    {
        r.status = Status::Cancelled;
        r.message = "The update check was cancelled.";
        return r;
    }
    if (! response.error.empty())
    {
        r.status = response.offline ? Status::Offline : Status::Failed;
        r.message = response.offline ? "GitHub could not be reached (" + response.error + ")."
                                     : "The update check failed: " + response.error + ".";
        return r;
    }
    if (response.status == 404)
    {
        r.status = Status::NoReleases;
        r.message = "No release has been published yet.";
        return r;
    }
    if (response.status == 403 || response.status == 429)
    {
        r.status = Status::Failed;
        r.message = "GitHub's limit of requests was reached; the check will be tried again next time.";
        return r;
    }
    if (response.status != 200)
    {
        r.status = Status::Failed;
        r.message = "GitHub answered " + std::to_string (response.status) + ".";
        return r;
    }

    std::string error;
    const auto info = parseRelease (response.body, &error, arch);
    if (! info)
    {
        r.status = Status::Failed;
        r.message = "The latest release could not be read: " + error + ".";
        return r;
    }
    r.info = *info;
    const auto order = semver::compare (info->version, currentVersion);
    if (! order)
    {
        r.status = Status::Failed;
        r.message = "This version (\"" + std::string (currentVersion) + "\") is not a version number.";
        return r;
    }
    r.status = *order > 0 ? Status::UpdateAvailable : Status::UpToDate;
    r.message = *order > 0 ? "Sun Loops Studio " + info->version + " is available (this is " + std::string (currentVersion) + ")."
                           : "Sun Loops Studio " + std::string (currentVersion) + " is the latest version.";
    return r;
}

UpdateChecker::UpdateChecker (Options o, Dispatcher d)
    : options (std::move (o)), dispatcher (std::move (d)), enabled (options.enabled)
{
}

UpdateChecker::~UpdateChecker()
{
    alive->store (false);
    cancel.store (true);
    const std::lock_guard<std::mutex> guard (threadLock);
    if (worker.joinable())
        worker.join();
}

bool UpdateChecker::start (Callback callback)
{
    if (! enabled.load())
        return false;
    const std::lock_guard<std::mutex> guard (threadLock);
    if (running.exchange (true))
        return false;
    if (worker.joinable())
        worker.join(); // the previous check, already finished
    cancel.store (false);

    worker = std::thread ([this, callback = std::move (callback), aliveFlag = alive]
    {
        http::Request request;
        request.url = options.apiBase + "/repos/" + options.repo.path() + "/releases/latest";
        request.headers = github::headers (options.userAgent.empty() ? "SunLoopsStudio/" + options.currentVersion : options.userAgent);
        request.timeoutSeconds = options.timeoutSeconds;
        request.connectTimeoutSeconds = options.timeoutSeconds;
        request.maxResponseBytes = std::size_t { 1024 } * 1024;
        request.cancel = &cancel;
        auto result = evaluate (http::perform (request), options.currentVersion);
        running.store (false);
        if (! callback || ! aliveFlag->load())
            return;
        if (dispatcher)
            dispatcher ([aliveFlag, callback, result = std::move (result)]
            {
                if (aliveFlag->load())
                    callback (result);
            });
        else
            callback (result);
    });
    return true;
}
} // namespace sls::updates
