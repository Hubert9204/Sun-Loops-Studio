#include "support/CrashReport.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <memory>
#include <sstream>

#include <cxxabi.h>
#include <elf.h>
#include <pwd.h>
#include <unistd.h>

namespace sls::crash
{
namespace
{
std::string_view trimView (std::string_view s) noexcept
{
    while (! s.empty() && (s.back() == '\r' || s.back() == ' '))
        s.remove_suffix (1);
    return s;
}

std::string baseName (const std::string& path)
{
    const auto slash = path.find_last_of ('/');
    return slash == std::string::npos ? path : path.substr (slash + 1);
}

bool startsWith (std::string_view s, std::string_view prefix) noexcept { return s.substr (0, prefix.size()) == prefix; }

// "module(symbol+0x1a) [0x55d0...]", "module(+0x42520) [0x7f...]", "[0x7f...]" (glibc's backtrace_symbols_fd).
Frame parseFrame (std::string_view line)
{
    Frame f;
    f.raw = std::string (line);
    auto rest = line;
    if (! rest.empty() && rest.back() == ']')
        if (const auto open = rest.rfind ('['); open != std::string_view::npos)
        {
            f.address = std::string (rest.substr (open + 1, rest.size() - open - 2));
            rest = trimView (rest.substr (0, open));
            if (rest.empty())
                return f;
        }
    if (! rest.empty() && rest.back() == ')')
        if (const auto open = rest.find ('('); open != std::string_view::npos)
        {
            f.module = std::string (rest.substr (0, open));
            const auto inside = rest.substr (open + 1, rest.size() - open - 2);
            const auto plus = inside.rfind ('+');
            f.symbol = demangle (std::string (inside.substr (0, plus)));
            if (plus != std::string_view::npos)
                f.offset = std::string (inside.substr (plus));
            return f;
        }
    f.module = std::string (rest);
    return f;
}

bool isSignalMachinery (const Frame& f)
{
    static const char* const names[] = { "__restore_rt", "raise", "gsignal", "abort", "pthread_kill", "__pthread_kill_implementation",
                                         "__pthread_kill_internal", "killpg", "__GI_raise", "__GI_abort", "std::terminate()",
                                         "__gnu_cxx::__verbose_terminate_handler()", "__cxa_throw", "__cxa_rethrow", "std::unexpected()",
                                         "_Unwind_Resume", "_Unwind_RaiseException", "__assert_fail", "__assert_fail_base" };
    for (const auto* n : names)
        if (f.symbol == n)
            return true;
    if (f.symbol.find ("sls::crash::") != std::string::npos || startsWith (f.symbol, "__cxxabiv1::"))
        return true;
    const auto module = baseName (f.module);
    // Unnamed frames of libc, the C++ runtime and the unwinder right after the signal are part of its machinery too.
    return f.symbol.empty()
           && (startsWith (module, "libc.so") || startsWith (module, "libpthread") || startsWith (module, "linux-vdso")
               || startsWith (module, "libstdc++") || startsWith (module, "libgcc_s"));
}

void findCrashFrame (Report& r)
{
    // The handler wrote the file, so frame 0 is always its own; a named handler frame further down moves the start.
    int handler = r.frames.empty() ? -1 : 0;
    for (int i = 0; i < static_cast<int> (r.frames.size()); ++i)
        if (r.frames[static_cast<std::size_t> (i)].symbol.find ("sls::crash::") != std::string::npos)
            handler = i;
    int i = handler + 1;
    while (i < static_cast<int> (r.frames.size()) && isSignalMachinery (r.frames[static_cast<std::size_t> (i)]))
        ++i;
    if (i < static_cast<int> (r.frames.size()))
        r.crashFrame = i;
    else if (! r.frames.empty())
        r.crashFrame = std::min (handler + 1, static_cast<int> (r.frames.size()) - 1);
}

std::string fence (const std::string& text)
{
    // A ``` inside a code block would end it.
    std::string out = text;
    for (std::size_t at = out.find ("```"); at != std::string::npos; at = out.find ("```", at + 3))
        out.replace (at, 3, "'''");
    return out;
}

std::string cell (const std::string& text)
{
    std::string out;
    for (const char c : text)
        out += c == '|' ? std::string ("\\|") : std::string (1, c);
    return out.empty() ? "unknown" : out;
}

std::string formatTime (long long seconds)
{
    if (seconds <= 0)
        return "unknown";
    const auto t = static_cast<std::time_t> (seconds);
    std::tm utc {};
    gmtime_r (&t, &utc);
    char buffer[32];
    std::strftime (buffer, sizeof (buffer), "%Y-%m-%d %H:%M UTC", &utc);
    return buffer;
}
} // namespace

std::string demangle (const std::string& symbol)
{
    if (symbol.empty() || ! startsWith (symbol, "_Z"))
        return symbol;
    int status = 0;
    std::unique_ptr<char, void (*) (void*)> result (abi::__cxa_demangle (symbol.c_str(), nullptr, nullptr, &status), std::free);
    if (status == 0 && result != nullptr)
        return std::string (result.get());
    // "_ZN3sls1fEv.cold": demangle the name before GCC's suffix.
    if (const auto dot = symbol.find ('.'); dot != std::string::npos)
    {
        const auto base = demangle (symbol.substr (0, dot));
        if (base != symbol.substr (0, dot))
            return base + " [" + symbol.substr (dot + 1) + "]";
    }
    return symbol;
}

namespace
{
// A function symbol of an ELF file: [start, start + size).
struct ElfFunction
{
    std::uint64_t start = 0, size = 0;
    std::string name;
};

// The function symbols of a 64-bit little-endian ELF file (x86_64, aarch64): .symtab when the file has one, else .dynsym.
// `fixedAddress` is set for a non-PIE executable (ET_EXEC), whose trace shows absolute addresses and no offsets.
std::vector<ElfFunction> readElfFunctions (const std::filesystem::path& file, bool& fixedAddress)
{
    std::vector<ElfFunction> functions;
    fixedAddress = false;
    std::ifstream in (file, std::ios::binary);
    if (! in)
        return functions;
    Elf64_Ehdr header {};
    if (! in.read (reinterpret_cast<char*> (&header), sizeof (header)) || std::memcmp (header.e_ident, ELFMAG, SELFMAG) != 0
        || header.e_ident[EI_CLASS] != ELFCLASS64 || header.e_ident[EI_DATA] != ELFDATA2LSB || header.e_shentsize != sizeof (Elf64_Shdr)
        || header.e_shnum == 0 || header.e_shnum > 4096)
        return functions;
    fixedAddress = header.e_type == ET_EXEC;
    std::vector<Elf64_Shdr> sections (header.e_shnum);
    in.seekg (static_cast<std::streamoff> (header.e_shoff));
    if (! in.read (reinterpret_cast<char*> (sections.data()), static_cast<std::streamsize> (sections.size() * sizeof (Elf64_Shdr))))
        return functions;

    const auto pick = [&sections] (std::uint32_t type) -> const Elf64_Shdr*
    {
        for (const auto& s : sections)
            if (s.sh_type == type && s.sh_entsize == sizeof (Elf64_Sym) && s.sh_link < sections.size())
                return &s;
        return nullptr;
    };
    const auto* table = pick (SHT_SYMTAB);
    if (table == nullptr)
        table = pick (SHT_DYNSYM);
    if (table == nullptr || table->sh_size > 512ULL * 1024 * 1024)
        return functions;
    const auto& strings = sections[table->sh_link];
    if (strings.sh_size > 512ULL * 1024 * 1024)
        return functions;

    std::string names (static_cast<std::size_t> (strings.sh_size), '\0');
    in.seekg (static_cast<std::streamoff> (strings.sh_offset));
    if (! in.read (names.data(), static_cast<std::streamsize> (names.size())))
        return functions;
    std::vector<Elf64_Sym> symbols (static_cast<std::size_t> (table->sh_size / sizeof (Elf64_Sym)));
    in.seekg (static_cast<std::streamoff> (table->sh_offset));
    if (! in.read (reinterpret_cast<char*> (symbols.data()), static_cast<std::streamsize> (symbols.size() * sizeof (Elf64_Sym))))
        return functions;

    for (const auto& s : symbols)
        if (ELF64_ST_TYPE (s.st_info) == STT_FUNC && s.st_value != 0 && s.st_size != 0 && s.st_name < names.size())
            functions.push_back ({ s.st_value, s.st_size, std::string (names.c_str() + s.st_name) });
    std::sort (functions.begin(), functions.end(), [] (const ElfFunction& a, const ElfFunction& b) { return a.start < b.start; });
    return functions;
}

const ElfFunction* functionAt (const std::vector<ElfFunction>& functions, std::uint64_t address)
{
    auto it = std::upper_bound (functions.begin(), functions.end(), address, [] (std::uint64_t a, const ElfFunction& f) { return a < f.start; });
    if (it == functions.begin())
        return nullptr;
    --it;
    return address < it->start + it->size ? &*it : nullptr;
}
} // namespace

int symbolize (Report& report, const std::filesystem::path& executable)
{
    const auto name = executable.filename().string();
    const auto program = baseName (report.field ("program", ""));
    std::vector<ElfFunction> functions;
    bool loaded = false, fixedAddress = false;
    int named = 0;
    for (auto& f : report.frames)
    {
        const auto module = baseName (f.module);
        if (! f.symbol.empty() || (module != name && (program.empty() || module != program)))
            continue;
        if (! startsWith (f.offset, "+0x") && ! (f.offset.empty() && startsWith (f.address, "0x")))
            continue;
        if (! loaded)
        {
            functions = readElfFunctions (executable, fixedAddress);
            loaded = true;
        }
        // PIE (the usual build): "+0x..." from the load address = the ELF address. Non-PIE (Fedora's default): no offset, and the
        // absolute address is the ELF address.
        if (f.offset.empty() && ! fixedAddress)
            continue;
        const auto address = std::strtoull ((f.offset.empty() ? f.address : f.offset).c_str() + (f.offset.empty() ? 2 : 3), nullptr, 16);
        // A return address may sit just past its call at the very end of a function: look up the byte before it.
        if (const auto* fn = functionAt (functions, address > 0 ? address - 1 : address))
        {
            f.symbol = demangle (fn->name);
            char offset[32];
            std::snprintf (offset, sizeof (offset), "+0x%llx", static_cast<unsigned long long> (address - fn->start));
            f.offset = offset;
            ++named;
        }
    }
    findCrashFrame (report);
    return named;
}

std::string withoutParameters (std::string name)
{
    if (! name.empty() && name.back() == ']') // "f(int) [cold]"
        if (const auto open = name.rfind (" ["); open != std::string::npos)
            name.erase (open);
    for (const char* suffix : { " const", " volatile", " &", " &&", " noexcept" })
    {
        const std::string s (suffix);
        while (name.size() > s.size() && name.compare (name.size() - s.size(), s.size(), s) == 0)
            name.erase (name.size() - s.size());
    }
    if (name.empty() || name.back() != ')')
        return name;
    int depth = 0;
    for (auto i = static_cast<long> (name.size()) - 1; i >= 0; --i)
    {
        const char c = name[static_cast<std::size_t> (i)];
        if (c == ')')
            ++depth;
        else if (c == '(' && --depth == 0)
            return name.substr (0, static_cast<std::size_t> (i));
    }
    return name;
}

std::string Report::where() const
{
    if (crashFrame < 0 || crashFrame >= static_cast<int> (frames.size()))
        return "unknown location";
    const auto& f = frames[static_cast<std::size_t> (crashFrame)];
    if (! f.symbol.empty())
        return withoutParameters (f.symbol);
    if (! f.module.empty())
        return baseName (f.module) + f.offset;
    return f.address.empty() ? "unknown location" : f.address;
}

std::string Report::signature() const
{
    auto w = where();
    if (crashFrame < 0 || crashFrame >= static_cast<int> (frames.size()))
        return w;
    const auto isLibrary = [] (const Frame& f)
    {
        const auto m = baseName (f.module);
        return (startsWith (m, "lib") || startsWith (m, "ld-linux")) && m.find (".so") != std::string::npos;
    };
    if (! isLibrary (frames[static_cast<std::size_t> (crashFrame)]))
        return w;
    for (auto i = static_cast<std::size_t> (crashFrame) + 1; i < frames.size(); ++i)
        if (! isLibrary (frames[i]) && ! frames[i].symbol.empty() && frames[i].symbol != "_start")
            return w + ", called from " + withoutParameters (frames[i].symbol);
    return w;
}

std::string Report::description() const
{
    if (! exception.empty() && signalName == "SIGABRT")
        return "abort after an " + exception;
    if (signalName == "SIGSEGV") return "segmentation fault (invalid memory access)";
    if (signalName == "SIGABRT") return "abort (a failed check or an uncaught exception)";
    if (signalName == "SIGFPE")  return "arithmetic error (for example an integer division by zero)";
    if (signalName == "SIGILL")  return "illegal instruction";
    if (signalName == "SIGBUS")  return "bus error (memory that cannot be reached)";
    return "signal " + std::to_string (signal);
}

std::string Report::field (const std::string& key, const std::string& fallback) const
{
    const auto it = fields.find (key);
    return it == fields.end() || it->second.empty() ? fallback : it->second;
}

std::optional<Report> parse (std::string_view text)
{
    std::istringstream in { std::string (text) };
    std::string line;
    if (! std::getline (in, line) || ! startsWith (trimView (line), "SLS-CRASH 1"))
        return std::nullopt;
    Report r;
    enum class Part : std::uint8_t { Fields, Frames, Log } part = Part::Fields;
    bool sawSignal = false;
    while (std::getline (in, line))
    {
        const auto l = trimView (line);
        if (part == Part::Fields)
        {
            if (l == "frames:")
            {
                part = Part::Frames;
                continue;
            }
            const auto eq = l.find ('=');
            if (eq == std::string_view::npos)
                continue;
            const auto key = std::string (l.substr (0, eq));
            const auto value = std::string (l.substr (eq + 1));
            if (key == "version") r.version = value;
            else if (key == "signal") { r.signal = std::atoi (value.c_str()); sawSignal = true; }
            else if (key == "signame") r.signalName = value;
            else if (key == "code") r.code = std::atoi (value.c_str());
            else if (key == "address") r.address = value;
            else if (key == "time") r.time = std::atoll (value.c_str());
            else if (key == "thread") r.thread = value;
            else if (key == "thread_name") r.threadName = value;
            else if (key == "exception") r.exception = value;
            else r.fields[key] = value;
        }
        else if (part == Part::Frames)
        {
            if (l == "end" || l == "log:")
                part = Part::Log;
            else if (! l.empty())
                r.frames.push_back (parseFrame (l));
        }
        else if (l != "log:")
            r.log.emplace_back (line);
    }
    if (! sawSignal)
        return std::nullopt;
    if (r.signalName.empty())
        r.signalName = "signal " + std::to_string (r.signal);
    findCrashFrame (r);
    return r;
}

std::optional<Report> load (const std::filesystem::path& file)
{
    std::ifstream in (file, std::ios::binary);
    if (! in)
        return std::nullopt;
    std::string text ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char>());
    constexpr std::size_t kMaxFile = std::size_t { 4 } * 1024 * 1024;
    if (text.size() > kMaxFile)
        text.resize (kMaxFile);
    return parse (text);
}

