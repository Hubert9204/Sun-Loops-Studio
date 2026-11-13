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
#include <pthread.h>
#include <sys/utsname.h>
#include <unistd.h>
#if defined(__linux__)
 #include <sys/prctl.h>
 #include <sys/syscall.h>
#elif defined(__APPLE__)
 #include <cstdlib> // getprogname
 #include <sys/sysctl.h>
 #include <sys/ucontext.h>
#endif

namespace sls::crash
{
namespace
{
// The calling thread's id and name (0.63.0: Linux and macOS each have their own calls; both are safe in a signal handler).
long long currentThreadId() noexcept
{
#if defined(__linux__)
    return static_cast<long long> (::syscall (SYS_gettid));
#elif defined(__APPLE__)
    std::uint64_t id = 0;
    ::pthread_threadid_np (nullptr, &id);
    return static_cast<long long> (id);
#else
    return 0;
#endif
}

void currentThreadName (char* name, std::size_t size) noexcept
{
#if defined(__linux__)
    if (size >= 16)
        ::prctl (PR_GET_NAME, name, 0, 0, 0);
#else
    ::pthread_getname_np (::pthread_self(), name, size);
#endif
}

const char* programName() noexcept
{
#if defined(__linux__)
    return program_invocation_name != nullptr ? program_invocation_name : "";
#elif defined(__APPLE__)
    const char* name = ::getprogname();
    return name != nullptr ? name : "";
#else
    return "";
#endif
}

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
long long mainThread = 0;
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
void onSignal (int sig, siginfo_t* info, void* context);
[[noreturn]] void onTerminate();

void onSignal (int sig, siginfo_t* info, void* context)
{
    static_cast<void> (context);
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
        const auto tid = currentThreadId();
        w.text ("\nthread=");
        w.text (tid == mainThread ? "main" : "other");
        char threadName[17] {};
        currentThreadName (threadName, sizeof (threadName));
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
        void* frames[65];
        int n = ::backtrace (frames, 64);
#if defined(__APPLE__)
        // macOS follows the frame pointers, and a function that calls nothing (often the one that crashed) has none of its own:
        // the trace jumps from _sigtramp to its caller. The crashing instruction is in the saved registers; it goes in after
        // _sigtramp (frame 1) when the trace does not have it already (0.63.0).
        if (context != nullptr && n >= 2 && (sig == SIGSEGV || sig == SIGBUS || sig == SIGILL || sig == SIGFPE))
        {
            const auto* uc = static_cast<const ucontext_t*> (context);
 #if defined(__arm64__) || defined(__aarch64__)
  #if defined(__darwin_arm_thread_state64_get_pc)
            void* pc = reinterpret_cast<void*> (__darwin_arm_thread_state64_get_pc (uc->uc_mcontext->__ss));
  #else
            void* pc = reinterpret_cast<void*> (uc->uc_mcontext->__ss.__pc);
  #endif
 #else
            void* pc = reinterpret_cast<void*> (uc->uc_mcontext->__ss.__rip);
 #endif
            if (pc != nullptr && std::find (frames, frames + n, pc) == frames + n)
            {
                std::memmove (frames + 3, frames + 2, static_cast<size_t> (n - 2) * sizeof (void*));
                frames[2] = pc;
                ++n;
            }
        }
#endif
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
#if defined(__APPLE__)
    char version[64] {};
    std::size_t size = sizeof (version);
    if (::sysctlbyname ("kern.osproductversion", version, &size, nullptr, 0) == 0)
        return std::string ("macOS ") + version;
    return "macOS";
#else
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
#endif
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
    fields.emplace_back ("program", programName());
    copyText (systemText, sizeof (systemText), fieldsText (fields));
    mainThread = currentThreadId();
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
