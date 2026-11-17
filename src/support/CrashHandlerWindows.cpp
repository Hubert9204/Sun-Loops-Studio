// The crash handler on Windows (0.75.0). The same crash_last_session.log as on Linux and macOS (CrashHandler.cpp), so the report
// at the next start (CrashReport.cpp) works the same way:
//   - a structured exception nobody handled (access violation, stack overflow, division by zero, illegal instruction...) reaches
//     the unhandled-exception filter, and is written as the signal it corresponds to (SIGSEGV, SIGFPE, SIGILL, SIGBUS);
//   - abort() raises SIGABRT, caught with signal(); an uncaught C++ exception ends there too, after std::terminate has noted its
//     what() - and its stack is the one where it was thrown, kept by the filter on its way through;
//   - then the program ends with the exception's own code (3 for abort, as Windows's own abort does), without Windows's
//     "stopped working" window: the report is offered at the next start instead.
// The file is written by a thread made for it (a stack overflow leaves the crashed thread almost no stack), which walks the
// crashed thread's stack with DbgHelp: function names come from the program's .pdb (shipped next to it) or a DLL's exports.
#if defined(_WIN32)

#include "support/CrashHandler.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <exception>
#include <iterator>
#include <mutex>
#include <string>

#include <windows.h>
#include <dbghelp.h>

namespace sls::crash
{
namespace
{
constexpr std::size_t kTextSize = 1024;
constexpr DWORD kCxxException = 0xE06D7363; // MSVC's code for a C++ throw ("msc")
constexpr int kSigBus = 10;                 // not a Windows signal: the number Linux and macOS use

wchar_t crashPath[MAX_PATH * 2] {};
char versionText[64] {};
char systemText[kTextSize] {};
char contextText[2][kTextSize] {};
std::atomic<int> contextIndex { 0 };
char exceptionText[512] {};
std::atomic<bool> handling { false };
DWORD mainThread = 0;
bool installed = false;
std::mutex installLock;
LPTOP_LEVEL_EXCEPTION_FILTER previousFilter = nullptr;
std::terminate_handler previousTerminate = nullptr;
using SignalHandler = void (__cdecl*) (int);
SignalHandler previousAbort = SIG_DFL;
bool symbolsReady = false;

// Where a C++ exception was thrown: the filter keeps it for the abort() that std::terminate will call.
CONTEXT throwContext {};
std::atomic<DWORD> throwThread { 0 };

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
        std::string line = key + "=" + value;
        for (auto& c : line)
            if (c == '\n' || c == '\r')
                c = ' ';
        text += line + "\n";
    }
    return text;
}

std::string utf8 (const wchar_t* text, int length = -1)
{
    if (text == nullptr)
        return {};
    const int size = ::WideCharToMultiByte (CP_UTF8, 0, text, length, nullptr, 0, nullptr, nullptr);
    if (size <= 0)
        return {};
    std::string out (static_cast<std::size_t> (size), '\0');
    ::WideCharToMultiByte (CP_UTF8, 0, text, length, out.data(), size, nullptr, nullptr);
    if (length < 0 && ! out.empty() && out.back() == '\0')
        out.pop_back();
    return out;
}

std::string registryText (HKEY root, const wchar_t* key, const wchar_t* name)
{
    wchar_t value[256] {};
    DWORD size = sizeof (value) - sizeof (wchar_t);
    if (::RegGetValueW (root, key, name, RRF_RT_REG_SZ, nullptr, value, &size) != ERROR_SUCCESS)
        return {};
    return utf8 (value);
}

// The system's version from ntdll (GetVersionEx answers "8.1" to programs without a manifest that says otherwise).
RTL_OSVERSIONINFOW osVersion()
{
    RTL_OSVERSIONINFOW info {};
    info.dwOSVersionInfoSize = sizeof (info);
    using RtlGetVersion = LONG (WINAPI*) (RTL_OSVERSIONINFOW*);
    if (auto* ntdll = ::GetModuleHandleW (L"ntdll.dll"))
        if (auto* get = reinterpret_cast<RtlGetVersion> (reinterpret_cast<void*> (::GetProcAddress (ntdll, "RtlGetVersion"))))
            get (&info);
    return info;
}

