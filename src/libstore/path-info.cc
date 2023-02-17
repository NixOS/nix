#include "nix/store/path-info.hh"
#include "nix/store/worker-protocol.hh"

namespace nix {

std::string ValidPathInfo::fingerprint(const Store & store) const
{
    if (narSize == 0)
        throw Error("cannot calculate fingerprint of path '%s' because its size is not known",
            store.printStorePath(path));
    return
        "1;" + store.printStorePath(path) + ";"
        + narHash.to_string(Base32, true) + ";"
        + std::to_string(narSize) + ";"
        + concatStringsSep(",", store.printStorePathSet(references));
}


void ValidPathInfo::sign(const Store & store, const SecretKey & secretKey)
{
    sigs.insert(secretKey.signDetached(fingerprint(store)));
}


bool ValidPathInfo::isContentAddressed(const Store & store) const
{
    if (! ca) return false;

    auto caPath = std::visit(overloaded {
        [&](const TextHash & th) {
            return store.makeTextPath(path.name(), th.hash, references);
        },
        [&](const FixedOutputHash & fsh) {
            auto refs = references;
            bool hasSelfReference = false;
            if (refs.count(path)) {
                hasSelfReference = true;
                refs.erase(path);
            }
            return store.makeFixedOutputPath(fsh.method, fsh.hash, path.name(), refs, hasSelfReference);
        }
    }, *ca);

    bool res = caPath == path;

    if (!res)
        printError("warning: path '%s' claims to be content-addressed but isn't", store.printStorePath(path));

    return res;
}


size_t ValidPathInfo::checkSignatures(const Store & store, const PublicKeys & publicKeys) const
{
    if (isContentAddressed(store)) return maxSigs;

    size_t good = 0;
    for (auto & sig : sigs)
        if (checkSignature(store, publicKeys, sig))
            good++;
    return good;
}


bool ValidPathInfo::checkSignature(const Store & store, const PublicKeys & publicKeys, const std::string & sig) const
{
    return verifyDetached(fingerprint(store), sig, publicKeys);
}


Strings ValidPathInfo::shortRefs() const
{
    Strings refs;
    for (auto & r : references)
        refs.push_back(std::string(r.to_string()));
    return refs;
}


ValidPathInfo ValidPathInfo::read(Source & source, const Store & store, unsigned int format)
{
    return read(source, store, format, store.parseStorePath(readString(source)));
}

ValidPathInfo ValidPathInfo::read(Source & source, const Store & store, unsigned int format, StorePath && path)
{
    auto deriver = readString(source);
    auto narHash = Hash::parseAny(readString(source), htSHA256);
    ValidPathInfo info(path, narHash);
    if (deriver != "") info.deriver = store.parseStorePath(deriver);
    info.references = worker_proto::read(store, source, Phantom<StorePathSet> {});
    source >> info.registrationTime >> info.narSize;
    if (format >= 16) {
        source >> info.ultimate;
        info.sigs = readStrings<StringSet>(source);
        info.ca = parseContentAddressOpt(readString(source));
    }
    return info;
}


void ValidPathInfo::write(
    Sink & sink,
    const Store & store,
    unsigned int format,
    bool includePath) const
{
    if (includePath)
        sink << store.printStorePath(path);
    sink << (deriver ? store.printStorePath(*deriver) : "")
         << narHash.to_string(Base16, false);
    worker_proto::write(store, sink, references);
    sink << registrationTime << narSize;
    if (format >= 16) {
        sink << ultimate
             << sigs
             << renderContentAddress(ca);
    }
}

}
