#pragma once

#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace spdlog
{
class logger;
}

namespace sls::logging
{
// The session log (0.43.0), written with spdlog to <directory>/session.log and rotated when it reaches `maxFileBytes`
// (session.1.log is the previous file, up to session.<maxFiles>.log). Messages at warning level or above are written to disk at
// once, the rest every two seconds. Never call it from the audio thread. Plain C++ (no JUCE), tested.
struct Options
{
    std::filesystem::path directory;
    std::size_t maxFileBytes = 5 * 1024 * 1024; // 5 MB
    std::size_t maxFiles = 3;
    std::string loggerName = "session";
};

// The folder for the program's own state - the session log and crash files: $XDG_STATE_HOME/SunLoopsStudio, by default
// ~/.local/state/SunLoopsStudio. Created when missing.
std::filesystem::path stateDirectory();

// Starts the log and makes it spdlog's default logger (so spdlog::info (...) etc. write to it). Returns false, and logs to
// stderr only, when the folder cannot be written. Calling it again restarts the log with the new options.
bool start (const Options& options);

// Flushes and closes the log; spdlog's default logger writes to stderr again.
void stop();

// The file being written (empty before start).
std::filesystem::path currentFile();

// The last `count` lines of a text file (fewer if it is shorter; nothing if it cannot be read). Reads at most the last 256 KB.
std::vector<std::string> lastLines (const std::filesystem::path& file, std::size_t count);
} // namespace sls::logging