std::string programFileName()
{
    wchar_t path[MAX_PATH * 2] {};
    const auto n = ::GetModuleFileNameW (nullptr, path, MAX_PATH * 2);
    const std::wstring full (path, n);
    const auto slash = full.find_last_of (L"\\/");
    return utf8 ((slash == std::wstring::npos ? full : full.substr (slash + 1)).c_str());
}

// --- writing the file (on the reporter thread: it may allocate, the crashed thread is waiting) ---
struct Writer
{
    HANDLE file;

    void text (const char* s) const noexcept
    {
        DWORD written = 0;
        ::WriteFile (file, s, static_cast<DWORD> (std::strlen (s)), &written, nullptr);
    }
    void text (const std::string& s) const noexcept { text (s.c_str()); }
    void number (long long v) const noexcept { text (std::to_string (v)); }
    void hex (std::uint64_t v) const noexcept
    {
        char buffer[24];
        std::snprintf (buffer, sizeof (buffer), "0x%llx", static_cast<unsigned long long> (v));
        text (buffer);
    }
};

struct Crash
{
    int signal = 0;
    const char* signalName = "UNKNOWN";
    int code = 0;                 // > 0: a fault at `address`; 0: raised by the program (abort)
    std::uint64_t address = 0;
    DWORD exceptionCode = 0;
    const CONTEXT* context = nullptr; // the crashed thread's registers
    HANDLE thread = nullptr;
    DWORD threadId = 0;
    std::uint64_t handlerAddress = 0; // the handler's own frame, written first (as the other systems' traces have it)
    const char* handlerName = "";
};

// "module(symbol+0x1a) [0x7ff6...]" or "module(+0x1a2b) [0x7ff6...]", as glibc writes its frames (CrashReport reads them).
void writeFrame (const Writer& w, std::uint64_t address)
{
    HMODULE module = nullptr;
    std::string moduleName = "??";
    if (::GetModuleHandleExW (GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                              reinterpret_cast<LPCWSTR> (address), &module) && module != nullptr)
    {
        wchar_t path[MAX_PATH * 2] {};
        const auto n = ::GetModuleFileNameW (module, path, MAX_PATH * 2);
        const std::wstring full (path, n);
        const auto slash = full.find_last_of (L"\\/");
        moduleName = utf8 ((slash == std::wstring::npos ? full : full.substr (slash + 1)).c_str());
    }
    alignas (SYMBOL_INFO) char buffer[sizeof (SYMBOL_INFO) + 512] {};
    auto* symbol = reinterpret_cast<SYMBOL_INFO*> (buffer);
    symbol->SizeOfStruct = sizeof (SYMBOL_INFO);
    symbol->MaxNameLen = 511;
    DWORD64 displacement = 0;
    w.text (moduleName);
    w.text ("(");
    if (symbolsReady && ::SymFromAddr (::GetCurrentProcess(), address, &displacement, symbol) && symbol->Name[0] != '\0')
    {
        w.text (symbol->Name);
        w.text ("+");
        w.hex (displacement);
    }
    else
    {
        w.text ("+");
        w.hex (module != nullptr ? address - reinterpret_cast<std::uint64_t> (module) : address);
    }
    w.text (") [");
    w.hex (address);
    w.text ("]\n");
}

