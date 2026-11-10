#include "support/GitHub.h"

#include <algorithm>

#include <nlohmann/json.hpp>

#include "support/Http.h"

namespace sls::github
{
std::optional<Repo> Repo::parse (const std::string& text)
{
    const auto slash = text.find ('/');
    if (slash == std::string::npos || slash == 0 || slash + 1 >= text.size() || text.find ('/', slash + 1) != std::string::npos)
        return std::nullopt;
    const auto valid = [] (const std::string& part)
    {
        for (const char c : part)
            if (! ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.'))
                return false;
        return ! part.empty();
    };
    Repo r { text.substr (0, slash), text.substr (slash + 1) };
    if (! valid (r.owner) || ! valid (r.name))
        return std::nullopt;
    return r;
}

std::vector<std::string> headers (const std::string& userAgent, const std::string& token)
{
    std::vector<std::string> h { "Accept: application/vnd.github+json", "X-GitHub-Api-Version: 2022-11-28",
                                 "User-Agent: " + (userAgent.empty() ? std::string ("SunLoopsStudio") : userAgent) };
    if (! token.empty())
        h.push_back ("Authorization: Bearer " + token);
    return h;
}

IssueResult createIssue (const IssueRequest& request)
{
    IssueResult result;
    if (request.token.empty())
    {
        result.error = "no GitHub token is set";
        return result;
    }
    nlohmann::json payload { { "title", request.title }, { "body", request.body } };
    if (! request.labels.empty())
        payload["labels"] = request.labels;

    http::Request r;
    r.method = "POST";
    r.url = request.apiBase + "/repos/" + request.repo.path() + "/issues";
    r.headers = headers (request.userAgent, request.token);
    r.headers.emplace_back ("Content-Type: application/json");
    r.body = payload.dump (-1, ' ', false, nlohmann::json::error_handler_t::replace);
    r.timeoutSeconds = request.timeoutSeconds;
    r.connectTimeoutSeconds = std::min (10L, request.timeoutSeconds);
    r.cancel = request.cancel;
    const auto response = http::perform (r);
    result.status = response.status;
    result.offline = response.offline;

    if (! response.error.empty())
    {
        result.error = response.offline ? "GitHub could not be reached (" + response.error + ")" : response.error;
        return result;
    }
    if (response.status == 201)
    {
        const auto answer = nlohmann::json::parse (response.body, nullptr, false);
        if (answer.is_object())
        {
            if (const auto n = answer.find ("number"); n != answer.end() && n->is_number_integer())
                result.number = n->get<int>();
            if (const auto u = answer.find ("html_url"); u != answer.end() && u->is_string())
                result.url = u->get<std::string>();
        }
        result.created = true;
        return result;
    }

    std::string message;
    if (const auto answer = nlohmann::json::parse (response.body, nullptr, false); answer.is_object())
        if (const auto m = answer.find ("message"); m != answer.end() && m->is_string())
            message = m->get<std::string>();
    switch (response.status)
    {
        case 401: result.error = "GitHub refused the token (401: wrong or expired)"; break;
        case 403: result.error = "the token may not create issues in " + request.repo.path() + ", or the limit of requests was reached (403)"; break;
        case 404: result.error = "the repository " + request.repo.path() + " was not found, or the token cannot see it (404)"; break;
        case 410: result.error = "issues are switched off in " + request.repo.path() + " (410)"; break;
        case 422: result.error = "GitHub did not accept the report (422)"; break;
        default:  result.error = "GitHub answered " + std::to_string (response.status); break;
    }
    if (! message.empty())
        result.error += ": " + message;
    return result;
}

std::string newIssueLink (const Repo& repo, const std::string& title, const std::string& body, std::size_t maxLength)
{
    const auto base = "https://github.com/" + repo.path() + "/issues/new?labels=crash&title=" + http::urlEncode (title) + "&body=";
    auto encoded = http::urlEncode (body);
    if (base.size() + encoded.size() <= maxLength)
        return base + encoded;

    // Cut the body (at a line) until it fits, and close the code block it was cut in.
    const std::string note = "\n```\n_(cut to fit in a link: the full report is in the crash file)_\n";
    auto cut = body;
    while (! cut.empty())
    {
        const auto keep = cut.size() > 400 ? cut.size() - 400 : 0;
        const auto nl = cut.rfind ('\n', keep);
        cut.resize (nl == std::string::npos ? keep : nl);
        encoded = http::urlEncode (cut + note);
        if (base.size() + encoded.size() <= maxLength)
            return base + encoded;
    }
    return base + http::urlEncode (note);
}
} // namespace sls::github
