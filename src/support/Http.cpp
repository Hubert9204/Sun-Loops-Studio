#include "support/Http.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <mutex>

#include <curl/curl.h>

namespace sls::http
{
namespace
{
std::once_flag globalInit;

struct Transfer
{
    std::string* body;
    std::size_t limit;
    bool tooBig = false;
};

std::size_t onData (char* data, std::size_t size, std::size_t count, void* user)
{
    auto& t = *static_cast<Transfer*> (user);
    const auto n = size * count;
    if (t.body->size() + n > t.limit)
    {
        t.tooBig = true;
        return 0; // stops the transfer
    }
    t.body->append (data, n);
    return n;
}

int onProgress (void* user, curl_off_t, curl_off_t, curl_off_t, curl_off_t)
{
    const auto* cancel = static_cast<const std::atomic<bool>*> (user);
    return cancel != nullptr && cancel->load() ? 1 : 0;
}

bool isOffline (CURLcode code) noexcept
{
    return code == CURLE_COULDNT_RESOLVE_HOST || code == CURLE_COULDNT_RESOLVE_PROXY || code == CURLE_COULDNT_CONNECT
           || code == CURLE_OPERATION_TIMEDOUT || code == CURLE_SEND_ERROR || code == CURLE_RECV_ERROR || code == CURLE_GOT_NOTHING;
}
} // namespace

std::string caBundle()
{
    for (const char* variable : { "CURL_CA_BUNDLE", "SSL_CERT_FILE" })
        if (const char* value = std::getenv (variable); value != nullptr && *value != '\0' && std::filesystem::exists (value))
            return value;
    for (const char* path : { "/etc/pki/tls/certs/ca-bundle.crt",   // Fedora, Nobara, RHEL
                              "/etc/ssl/certs/ca-certificates.crt", // Debian, Ubuntu, Arch, Gentoo
                              "/etc/ssl/ca-bundle.pem",             // openSUSE
                              "/etc/pki/tls/cacert.pem",            // OpenELEC
                              "/etc/ssl/cert.pem" })                // Alpine, Void
    {
        std::error_code error;
        if (std::filesystem::exists (path, error))
            return path;
    }
    return {};
}

std::string urlEncode (std::string_view text)
{
    static const char* const hex = "0123456789ABCDEF";
    std::string out;
    out.reserve (text.size() * 3);
    for (const char ch : text)
    {
        const auto c = static_cast<unsigned char> (ch);
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~')
            out += static_cast<char> (c);
        else
        {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 0xF];
        }
    }
    return out;
}

