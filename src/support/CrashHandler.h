#pragma once

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace sls::crash
{
// The in-process crash handler (0.43.0). When the program receives SIGSEGV, SIGABRT, SIGFPE, SIGILL or SIGBUS, the handler -
// using only async-signal-safe calls, on its own stack so a stack overflow is caught too - writes the signal, the faulting
// address, the thread, the version, the system and audio settings and the stack trace (glibc's backtrace) to
// <directory>/crash_last_session.log, then lets the signal end the program as it would have. An uncaught C++ exception is noted
// (its what()) before std::terminate aborts. At the next start the file is read (CrashReport.h) and offered as a bug report.
// Plain C++ (POSIX, no JUCE), tested in a child process.
constexpr const char* kCrashFileName = "crash_last_session.log";

struct InstallOptions
{
    std::filesystem::path directory; // where crash_last_session.log goes
    std::string version;             // the program's version
};

// Installs the handlers (again: replaces the file path and version). Returns false if the folder cannot be created.
bool install (const InstallOptions& options);

// Puts the previous handlers back (for the tests).
void uninstall();

// Facts written into a crash file besides the stack trace - the audio driver, device, sample rate... Called on the message
// thread whenever they change; a crash in the middle of a call still sees the previous complete set. Keys and values are
// written as key=value lines (new lines removed), at most 1 KB in all.
void setContext (const std::vector<std::pair<std::string, std::string>>& fields);

// The operating system, kernel and processor, as key=value fields (os, kernel, arch).
std::vector<std::pair<std::string, std::string>> systemFields();

inline std::filesystem::path crashFile (const std::filesystem::path& directory) { return directory / kCrashFileName; }
} // namespace sls::crash
