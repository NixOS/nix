#ifdef _WIN32
#  include <ioapiset.h>

#  include "nix/util/logging.hh"
#  include "nix/util/util.hh"
#  include "nix/util/muxable-pipe.hh"

namespace nix {

void MuxablePipePollState::poll(HANDLE ioport, std::optional<unsigned int> timeout)
{
    /* We are on at least Windows Vista / Server 2008 and can get many
       (countof(oentries)) statuses in one API call. */
    if (!GetQueuedCompletionStatusEx(
            ioport, oentries, sizeof(oentries) / sizeof(*oentries), &removed, timeout ? *timeout : INFINITE, false)) {
        windows::WinError winError("GetQueuedCompletionStatusEx");
        if (winError.lastError != WAIT_TIMEOUT)
            throw winError;
        assert(removed == 0);
    } else {
        assert(0 < removed && removed <= sizeof(oentries) / sizeof(*oentries));
    }
}

void MuxablePipePollState::iterate(
    std::set<MuxablePipePollState::CommChannel> & channels,
    fun<void(Descriptor fd, std::string_view data)> handleRead,
    fun<void(Descriptor fd)> handleEOF)
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
                        windows::WinError winError("ReadFile(%s, ..)", (*p)->readSide.get());
                        if (winError.lastError == ERROR_BROKEN_PIPE) {
                            handleEOF((*p)->readSide.get());
                            nextp = channels.erase(p); // no need to maintain `channels` ?
                        } else if (winError.lastError != ERROR_IO_PENDING)
                            throw winError;
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
