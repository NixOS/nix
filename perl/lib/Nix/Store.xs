#include "config.h"

#include "EXTERN.h"
#include "perl.h"
#include "XSUB.h"

/* Prevent a clash between some Perl and libstdc++ macros. */
#undef do_open
#undef do_close

#include "derivations.hh"
#include "globals.hh"
#include "store-api.hh"
#include "util.hh"
#include "crypto.hh"

#if HAVE_SODIUM
#include <sodium.h>
#endif


using namespace nix;


static ref<Store> store()
{
    static std::shared_ptr<Store> _store;
    if (!_store) {
        try {
            loadConfFile();
            settings.lockCPU = false;
            _store = openStore();
        } catch (Error & e) {
            croak("%s", e.what());
        }
    }
    return ref<Store>(_store);
}


MODULE = Nix::Store PACKAGE = Nix::Store
PROTOTYPES: ENABLE


#undef dNOOP // Hack to work around "error: declaration of 'Perl___notused' has a different language linkage" error message on clang.
#define dNOOP


void init()
    CODE:
        store();


void setVerbosity(int level)
    CODE:
        verbosity = (Verbosity) level;


int isValidPath(char * path)
    CODE:
        try {
            RETVAL = store()->isValidPath(path);
        } catch (Error & e) {
            croak("%s", e.what());
        }
    OUTPUT:
        RETVAL


SV * queryReferences(char * path)
    PPCODE:
        try {
            PathSet paths = store()->queryPathInfo(path)->references;
            for (PathSet::iterator i = paths.begin(); i != paths.end(); ++i)
                XPUSHs(sv_2mortal(newSVpv(i->c_str(), 0)));
        } catch (Error & e) {
            croak("%s", e.what());
        }


SV * queryPathHash(char * path)
    PPCODE:
        try {
            auto s = store()->queryPathInfo(path)->narHash.to_string();
            XPUSHs(sv_2mortal(newSVpv(s.c_str(), 0)));
        } catch (Error & e) {
            croak("%s", e.what());
        }


SV * queryDeriver(char * path)
    PPCODE:
        try {
            auto deriver = store()->queryPathInfo(path)->deriver;
            if (deriver == "") XSRETURN_UNDEF;
            XPUSHs(sv_2mortal(newSVpv(deriver.c_str(), 0)));
        } catch (Error & e) {
            croak("%s", e.what());
        }


SV * queryPathInfo(char * path, int base32)
    PPCODE:
        try {
            auto info = store()->queryPathInfo(path);
            if (info->deriver == "")
                XPUSHs(&PL_sv_undef);
            else
                XPUSHs(sv_2mortal(newSVpv(info->deriver.c_str(), 0)));
            auto s = info->narHash.to_string(base32 ? Base32 : Base16);
            XPUSHs(sv_2mortal(newSVpv(s.c_str(), 0)));
            mXPUSHi(info->registrationTime);
            mXPUSHi(info->narSize);
            AV * arr = newAV();
            for (PathSet::iterator i = info->references.begin(); i != info->references.end(); ++i)
                av_push(arr, newSVpv(i->c_str(), 0));
            XPUSHs(sv_2mortal(newRV((SV *) arr)));
        } catch (Error & e) {
            croak("%s", e.what());
        }


SV * queryPathFromHashPart(char * hashPart)
    PPCODE:
        try {
            Path path = store()->queryPathFromHashPart(hashPart);
            XPUSHs(sv_2mortal(newSVpv(path.c_str(), 0)));
        } catch (Error & e) {
            croak("%s", e.what());
        }


SV * computeFSClosure(int flipDirection, int includeOutputs, ...)
    PPCODE:
        try {
            PathSet paths;
            for (int n = 2; n < items; ++n)
                store()->computeFSClosure(SvPV_nolen(ST(n)), paths, flipDirection, includeOutputs);
            for (PathSet::iterator i = paths.begin(); i != paths.end(); ++i)
                XPUSHs(sv_2mortal(newSVpv(i->c_str(), 0)));
        } catch (Error & e) {
            croak("%s", e.what());
        }


SV * topoSortPaths(...)
    PPCODE:
        try {
            PathSet paths;
            for (int n = 0; n < items; ++n) paths.insert(SvPV_nolen(ST(n)));
            Paths sorted = store()->topoSortPaths(paths);
            for (Paths::iterator i = sorted.begin(); i != sorted.end(); ++i)
                XPUSHs(sv_2mortal(newSVpv(i->c_str(), 0)));
        } catch (Error & e) {
            croak("%s", e.what());
        }


SV * followLinksToStorePath(char * path)
    CODE:
        try {
            RETVAL = newSVpv(store()->followLinksToStorePath(path).c_str(), 0);
        } catch (Error & e) {
            croak("%s", e.what());
        }
    OUTPUT:
        RETVAL


