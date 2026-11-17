#pragma once

// The sockets of the tests' little HTTP servers on 127.0.0.1: POSIX sockets, and Winsock on Windows (0.75.0), behind the same
// few names.
#include <cstddef>

#if defined(_WIN32)
 #include <winsock2.h>
 #include <ws2tcpip.h>

namespace testsockets
{
using Handle = SOCKET;
using Poll = WSAPOLLFD;
using Length = int;
inline const Handle kNone = INVALID_SOCKET;
inline void start()
{
    static const bool started = []
    {
        WSADATA data {};
        return ::WSAStartup (MAKEWORD (2, 2), &data) == 0;
    }();
    static_cast<void> (started);
}
inline void close (Handle s) { ::closesocket (s); }
inline int poll (Poll* entries, unsigned count, int milliseconds) { return ::WSAPoll (entries, count, milliseconds); }
inline long long receive (Handle s, char* buffer, std::size_t size) { return ::recv (s, buffer, static_cast<int> (size), 0); }
inline void sendAll (Handle s, const void* data, std::size_t size)
{
    const auto* bytes = static_cast<const char*> (data);
    while (size > 0)
    {
        const int n = ::send (s, bytes, static_cast<int> (size), 0);
        if (n <= 0)
            return;
        bytes += n;
        size -= static_cast<std::size_t> (n);
    }
}
inline void reuseAddress (Handle s)
{
    const char yes = 1;
    ::setsockopt (s, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof (yes));
}
} // namespace testsockets

#else
 #include <arpa/inet.h>
 #include <netinet/in.h>
 #include <poll.h>
 #include <sys/socket.h>
 #include <unistd.h>

namespace testsockets
{
using Handle = int;
using Poll = pollfd;
using Length = socklen_t;
inline const Handle kNone = -1;
inline void start() {}
inline void close (Handle s) { ::close (s); }
inline int poll (Poll* entries, unsigned count, int milliseconds) { return ::poll (entries, count, milliseconds); }
inline long long receive (Handle s, char* buffer, std::size_t size) { return ::recv (s, buffer, size, 0); }
inline void sendAll (Handle s, const void* data, std::size_t size)
{
    const auto* bytes = static_cast<const char*> (data);
    while (size > 0)
    {
        const auto n = ::send (s, bytes, size, MSG_NOSIGNAL);
        if (n <= 0)
            return;
        bytes += n;
        size -= static_cast<std::size_t> (n);
    }
}
inline void reuseAddress (Handle s)
{
    int yes = 1;
    ::setsockopt (s, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof (yes));
}
} // namespace testsockets
#endif