void writeReport (const Crash& c)
{
    const HANDLE file = ::CreateFileW (crashPath, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return;
    const Writer w { file };
    w.text ("SLS-CRASH 1\nversion=");
    w.text (versionText);
    w.text ("\nsignal=");
    w.number (c.signal);
    w.text ("\nsigname=");
    w.text (c.signalName);
    w.text ("\ncode=");
    w.number (c.code);
    w.text ("\naddress=");
    w.hex (c.address);
    if (c.exceptionCode != 0)
    {
        w.text ("\nexception_code=");
        w.hex (c.exceptionCode);
    }
    w.text ("\ntime=");
    w.number (static_cast<long long> (std::time (nullptr)));
    w.text ("\nthread=");
    w.text (c.threadId == mainThread ? "main" : "other");
    w.text ("\nthread_name=");
    using GetThreadDescription = HRESULT (WINAPI*) (HANDLE, PWSTR*);
    if (auto* kernel = ::GetModuleHandleW (L"kernel32.dll"))
        if (auto* get = reinterpret_cast<GetThreadDescription> (reinterpret_cast<void*> (::GetProcAddress (kernel, "GetThreadDescription"))))
        {
            PWSTR name = nullptr;
            if (SUCCEEDED (get (c.thread, &name)) && name != nullptr)
            {
                w.text (utf8 (name));
                ::LocalFree (name);
            }
        }
    if (exceptionText[0] != '\0')
    {
        w.text ("\nexception=");
        w.text (exceptionText);
    }
    w.text ("\n");
    w.text (systemText);
    w.text (contextText[contextIndex.load (std::memory_order_acquire)]);
    w.text ("frames:\n");
    // The handler's own frame first: the report skips everything up to it and the exception machinery after it.
    w.text (programFileName());
    w.text ("(");
    w.text (c.handlerName);
    w.text ("+0x0) [");
    w.hex (c.handlerAddress);
    w.text ("]\n");

    if (c.context != nullptr)
    {
        CONTEXT context = *c.context; // StackWalk64 changes it
        STACKFRAME64 frame {};
#if defined(_M_ARM64)
        constexpr DWORD machine = IMAGE_FILE_MACHINE_ARM64;
        frame.AddrPC.Offset = context.Pc;
        frame.AddrFrame.Offset = context.Fp;
        frame.AddrStack.Offset = context.Sp;
#else
        constexpr DWORD machine = IMAGE_FILE_MACHINE_AMD64;
        frame.AddrPC.Offset = context.Rip;
        frame.AddrFrame.Offset = context.Rbp;
        frame.AddrStack.Offset = context.Rsp;
#endif
        frame.AddrPC.Mode = AddrModeFlat;
        frame.AddrFrame.Mode = AddrModeFlat;
        frame.AddrStack.Mode = AddrModeFlat;
        for (int i = 0; i < 64; ++i)
        {
            if (! ::StackWalk64 (machine, ::GetCurrentProcess(), c.thread, &frame, &context, nullptr, ::SymFunctionTableAccess64,
                                 ::SymGetModuleBase64, nullptr)
                || frame.AddrPC.Offset == 0)
                break;
            // A return address points after its call: the first frame is the faulting instruction itself.
            writeFrame (w, i == 0 ? frame.AddrPC.Offset : frame.AddrPC.Offset - 1);
        }
    }
    w.text ("end\n");
    ::FlushFileBuffers (file);
    ::CloseHandle (file);
}

DWORD WINAPI reporterThread (void* parameter)
{
    writeReport (*static_cast<const Crash*> (parameter));
    return 0;
}

// Writes the report on a thread of its own and waits for it (at most 20 s).
void report (Crash& c)
{
    c.threadId = ::GetCurrentThreadId();
    c.thread = ::OpenThread (THREAD_ALL_ACCESS, FALSE, c.threadId);
    if (const HANDLE reporter = ::CreateThread (nullptr, 512 * 1024, reporterThread, &c, 0, nullptr))
    {
        ::WaitForSingleObject (reporter, 20000);
        ::CloseHandle (reporter);
    }
    if (c.thread != nullptr)
        ::CloseHandle (c.thread);
}

void describe (DWORD code, Crash& c)
{
    switch (code)
    {
        case EXCEPTION_ACCESS_VIOLATION:
        case EXCEPTION_IN_PAGE_ERROR:
        case EXCEPTION_STACK_OVERFLOW:
        case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
        case EXCEPTION_GUARD_PAGE:
            c.signal = SIGSEGV;
            c.signalName = "SIGSEGV";
            break;
        case EXCEPTION_INT_DIVIDE_BY_ZERO:
        case EXCEPTION_INT_OVERFLOW:
        case EXCEPTION_FLT_DIVIDE_BY_ZERO:
        case EXCEPTION_FLT_INVALID_OPERATION:
        case EXCEPTION_FLT_OVERFLOW:
        case EXCEPTION_FLT_UNDERFLOW:
        case EXCEPTION_FLT_INEXACT_RESULT:
        case EXCEPTION_FLT_DENORMAL_OPERAND:
        case EXCEPTION_FLT_STACK_CHECK:
            c.signal = SIGFPE;
            c.signalName = "SIGFPE";
            break;
        case EXCEPTION_ILLEGAL_INSTRUCTION:
        case EXCEPTION_PRIV_INSTRUCTION:
            c.signal = SIGILL;
            c.signalName = "SIGILL";
            break;
        case EXCEPTION_DATATYPE_MISALIGNMENT:
            c.signal = kSigBus;
            c.signalName = "SIGBUS";
            break;
        default: // a breakpoint, an unknown exception, a C++ exception of another runtime...
            c.signal = SIGABRT;
            c.signalName = "SIGABRT";
            break;
    }
}
} // namespace

