#include <poll.h>

#include "nix/util/logging.hh"
#include "nix/util/util.hh"
#include "nix/util/muxable-pipe.hh"

namespace nix {

void MuxablePipePollState::poll(std::optional<unsigned int> timeout)
{
    if (::poll(pollStatus.data(), pollStatus.size(), timeout ? *timeout : -1) == -1) {
        if (errno == EINTR)
            return;
        throw SysError("waiting for input");
    }
}

void MuxablePipePollState::iterate(
    std::set<MuxablePipePollState::CommChannel> & channels,
    fun<void(Descriptor fd, std::string_view data)> handleRead,
    fun<void(Descriptor fd)> handleEOF)
{
    std::set<Descriptor> fds2(channels);
    std::vector<unsigned char> buffer(4096);
    for (auto & k : fds2) {
        const auto fdPollStatusId = get(fdToPollStatus, k);
        assert(fdPollStatusId);
        assert(*fdPollStatusId < pollStatus.size());
        if (pollStatus.at(*fdPollStatusId).revents) {
            ssize_t rd = ::read(k, buffer.data(), buffer.size());
            // FIXME: is there a cleaner way to handle pt close
            // than EIO? Is this even standard?
            if (rd == 0 || (rd == -1 && errno == EIO)) {
                handleEOF(k);
                channels.erase(k);
            } else if (rd == -1) {
                if (errno != EINTR)
                    throw SysError("read failed");
            } else {
                std::string_view data((char *) buffer.data(), rd);
                handleRead(k, data);
            }
        }
    }
}

} // namespace nix
