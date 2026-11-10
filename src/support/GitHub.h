#pragma once

#include <atomic>
#include <optional>
#include <string>
#include <vector>

namespace sls::github
{
// The two GitHub REST API calls the program makes (0.43.0): creating an issue for a crash report (POST
// /repos/{owner}/{repo}/issues, with a token) and reading the latest release (UpdateChecker.h, no token). Plain C++, tested
// against a local server.
constexpr const char* kApiBase = "https://api.github.com";

struct Repo
{
    std::string owner, name;
    static std::optional<Repo> parse (const std::string& ownerSlashName); // "Hubert9204/Sun-Loops-Studio"
    std::string path() const { return owner + "/" + name; }
};

// Accept, API version and User-Agent (GitHub refuses requests without one), plus Authorization when a token is given.
std::vector<std::string> headers (const std::string& userAgent, const std::string& token = {});

struct IssueResult
{
    bool created = false;
    int number = 0;
    std::string url;   // the issue's page
    long status = 0;   // the HTTP status (201 when created)
    bool offline = false;
    std::string error; // why it failed, for the user
};

struct IssueRequest
{
    std::string apiBase = kApiBase;
    Repo repo;
    std::string token;
    std::string userAgent;
    std::string title, body;
    std::vector<std::string> labels;
    long timeoutSeconds = 20;
    const std::atomic<bool>* cancel = nullptr;
};

IssueResult createIssue (const IssueRequest& request);

// A link to GitHub's "new issue" page with the title and body filled in, for when no token is set: the user sends it from the
// browser. The body is cut (and says so) to keep the link under `maxLength` characters.
std::string newIssueLink (const Repo& repo, const std::string& title, const std::string& body, std::size_t maxLength = 7500);
} // namespace sls::github
