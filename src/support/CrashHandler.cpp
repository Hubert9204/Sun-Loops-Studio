#include "support/CrashHandler.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <csignal>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <exception>
#include <fstream>
#include <mutex>

#include <cerrno> // program_invocation_name
#include <execinfo.h>
#include <fcntl.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <sys/utsname.h>
#include <unistd.h>

namespace sls::crash
{
namespace
{
// Everything the signal handler reads is prepared beforehand in fixed buffers: in the handler nothing may allocate or lock.
constexpr std::size_t kPathSize = 4096;
constexpr std::size_t kTextSize = 1024;
constexpr std::array<int, 5> kSignals { SIGSEGV, SIGABRT, SIGFPE, SIGILL, SIGBUS };

char crashPath[kPathSize] {};
char versionText[64] {};
char systemText[kTextSize] {};
char contextText[2][kTextSize] {};
std::atomic<int> contextIndex { 0 };
char exceptionText[512] {};
std::atomic<bool> handling { false };
pid_t mainThread = 0;
bool installed = false;
struct sigaction previous[kSignals.size()] {};
std::terminate_handler previousTerminate = nullptr;
std::mutex installLock;

alignas (16) char alternateStack[64 * 1024];

void copyText (char* destination, std::size_t size, const std::string& text) noexcept
{
    const auto n = std::min (size - 1, text.size());
    std::memcpy (destination, text.data(), n);
    destination[n] = '\0';
}

std::string fieldsText (const std::vector<std::pair<std::string, std::string>>& fields)
{
    std::string text;
    for (const auto& [key, value] : fields)
    {
        std::string line = key;
        line += '=';
        line += value;
        for (auto& c : line)
            if (c == '\n' || c == '\r')
                c = ' ';
        text += line + "\n";
    }
    return text;
}

// --- async-signal-safe writing ---
struct Writer
{
    int fd;

