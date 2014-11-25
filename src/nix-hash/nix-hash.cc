#include "hash.hh"
#include "shared.hh"

#include <iostream>

using namespace nix;


int main(int argc, char * * argv)
{
    HashType ht = htMD5;
    bool flat = false;
    bool base32 = false;
    bool truncate = false;
    enum { opHash, opTo32, opTo16 } op = opHash;

    Strings ss;

    return handleExceptions(argv[0], [&]() {
        initNix();

        parseCmdLine(argc, argv, [&](Strings::iterator & arg, const Strings::iterator & end) {
            if (*arg == "--help")
                showManPage("nix-hash");
            else if (*arg == "--version")
                printVersion("nix-hash");
            else if (*arg == "--flat") flat = true;
            else if (*arg == "--base32") base32 = true;
            else if (*arg == "--truncate") truncate = true;
            else if (*arg == "--type") {
                string s = getArg(*arg, arg, end);
                ht = parseHashType(s);
                if (ht == htUnknown)
                    throw UsageError(format("unknown hash type ‘%1%’") % s);
            }
            else if (*arg == "--to-base16") op = opTo16;
            else if (*arg == "--to-base32") op = opTo32;
            else if (*arg != "" && arg->at(0) == '-')
                return false;
            else
                ss.push_back(*arg);
            return true;
        });

        if (op == opHash) {
            for (auto & i : ss) {
                Hash h = flat ? hashFile(ht, i) : hashPath(ht, i).first;
                if (truncate && h.hashSize > 20) h = compressHash(h, 20);
                std::cout << format("%1%\n") %
                    (base32 ? printHash32(h) : printHash(h));
            }
        }

        else {
            for (auto & i : ss) {
                Hash h = parseHash16or32(ht, i);
                std::cout << format("%1%\n") %
                    (op == opTo16 ? printHash(h) : printHash32(h));
            }
        }
    });
}

