#include "command.hh"
#include "common-args.hh"
#include "shared.hh"
#include "store-api.hh"
#include "local-fs-store.hh"
#include "fs-accessor.hh"
#include "eval-cache.hh"

using namespace nix;
using namespace nix::flake;

struct CmdFlakeInitCommon : virtual Args, EvalCommand
{
    std::string templateUrl = "templates";
    Path destDir;

    const Strings attrsPathPrefixes{"templates."};
    const LockFlags lockFlags{ .writeLockFile = false };

    CmdFlakeInitCommon()
    {
        addFlag({
            .longName = "template",
            .shortName = 't',
            .description = "The template to use.",
            .labels = {"template"},
            .handler = {&templateUrl},
            .completer = {[&](size_t, std::string_view prefix) {
                completeFlakeRefWithFragment(
                    getEvalState(),
                    lockFlags,
                    attrsPathPrefixes,
                    {"defaultTemplate"},
                    prefix);
            }}
        });
    }

    void run(nix::ref<nix::Store> store) override
    {
        auto flakeDir = absPath(destDir);

        auto evalState = getEvalState();

        auto [templateFlakeRef, templateName] = parseFlakeRefWithFragment(templateUrl, absPath("."));

        auto installable = InstallableFlake(nullptr,
            evalState, std::move(templateFlakeRef),
            Strings{templateName == "" ? "defaultTemplate" : templateName},
            Strings(attrsPathPrefixes), lockFlags);

        auto [cursor, attrPath] = installable.getCursor(*evalState);

        auto templateDir = cursor->getAttr("path")->getString();

        assert(store->isInStore(templateDir));

        std::vector<Path> files;

        std::function<void(const Path & from, const Path & to)> copyDir;
        copyDir = [&](const Path & from, const Path & to)
        {
            createDirs(to);

            for (auto & entry : readDirectory(from)) {
                auto from2 = from + "/" + entry.name;
                auto to2 = to + "/" + entry.name;
                auto st = lstat(from2);
                if (S_ISDIR(st.st_mode))
                    copyDir(from2, to2);
                else if (S_ISREG(st.st_mode)) {
                    auto contents = readFile(from2);
                    if (pathExists(to2)) {
                        auto contents2 = readFile(to2);
                        if (contents != contents2)
                            throw Error("refusing to overwrite existing file '%s'", to2);
                    } else
                        writeFile(to2, contents);
                }
                else if (S_ISLNK(st.st_mode)) {
                    auto target = readLink(from2);
                    if (pathExists(to2)) {
                        if (readLink(to2) != target)
                            throw Error("refusing to overwrite existing symlink '%s'", to2);
                    } else
                          createSymlink(target, to2);
                }
                else
                    throw Error("file '%s' has unsupported type", from2);
                files.push_back(to2);
            }
        };

        copyDir(templateDir, flakeDir);

        if (pathExists(flakeDir + "/.git")) {
            Strings args = { "-C", flakeDir, "add", "--intent-to-add", "--force", "--" };
            for (auto & s : files) args.push_back(s);
            runProgram("git", true, args);
        }
    }
};

struct CmdFlakeInit : CmdFlakeInitCommon
{
    std::string description() override
    {
        return "create a flake in the current directory from a template";
    }

    std::string doc() override
    {
        return
          #include "init.md"
          ;
    }

    CmdFlakeInit()
    {
        destDir = ".";
    }
};

struct CmdFlakeNew : CmdFlakeInitCommon
{
    std::string description() override
    {
        return "create a flake in the specified directory from a template";
    }

    std::string doc() override
    {
        return
          #include "new.md"
          ;
    }

    CmdFlakeNew()
    {
        expectArgs({
            .label = "dest-dir",
            .handler = {&destDir},
            .completer = completePath
        });
    }
};

static auto r0 = registerCommand<CmdFlakeInit>("init");
static auto r1 = registerCommand<CmdFlakeNew>("new");

