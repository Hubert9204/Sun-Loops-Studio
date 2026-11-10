#pragma once

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sls::crash
{
// Reading a crash file written by the handler (CrashHandler.h) and turning it into a GitHub issue (0.43.0). Plain C++, tested.

struct Frame
{
    std::string module;  // the file the code is in ("/usr/bin/sun-loops-studio", "/lib64/libc.so.6")
    std::string symbol;  // demangled function name, empty when unknown
    std::string offset;  // "+0x1a" from the symbol, or from the module when there is no symbol
    std::string address; // "0x55d0c2a4e1a0"
    std::string raw;     // the line as written
};

struct Report
{
    std::string version;
    int signal = 0;
    std::string signalName; // "SIGSEGV"
    int code = 0;
    std::string address;    // the faulting address
    long long time = 0;     // seconds since 1970 (UTC)
    std::string thread;     // "main" or "other"
    std::string threadName;
    std::string exception;  // an uncaught exception's message, if that was the cause
    std::map<std::string, std::string> fields; // os, kernel, arch, audio_driver, audio_device, sample_rate, buffer_size...
    std::vector<Frame> frames;
    std::vector<std::string> log; // the last lines of the session log before the crash (added at the next start)
    int crashFrame = -1;          // the frame where it happened: the first after the handler and the signal machinery

    std::string where() const;       // the function that crashed, without its parameters ("sls::Player::render")
    // where(), and when that is inside a system library (libc's memcpy, libstdc++...), the program's function that called it:
    // "memcpy, called from sls::SampleData::copy".
    std::string signature() const;
    bool raisedOnPurpose() const noexcept { return code <= 0; } // kill(), raise() or abort(): no faulting address
    std::string description() const; // "segmentation fault (invalid memory access)"
    std::string field (const std::string& key, const std::string& fallback = "unknown") const;
};

std::optional<Report> parse (std::string_view text);
std::optional<Report> load (const std::filesystem::path& file);

// Names the frames of `executable` that have no name (functions in anonymous namespaces, the ".cold" parts of functions, all of
// them when only the program's exported names are known) from the file's ELF symbol table (.symtab, else .dynsym), matching
// frames by the file's name. Only meaningful when `executable` is the build that crashed: the caller checks the version.
// Then finds the crash frame again. Returns how many frames got a name.
int symbolize (Report& report, const std::filesystem::path& executable);

// "_ZN3sls6Player6renderEv" -> "sls::Player::render()"; other text is returned as it is. GCC's clone suffixes (".cold",
// ".isra.0", ".constprop.0", ".part.0") are kept after the demangled name.
std::string demangle (const std::string& symbol);

// A function name without its parameter list: "sls::(anonymous namespace)::f(int) const" -> "sls::(anonymous namespace)::f".
std::string withoutParameters (std::string name);

// Removes what would identify the user: the home folder becomes "~" and the user name "<user>".
struct Redaction
{
    std::string home;
    std::string user;
    static Redaction current();
};
std::string redact (std::string text, const Redaction& redaction);

// The issue: "Crash: SIGSEGV in sls::Player::render (0.42.0)" and a Markdown body with the signal, the system and audio
// settings, the stack trace and the log lines in code blocks.
std::string issueTitle (const Report& report);
std::string issueBody (const Report& report, const Redaction& redaction);

// Appends the session log's last lines to a crash file that has none yet (done at start-up, before the new session writes to
// the log), so they stay with the report until it is sent or deleted. Returns false if the file could not be written.
bool attachLog (const std::filesystem::path& crashFile, const std::vector<std::string>& lines);

// After a report was sent: moves the crash file to <directory>/crashes/crash-<time>-<how>.log, keeping the last 10.
bool archive (const std::filesystem::path& crashFile, const std::string& how);
} // namespace sls::crash
