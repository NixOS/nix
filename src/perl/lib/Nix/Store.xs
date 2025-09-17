#include "EXTERN.h"
#include "perl.h"
#include "XSUB.h"

/* Prevent a clash between some Perl and libstdc++ macros. */
#undef do_open
#undef do_close

#include "nix/store/derivations.hh"
#include "nix/store/realisation.hh"
#include "nix/store/globals.hh"
#include "nix/store/store-open.hh"
#include "nix/util/posix-source-accessor.hh"
#include "nix/store/export-import.hh"

#include <sodium.h>
#include <nlohmann/json.hpp>

using namespace nix;

static bool libStoreInitialized = false;

struct StoreWrapper {
    ref<Store> store;
};

MODULE = Nix::Store PACKAGE = Nix::Store
PROTOTYPES: ENABLE

TYPEMAP: <<HERE
StoreWrapper *      O_OBJECT

OUTPUT
O_OBJECT
    sv_setref_pv( $arg, CLASS, (void*)$var );

INPUT
O_OBJECT
    if ( sv_isobject($arg) && (SvTYPE(SvRV($arg)) == SVt_PVMG) ) {
        $var = ($type)SvIV((SV*)SvRV( $arg ));
    }
    else {
        warn( \"${Package}::$func_name() -- \"
		\"$var not a blessed SV reference\");
        XSRETURN_UNDEF;
    }
HERE

#undef dNOOP // Hack to work around "error: declaration of 'Perl___notused' has a different language linkage" error message on clang.
#define dNOOP

void
StoreWrapper::DESTROY()

StoreWrapper *
StoreWrapper::new(char * s = nullptr)
    CODE:
        static std::shared_ptr<Store> _store;
        try {
            if (!libStoreInitialized) {
                initLibStore();
                libStoreInitialized = true;
            }
            if (items == 1) {
                _store = openStore();
                RETVAL = new StoreWrapper {
                    .store = ref<Store>{_store}
                };
            } else {
                RETVAL = new StoreWrapper {
                    .store = openStore(s)
                };
            }
        } catch (Error & e) {
            croak("%s", e.what());
        }
    OUTPUT:
        RETVAL


void init()
    CODE:
        if (!libStoreInitialized) {
            initLibStore();
            libStoreInitialized = true;
        }


void setVerbosity(int level)
    CODE:
        verbosity = (Verbosity) level;


int
StoreWrapper::isValidPath(char * path)
    CODE:
        try {
            RETVAL = THIS->store->isValidPath(THIS->store->parseStorePath(path));
        } catch (Error & e) {
            croak("%s", e.what());
        }
    OUTPUT:
        RETVAL


SV *
StoreWrapper::queryReferences(char * path)
    PPCODE:
        try {
            for (auto & i : THIS->store->queryPathInfo(THIS->store->parseStorePath(path))->references)
                XPUSHs(sv_2mortal(newSVpv(THIS->store->printStorePath(i).c_str(), 0)));
        } catch (Error & e) {
            croak("%s", e.what());
        }


SV *
StoreWrapper::queryPathHash(char * path)
    PPCODE:
        try {
            auto s = THIS->store->queryPathInfo(THIS->store->parseStorePath(path))->narHash.to_string(HashFormat::Nix32, true);
            XPUSHs(sv_2mortal(newSVpv(s.c_str(), 0)));
        } catch (Error & e) {
            croak("%s", e.what());
        }


SV *
StoreWrapper::queryDeriver(char * path)
    PPCODE:
        try {
            auto info = THIS->store->queryPathInfo(THIS->store->parseStorePath(path));
            if (!info->deriver) XSRETURN_UNDEF;
            XPUSHs(sv_2mortal(newSVpv(THIS->store->printStorePath(*info->deriver).c_str(), 0)));
        } catch (Error & e) {
            croak("%s", e.what());
        }


SV *
StoreWrapper::queryPathInfo(char * path, int base32)
    PPCODE:
        try {
            auto info = THIS->store->queryPathInfo(THIS->store->parseStorePath(path));
            if (!info->deriver)
                XPUSHs(&PL_sv_undef);
            else
                XPUSHs(sv_2mortal(newSVpv(THIS->store->printStorePath(*info->deriver).c_str(), 0)));
            auto s = info->narHash.to_string(base32 ? HashFormat::Nix32 : HashFormat::Base16, true);
            XPUSHs(sv_2mortal(newSVpv(s.c_str(), 0)));
            mXPUSHi(info->registrationTime);
            mXPUSHi(info->narSize);
            AV * refs = newAV();
            for (auto & i : info->references)
                av_push(refs, newSVpv(THIS->store->printStorePath(i).c_str(), 0));
            XPUSHs(sv_2mortal(newRV((SV *) refs)));
            AV * sigs = newAV();
            for (auto & i : info->sigs)
                av_push(sigs, newSVpv(i.c_str(), 0));
            XPUSHs(sv_2mortal(newRV((SV *) sigs)));
        } catch (Error & e) {
            croak("%s", e.what());
        }

SV *
StoreWrapper::queryRawRealisation(char * outputId)
    PPCODE:
      try {
        auto realisation = THIS->store->queryRealisation(DrvOutput::parse(outputId));
        if (realisation)
            XPUSHs(sv_2mortal(newSVpv(static_cast<nlohmann::json>(*realisation).dump().c_str(), 0)));
        else
            XPUSHs(sv_2mortal(newSVpv("", 0)));
      } catch (Error & e) {
        croak("%s", e.what());
      }


SV *
StoreWrapper::queryPathFromHashPart(char * hashPart)
    PPCODE:
        try {
            auto path = THIS->store->queryPathFromHashPart(hashPart);
            XPUSHs(sv_2mortal(newSVpv(path ? THIS->store->printStorePath(*path).c_str() : "", 0)));
        } catch (Error & e) {
            croak("%s", e.what());
        }


SV *
StoreWrapper::computeFSClosure(int flipDirection, int includeOutputs, ...)
    PPCODE:
        try {
            StorePathSet paths;
            for (int n = 3; n < items; ++n)
                THIS->store->computeFSClosure(THIS->store->parseStorePath(SvPV_nolen(ST(n))), paths, flipDirection, includeOutputs);
            for (auto & i : paths)
                XPUSHs(sv_2mortal(newSVpv(THIS->store->printStorePath(i).c_str(), 0)));
        } catch (Error & e) {
            croak("%s", e.what());
        }


SV *
StoreWrapper::topoSortPaths(...)
    PPCODE:
        try {
            StorePathSet paths;
            for (int n = 1; n < items; ++n) paths.insert(THIS->store->parseStorePath(SvPV_nolen(ST(n))));
            auto sorted = THIS->store->topoSortPaths(paths);
            for (auto & i : sorted)
                XPUSHs(sv_2mortal(newSVpv(THIS->store->printStorePath(i).c_str(), 0)));
        } catch (Error & e) {
            croak("%s", e.what());
        }


SV *
StoreWrapper::followLinksToStorePath(char * path)
    CODE:
        try {
            RETVAL = newSVpv(THIS->store->printStorePath(THIS->store->followLinksToStorePath(path)).c_str(), 0);
        } catch (Error & e) {
            croak("%s", e.what());
        }
    OUTPUT:
        RETVAL


void
StoreWrapper::exportPaths(int fd, ...)
    PPCODE:
        try {
            StorePathSet paths;
            for (int n = 2; n < items; ++n) paths.insert(THIS->store->parseStorePath(SvPV_nolen(ST(n))));
            FdSink sink(fd);
            exportPaths(*THIS->store, paths, sink);
        } catch (Error & e) {
            croak("%s", e.what());
        }


void
StoreWrapper::importPaths(int fd, int dontCheckSigs)
    PPCODE:
        try {
            FdSource source(fd);
            importPaths(*THIS->store, source, dontCheckSigs ? NoCheckSigs : CheckSigs);
        } catch (Error & e) {
            croak("%s", e.what());
        }


SV *
hashPath(char * algo, int base32, char * path)
    PPCODE:
        try {
            Hash h = hashPath(
                PosixSourceAccessor::createAtRoot(path),
                FileIngestionMethod::NixArchive, parseHashAlgo(algo)).first;
            auto s = h.to_string(base32 ? HashFormat::Nix32 : HashFormat::Base16, false);
            XPUSHs(sv_2mortal(newSVpv(s.c_str(), 0)));
        } catch (Error & e) {
            croak("%s", e.what());
        }


SV * hashFile(char * algo, int base32, char * path)
    PPCODE:
        try {
            Hash h = hashFile(parseHashAlgo(algo), path);
            auto s = h.to_string(base32 ? HashFormat::Nix32 : HashFormat::Base16, false);
            XPUSHs(sv_2mortal(newSVpv(s.c_str(), 0)));
        } catch (Error & e) {
            croak("%s", e.what());
        }


SV * hashString(char * algo, int base32, char * s)
    PPCODE:
        try {
            Hash h = hashString(parseHashAlgo(algo), s);
            auto s = h.to_string(base32 ? HashFormat::Nix32 : HashFormat::Base16, false);
            XPUSHs(sv_2mortal(newSVpv(s.c_str(), 0)));
        } catch (Error & e) {
            croak("%s", e.what());
        }


SV * convertHash(char * algo, char * s, int toBase32)
    PPCODE:
        try {
            auto h = Hash::parseAny(s, parseHashAlgo(algo));
            auto s = h.to_string(toBase32 ? HashFormat::Nix32 : HashFormat::Base16, false);
            XPUSHs(sv_2mortal(newSVpv(s.c_str(), 0)));
        } catch (Error & e) {
            croak("%s", e.what());
        }


SV * signString(char * secretKey_, char * msg)
    PPCODE:
        try {
            auto sig = SecretKey(secretKey_).signDetached(msg);
            XPUSHs(sv_2mortal(newSVpv(sig.c_str(), sig.size())));
        } catch (Error & e) {
            croak("%s", e.what());
        }


int checkSignature(SV * publicKey_, SV * sig_, char * msg)
    CODE:
        try {
            STRLEN publicKeyLen;
            unsigned char * publicKey = (unsigned char *) SvPV(publicKey_, publicKeyLen);
            if (publicKeyLen != crypto_sign_PUBLICKEYBYTES)
                throw Error("public key is not valid");

            STRLEN sigLen;
            unsigned char * sig = (unsigned char *) SvPV(sig_, sigLen);
            if (sigLen != crypto_sign_BYTES)
                throw Error("signature is not valid");

            RETVAL = crypto_sign_verify_detached(sig, (unsigned char *) msg, strlen(msg), publicKey) == 0;
        } catch (Error & e) {
            croak("%s", e.what());
        }
    OUTPUT:
        RETVAL


SV *
StoreWrapper::addToStore(char * srcPath, int recursive, char * algo)
    PPCODE:
        try {
            auto method = recursive ? ContentAddressMethod::Raw::NixArchive : ContentAddressMethod::Raw::Flat;
            auto path = THIS->store->addToStore(
                std::string(baseNameOf(srcPath)),
                PosixSourceAccessor::createAtRoot(srcPath),
                method, parseHashAlgo(algo));
            XPUSHs(sv_2mortal(newSVpv(THIS->store->printStorePath(path).c_str(), 0)));
        } catch (Error & e) {
            croak("%s", e.what());
        }


SV *
StoreWrapper::makeFixedOutputPath(int recursive, char * algo, char * hash, char * name)
    PPCODE:
        try {
            auto h = Hash::parseAny(hash, parseHashAlgo(algo));
            auto method = recursive ? FileIngestionMethod::NixArchive : FileIngestionMethod::Flat;
            auto path = THIS->store->makeFixedOutputPath(name, FixedOutputInfo {
                .method = method,
                .hash = h,
                .references = {},
            });
            XPUSHs(sv_2mortal(newSVpv(THIS->store->printStorePath(path).c_str(), 0)));
        } catch (Error & e) {
            croak("%s", e.what());
        }


SV *
StoreWrapper::derivationFromPath(char * drvPath)
    PREINIT:
        HV *hash;
    CODE:
        try {
            Derivation drv = THIS->store->derivationFromPath(THIS->store->parseStorePath(drvPath));
            hash = newHV();

            HV * outputs = newHV();
            for (auto & i : drv.outputsAndOptPaths(*THIS->store)) {
                hv_store(
                    outputs, i.first.c_str(), i.first.size(),
                    !i.second.second
                        ? newSV(0) /* null value */
                        : newSVpv(THIS->store->printStorePath(*i.second.second).c_str(), 0),
                    0);
            }
            hv_stores(hash, "outputs", newRV((SV *) outputs));

            AV * inputDrvs = newAV();
            for (auto & i : drv.inputDrvs.map)
                av_push(inputDrvs, newSVpv(THIS->store->printStorePath(i.first).c_str(), 0)); // !!! ignores i->second
            hv_stores(hash, "inputDrvs", newRV((SV *) inputDrvs));

            AV * inputSrcs = newAV();
            for (auto & i : drv.inputSrcs)
                av_push(inputSrcs, newSVpv(THIS->store->printStorePath(i).c_str(), 0));
            hv_stores(hash, "inputSrcs", newRV((SV *) inputSrcs));

            hv_stores(hash, "platform", newSVpv(drv.platform.c_str(), 0));
            hv_stores(hash, "builder", newSVpv(drv.builder.c_str(), 0));

            AV * args = newAV();
            for (auto & i : drv.args)
                av_push(args, newSVpv(i.c_str(), 0));
            hv_stores(hash, "args", newRV((SV *) args));

            HV * env = newHV();
            for (auto & i : drv.env)
                hv_store(env, i.first.c_str(), i.first.size(), newSVpv(i.second.c_str(), 0), 0);
            hv_stores(hash, "env", newRV((SV *) env));

            RETVAL = newRV_noinc((SV *)hash);
        } catch (Error & e) {
            croak("%s", e.what());
        }
    OUTPUT:
        RETVAL


void
StoreWrapper::addTempRoot(char * storePath)
    PPCODE:
        try {
            THIS->store->addTempRoot(THIS->store->parseStorePath(storePath));
        } catch (Error & e) {
            croak("%s", e.what());
        }


SV * getStoreDir()
    PPCODE:
        XPUSHs(sv_2mortal(newSVpv(settings.nixStore.c_str(), 0)));
