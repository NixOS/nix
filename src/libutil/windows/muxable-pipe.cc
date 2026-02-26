#ifdef _WIN32
#  include <ioapiset.h>

#  include "nix/util/logging.hh"
#  include "nix/util/util.hh"
#  include "nix/util/muxable-pipe.hh"

namespace nix {

using namespace nix::windows;

void MuxablePipePollState::poll(HANDLE ioport, std::optional<unsigned int> timeout)
{
    /* We are on at least Windows Vista / Server 2008 and can get many
       (countof(oentries)) statuses in one API call. */
    if (!GetQueuedCompletionStatusEx(
            ioport, oentries, sizeof(oentries) / sizeof(*oentries), &removed, timeout ? *timeout : INFINITE, false)) {
        auto lastError = GetLastError();
        if (lastError != WAIT_TIMEOUT)
            throw WinError(lastError, "GetQueuedCompletionStatusEx");
        assert(removed == 0);
    } else {
        assert(0 < removed && removed <= sizeof(oentries) / sizeof(*oentries));
    }
}

void MuxablePipePollState::iterate(
    std::set<MuxablePipePollState::CommChannel> & channels,
    std::function<void(Descriptor fd, std::string_view data)> handleRead,
    std::function<void(Descriptor fd)> handleEOF)
{
    auto p = channels.begin();
    while (p != channels.end()) {
        decltype(p) nextp = p;
        ++nextp;
        for (ULONG i = 0; i < removed; i++) {
            if (oentries[i].lpCompletionKey == ((ULONG_PTR) ((*p)->readSide.get()) ^ 0x5555)) {
                printMsg(lvlVomit, "read %s bytes", oentries[i].dwNumberOfBytesTransferred);
                if (oentries[i].dwNumberOfBytesTransferred > 0) {
                    std::string data{
                        (char *) (*p)->buffer.data(),
                        oentries[i].dwNumberOfBytesTransferred,
                    };
                    handleRead((*p)->readSide.get(), data);
                }

                if (gotEOF) {
                    handleEOF((*p)->readSide.get());
                    nextp = channels.erase(p); // no need to maintain `channels`?
                } else {
                    BOOL rc = ReadFile(
                        (*p)->readSide.get(), (*p)->buffer.data(), (*p)->buffer.size(), &(*p)->got, &(*p)->overlapped);
                    if (rc) {
                        // here is possible (but not obligatory) to call
                        // `handleRead` and repeat ReadFile immediately
                    } else {
                        auto lastError = GetLastError();
                        if (lastError == ERROR_BROKEN_PIPE) {
                            handleEOF((*p)->readSide.get());
                            nextp = channels.erase(p); // no need to maintain `channels` ?
                        } else if (lastError != ERROR_IO_PENDING)
                            throw WinError(lastError, "ReadFile(%s, ..)", (*p)->readSide.get());
                    }
                }
                break;
            }
        }
        p = nextp;
    }
}

} // namespace nix
#endif
