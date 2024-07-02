namespace nix {

#include "nix/util/fs-sink.hh"
#include "nix/util/logging.hh"

struct LoggerSettings : Config
{
    Setting<bool> showTrace{
        this,
        false,
        "show-trace",
        R"(
          Whether Nix should print out a stack trace in case of Nix
          expression evaluation errors.
        )"};
};

static GlobalConfig::Register r1(&restoreSinkSettings);

struct RestoreSinkSettings : Config
{
    Setting<bool> preallocateContents{
        this, false, "preallocate-contents", "Whether to preallocate files when writing objects with known size."};
};

static GlobalConfig::Register rLoggerSettings(&loggerSettings);

struct ArchiveSettings : Config
{
    Setting<bool> useCaseHack
    {
        this,
#if __APPLE__
            true,
#else
            false,
#endif
            "use-case-hack", "Whether to enable a macOS-specific hack for dealing with file name case collisions."
    };
};

static GlobalConfig::Register rArchiveSettings(&archiveSettings);

}