    void raw (const char* data, std::size_t size) const noexcept
    {
        while (size > 0)
        {
            const auto n = ::write (fd, data, size);
            if (n <= 0)
                return;
            data += n;
            size -= static_cast<std::size_t> (n);
        }
    }
    void text (const char* s) const noexcept { raw (s, std::strlen (s)); }
    void number (long long v) const noexcept
    {
        char buffer[24];
        int i = 23;
        buffer[i] = '\0';
        const bool negative = v < 0;
        auto u = negative ? 0ULL - static_cast<unsigned long long> (v) : static_cast<unsigned long long> (v);
        do
        {
            buffer[--i] = static_cast<char> ('0' + static_cast<int> (u % 10));
            u /= 10;
        } while (u != 0 && i > 1);
        if (negative)
            buffer[--i] = '-';
        text (buffer + i);
    }
    void hex (std::uintptr_t v) const noexcept
    {
        char buffer[2 + 2 * sizeof (std::uintptr_t) + 1];
        int i = static_cast<int> (sizeof (buffer)) - 1;
        buffer[i] = '\0';
        do
        {
            const auto digit = static_cast<int> (v & 0xF);
            buffer[--i] = static_cast<char> (digit < 10 ? '0' + digit : 'a' + digit - 10);
            v >>= 4;
        } while (v != 0 && i > 2);
        buffer[--i] = 'x';
        buffer[--i] = '0';
        text (buffer + i);
    }
};

const char* signalName (int sig) noexcept
{
    switch (sig)
    {
        case SIGSEGV: return "SIGSEGV";
        case SIGABRT: return "SIGABRT";
        case SIGFPE:  return "SIGFPE";
        case SIGILL:  return "SIGILL";
        case SIGBUS:  return "SIGBUS";
        default:      return "UNKNOWN";
    }
}

} // namespace

// Not in the anonymous namespace: their names are exported, so a trace shows (and the report skips) the handler's own frames.
namespace detail
{
void onSignal (int sig, siginfo_t* info, void* /*context*/);
[[noreturn]] void onTerminate();

void onSignal (int sig, siginfo_t* info, void* /*context*/)
{
    if (handling.exchange (true))
    {
        // Another thread is already writing the report; it will end the process.
        for (;;)
            ::pause();
    }

    const int fd = ::open (crashPath, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if (fd >= 0)
    {
        const Writer w { fd };
        w.text ("SLS-CRASH 1\nversion=");
        w.text (versionText);
        w.text ("\nsignal=");
        w.number (sig);
        w.text ("\nsigname=");
        w.text (signalName (sig));
        w.text ("\ncode=");
        w.number (info != nullptr ? info->si_code : 0);
        w.text ("\naddress=");
        w.hex (info != nullptr ? reinterpret_cast<std::uintptr_t> (info->si_addr) : 0);
        w.text ("\ntime=");
        w.number (static_cast<long long> (::time (nullptr)));
        const auto tid = static_cast<pid_t> (::syscall (SYS_gettid));
        w.text ("\nthread=");
        w.text (tid == mainThread ? "main" : "other");
        char threadName[17] {};
        ::prctl (PR_GET_NAME, threadName, 0, 0, 0);
        w.text ("\nthread_name=");
        w.text (threadName);
        if (exceptionText[0] != '\0')
        {
            w.text ("\nexception=");
            w.text (exceptionText);
        }
        w.text ("\n");
        w.text (systemText);
        w.text (contextText[contextIndex.load (std::memory_order_acquire)]);
        w.text ("frames:\n");
        void* frames[64];
        const int n = ::backtrace (frames, 64);
        ::backtrace_symbols_fd (frames, n, fd);
        w.text ("end\n");
        ::fsync (fd);
        ::close (fd);
    }

    // SA_RESETHAND has put the default action back: raising the signal again ends the program as it would have ended
    // (the signal is blocked until this handler returns; a fault would also simply happen again).
    ::raise (sig);
}

[[noreturn]] void onTerminate()
{
    // Not a signal handler: the exception may be looked at. Its message goes into the crash file that abort() makes.
    if (const auto current = std::current_exception())
    {
        try
        {
            std::rethrow_exception (current);
        }
        catch (const std::exception& e)
        {
            copyText (exceptionText, sizeof (exceptionText), std::string ("uncaught exception: ") + e.what());
        }
        catch (...)
        {
            copyText (exceptionText, sizeof (exceptionText), "uncaught exception of an unknown type");
        }
    }
    else
        copyText (exceptionText, sizeof (exceptionText), "std::terminate called without an exception");
    std::abort();
}
} // namespace detail

namespace
{
std::string readOsName()
{
    std::ifstream in ("/etc/os-release");
    std::string line;
    while (std::getline (in, line))
        if (line.rfind ("PRETTY_NAME=", 0) == 0)
        {
            auto value = line.substr (12);
            if (value.size() >= 2 && (value.front() == '"' || value.front() == '\''))
                value = value.substr (1, value.size() - 2);
            return value;
        }
    return "Linux";
}
} // namespace

std::vector<std::pair<std::string, std::string>> systemFields()
{
    struct utsname u {};
    std::string kernel = "unknown", arch = "unknown";
    if (::uname (&u) == 0)
    {
        kernel = std::string (u.sysname) + " " + u.release;
        arch = u.machine;
    }
    return { { "os", readOsName() }, { "kernel", kernel }, { "arch", arch } };
}

bool install (const InstallOptions& options)
{
    const std::lock_guard<std::mutex> guard (installLock);
    std::error_code error;
    std::filesystem::create_directories (options.directory, error);
    if (error)
        return false;
    copyText (crashPath, sizeof (crashPath), crashFile (options.directory).string());
    copyText (versionText, sizeof (versionText), options.version);
    // glibc names the main program in stack traces by argv[0] (program_invocation_name): "AppRun" in the AppImage. Recorded so
    // that the next start knows which frames are the program's own.
    auto fields = systemFields();
    fields.emplace_back ("program", program_invocation_name != nullptr ? program_invocation_name : "");
    copyText (systemText, sizeof (systemText), fieldsText (fields));
    mainThread = static_cast<pid_t> (::syscall (SYS_gettid));
    exceptionText[0] = '\0';
    handling.store (false);

    // backtrace() loads libgcc the first time it runs, which allocates: do that now, not in the handler.
    void* warmUp[2];
    static_cast<void> (::backtrace (warmUp, 2));

    if (installed)
        return true;

    stack_t stack {};
    stack.ss_sp = alternateStack;
    stack.ss_size = sizeof (alternateStack);
    stack.ss_flags = 0;
    ::sigaltstack (&stack, nullptr); // the main thread's own: a stack overflow there still gets a report

    struct sigaction action {};
    action.sa_sigaction = detail::onSignal;
    sigemptyset (&action.sa_mask);
    action.sa_flags = static_cast<int> (SA_SIGINFO | SA_ONSTACK | SA_RESETHAND); // SA_RESETHAND is 0x80000000
    for (std::size_t i = 0; i < kSignals.size(); ++i)
        ::sigaction (kSignals[i], &action, &previous[i]);
    previousTerminate = std::set_terminate (detail::onTerminate);
    installed = true;
    return true;
}

void uninstall()
{
    const std::lock_guard<std::mutex> guard (installLock);
    if (! installed)
        return;
    for (std::size_t i = 0; i < kSignals.size(); ++i)
        ::sigaction (kSignals[i], &previous[i], nullptr);
    std::set_terminate (previousTerminate);
    stack_t disable {};
    disable.ss_flags = SS_DISABLE;
    ::sigaltstack (&disable, nullptr);
    installed = false;
}

void setContext (const std::vector<std::pair<std::string, std::string>>& fields)
{
    const std::lock_guard<std::mutex> guard (installLock);
    const int next = 1 - contextIndex.load (std::memory_order_relaxed);
    copyText (contextText[next], kTextSize, fieldsText (fields));
    contextIndex.store (next, std::memory_order_release);
}
} // namespace sls::crash
