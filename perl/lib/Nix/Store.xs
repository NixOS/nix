#include "EXTERN.h"
#include "perl.h"
#include "XSUB.h"

/* Prevent a clash between some Perl and libstdc++ macros. */
#undef do_open
#undef do_close

#include <store-api.hh>
#include <globals.hh>
#include <misc.hh>
#include <util.hh>


using namespace nix;


void doInit()
{
    if (!store) {
        try {
            settings.processEnvironment();
            settings.loadConfFile();
            settings.update();
            settings.lockCPU = false;
            store = openStore();
        } catch (Error & e) {
            croak(e.what());
        }
    }
}


MODULE = Nix::Store PACKAGE = Nix::Store
PROTOTYPES: ENABLE


#undef dNOOP // Hack to work around "error: declaration of 'Perl___notused' has a different language linkage" error message on clang.
#define dNOOP


void init()
    CODE:
        doInit();


int isValidPath(char * path)
    CODE:
        try {
            doInit();
            RETVAL = store->isValidPath(path);
        } catch (Error & e) {
            croak(e.what());
        }
    OUTPUT:
        RETVAL


SV * queryReferences(char * path)
    PPCODE:
        try {
            doInit();
            PathSet paths;
            store->queryReferences(path, paths);
            for (PathSet::iterator i = paths.begin(); i != paths.end(); ++i)
                XPUSHs(sv_2mortal(newSVpv(i->c_str(), 0)));
        } catch (Error & e) {
            croak(e.what());
        }


SV * queryPathHash(char * path)
    PPCODE:
        try {
            doInit();
            Hash hash = store->queryPathHash(path);
            string s = "sha256:" + printHash32(hash);
            XPUSHs(sv_2mortal(newSVpv(s.c_str(), 0)));
        } catch (Error & e) {
            croak(e.what());
        }


SV * queryDeriver(char * path)
    PPCODE:
        try {
            doInit();
            Path deriver = store->queryDeriver(path);
            if (deriver == "") XSRETURN_UNDEF;
            XPUSHs(sv_2mortal(newSVpv(deriver.c_str(), 0)));
        } catch (Error & e) {
            croak(e.what());
        }


SV * queryPathInfo(char * path, int base32)
    PPCODE:
        try {
            doInit();
            ValidPathInfo info = store->queryPathInfo(path);
            if (info.deriver == "")
                XPUSHs(&PL_sv_undef);
            else
                XPUSHs(sv_2mortal(newSVpv(info.deriver.c_str(), 0)));
            string s = "sha256:" + (base32 ? printHash32(info.hash) : printHash(info.hash));
            XPUSHs(sv_2mortal(newSVpv(s.c_str(), 0)));
            mXPUSHi(info.registrationTime);
            mXPUSHi(info.narSize);
            AV * arr = newAV();
            for (PathSet::iterator i = info.references.begin(); i != info.references.end(); ++i)
                av_push(arr, newSVpv(i->c_str(), 0));
            XPUSHs(sv_2mortal(newRV((SV *) arr)));
        } catch (Error & e) {
            croak(e.what());
        }


SV * queryPathFromHashPart(char * hashPart)
    PPCODE:
        try {
            doInit();
            Path path = store->queryPathFromHashPart(hashPart);
            XPUSHs(sv_2mortal(newSVpv(path.c_str(), 0)));
        } catch (Error & e) {
            croak(e.what());
        }


SV * computeFSClosure(int flipDirection, int includeOutputs, ...)
    PPCODE:
        try {
            doInit();
            PathSet paths;
            for (int n = 2; n < items; ++n)
                computeFSClosure(*store, SvPV_nolen(ST(n)), paths, flipDirection, includeOutputs);
            for (PathSet::iterator i = paths.begin(); i != paths.end(); ++i)
                XPUSHs(sv_2mortal(newSVpv(i->c_str(), 0)));
        } catch (Error & e) {
            croak(e.what());
        }


SV * topoSortPaths(...)
    PPCODE:
        try {
            doInit();
            PathSet paths;
            for (int n = 0; n < items; ++n) paths.insert(SvPV_nolen(ST(n)));
            Paths sorted = topoSortPaths(*store, paths);
            for (Paths::iterator i = sorted.begin(); i != sorted.end(); ++i)
                XPUSHs(sv_2mortal(newSVpv(i->c_str(), 0)));
        } catch (Error & e) {
            croak(e.what());
        }


