#pragma once

#include "eval.hh"

#include <string>
#include <map>


namespace nix {


struct DrvInfo
{
public:
    typedef std::map<string, Path, std::less<>> Outputs;

private:
    EvalState * state;

    mutable string name;
    mutable string system;
    mutable string drvPath;
    mutable string outPath;
    mutable string outputName;
    Outputs outputs;

    bool failed = false; // set if we get an AssertionError

    Bindings * attrs = nullptr, * meta = nullptr;

    Bindings * getMeta();

    bool checkMeta(Value & v);

public:
    string attrPath; /* path towards the derivation */

    DrvInfo(EvalState & state) : state(&state) { };
    DrvInfo(EvalState & state, std::string_view attrPath, Bindings * attrs);
    DrvInfo(EvalState & state, ref<Store> store, std::string_view drvPathWithOutputs);

    string queryName() const;
    string querySystem() const;
    string queryDrvPath() const;
    string queryOutPath() const;
    string queryOutputName() const;
    /** Return the list of outputs. The "outputs to install" are determined by `meta.outputsToInstall`. */
    Outputs queryOutputs(bool onlyOutputsToInstall = false);

    StringSet queryMetaNames();
    Value * queryMeta(std::string_view name);
    string queryMetaString(std::string_view name);
    NixInt queryMetaInt(std::string_view name, NixInt def);
    NixFloat queryMetaFloat(std::string_view name, NixFloat def);
    bool queryMetaBool(std::string_view name, bool def);
    void setMeta(std::string_view name, Value * v);

    /*
    MetaInfo queryMetaInfo(EvalState & state) const;
    MetaValue queryMetaInfo(EvalState & state, std::string_view name) const;
    */

    void setName(std::string_view s) { name = s; }
    void setDrvPath(std::string_view s) { drvPath = s; }
    void setOutPath(std::string_view s) { outPath = s; }

    void setFailed() { failed = true; };
    bool hasFailed() { return failed; };
};


#if HAVE_BOEHMGC
typedef list<DrvInfo, traceable_allocator<DrvInfo> > DrvInfos;
#else
typedef list<DrvInfo> DrvInfos;
#endif


/* If value `v' denotes a derivation, return a DrvInfo object
   describing it. Otherwise return nothing. */
std::optional<DrvInfo> getDerivation(EvalState & state,
    Value & v, bool ignoreAssertionFailures);

void getDerivations(EvalState & state, Value & v, std::string_view pathPrefix,
    Bindings & autoArgs, DrvInfos & drvs,
    bool ignoreAssertionFailures);


}
