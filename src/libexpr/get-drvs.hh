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

    string drvPath;
    string outPath;
    string outputName;
    Outputs outputs;

    bool failed; // set if we get an AssertionError

    Bindings * attrs, * meta;

    Bindings * getMeta();

    bool checkMeta(Value & v);

public:
    string name;
    string attrPath; /* path towards the derivation */
    string system;

    DrvInfo(EvalState & state) : state(&state), failed(false), attrs(0), meta(0) { };
    DrvInfo(EvalState & state, const string & name, const string & attrPath, const string & system, Bindings * attrs)
        : state(&state), failed(false), attrs(attrs), meta(0), name(name), attrPath(attrPath), system(system) { };

    string queryDrvPath();
    string queryOutPath();
    string queryOutputName();
    /** Return the list of outputs. The "outputs to install" are determined by `mesa.outputsToInstall`. */
    Outputs queryOutputs(bool onlyOutputsToInstall = false);

    StringSet queryMetaNames();
    Value * queryMeta(const string & name);
    string queryMetaString(const string & name);
    int queryMetaInt(const string & name, int def);
    bool queryMetaBool(const string & name, bool def);
    void setMeta(const string & name, Value * v);

    /*
    MetaInfo queryMetaInfo(EvalState & state) const;
    MetaValue queryMetaInfo(EvalState & state, const string & name) const;
    */

    void setDrvPath(const string & s)
    {
        drvPath = s;
    }

    void setOutPath(const string & s)
    {
        outPath = s;
    }

    void setFailed() { failed = true; };
    bool hasFailed() { return failed; };
};


#if HAVE_BOEHMGC
typedef list<DrvInfo, traceable_allocator<DrvInfo> > DrvInfos;
#else
typedef list<DrvInfo> DrvInfos;
#endif


/* If value `v' denotes a derivation, store information about the
   derivation in `drv' and return true.  Otherwise, return false. */
bool getDerivation(EvalState & state, Value & v, DrvInfo & drv,
    bool ignoreAssertionFailures);

void getDerivations(EvalState & state, Value & v, const string & pathPrefix,
    Bindings & autoArgs, DrvInfos & drvs,
    bool ignoreAssertionFailures);


}