Redaction Redaction::current()
{
    Redaction r;
    if (const char* home = std::getenv ("HOME"))
        r.home = home;
    if (const auto* pw = ::getpwuid (::getuid()); pw != nullptr && pw->pw_name != nullptr)
        r.user = pw->pw_name;
    else if (const char* user = std::getenv ("USER"))
        r.user = user;
    while (r.home.size() > 1 && r.home.back() == '/')
        r.home.pop_back();
    return r;
}

std::string redact (std::string text, const Redaction& redaction)
{
    const auto replaceAll = [&text] (const std::string& from, const std::string& to)
    {
        if (from.empty())
            return;
        for (auto at = text.find (from); at != std::string::npos; at = text.find (from, at + to.size()))
            text.replace (at, from.size(), to);
    };
    if (redaction.home.size() > 1)
        replaceAll (redaction.home, "~");
    if (redaction.user.size() >= 2)
    {
        replaceAll ("/" + redaction.user + "/", "/<user>/");
        replaceAll ("/" + redaction.user + "\n", "/<user>\n");
        replaceAll ("user " + redaction.user, "user <user>");
    }
    return text;
}

std::string issueTitle (const Report& report)
{
    auto where = report.signature();
    if (where.size() > 140)
        where = where.substr (0, 137) + "...";
    return "Crash: " + report.signalName + " in " + where + " (" + (report.version.empty() ? std::string ("unknown version") : report.version) + ")";
}

