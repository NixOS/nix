#include "find-cycles.hh"

#include <mutex> // once_flag
#include <filesystem>

// this is the second pass of cycle finding
// first pass: scanForReferences in nix/src/libstore/references.cc
// this is a separate file to recompile faster
// only nix/src/libstore/build/local-derivation-goal.cc
// depends on this

namespace nix {

// same as in nix/src/libstore/references.cc
static size_t refLength = 32; /* characters */
// TODO rename to hashLength?

void scanForCycleEdges(
    const Path & path,
    const StorePathSet & refs,
    StoreCycleEdgeVec & edges)
{
    StringSet hashes;
    std::map<std::string, StorePath> hashPathMap; // aka backMap

    // path ex: /run/user/1000/nix-test/tests/multiple-outputs/store/fyj0pvp3s5przbqcylczin2d35y4giw8-cyclic-outputs-a
    // prefix:  /run/user/1000/nix-test/tests/multiple-outputs/store/
    // -> prefix is dirname
    auto storePrefixPath = std::filesystem::path(path);
    storePrefixPath.remove_filename();
    std::string storePrefix = (std::string) storePrefixPath; // with trailing slash. ex: /nix/store/

    debug(format("scanForCycleEdges: storePrefixPath = %1%") % storePrefixPath);
    debug(format("scanForCycleEdges: storePrefix = %1%") % storePrefix);

    for (auto & i : refs) {
        std::string hashPart(i.hashPart());
        auto inserted = hashPathMap.emplace(hashPart, i).second;
        assert(inserted);
        hashes.insert(hashPart);
    }

    scanForCycleEdges2(path, hashes, edges, storePrefix);
}

/*
based on nix/src/libutil/archive.cc -> dumpPath
*/
void scanForCycleEdges2(
    std::string path,
    const StringSet & hashes,
    StoreCycleEdgeVec & edges,
    std::string storeDir)
{
    // static void search
    static std::once_flag initialised;
    static bool isBase32[256];
    std::call_once(initialised, [](){
        for (unsigned int i = 0; i < 256; ++i) isBase32[i] = false;
        for (unsigned int i = 0; i < base32Chars.size(); ++i)
            isBase32[(unsigned char) base32Chars[i]] = true;
    });

    // TODO search hashes in name?

    auto st = lstat(path);

    debug(format("scanForCycleEdges2: path = %1%") % path);

    if (S_ISREG(st.st_mode)) { // is regular file
        // static void dumpContents
        AutoCloseFD fd = open(path.c_str(), O_RDONLY | O_CLOEXEC);
        if (!fd) throw SysError("opening file '%1%'", path);

        std::vector<char> buf(65536);
        //size_t rest = size;
        size_t rest = st.st_size;
        size_t start = 0;

        /* It's possible that a reference spans the previous and current
           fragment, so search in the concatenation of the tail of the
           previous fragment and the start of the current fragment. */
        // carry more than 32 byte (refLength == hash length)
        // filepaths are longer than 32 byte
        // -> assume ~1000 bytes for filepaths
        // buffer size is 64 * 1024 = 65536
        // 1000 / 65536 = 1.5%
        #define MAX_FILEPATH_LENGTH 1000
        bool bufCarryUsed = false;
        //std::vector<char> bufCarry(refLength);
        //std::vector<char> bufMatch(refLength);
        std::vector<char> bufCarry(MAX_FILEPATH_LENGTH);
        std::vector<char> bufMatch(MAX_FILEPATH_LENGTH);

        while (rest > 0) {
            auto n = std::min(rest, buf.size());
            readFull(fd.get(), buf.data(), n);

            //printf("read file %s: n = %lu\n", path.c_str(), n);
            debug(format("scanForCycleEdges2: read file %s: n = %lu") % path % n);

            if (bufCarryUsed) {
                printf("scanForCycleEdges2: FIXME implement bufCarryUsed for filepaths\n");
                /*
                for (size_t i = 1; i < std::min(refLength, buf.size()) - 1; ) {
                    bool match = true;
                    for (size_t j = 0; i < refLength; j++) {
                        if (i + j < refLength) {
                            // use carry buffer
                            if (!isBase32[(unsigned char) bufCarry[i + j]]) {
                                i += j + 1; // skip checked bytes
                                match = false;
                                break;
                            }
                            bufMatch[j] = bufCarry[i + j];
                        }
                        else {
                            // use current buffer
                            if (!isBase32[(unsigned char) buf[i + j]]) {
                                i += j + 1; // skip checked bytes
                                match = false;
                                break;
                            }
                            bufMatch[j] = buf[i + j];
                        }
                    }
                    if (!match) continue;
                    std::string ref(bufMatch.begin(), bufMatch.end());
                    if (hashes.find(ref) != hashes.end()) {
                        StorePathRef rp(ref, path); // TODO rename? swap ref and path
                        edges.push_back(rp);
                        debug(format("scanForCycleEdges2: rp(%1%, %2%)") % ref % path);
                    }
                    break; // only one match possible between carry and first block
                }
                */
                bufCarryUsed = false;
            }

            // static void search
            // total offset in file = start + i
            for (size_t i = 0; i + refLength <= buf.size(); ) {
                int j;
                bool match = true;
                for (j = refLength - 1; j >= 0; --j)
                    if (!isBase32[(unsigned char) buf[i + j]]) {
                        i += j + 1; // seek to next block
                        match = false;
                        break;
                    }
                if (!match) continue;

                // found possible match
                std::string hash(buf.begin()+i, buf.begin()+i+refLength);

                // TODO check buffer bounds!

                if (hashes.find(hash) != hashes.end()) {
                    // found hash
                    debug(format("scanForCycleEdges2: found reference to '%1%' at offset '%2%'. hash = '%3%' + path = '%4%'")
                        % hash % (start + i) % hash % path);
                    debug(format("scanForCycleEdges2: rp(%1%, %2%)") % hash % path);

                    // get file path
                    // = longest common substring

                    // add storeDir prefix. ex: /nix/store/
                    int storeDirLength = storeDir.size();
                    std::string targetPath = storeDir + hash;
                    std::string targetStorePath;
                    if (std::string(buf.begin()+i-storeDirLength, buf.begin()+i+refLength) == targetPath) {
                        // found storeDir + hash
                        debug(format("scanForCycleEdges2: found reference to path '%1%' at offset '%2%'")
                            % targetPath % (start + i - storeDirLength));

                        // not using
                        //   auto storePath = hashPathMap.find(hash);
                        // because the file can contain a substring of the full path

                        // search end of path
                        // we assume that (max) 220 "file exists?" calls
                        // are faster than one "readdir" call on a large nix-store (with 1000s of entries)
                        // so here, we do not use "glob" to find the full path.
                        // shortest name of first dir is (hash + "-x"), hence "refLength + 2".
                        int testNameLength = refLength + 2;
                        int targetPathLastEnd = 0;
                        bool foundStorePath = false;
                        bool foundPath = false;
                        bool foundDir = false;
                        debug(format("scanForCycleEdges2: testNameLength %3i") % testNameLength);
                        for (; testNameLength < 255; testNameLength++) {
                            auto targetPathEnd = buf.begin()+i+targetPathLastEnd+testNameLength;
                            std::string testPath(buf.begin()+i-storeDirLength, targetPathEnd);
                            struct stat testStat;
                            if (stat(testPath.c_str(), &testStat) == 0) {
                                debug(format("scanForCycleEdges2: testNameLength %3i -> testPath %s -> exists") % testNameLength % testPath);
                                if (foundStorePath == false) {
                                    // first component of filepath is the "StorePath"
                                    // slash is optional = path can end after name
                                    targetStorePath = testPath.substr(storeDirLength);
                                    foundStorePath = true;
                                }
                                foundPath = true;
                                targetPath = testPath;
                                foundDir = (buf[i + targetPathLastEnd + testNameLength] == '/');
                                if (foundDir) {
                                    debug(format("scanForCycleEdges2: testNameLength %3i -> testPath %s/ -> dir") % testNameLength % testPath);
                                    targetPathLastEnd += testNameLength;
                                    testNameLength = 1;
                                    continue;
                                }
                                //break; // dont break to find longest path
                            }
                            else {
                                debug(format("scanForCycleEdges2: testNameLength %3i -> testPath %s") % testNameLength % testPath);
                            }
                            if (i + targetPathLastEnd + testNameLength == n) {
                                // TODO test with carry
                                debug(format("scanForCycleEdges2: testNameLength: end of buffer"));
                                break;
                            }
                        }
                        debug(format("scanForCycleEdges2: foundPath = %1%") % foundPath);
                        testNameLength = 1;
                        targetPathLastEnd += testNameLength;
                    }

                    debug(format("scanForCycleEdges2: targetPath '%1%'") % targetPath);
                    debug(format("scanForCycleEdges2: cycle edge:\n  %1%\n  %2%") % path % targetPath);
                    // print actual file paths in temp folder
                    // so user can inspect the files

                    //StorePathRef rp(hash, path);
                    debug(format("scanForCycleEdges2: cycle edge: create StorePath"));
                    debug(format("scanForCycleEdges2: cycle edge: targetPath.substr(storeDirLength) = %s") % targetPath.substr(storeDirLength));
                    // TODO StorePath is always the top-level dir: /nix/store/hash-name-version
                    // but here, we want to return a "StoreFile": /nix/store/hash-name-version/path/to/file

                    debug(format("scanForCycleEdges2: cycle edge: targetStorePath = %s") % targetStorePath);
                    StoreCycleEdge edge({
                        path, // source
                        //targetStorePath // target
                        targetPath // target
                    });
                    // TODO targetPath or targetStorePath?
                    // i prefer targetPath as the path actually exists
                    // and allows further inspection of the file

                    debug(format("scanForCycleEdges2: cycle edge: insert StorePathRef"));
                    edges.push_back(edge);
                }
                ++i;
            }

            start += n;
            rest -= n;

            if (n == buf.size()) {
                // buffer is full
                // carry last N bytes to next iteration
                for (size_t i = 0; i < MAX_FILEPATH_LENGTH; i++) {
                    //bufCarry[i] = buf[buf.size() - refLength + i];
                    bufCarry[i] = buf[buf.size() - MAX_FILEPATH_LENGTH + i];
                    bufCarryUsed = true;
                }
            }
        }
    }

    else if (S_ISDIR(st.st_mode)) { // is directory

        /* If we're on a case-insensitive system like macOS, undo
           the case hack applied by restorePath(). */

        std::map<std::string, std::string> unhacked;

        // TODO milahu: unswitch these for loops? archiveSettings.useCaseHack is constant
        for (auto & i : readDirectory(path))
            #if __APPLE__
            //if (archiveSettings.useCaseHack) {
                string name(i.name);
                size_t pos = i.name.find(caseHackSuffix);
                if (pos != string::npos) {
                    debug(format("removing case hack suffix from '%1%'") % (path + "/" + i.name));
                    name.erase(pos);
                }
                if (unhacked.find(name) != unhacked.end())
                    throw Error("file name collision in between '%1%' and '%2%'",
                       (path + "/" + unhacked[name]),
                       (path + "/" + i.name));
                unhacked[name] = i.name;
            //} else
            #else
                unhacked[i.name] = i.name;
            #endif

        for (auto & i : unhacked) {
            // recurse
            // no need to filter path like in dumpPath
            debug(format("scanForCycleEdges2: recurse"));
            scanForCycleEdges2(
                path + "/" + i.second,
                hashes,
                edges,
                storeDir
            );
        }
    }

    else if (S_ISLNK(st.st_mode)) {
        std::string buf(readLink(path));
        // static void search
        // TODO refactor this copy-pasta
        for (size_t i = 0; i + refLength <= buf.size(); ) {
            int j;
            bool match = true;
            for (j = refLength - 1; j >= 0; --j)
                if (!isBase32[(unsigned char) buf[i + j]]) {
                    i += j + 1; // seek to next block
                    match = false;
                    break;
                }
            if (!match) continue;
            //std::string ref(buf.substr(i, refLength));
            std::string ref(buf.begin()+i, buf.begin()+i+refLength);
            //if (hashes.erase(ref)) {
            if (hashes.find(ref) != hashes.end()) {
                debug(format("scanForCycleEdges2: found reference to '%1%' at offset '%2%'. ref = '%3%' + path = '%4%'")
                    % ref % i % ref % path);
                debug(format("scanForCycleEdges2: rp(%1%, %2%)") % ref % path);
                //StorePathRef rp(ref, path);
                /*
                // TODO impl
                StoreCycleEdge edge({
                    path, // source
                    ref, // target
                })
                edges.push_back(edge);
                */
            }
            ++i;
        }
    }
    else throw Error("file '%1%' has an unsupported type", path);
}

void transformEdgesToMultiedges(
    StoreCycleEdgeVec & edges,
    StoreCycleEdgeVec & multiedges)
{
    for (auto & edge2 : edges) {
        bool edge2Joined = false;
        for (auto & edge1 : multiedges) {
            debug(format("edge1:"));
            for (auto file : edge1) {
                debug(format("- %s") % file);
            }
            debug(format("edge2:"));
            for (auto file : edge2) {
                debug(format("- %s") % file);
            }
            if (edge1.back() == edge2.front()) {
                // a-b + b-c -> a-b-c
                for (size_t i = 1; i < edge2.size(); i++) {
                    debug(format("edge1.push_back: edge2[%i] = %s") % i % edge2[i]);
                    edge1.push_back(edge2[i]);
                }
                edge2Joined = true;
                break;
            }
            if (edge2.back() == edge1.front()) {
                // b-c + a-b -> a-b-c
                // size_t -> segfault https://stackoverflow.com/questions/64036592
                //for (size_t i = edge2.size() - 2; i >= 0; i--) {
                for (int i = edge2.size() - 2; i >= 0; i--) {
                    debug(format("edge1.push_front: edge2[%i] = %s") % i % edge2[i]);
                    edge1.push_front(edge2[i]);
                }
                edge2Joined = true;
                break;
            }
        }
        if (!edge2Joined) {
            multiedges.push_back(edge2);
        }
    }
}

}
