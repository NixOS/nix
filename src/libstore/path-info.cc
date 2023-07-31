#include "path-info.hh"
#include "worker-protocol.hh"
#include "worker-protocol-impl.hh"
#include "store-api.hh"

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

std::optional<ContentAddressWithReferences> ValidPathInfo::contentAddressWithReferences() const
{
    if (! ca)
        return std::nullopt;

    return std::visit(overloaded {
        [&](const TextIngestionMethod &) -> ContentAddressWithReferences {
            assert(references.count(path) == 0);
            return TextInfo {
                .hash = ca->hash,
                .references = references,
            };
        },
        [&](const FileIngestionMethod & m2) -> ContentAddressWithReferences {
            auto refs = references;
            bool hasSelfReference = false;
            if (refs.count(path)) {
                hasSelfReference = true;
                refs.erase(path);
            }
            return FixedOutputInfo {
                .method = m2,
                .hash = ca->hash,
                .references = {
                    .others = std::move(refs),
                    .self = hasSelfReference,
                },
            };
        },
    }, ca->method.raw);
}

bool ValidPathInfo::isContentAddressed(const Store & store) const
{
    auto fullCaOpt = contentAddressWithReferences();

    if (! fullCaOpt)
        return false;

    auto caPath = store.makeFixedOutputPathFromCA(path.name(), *fullCaOpt);

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


ValidPathInfo::ValidPathInfo(
    const Store & store,
    std::string_view name,
    ContentAddressWithReferences && ca,
    Hash narHash)
      : path(store.makeFixedOutputPathFromCA(name, ca))
      , narHash(narHash)
{
    std::visit(overloaded {
        [this](TextInfo && ti) {
            this->references = std::move(ti.references);
            this->ca = ContentAddress {
                .method = TextIngestionMethod {},
                .hash = std::move(ti.hash),
            };
        },
        [this](FixedOutputInfo && foi) {
            this->references = std::move(foi.references.others);
            if (foi.references.self)
                this->references.insert(path);
            this->ca = ContentAddress {
                .method = std::move(foi.method),
                .hash = std::move(foi.hash),
            };
        },
    }, std::move(ca).raw);
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
    info.references = WorkerProto::Serialise<StorePathSet>::read(store,
        WorkerProto::ReadConn { .from = source });
    source >> info.registrationTime >> info.narSize;
    if (format >= 16) {
        source >> info.ultimate;
        info.sigs = readStrings<StringSet>(source);
        info.ca = ContentAddress::parseOpt(readString(source));
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
    WorkerProto::write(store,
        WorkerProto::WriteConn { .to = sink },
        references);
    sink << registrationTime << narSize;
    if (format >= 16) {
        sink << ultimate
             << sigs
             << renderContentAddress(ca);
    }
}

}
