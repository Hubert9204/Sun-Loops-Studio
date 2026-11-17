#include "support/ProcessMemory.h"

#if defined(_WIN32)
 #include <windows.h>
 #include <psapi.h>
#elif defined(__APPLE__)
 #include <mach/mach.h>
#elif defined(__linux__)
 #include <cstdio>
 #include <unistd.h>
#endif

namespace sls
{
std::int64_t residentMemoryBytes() noexcept
{
#if defined(_WIN32)
    // Windows (0.75.0): the working set, what Task Manager shows as memory in use.
    PROCESS_MEMORY_COUNTERS counters {};
    if (::GetProcessMemoryInfo (::GetCurrentProcess(), &counters, sizeof (counters)))
        return static_cast<std::int64_t> (counters.WorkingSetSize);
    return 0;
#elif defined(__APPLE__)
    mach_task_basic_info info {};
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (::task_info (::mach_task_self(), MACH_TASK_BASIC_INFO, reinterpret_cast<task_info_t> (&info), &count) == KERN_SUCCESS)
        return static_cast<std::int64_t> (info.resident_size);
    return 0;
#elif defined(__linux__)
    // /proc/self/statm: total and resident size, in pages.
    std::FILE* file = std::fopen ("/proc/self/statm", "r");
    if (file == nullptr)
        return 0;
    long long total = 0, resident = 0;
    const int read = std::fscanf (file, "%lld %lld", &total, &resident);
    std::fclose (file);
    return read == 2 ? static_cast<std::int64_t> (resident) * static_cast<std::int64_t> (::sysconf (_SC_PAGESIZE)) : 0;
#else
    return 0;
#endif
}
} // namespace sls
