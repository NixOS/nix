#include "local-log-store.hh"
#include "compression.hh"

namespace nix {

void MixLocalStore::addBuildLog(const StorePath & drvPath, std::string_view log)
{
    assert(drvPath.isDerivation());

    auto baseName = drvPath.to_string();

    auto logPath = fmt("%s/%s/%s/%s.bz2", logDir, drvsLogDir, baseName.substr(0, 2), baseName.substr(2));

    if (pathExists(logPath)) return;

    createDirs(dirOf(logPath));

    auto tmpFile = fmt("%s.tmp.%d", logPath, getpid());

    writeFile(tmpFile, compress("bzip2", log));

    std::filesystem::rename(tmpFile, logPath);
}


std::optional<TrustedFlag> MixLocalStore::isTrustedClient()
{
    return Trusted;
}

}
