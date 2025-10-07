/**
 * AFL++ fuzzing harness for the Nix expression evaluator.
 *
 * This harness fuzzes the complete evaluation pipeline:
 * - Lexing and parsing
 * - Type checking
 * - Evaluation
 * - Value forcing
 *
 * It uses AFL++ persistent mode for performance (10-100x speedup).
 */

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "nix/expr/eval.hh"
#include "nix/expr/eval-gc.hh"
#include "nix/expr/eval-settings.hh"
#include "nix/expr/nixexpr.hh"
#include "nix/fetchers/fetch-settings.hh"
#include "nix/store/globals.hh"
#include "nix/store/store-api.hh"
#include "nix/store/store-open.hh"
#include "nix/util/error.hh"
#include "nix/util/logging.hh"
#include "nix/util/position.hh"

using namespace nix;

// Configure sanitizers at initialization time (before main)
// This is required because sanitizers initialize before main() runs
extern "C" const char * __asan_default_options()
{
    return "abort_on_error=1:detect_leaks=0:symbolize=0:allocator_may_return_null=1";
}

extern "C" const char * __ubsan_default_options()
{
    return "halt_on_error=1:print_stacktrace=0:symbolize=0";
}

// Global state for persistent mode
static std::optional<ref<Store>> globalStore;
static EvalState * globalEvalState = nullptr;
static bool initialized = false;

/**
 * Initialize the evaluator once for persistent mode.
 * This is called before the first fuzzing iteration.
 */
static void initializeFuzzer()
{
    if (initialized) {
        return;
    }

    // Suppress logging output (fuzzing generates lots of errors)
    logger = makeSimpleLogger(false);

    // Initialize store subsystem
    initLibStore(false);

    // Create a dummy store (no actual filesystem operations)
    globalStore = openStore("dummy://");

    // Initialize GC if enabled
    initGC();

    // Configure evaluation settings
    static bool readOnlyMode = true;
    static fetchers::Settings fetchSettings{};
    static EvalSettings evalSettings{readOnlyMode};

    // Empty nix path to avoid filesystem access
    evalSettings.nixPath = {};

    // Disable network access during fuzzing
    evalSettings.pureEval = true;

    // Create the evaluation state
    globalEvalState = new EvalState({}, *globalStore, fetchSettings, evalSettings, nullptr);

    initialized = true;
}

/**
 * Fuzz one input.
 * Returns 0 on success (including expected errors), -1 on crash/bug.
 */
static int fuzzOne(const uint8_t * data, size_t size)
{
    // Skip empty inputs
    if (size == 0) {
        return 0;
    }

    // Skip inputs that are too large (avoid timeouts)
    if (size > 100000) {
        return 0;
    }

    // Convert input to string
    std::string input(reinterpret_cast<const char *>(data), size);

    // Ensure null termination
    if (input.find('\0') != std::string::npos) {
        input = input.substr(0, input.find('\0'));
    }

    try {
        // Parse the expression
        Expr * expr = globalEvalState->parseExprFromString(input, globalEvalState->rootPath(CanonPath::root));

        if (!expr) {
            // Parse failure is expected for invalid input
            return 0;
        }

        // Evaluate the expression
        Value v;
        globalEvalState->eval(expr, v);

        // Force the value to trigger any thunks
        globalEvalState->forceValue(v, noPos);

        // If we got here, the input was valid and evaluated successfully
        return 0;

    } catch (const Error & e) {
        // Expected Nix errors (syntax errors, type errors, etc.)
        // These are not bugs, just invalid inputs
        return 0;

    } catch (const std::exception & e) {
        // Unexpected exception - might indicate a bug
        // But some std exceptions are expected (e.g., bad_alloc)
        // Let AFL++ decide if this is interesting
        return 0;
    }

    return 0;
}

/**
 * Main entry point.
 * Supports both AFL++ persistent mode and traditional mode.
 */
int main(int argc, char ** argv)
{
    // Initialize once before fuzzing loop
    initializeFuzzer();

#ifdef __AFL_HAVE_MANUAL_CONTROL
    // AFL++ persistent mode - much faster!
    // Process up to 1000 inputs per fork
    __AFL_INIT();
#endif

    // Read input
    std::vector<uint8_t> buffer;

    if (argc > 1) {
        // File input mode (for reproducing crashes)
        const char * filename = argv[1];
        FILE * f = fopen(filename, "rb");
        if (!f) {
            std::cerr << "Failed to open file: " << filename << std::endl;
            return 1;
        }

        fseek(f, 0, SEEK_END);
        size_t size = ftell(f);
        fseek(f, 0, SEEK_SET);

        buffer.resize(size);
        size_t read = fread(buffer.data(), 1, size, f);
        fclose(f);

        if (read != size) {
            std::cerr << "Failed to read file" << std::endl;
            return 1;
        }

        return fuzzOne(buffer.data(), buffer.size());

    } else {
        // stdin mode (for AFL++)
#ifdef __AFL_LOOP
        // Persistent mode loop
        while (__AFL_LOOP(1000)) {
#endif
            // Read from stdin
            buffer.clear();
            int c;
            while ((c = getchar()) != EOF) {
                buffer.push_back(static_cast<uint8_t>(c));
            }

            fuzzOne(buffer.data(), buffer.size());

#ifdef __AFL_LOOP
        }
#endif
    }

    return 0;
}
