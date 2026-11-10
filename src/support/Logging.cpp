#include "support/Logging.h"

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <mutex>

#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

namespace sls::logging
{
namespace
{
std::mutex lock;
std::filesystem::path file;
} // namespace

std::filesystem::path stateDirectory()
{
    std::filesystem::path base;
    if (const char* xdg = std::getenv ("XDG_STATE_HOME"); xdg != nullptr && *xdg == '/')
        base = xdg;
    else if (const char* home = std::getenv ("HOME"); home != nullptr && *home != '\0')
        base = std::filesystem::path (home) / ".local" / "state";
    else
        base = std::filesystem::temp_directory_path();
    auto dir = base / "SunLoopsStudio";
    std::error_code ignored;
    std::filesystem::create_directories (dir, ignored);
    return dir;
}

bool start (const Options& options)
{
    const std::lock_guard<std::mutex> guard (lock);
    spdlog::shutdown(); // a previous log (and its flushing thread) ends here
    file.clear();
    try
    {
        std::filesystem::create_directories (options.directory);
        const auto path = options.directory / "session.log";
        auto sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt> (path.string(), options.maxFileBytes,
                                                                            std::max<std::size_t> (1, options.maxFiles));
        auto logger = std::make_shared<spdlog::logger> (options.loggerName, std::move (sink));
        logger->set_pattern ("[%Y-%m-%d %H:%M:%S.%e] [%l] [thread %t] %v");
        logger->set_level (spdlog::level::info);
        logger->flush_on (spdlog::level::warn);
        spdlog::set_default_logger (logger);
        spdlog::flush_every (std::chrono::seconds (2));
        file = path;
        return true;
    }
    catch (const std::exception& e)
    {
        auto fallback = spdlog::stderr_color_mt (options.loggerName + "-stderr");
        spdlog::set_default_logger (fallback);
        spdlog::warn ("The session log could not be opened in {}: {}", options.directory.string(), e.what());
        return false;
    }
}

void stop()
{
    const std::lock_guard<std::mutex> guard (lock);
    if (auto logger = spdlog::default_logger())
        logger->flush();
    spdlog::shutdown();
    file.clear();
}

std::filesystem::path currentFile()
{
    const std::lock_guard<std::mutex> guard (lock);
    return file;
}

std::vector<std::string> lastLines (const std::filesystem::path& path, std::size_t count)
{
    std::vector<std::string> lines;
    std::ifstream in (path, std::ios::binary);
    if (! in || count == 0)
        return lines;
    in.seekg (0, std::ios::end);
    const auto size = static_cast<long long> (in.tellg());
    constexpr long long kMaxRead = 256LL * 1024;
    const auto from = std::max (0LL, size - kMaxRead);
    in.seekg (from);
    std::string text (static_cast<std::size_t> (size - from), '\0');
    in.read (text.data(), static_cast<std::streamsize> (text.size()));
    text.resize (static_cast<std::size_t> (in.gcount()));

    std::size_t start = 0;
    if (from > 0) // the first line read is probably cut: skip it
        if (const auto nl = text.find ('\n'); nl != std::string::npos)
            start = nl + 1;
    while (start < text.size())
    {
        auto end = text.find ('\n', start);
        if (end == std::string::npos)
            end = text.size();
        auto line = text.substr (start, end - start);
        if (! line.empty() && line.back() == '\r')
            line.pop_back();
        lines.push_back (std::move (line));
        start = end + 1;
    }
    if (lines.size() > count)
        lines.erase (lines.begin(), lines.end() - static_cast<std::ptrdiff_t> (count));
    return lines;
}
} // namespace sls::logging
