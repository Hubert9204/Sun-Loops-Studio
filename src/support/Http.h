#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace sls::http
{
// A small blocking HTTPS client on libcurl (0.43.0), for the update check and crash reports. Call it from a worker thread,
// never from the message or audio thread. Proxies come from the usual environment variables (https_proxy, no_proxy); the
// certificate store is found on Fedora / Nobara, Debian / Ubuntu, Arch and openSUSE even when libcurl was built for another one
// (the AppImage). Plain C++ (no JUCE), tested against a local server.
struct Request
{
    std::string method = "GET"; // GET or POST
    std::string url;
    std::vector<std::string> headers; // "Name: value"
    std::string body;                 // sent with POST
    long timeoutSeconds = 5;          // the whole request
    long connectTimeoutSeconds = 5;
    std::size_t maxResponseBytes = 2 * 1024 * 1024;
    const std::atomic<bool>* cancel = nullptr; // set to true to stop the request (within about a second)
};

struct Response
{
    long status = 0;      // the HTTP status, 0 when no answer came
    std::string body;
    std::string error;    // empty when an answer came (whatever its status)
    bool offline = false; // no network, no DNS, refused, timed out: nothing to tell the user beyond "not reachable"
    bool cancelled = false;

    bool ok() const noexcept { return error.empty() && status >= 200 && status < 300; }
};

Response perform (const Request& request);

// The CA certificate bundle used (CURL_CA_BUNDLE or SSL_CERT_FILE when set, else the first that exists among the distributions'
// usual places); empty means libcurl's own default.
std::string caBundle();

// A big file downloaded to disk (0.64.0: Helios's language models, a few GB). It is written to "<destination>.part" and renamed to
// `destination` only when complete; a ".part" left by an interrupted download is continued where it stopped (HTTP Range), or
// started again when the server cannot continue it. Blocking: call it from a worker thread.
struct Download
{
    std::string url;
    std::filesystem::path destination;
    // Bytes so far and in all (0 when the server does not say), at most a few times a second, on the downloading thread.
    std::function<void (std::uint64_t done, std::uint64_t total)> progress;
    const std::atomic<bool>* cancel = nullptr; // set to true to stop (the .part file stays, for continuing later)
    long connectTimeoutSeconds = 20;
    long stallSeconds = 60; // gives up when less than 1 KB/s arrives for this long
};

struct DownloadResult
{
    long status = 0;          // the last HTTP status
    std::string error;        // empty when the file is complete
    bool offline = false;
    bool cancelled = false;
    std::uint64_t bytes = 0;  // the size of the file (or of the .part file)

    bool ok() const noexcept { return error.empty(); }
};

DownloadResult download (const Download& request);

// Percent-encoding for URL query values (RFC 3986 unreserved characters are kept).
std::string urlEncode (std::string_view text);
} // namespace sls::http
