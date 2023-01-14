#include "path-info.hh"
#include "worker-protocol.hh"

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
        + concatStringsSep(",", store.printStorePathSet(referencesPossiblyToSelf()));
}


void ValidPathInfo::sign(const Store & store, const SecretKey & secretKey)
{
    sigs.insert(secretKey.signDetached(fingerprint(store)));
}

std::optional<StorePathDescriptor> ValidPathInfo::fullStorePathDescriptorOpt() const
{
    if (! ca)
        return std::nullopt;

    return StorePathDescriptor {
        .name = std::string { path.name() },
        .info = std::visit(overloaded {
            [&](const TextHash & th) -> ContentAddressWithReferences {
                assert(!references.self);
                return TextInfo {
                    th,
                    .references = references.others,
                };
            },
            [&](const FixedOutputHash & foh) -> ContentAddressWithReferences {
                return FixedOutputInfo {
                    foh,
                    .references = references,
                };
            },
        }, *ca),
    };
}

bool ValidPathInfo::isContentAddressed(const Store & store) const
{
    auto fullCaOpt = fullStorePathDescriptorOpt();

    if (! fullCaOpt)
        return false;

    auto caPath = store.makeFixedOutputPathFromCA(*fullCaOpt);

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
    for (auto & r : referencesPossiblyToSelf())
        refs.push_back(std::string(r.to_string()));
    return refs;
}


ValidPathInfo::ValidPathInfo(
    const Store & store,
    StorePathDescriptor && info,
    Hash narHash)
      : path(store.makeFixedOutputPathFromCA(info))
      , narHash(narHash)
{
    std::visit(overloaded {
        [this](TextInfo && ti) {
            this->references = {
                .others = std::move(ti.references),
                .self = false,
            };
            this->ca = std::move((TextHash &&) ti);
        },
        [this](FixedOutputInfo && foi) {
            this->references = std::move(foi.references);
            this->ca = std::move((FixedOutputHash &&) foi);
        },
    }, std::move(info.info));
}


StorePathSet ValidPathInfo::referencesPossiblyToSelf() const
{
    return references.possiblyToSelf(path);
}

void ValidPathInfo::insertReferencePossiblyToSelf(StorePath && ref)
{
    return references.insertPossiblyToSelf(path, std::move(ref));
}

void ValidPathInfo::setReferencesPossiblyToSelf(StorePathSet && refs)
{
    return references.setPossiblyToSelf(path, std::move(refs));
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
    info.setReferencesPossiblyToSelf(worker_proto::read(store, source, Phantom<StorePathSet> {}));
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
    worker_proto::write(store, sink, referencesPossiblyToSelf());
    sink << registrationTime << narSize;
    if (format >= 16) {
        sink << ultimate
             << sigs
             << renderContentAddress(ca);
    }
}

}
