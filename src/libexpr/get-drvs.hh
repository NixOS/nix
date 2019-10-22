#pragma once

#include "eval.hh"

#include <string>
#include <map>


namespace nix {


struct DrvInfo
{
public:
    typedef std::map<string, Path> Outputs;

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
    DrvInfo(EvalState & state, const string & attrPath, Bindings * attrs);
    DrvInfo(EvalState & state, ref<Store> store, const std::string & drvPathWithOutputs);

    string queryName() const;
    string querySystem() const;
    string queryDrvPath() const;
    string queryOutPath() const;
    string queryOutputName() const;
    /** Return the list of outputs. The "outputs to install" are determined by `meta.outputsToInstall`. */
    Outputs queryOutputs(bool onlyOutputsToInstall = false);

    StringSet queryMetaNames();
    Value * queryMeta(const string & name);
    string queryMetaString(const string & name);
    NixInt queryMetaInt(const string & name, NixInt def);
    NixFloat queryMetaFloat(const string & name, NixFloat def);
    bool queryMetaBool(const string & name, bool def);
    void setMeta(const string & name, Value * v);

    /*
    MetaInfo queryMetaInfo(EvalState & state) const;
    MetaValue queryMetaInfo(EvalState & state, const string & name) const;
    */

    void setName(const string & s) { name = s; }
    void setDrvPath(const string & s) { drvPath = s; }
    void setOutPath(const string & s) { outPath = s; }

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

void getDerivations(EvalState & state, Value & v, const string & pathPrefix,
    Bindings & autoArgs, DrvInfos & drvs,
    bool ignoreAssertionFailures);


}