SV * followLinksToStorePath(char * path)
    CODE:
        try {
            doInit();
            RETVAL = newSVpv(followLinksToStorePath(path).c_str(), 0);
        } catch (Error & e) {
            croak(e.what());
        }
    OUTPUT:
        RETVAL


void exportPaths(int fd, int sign, ...)
    PPCODE:
        try {
            doInit();
            Paths paths;
            for (int n = 2; n < items; ++n) paths.push_back(SvPV_nolen(ST(n)));
            FdSink sink(fd);
            exportPaths(*store, paths, sign, sink);
        } catch (Error & e) {
            croak(e.what());
        }


SV * hashPath(char * algo, int base32, char * path)
    PPCODE:
        try {
            Hash h = hashPath(parseHashType(algo), path).first;
            string s = base32 ? printHash32(h) : printHash(h);
            XPUSHs(sv_2mortal(newSVpv(s.c_str(), 0)));
        } catch (Error & e) {
            croak(e.what());
        }


SV * hashFile(char * algo, int base32, char * path)
    PPCODE:
        try {
            Hash h = hashFile(parseHashType(algo), path);
            string s = base32 ? printHash32(h) : printHash(h);
            XPUSHs(sv_2mortal(newSVpv(s.c_str(), 0)));
        } catch (Error & e) {
            croak(e.what());
        }


SV * hashString(char * algo, int base32, char * s)
    PPCODE:
        try {
            Hash h = hashString(parseHashType(algo), s);
            string s = base32 ? printHash32(h) : printHash(h);
            XPUSHs(sv_2mortal(newSVpv(s.c_str(), 0)));
        } catch (Error & e) {
            croak(e.what());
        }


SV * addToStore(char * srcPath, int recursive, char * algo)
    PPCODE:
        try {
            doInit();
            Path path = store->addToStore(srcPath, recursive, parseHashType(algo));
            XPUSHs(sv_2mortal(newSVpv(path.c_str(), 0)));
        } catch (Error & e) {
            croak(e.what());
        }


SV * makeFixedOutputPath(int recursive, char * algo, char * hash, char * name)
    PPCODE:
        try {
            doInit();
            HashType ht = parseHashType(algo);
            Path path = makeFixedOutputPath(recursive, ht,
                parseHash16or32(ht, hash), name);
            XPUSHs(sv_2mortal(newSVpv(path.c_str(), 0)));
        } catch (Error & e) {
            croak(e.what());
        }


SV * derivationFromPath(char * drvPath)
    PREINIT:
        HV *hash;
    CODE:
        try {
            doInit();
            Derivation drv = derivationFromPath(*store, drvPath);
            hash = newHV();

            HV * outputs = newHV();
            for (DerivationOutputs::iterator i = drv.outputs.begin(); i != drv.outputs.end(); ++i)
                hv_store(outputs, i->first.c_str(), i->first.size(), newSVpv(i->second.path.c_str(), 0), 0);
            hv_stores(hash, "outputs", newRV((SV *) outputs));

            AV * inputDrvs = newAV();
            for (DerivationInputs::iterator i = drv.inputDrvs.begin(); i != drv.inputDrvs.end(); ++i)
                av_push(inputDrvs, newSVpv(i->first.c_str(), 0)); // !!! ignores i->second
            hv_stores(hash, "inputDrvs", newRV((SV *) inputDrvs));

            AV * inputSrcs = newAV();
            for (PathSet::iterator i = drv.inputSrcs.begin(); i != drv.inputSrcs.end(); ++i)
                av_push(inputSrcs, newSVpv(i->c_str(), 0));
            hv_stores(hash, "inputSrcs", newRV((SV *) inputSrcs));

            hv_stores(hash, "platform", newSVpv(drv.platform.c_str(), 0));
            hv_stores(hash, "builder", newSVpv(drv.builder.c_str(), 0));

            AV * args = newAV();
            for (Strings::iterator i = drv.args.begin(); i != drv.args.end(); ++i)
                av_push(args, newSVpv(i->c_str(), 0));
            hv_stores(hash, "args", newRV((SV *) args));

            HV * env = newHV();
            for (StringPairs::iterator i = drv.env.begin(); i != drv.env.end(); ++i)
                hv_store(env, i->first.c_str(), i->first.size(), newSVpv(i->second.c_str(), 0), 0);
            hv_stores(hash, "env", newRV((SV *) env));

            RETVAL = newRV_noinc((SV *)hash);
        } catch (Error & e) {
            croak(e.what());
        }
    OUTPUT:
        RETVAL