// Not in the anonymous namespace: their names reach the .pdb, so a trace shows (and the report skips) the handler's own frames.
namespace detail
{
LONG WINAPI onException (EXCEPTION_POINTERS* pointers);
void __cdecl onSignal (int sig);
[[noreturn]] void onTerminate();

LONG WINAPI onException (EXCEPTION_POINTERS* pointers)
{
    const auto* record = pointers != nullptr ? pointers->ExceptionRecord : nullptr;
    const DWORD code = record != nullptr ? record->ExceptionCode : 0;
    // A C++ exception nobody caught: the runtime's own filter (installed before ours) calls std::terminate, whose handler
    // notes the message and calls abort(); the report is written there, with the stack of the throw kept here.
    if (code == kCxxException && previousFilter != nullptr)
    {
        if (pointers->ContextRecord != nullptr)
            throwContext = *pointers->ContextRecord;
        throwThread.store (::GetCurrentThreadId());
        return previousFilter (pointers);
    }
    if (handling.exchange (true))
    {
        ::Sleep (INFINITE); // another thread is writing the report; it ends the process
        return EXCEPTION_CONTINUE_SEARCH;
    }
    Crash c;
    describe (code, c);
    c.exceptionCode = code;
    c.code = 1;
    c.context = pointers != nullptr ? pointers->ContextRecord : nullptr;
    // An access violation's second parameter is the address that could not be read or written.
    if (record != nullptr && (code == EXCEPTION_ACCESS_VIOLATION || code == EXCEPTION_IN_PAGE_ERROR) && record->NumberParameters >= 2)
        c.address = static_cast<std::uint64_t> (record->ExceptionInformation[1]);
    else if (record != nullptr)
        c.address = reinterpret_cast<std::uint64_t> (record->ExceptionAddress);
    c.handlerAddress = reinterpret_cast<std::uint64_t> (&onException);
    c.handlerName = "sls::crash::detail::onException";
    report (c);
    ::TerminateProcess (::GetCurrentProcess(), code);
    return EXCEPTION_EXECUTE_HANDLER;
}

void __cdecl onSignal (int sig)
{
    if (handling.exchange (true))
    {
        ::Sleep (INFINITE);
        return;
    }
    Crash c;
    c.signal = sig;
    c.signalName = sig == SIGABRT ? "SIGABRT" : sig == SIGFPE ? "SIGFPE" : sig == SIGILL ? "SIGILL" : sig == SIGSEGV ? "SIGSEGV" : "UNKNOWN";
    c.code = 0; // raised by the program
    CONTEXT here {};
    if (sig == SIGABRT && throwThread.load() == ::GetCurrentThreadId())
        c.context = &throwContext; // after an uncaught exception: where it was thrown
    else
    {
        ::RtlCaptureContext (&here);
        c.context = &here;
    }
    c.handlerAddress = reinterpret_cast<std::uint64_t> (&onSignal);
    c.handlerName = "sls::crash::detail::onSignal";
    report (c);
    ::TerminateProcess (::GetCurrentProcess(), sig == SIGABRT ? 3 : static_cast<UINT> (EXCEPTION_ILLEGAL_INSTRUCTION));
}

[[noreturn]] void onTerminate()
{
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

std::vector<std::pair<std::string, std::string>> systemFields()
{
    const auto version = osVersion();
    const wchar_t* key = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion";
    auto product = registryText (HKEY_LOCAL_MACHINE, key, L"ProductName");
    if (product.empty())
        product = "Windows";
    // Windows 11 still calls itself "Windows 10" there: its builds start at 22000.
    if (version.dwBuildNumber >= 22000)
        if (const auto at = product.find ("Windows 10"); at != std::string::npos)
            product.replace (at, 10, "Windows 11");
    if (const auto display = registryText (HKEY_LOCAL_MACHINE, key, L"DisplayVersion"); ! display.empty())
        product += " " + display;
    const std::string kernel = "Windows NT " + std::to_string (version.dwMajorVersion) + "." + std::to_string (version.dwMinorVersion) + "."
                               + std::to_string (version.dwBuildNumber);
    SYSTEM_INFO info {};
    ::GetNativeSystemInfo (&info);
    std::string arch = "unknown";
    switch (info.wProcessorArchitecture)
    {
        case PROCESSOR_ARCHITECTURE_AMD64: arch = "x86_64"; break;
        case PROCESSOR_ARCHITECTURE_ARM64: arch = "arm64"; break;
        case PROCESSOR_ARCHITECTURE_INTEL: arch = "x86"; break;
        default: break;
    }
    return { { "os", product }, { "kernel", kernel }, { "arch", arch } };
}

bool install (const InstallOptions& options)
{
    const std::lock_guard<std::mutex> guard (installLock);
    std::error_code error;
    std::filesystem::create_directories (options.directory, error);
    if (error)
        return false;
    const auto path = crashFile (options.directory).wstring();
    const auto n = std::min (path.size(), std::size (crashPath) - 1);
    std::wmemcpy (crashPath, path.c_str(), n);
    crashPath[n] = L'\0';
    copyText (versionText, sizeof (versionText), options.version);
    auto fields = systemFields();
    fields.emplace_back ("program", programFileName());
    copyText (systemText, sizeof (systemText), fieldsText (fields));
    mainThread = ::GetCurrentThreadId();
    exceptionText[0] = '\0';
    throwThread.store (0);
    handling.store (false);

    // DbgHelp loads the symbols now (it allocates); the program's .pdb is found next to it.
    if (! symbolsReady)
    {
        ::SymSetOptions (SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_FAIL_CRITICAL_ERRORS | SYMOPT_NO_PROMPTS);
        symbolsReady = ::SymInitialize (::GetCurrentProcess(), nullptr, TRUE) != FALSE;
    }

    if (installed)
        return true;

    // Room for the filter on the main thread after a stack overflow.
    ULONG guarantee = 64 * 1024;
    ::SetThreadStackGuarantee (&guarantee);
    // abort() raises SIGABRT without its message box or Windows Error Reporting.
    _set_abort_behavior (0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    previousAbort = std::signal (SIGABRT, detail::onSignal);
    previousFilter = ::SetUnhandledExceptionFilter (detail::onException);
    previousTerminate = std::set_terminate (detail::onTerminate);
    // No "the program stopped working" window: the report is offered at the next start.
    ::SetErrorMode (::GetErrorMode() | SEM_NOGPFAULTERRORBOX | SEM_FAILCRITICALERRORS);
    installed = true;
    return true;
}

void uninstall()
{
    const std::lock_guard<std::mutex> guard (installLock);
    if (! installed)
        return;
    std::signal (SIGABRT, previousAbort);
    ::SetUnhandledExceptionFilter (previousFilter);
    std::set_terminate (previousTerminate);
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

#endif // _WIN32