Response perform (const Request& request)
{
    std::call_once (globalInit, [] { curl_global_init (CURL_GLOBAL_DEFAULT); });
    Response response;
    CURL* curl = curl_easy_init();
    if (curl == nullptr)
    {
        response.error = "libcurl could not start";
        return response;
    }

    Transfer transfer { &response.body, request.maxResponseBytes };
    char errorBuffer[CURL_ERROR_SIZE] {};
    curl_slist* headers = nullptr;
    for (const auto& h : request.headers)
        headers = curl_slist_append (headers, h.c_str());

    curl_easy_setopt (curl, CURLOPT_URL, request.url.c_str());
    curl_easy_setopt (curl, CURLOPT_NOSIGNAL, 1L); // threads: no SIGALRM for DNS timeouts
    curl_easy_setopt (curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt (curl, CURLOPT_MAXREDIRS, 3L);
#if LIBCURL_VERSION_NUM >= 0x075500
    curl_easy_setopt (curl, CURLOPT_PROTOCOLS_STR, "https,http");
    curl_easy_setopt (curl, CURLOPT_REDIR_PROTOCOLS_STR, "https");
#endif
    curl_easy_setopt (curl, CURLOPT_TIMEOUT, request.timeoutSeconds);
    curl_easy_setopt (curl, CURLOPT_CONNECTTIMEOUT, request.connectTimeoutSeconds);
    curl_easy_setopt (curl, CURLOPT_WRITEFUNCTION, onData);
    curl_easy_setopt (curl, CURLOPT_WRITEDATA, &transfer);
    curl_easy_setopt (curl, CURLOPT_ERRORBUFFER, errorBuffer);
    curl_easy_setopt (curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt (curl, CURLOPT_XFERINFOFUNCTION, onProgress);
    curl_easy_setopt (curl, CURLOPT_XFERINFODATA, const_cast<std::atomic<bool>*> (request.cancel));
    curl_easy_setopt (curl, CURLOPT_ACCEPT_ENCODING, ""); // gzip if the server likes
    if (const auto ca = caBundle(); ! ca.empty())
        curl_easy_setopt (curl, CURLOPT_CAINFO, ca.c_str());
    if (request.method == "POST")
    {
        headers = curl_slist_append (headers, "Expect:"); // no "100-continue" round trip before the body
        curl_easy_setopt (curl, CURLOPT_POST, 1L);
        curl_easy_setopt (curl, CURLOPT_POSTFIELDS, request.body.c_str());
        curl_easy_setopt (curl, CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t> (request.body.size()));
    }

    curl_easy_setopt (curl, CURLOPT_HTTPHEADER, headers); // after "Expect:" was added: the list's head may have changed
    const auto code = curl_easy_perform (curl);
    curl_easy_getinfo (curl, CURLINFO_RESPONSE_CODE, &response.status);
    if (code != CURLE_OK)
    {
        response.cancelled = code == CURLE_ABORTED_BY_CALLBACK;
        response.offline = isOffline (code);
        if (transfer.tooBig)
            response.error = "the answer was too long";
        else if (response.cancelled)
            response.error = "cancelled";
        else
            response.error = errorBuffer[0] != '\0' ? std::string (errorBuffer) : std::string (curl_easy_strerror (code));
        response.status = 0;
    }
    curl_slist_free_all (headers);
    curl_easy_cleanup (curl);
    return response;
}
namespace
{
struct FileTransfer
{
    std::FILE* file = nullptr;
    std::filesystem::path path;
    CURL* curl = nullptr;
    std::uint64_t offset = 0;  // bytes that were there before this request
    std::uint64_t written = 0; // bytes this request wrote
    bool checked = false;      // the answer's status was looked at
    bool restarted = false;    // the server sent the whole file again: the .part file was emptied
    bool failed = false;       // the disk refused
    const Download* request = nullptr;
    std::chrono::steady_clock::time_point lastReport {};
};

std::size_t onFileData (char* data, std::size_t size, std::size_t count, void* user)
{
    auto& t = *static_cast<FileTransfer*> (user);
    const auto n = size * count;
    if (! t.checked)
    {
        t.checked = true;
        long status = 0;
        curl_easy_getinfo (t.curl, CURLINFO_RESPONSE_CODE, &status);
        if (t.offset > 0 && status == 200) // asked to continue, got the whole file: start the .part file again
        {
            std::fclose (t.file);
            t.file = std::fopen (t.path.string().c_str(), "wb");
            t.offset = 0;
            t.restarted = true;
            if (t.file == nullptr)
            {
                t.failed = true;
                return 0;
            }
        }
    }
    if (std::fwrite (data, 1, n, t.file) != n)
    {
        t.failed = true;
        return 0;
    }
    t.written += n;
    return n;
}

int onFileProgress (void* user, curl_off_t total, curl_off_t now, curl_off_t, curl_off_t)
{
    auto& t = *static_cast<FileTransfer*> (user);
    if (t.request->cancel != nullptr && t.request->cancel->load())
        return 1;
    const auto clock = std::chrono::steady_clock::now();
    if (t.request->progress && clock - t.lastReport >= std::chrono::milliseconds (200))
    {
        t.lastReport = clock;
        t.request->progress (t.offset + static_cast<std::uint64_t> (now), total > 0 ? t.offset + static_cast<std::uint64_t> (total) : 0);
    }
    return 0;
}
} // namespace

DownloadResult download (const Download& request)
{
    std::call_once (globalInit, [] { curl_global_init (CURL_GLOBAL_DEFAULT); });
    DownloadResult result;
    namespace fs = std::filesystem;
    std::error_code fsError;
    auto part = request.destination;
    part += ".part";
    if (request.destination.has_parent_path())
        fs::create_directories (request.destination.parent_path(), fsError);

    for (int attempt = 0; attempt < 2; ++attempt) // the second time without continuing (a .part file the server refused)
    {
        FileTransfer t;
        t.path = part;
        t.request = &request;
        t.offset = fs::exists (part, fsError) ? static_cast<std::uint64_t> (fs::file_size (part, fsError)) : 0;
        t.file = std::fopen (part.string().c_str(), t.offset > 0 ? "ab" : "wb");
        if (t.file == nullptr)
        {
            result.error = "cannot write " + part.string();
            return result;
        }
        CURL* curl = curl_easy_init();
        if (curl == nullptr)
        {
            std::fclose (t.file);
            result.error = "libcurl could not start";
            return result;
        }
        t.curl = curl;
        char errorBuffer[CURL_ERROR_SIZE] {};
        curl_easy_setopt (curl, CURLOPT_URL, request.url.c_str());
        curl_easy_setopt (curl, CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt (curl, CURLOPT_FOLLOWLOCATION, 1L); // (Hugging Face sends the file from its storage, elsewhere)
        curl_easy_setopt (curl, CURLOPT_MAXREDIRS, 5L);
#if LIBCURL_VERSION_NUM >= 0x075500
        curl_easy_setopt (curl, CURLOPT_PROTOCOLS_STR, "https,http");
        curl_easy_setopt (curl, CURLOPT_REDIR_PROTOCOLS_STR, "https");
#endif
        curl_easy_setopt (curl, CURLOPT_FAILONERROR, 1L); // an error page is not written into the file
        curl_easy_setopt (curl, CURLOPT_CONNECTTIMEOUT, request.connectTimeoutSeconds);
        curl_easy_setopt (curl, CURLOPT_LOW_SPEED_LIMIT, 1024L);
        curl_easy_setopt (curl, CURLOPT_LOW_SPEED_TIME, request.stallSeconds);
        curl_easy_setopt (curl, CURLOPT_WRITEFUNCTION, onFileData);
        curl_easy_setopt (curl, CURLOPT_WRITEDATA, &t);
        curl_easy_setopt (curl, CURLOPT_ERRORBUFFER, errorBuffer);
        curl_easy_setopt (curl, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt (curl, CURLOPT_XFERINFOFUNCTION, onFileProgress);
        curl_easy_setopt (curl, CURLOPT_XFERINFODATA, &t);
        // A plain Range header rather than libcurl's "resume", which gives up when the server sends the whole file instead:
        // here that just starts the .part file over (onFileData).
        const auto range = std::to_string (t.offset) + "-";
        if (t.offset > 0)
            curl_easy_setopt (curl, CURLOPT_RANGE, range.c_str());
        if (const auto ca = caBundle(); ! ca.empty())
            curl_easy_setopt (curl, CURLOPT_CAINFO, ca.c_str());

        const auto code = curl_easy_perform (curl);
        curl_easy_getinfo (curl, CURLINFO_RESPONSE_CODE, &result.status);
        curl_easy_cleanup (curl);
        if (t.file != nullptr)
            std::fclose (t.file);
        result.bytes = t.offset + t.written;

        if (code == CURLE_OK)
        {
            if (request.progress)
                request.progress (result.bytes, result.bytes);
            fs::rename (part, request.destination, fsError);
            if (fsError)
                result.error = "cannot rename " + part.string() + ": " + fsError.message();
            return result;
        }
        // 416: the .part file is not a beginning of this file (or is all of it, but that cannot be told): start again.
        if (attempt == 0 && t.offset > 0 && result.status == 416)
        {
            fs::remove (part, fsError);
            continue;
        }
        result.cancelled = code == CURLE_ABORTED_BY_CALLBACK && ! t.failed;
        result.offline = isOffline (code);
        if (t.failed)
            result.error = "cannot write " + part.string() + " (is the disk full?)";
        else if (result.cancelled)
            result.error = "cancelled";
        else if (code == CURLE_HTTP_RETURNED_ERROR && result.status > 0)
            result.error = "the server answered " + std::to_string (result.status);
        else
            result.error = errorBuffer[0] != '\0' ? std::string (errorBuffer) : std::string (curl_easy_strerror (code));
        return result;
    }
    return result;
}
} // namespace sls::http
