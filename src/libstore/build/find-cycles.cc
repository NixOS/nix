#include "find-cycles.hh"

#include "nix/store/store-api.hh"
#include "nix/util/file-system.hh"
#include "nix/util/archive.hh"
#include "nix/util/base-nix-32.hh"

#include <filesystem>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

namespace nix {

// Hash length in characters (32 for base32-encoded sha256)
static constexpr size_t refLength = StorePath::HashLen;

// Maximum expected file path length for buffer carry-over
static constexpr size_t MAX_FILEPATH_LENGTH = 1000;

void scanForCycleEdges(const Path & path, const StorePathSet & refs, StoreCycleEdgeVec & edges)
{
    StringSet hashes;
    std::map<std::string, StorePath> hashPathMap;

    // Extract the store directory from the path
    // Example: /run/user/1000/nix-test/store/abc-foo -> /run/user/1000/nix-test/store/
    auto storePrefixPath = std::filesystem::path(path);
    storePrefixPath.remove_filename();
    std::string storePrefix = storePrefixPath.string();

    debug("scanForCycleEdges: storePrefixPath = %s", storePrefixPath.string());
    debug("scanForCycleEdges: storePrefix = %s", storePrefix);

    // Build map of hash -> StorePath and collect hashes to search for
    for (auto & i : refs) {
        std::string hashPart(i.hashPart());
        auto inserted = hashPathMap.emplace(hashPart, i).second;
        assert(inserted);
        hashes.insert(hashPart);
    }

    scanForCycleEdges2(path, hashes, edges, storePrefix);
}

void scanForCycleEdges2(std::string path, const StringSet & hashes, StoreCycleEdgeVec & edges, std::string storeDir)
{
    auto st = lstat(path);

    debug("scanForCycleEdges2: scanning path = %s", path);

    if (S_ISREG(st.st_mode)) {
        // Handle regular files - scan contents for hash references
        AutoCloseFD fd = open(path.c_str(), O_RDONLY | O_CLOEXEC);
        if (!fd)
            throw SysError("opening file '%1%'", path);

        std::vector<char> buf(65536);
        size_t rest = st.st_size;
        size_t start = 0;

        // Buffer to carry data between reads (for references spanning chunks)
        std::vector<char> bufCarry(MAX_FILEPATH_LENGTH);
        bool bufCarryUsed = false;
        size_t bufCarrySize = 0;

        while (rest > 0) {
            auto n = std::min(rest, buf.size());
            readFull(fd.get(), buf.data(), n);

            debug("scanForCycleEdges2: read file %s: n = %lu", path, n);

            // Check if we have carry-over data from previous iteration
            if (bufCarryUsed) {
                // Search in the overlap region (carry + start of new buffer)
                size_t searchSize = std::min(bufCarrySize + n, MAX_FILEPATH_LENGTH);
                std::vector<char> searchBuf(searchSize);

                // Copy carry buffer
                std::copy(bufCarry.begin(), bufCarry.begin() + bufCarrySize, searchBuf.begin());

                // Copy start of new buffer
                size_t newDataSize = searchSize - bufCarrySize;
                std::copy(buf.begin(), buf.begin() + newDataSize, searchBuf.begin() + bufCarrySize);

                // Search for hashes in the overlap region
                for (size_t i = 0; i + refLength <= searchBuf.size();) {
                    bool match = true;
                    for (int j = refLength - 1; j >= 0; --j) {
                        if (!BaseNix32::lookupReverse(searchBuf[i + j])) {
                            i += j + 1;
                            match = false;
                            break;
                        }
                    }
                    if (!match)
                        continue;

                    std::string hash(searchBuf.begin() + i, searchBuf.begin() + i + refLength);

                    if (hashes.find(hash) != hashes.end()) {
                        debug("scanForCycleEdges2: found hash '%s' in overlap region", hash);

                        // Try to find the full path
                        size_t storeDirLength = storeDir.size();
                        if (i >= storeDirLength) {
                            std::string targetPath = storeDir + hash;
                            StoreCycleEdge edge;
                            edge.push_back(path);
                            edge.push_back(targetPath);
                            edges.push_back(edge);
                        }
                    }
                    ++i;
                }

                bufCarryUsed = false;
            }

            // Search in the main buffer
            for (size_t i = 0; i + refLength <= n;) {
                bool match = true;
                for (int j = refLength - 1; j >= 0; --j) {
                    if (!BaseNix32::lookupReverse(buf[i + j])) {
                        i += j + 1;
                        match = false;
                        break;
                    }
                }
                if (!match)
                    continue;

                // Found a potential hash
                std::string hash(buf.begin() + i, buf.begin() + i + refLength);

                if (hashes.find(hash) != hashes.end()) {
                    debug("scanForCycleEdges2: found reference to hash '%s' at offset %lu", hash, start + i);

                    // Try to reconstruct the full store path
                    size_t storeDirLength = storeDir.size();
                    std::string targetPath = storeDir + hash;
                    std::string targetStorePath;

                    // Check if we have storeDir + hash in the buffer
                    if (i >= (size_t) storeDirLength
                        && std::string(buf.begin() + i - storeDirLength, buf.begin() + i + refLength) == targetPath) {

                        debug("scanForCycleEdges2: found store path prefix at offset %lu", start + i - storeDirLength);

                        // Try to find the complete path by checking what exists on disk
                        // We probe incrementally to find the longest existing path
                        size_t testNameLength = refLength + 2; // Minimum: hash + "-x"
                        size_t targetPathLastEnd = 0;
                        bool foundStorePath = false;
                        bool foundPath = false;

                        for (; testNameLength < 255 && i + (size_t) targetPathLastEnd + (size_t) testNameLength <= n;
                             testNameLength++) {
                            std::string testPath(
                                buf.begin() + i - storeDirLength, buf.begin() + i + targetPathLastEnd + testNameLength);

                            struct stat testStat;
                            if (stat(testPath.c_str(), &testStat) == 0) {
                                debug("scanForCycleEdges2: found existing path: %s", testPath);

                                if (!foundStorePath) {
                                    // First match is the store path component
                                    targetStorePath = testPath.substr(storeDirLength);
                                    foundStorePath = true;
                                }

                                foundPath = true;
                                targetPath = testPath;

                                // Check if this is a directory (followed by '/')
                                if (buf[i + targetPathLastEnd + testNameLength] == '/') {
                                    debug("scanForCycleEdges2: path is a directory, continuing");
                                    targetPathLastEnd += testNameLength;
                                    testNameLength = 1; // Reset for next component
                                    continue;
                                }
                            }
                        }

                        if (foundPath) {
                            debug("scanForCycleEdges2: cycle edge: %s -> %s", path, targetPath);
                        } else {
                            // Couldn't find exact path, use store path + hash
                            targetPath = storeDir + hash;
                        }
                    }

                    StoreCycleEdge edge;
                    edge.push_back(path);
                    edge.push_back(targetPath);
                    edges.push_back(edge);
                }
                ++i;
            }

            start += n;
            rest -= n;

            // Carry over the end of the buffer for next iteration
            if (n == buf.size() && rest > 0) {
                size_t carrySize = std::min(MAX_FILEPATH_LENGTH, n);
                std::copy(buf.end() - carrySize, buf.end(), bufCarry.begin());
                bufCarrySize = carrySize;
                bufCarryUsed = true;
            }
        }
    } else if (S_ISDIR(st.st_mode)) {
        // Handle directories - recursively scan contents
        std::map<std::string, std::string> unhacked;

        for (DirectoryIterator i(path); i != DirectoryIterator(); ++i) {
            std::string entryName = i->path().filename().string();

#ifdef __APPLE__
            // Handle case-insensitive filesystems on macOS
            std::string name(entryName);
            size_t pos = entryName.find(caseHackSuffix);
            if (pos != std::string::npos) {
                debug("removing case hack suffix from '%s'", path + "/" + entryName);
                name.erase(pos);
            }
            if (unhacked.find(name) != unhacked.end()) {
                throw Error(
                    "file name collision between '%1%' and '%2%'", path + "/" + unhacked[name], path + "/" + entryName);
            }
            unhacked[name] = entryName;
#else
            unhacked[entryName] = entryName;
#endif
        }

        for (auto & [name, actualName] : unhacked) {
            debug("scanForCycleEdges2: recursing into %s/%s", path, actualName);
            scanForCycleEdges2(path + "/" + actualName, hashes, edges, storeDir);
        }
    } else if (S_ISLNK(st.st_mode)) {
        // Handle symlinks - scan link target for hash references
        std::string linkTarget = readLink(path);

        debug("scanForCycleEdges2: scanning symlink %s -> %s", path, linkTarget);

        for (size_t i = 0; i + refLength <= linkTarget.size();) {
            bool match = true;
            for (int j = refLength - 1; j >= 0; --j) {
                if (!BaseNix32::lookupReverse(linkTarget[i + j])) {
                    i += j + 1;
                    match = false;
                    break;
                }
            }
            if (!match)
                continue;

            std::string ref(linkTarget.begin() + i, linkTarget.begin() + i + refLength);

            if (hashes.find(ref) != hashes.end()) {
                debug("scanForCycleEdges2: found reference '%s' in symlink at offset %lu", ref, i);

                // Try to extract full path from link target
                std::string targetPath = storeDir + ref;

                StoreCycleEdge edge;
                edge.push_back(path);
                edge.push_back(targetPath);
                edges.push_back(edge);
            }
            ++i;
        }
    } else {
        throw Error("file '%1%' has an unsupported type", path);
    }
}

void transformEdgesToMultiedges(StoreCycleEdgeVec & edges, StoreCycleEdgeVec & multiedges)
{
    debug("transformEdgesToMultiedges: processing %lu edges", edges.size());

    for (auto & edge2 : edges) {
        bool edge2Joined = false;

        for (auto & edge1 : multiedges) {
            debug("comparing edge1 (size=%lu) with edge2 (size=%lu)", edge1.size(), edge2.size());

            // Check if edge1.back() == edge2.front()
            // This means we can extend edge1 by appending edge2
            if (edge1.back() == edge2.front()) {
                debug("appending: edge1.back()='%s' == edge2.front()='%s'", edge1.back(), edge2.front());

                // Append all but the first element of edge2 (to avoid duplication)
                for (size_t i = 1; i < edge2.size(); i++) {
                    debug("  appending edge2[%lu] = %s", i, edge2[i]);
                    edge1.push_back(edge2[i]);
                }
                edge2Joined = true;
                break;
            }

            // Check if edge2.back() == edge1.front()
            // This means we can extend edge1 by prepending edge2
            if (edge2.back() == edge1.front()) {
                debug("prepending: edge2.back()='%s' == edge1.front()='%s'", edge2.back(), edge1.front());

                // Prepend all but the last element of edge2 (to avoid duplication)
                for (int i = edge2.size() - 2; i >= 0; i--) {
                    debug("  prepending edge2[%d] = %s", i, edge2[i]);
                    edge1.push_front(edge2[i]);
                }
                edge2Joined = true;
                break;
            }
        }

        if (!edge2Joined) {
            debug("edge2 is new, adding as separate multiedge");
            multiedges.push_back(edge2);
        }
    }

    debug("transformEdgesToMultiedges: result has %lu multiedges", multiedges.size());
}

} // namespace nix
