#pragma once
///@file

#include "nix/util/args.hh"
#include "nix/util/repair-flag.hh"

namespace nix {

// static constexpr auto commonArgsCategory = "Miscellaneous common options";
static constexpr auto loggingCategory = "Logging-related options";
static constexpr auto miscCategory = "Miscellaneous global options";

class MixCommonArgs : public virtual Args
{
    void initialFlagsProcessed() override;
public:
    std::string programName;
    MixCommonArgs(const std::string & programName);
protected:
    virtual void pluginsInited() {}
};

struct MixDryRun : virtual Args
{
    bool dryRun = false;

    MixDryRun()
    {
        addFlag({
            .longName = "dry-run",
            .description = "Show what this command would do without doing it.",
            .handler = {&dryRun, true},
        });
    }
};

/**
 * Commands that can print JSON according to the
 * `--pretty`/`--no-pretty` flag.
 *
 * This is distinct from MixJSON, because for some commands,
 * JSON outputs is not optional.
 */
struct MixPrintJSON : virtual Args
{
    bool outputPretty = isatty(STDOUT_FILENO);

    MixPrintJSON()
    {
        addFlag({
            .longName = "pretty",
            .description =
                R"(
                    Print multi-line, indented JSON output for readability.

                    Default: indent if output is to a terminal.

                    This option is only effective when `--json` is also specified.
                )",
            .handler = {&outputPretty, true},
        });
        addFlag({
            .longName = "no-pretty",
            .description =
                R"(
                    Print compact JSON output on a single line, even when the output is a terminal.
                    Some commands may print multiple JSON objects on separate lines.

                    See `--pretty`.
                )",
            .handler = {&outputPretty, false},
        });
    };

    /**
     * Print an `nlohmann::json` to stdout
     *
     * - respecting `--pretty` / `--no-pretty`.
     * - suspending the progress bar
     *
     * This is a template to avoid accidental coercions from `string` to `json` in the caller,
     * to avoid mistakenly passing an already serialized JSON to this function.
     *
     * It is not recommended to print a JSON string - see the JSON guidelines
     * about extensibility, https://nix.dev/manual/nix/development/development/json-guideline.html -
     * but you _can_ print a sole JSON string by explicitly coercing it to
     * `nlohmann::json` first.
     */
    template<typename T, typename = std::enable_if_t<std::is_same_v<T, nlohmann::json>>>
    void printJSON(const T & json);
};

/** Optional JSON support via `--json` flag */
struct MixJSON : virtual Args, virtual MixPrintJSON
{
    bool json = false;

    MixJSON()
    {
        addFlag({
            .longName = "json",
            .description = "Produce output in JSON format, suitable for consumption by another program.",
            .handler = {&json, true},
        });
    }
};

struct MixRepair : virtual Args
{
    RepairFlag repair = NoRepair;

    MixRepair()
    {
        addFlag({
            .longName = "repair",
            .description = "During evaluation, rewrite missing or corrupted files in the Nix store. "
                           "During building, rebuild missing or corrupted store paths.",
            .category = miscCategory,
            .handler = {&repair, Repair},
        });
    }
};

} // namespace nix