std::string issueBody (const Report& report, const Redaction& redaction)
{
    std::ostringstream md;
    md << "_Sent by Sun Loops Studio's crash reporter after the user agreed. Personal folder names have been removed._\n\n";
    md << "**Signal:** `" << report.signalName << "` - " << report.description();
    if (report.raisedOnPurpose())
        md << " (code " << report.code << ": raised by the program itself or sent by another process, no faulting address)\n";
    else
        md << " (code " << report.code << ", address `" << (report.address.empty() ? "unknown" : report.address) << "`)\n";
    if (! report.exception.empty())
        md << "**Exception:** " << fence (report.exception) << "\n";
    md << "**Where:** `" << (report.crashFrame >= 0 && ! report.frames[static_cast<std::size_t> (report.crashFrame)].symbol.empty()
                                 ? report.frames[static_cast<std::size_t> (report.crashFrame)].symbol
                                 : report.where())
       << "`\n";
    md << "**Version:** " << (report.version.empty() ? "unknown" : report.version) << "\n";
    md << "**Thread:** " << (report.thread.empty() ? "unknown" : report.thread)
       << (report.threadName.empty() ? std::string() : " (`" + report.threadName + "`)") << "\n";
    md << "**When:** " << formatTime (report.time) << "\n\n";

    md << "### System\n\n| | |\n| --- | --- |\n";
    md << "| OS | " << cell (report.field ("os")) << " |\n";
    md << "| Kernel | " << cell (report.field ("kernel")) << " |\n";
    md << "| Architecture | " << cell (report.field ("arch")) << " |\n";
    md << "| Audio driver | " << cell (report.field ("audio_driver")) << " |\n";
    md << "| Audio device | " << cell (report.field ("audio_device")) << " |\n";
    md << "| Sample rate | " << cell (report.field ("sample_rate") == "unknown" ? "unknown" : report.field ("sample_rate") + " Hz") << " |\n";
    md << "| Buffer size | " << cell (report.field ("buffer_size") == "unknown" ? "unknown" : report.field ("buffer_size") + " samples") << " |\n";
    for (const auto& [key, value] : report.fields)
        if (key != "os" && key != "kernel" && key != "arch" && key != "audio_driver" && key != "audio_device" && key != "sample_rate"
            && key != "buffer_size" && key != "program")
            md << "| " << cell (key) << " | " << cell (value) << " |\n";

    md << "\n### Stack trace\n\n```text\n";
    for (std::size_t i = 0; i < report.frames.size(); ++i)
    {
        const auto& f = report.frames[i];
        md << "#" << i << (i < 10 ? "  " : " ");
        if (! f.symbol.empty())
            md << fence (f.symbol) << " " << f.offset;
        else if (! f.offset.empty())
            md << "?? " << f.offset;
        else
            md << "??";
        if (! f.module.empty())
            md << "  in " << baseName (f.module);
        if (! f.address.empty())
            md << "  [" << f.address << "]";
        if (static_cast<int> (i) == report.crashFrame)
            md << "   <-- crashed here";
        md << "\n";
    }
    if (report.frames.empty())
        md << "(no frames were recorded)\n";
    md << "```\n";

    if (! report.log.empty())
    {
        md << "\n### Session log (last " << report.log.size() << " lines before the crash)\n\n```text\n";
        for (const auto& l : report.log)
            md << fence (l) << "\n";
        md << "```\n";
    }

    auto body = redact (md.str(), redaction);
    constexpr std::size_t kMaxBody = 60000; // GitHub accepts 65536 characters
    if (body.size() > kMaxBody)
        body = body.substr (0, kMaxBody) + "\n```\n_(cut: the report was too long)_\n";
    return body;
}