void exportPaths(int fd, ...)
    PPCODE:
        try {
            Paths paths;
            for (int n = 1; n < items; ++n) paths.push_back(SvPV_nolen(ST(n)));
            FdSink sink(fd);
            store()->exportPaths(paths, sink);
        } catch (Error & e) {
            croak("%s", e.what());
        }


void importPaths(int fd, int dontCheckSigs)
    PPCODE:
        try {
            FdSource source(fd);
            store()->importPaths(source, nullptr, dontCheckSigs ? NoCheckSigs : CheckSigs);
        } catch (Error & e) {
            croak("%s", e.what());
        }


SV * hashPath(char * algo, int base32, char * path)
    PPCODE:
        try {
            Hash h = hashPath(parseHashType(algo), path).first;
            auto s = h.to_string(base32 ? Base32 : Base16, false);
            XPUSHs(sv_2mortal(newSVpv(s.c_str(), 0)));
        } catch (Error & e) {
            croak("%s", e.what());
        }


SV * hashFile(char * algo, int base32, char * path)
    PPCODE:
        try {
            Hash h = hashFile(parseHashType(algo), path);
            auto s = h.to_string(base32 ? Base32 : Base16, false);
            XPUSHs(sv_2mortal(newSVpv(s.c_str(), 0)));
        } catch (Error & e) {
            croak("%s", e.what());
        }


SV * hashString(char * algo, int base32, char * s)
    PPCODE:
        try {
            Hash h = hashString(parseHashType(algo), s);
            auto s = h.to_string(base32 ? Base32 : Base16, false);
            XPUSHs(sv_2mortal(newSVpv(s.c_str(), 0)));
        } catch (Error & e) {
            croak("%s", e.what());
        }


SV * convertHash(char * algo, char * s, int toBase32)
    PPCODE:
        try {
            Hash h(s, parseHashType(algo));
            string s = h.to_string(toBase32 ? Base32 : Base16, false);
            XPUSHs(sv_2mortal(newSVpv(s.c_str(), 0)));
        } catch (Error & e) {
            croak("%s", e.what());
        }


SV * signString(char * secretKey_, char * msg)
    PPCODE:
        try {
#if HAVE_SODIUM
            auto sig = SecretKey(secretKey_).signDetached(msg);
            XPUSHs(sv_2mortal(newSVpv(sig.c_str(), sig.size())));
#else
            throw Error("Nix was not compiled with libsodium, required for signed binary cache support");
#endif
        } catch (Error & e) {
            croak("%s", e.what());
        }


int checkSignature(SV * publicKey_, SV * sig_, char * msg)
    CODE:
        try {
#if HAVE_SODIUM
            STRLEN publicKeyLen;
            unsigned char * publicKey = (unsigned char *) SvPV(publicKey_, publicKeyLen);
            if (publicKeyLen != crypto_sign_PUBLICKEYBYTES)
                throw Error("public key is not valid");

            STRLEN sigLen;
            unsigned char * sig = (unsigned char *) SvPV(sig_, sigLen);
            if (sigLen != crypto_sign_BYTES)
                throw Error("signature is not valid");

            RETVAL = crypto_sign_verify_detached(sig, (unsigned char *) msg, strlen(msg), publicKey) == 0;
#else
            throw Error("Nix was not compiled with libsodium, required for signed binary cache support");
#endif
        } catch (Error & e) {
            croak("%s", e.what());
        }
    OUTPUT:
        RETVAL


SV * addToStore(char * srcPath, int recursive, char * algo)
    PPCODE:
        try {
            Path path = store()->addToStore(baseNameOf(srcPath), srcPath, recursive, parseHashType(algo));
            XPUSHs(sv_2mortal(newSVpv(path.c_str(), 0)));
        } catch (Error & e) {
            croak("%s", e.what());
        }


SV * makeFixedOutputPath(int recursive, char * algo, char * hash, char * name)
    PPCODE:
        try {
            Hash h(hash, parseHashType(algo));
            Path path = store()->makeFixedOutputPath(recursive, h, name);
            XPUSHs(sv_2mortal(newSVpv(path.c_str(), 0)));
        } catch (Error & e) {
            croak("%s", e.what());
        }


SV * derivationFromPath(char * drvPath)
    PREINIT:
        HV *hash;
    CODE:
        try {
            Derivation drv = store()->derivationFromPath(drvPath);
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
            croak("%s", e.what());
        }
    OUTPUT:
        RETVAL


void addTempRoot(char * storePath)
    PPCODE:
        try {
            store()->addTempRoot(storePath);
        } catch (Error & e) {
            croak("%s", e.what());
        }