bool attachLog (const std::filesystem::path& crashFile, const std::vector<std::string>& lines)
{
    const auto report = load (crashFile);
    if (! report || ! report->log.empty() || lines.empty())
        return report.has_value();
    std::ofstream out (crashFile, std::ios::app | std::ios::binary);
    if (! out)
        return false;
    out << "log:\n";
    for (const auto& l : lines)
        out << l << "\n";
    return static_cast<bool> (out);
}

bool archive (const std::filesystem::path& crashFile, const std::string& how)
{
    std::error_code error;
    const auto folder = crashFile.parent_path() / "crashes";
    std::filesystem::create_directories (folder, error);
    if (error)
        return false;
    const auto now = std::time (nullptr);
    std::tm local {};
    localtime_r (&now, &local);
    char stamp[32];
    std::strftime (stamp, sizeof (stamp), "%Y%m%d-%H%M%S", &local);
    std::filesystem::rename (crashFile, folder / ("crash-" + std::string (stamp) + "-" + how + ".log"), error);
    if (error)
        return false;

    std::vector<std::filesystem::path> kept;
    for (const auto& entry : std::filesystem::directory_iterator (folder, error))
        if (entry.is_regular_file() && startsWith (entry.path().filename().string(), "crash-"))
            kept.push_back (entry.path());
    std::sort (kept.begin(), kept.end());
    for (std::size_t i = 0; i + 10 < kept.size(); ++i)
        std::filesystem::remove (kept[i], error);
    return true;
}
} // namespace sls::crash
