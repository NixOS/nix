#ifndef NIX_API_UTIL_H
#define NIX_API_UTIL_H
/**
 * @defgroup libutil libutil
 * @brief C bindings for nix libutil
 *
 * libutil is used for functionality shared between
 * different Nix modules.
 * @{
 */
/** @file
 * @brief Main entry for the libutil C bindings
 *
 * Also contains error handling utilities
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif
// cffi start

/** @defgroup errors Handling errors
 * @brief Dealing with errors from the Nix side
 *
 * To handle errors that can be returned from the Nix API,
 * a nix_c_context can be passed to any function that potentially returns an
 * error.
 *
 * Error information will be stored in this context, and can be retrieved
 * using nix_err_code and nix_err_msg.
 *
 * Passing NULL instead will cause the API to throw C++ errors.
 *
 * Example:
 * @code{.c}
 * int main() {
 *     nix_c_context* ctx = nix_c_context_create();
 *     nix_libutil_init(ctx);
 *     if (nix_err_code(ctx) != NIX_OK) {
 *         printf("error: %s\n", nix_err_msg(NULL, ctx, NULL));
 *         return 1;
 *     }
 *     return 0;
 * }
 * @endcode
 *  @{
 */
// Error codes
/**
 * @brief Type for error codes in the Nix system
 *
 * This type can have one of several predefined constants:
 * - NIX_OK: No error occurred (0)
 * - NIX_ERR_UNKNOWN: An unknown error occurred (-1)
 * - NIX_ERR_OVERFLOW: An overflow error occurred (-2)
 * - NIX_ERR_KEY: A key/index access error occurred in C API functions (-3)
 * - NIX_ERR_NIX_ERROR: A generic Nix error occurred (-4)
 */
enum nix_err {

    /**
     * @brief No error occurred.
     *
     * This error code is returned when no error has occurred during the function
     * execution.
     */
    NIX_OK = 0,

    /**
     * @brief An unknown error occurred.
     *
     * This error code is returned when an unknown error occurred during the
     * function execution.
     */
    NIX_ERR_UNKNOWN = -1,

    /**
     * @brief An overflow error occurred.
     *
     * This error code is returned when an overflow error occurred during the
     * function execution.
     */
    NIX_ERR_OVERFLOW = -2,

    /**
     * @brief A key/index access error occurred in C API functions.
     *
     * This error code is returned when accessing a key, index, or identifier that
     * does not exist in C API functions. Common scenarios include:
     * - Setting keys that don't exist (nix_setting_get, nix_setting_set)
     * - List indices that are out of bounds (nix_get_list_byidx*)
     * - Attribute names that don't exist (nix_get_attr_byname*)
     * - Attribute indices that are out of bounds (nix_get_attr_byidx*, nix_get_attr_name_byidx)
     *
     * This error typically indicates incorrect usage or assumptions about data structure
     * contents, rather than internal Nix evaluation errors.
     *
     * @note This error code should ONLY be returned by C API functions themselves,
     * not by underlying Nix evaluation. For example, evaluating `{}.foo` in Nix
     * will throw a normal error (NIX_ERR_NIX_ERROR), not NIX_ERR_KEY.
     */
    NIX_ERR_KEY = -3,

    /**
     * @brief A generic Nix error occurred.
     *
     * This error code is returned when a generic Nix error occurred during the
     * function execution.
     */
    NIX_ERR_NIX_ERROR = -4,

    /**
     * @brief A recoverable error occurred.
     *
     * This is used primarily by C API *consumers* to communicate that a failed
     * primop call should be retried on the next evaluation attempt.
     */
    NIX_ERR_RECOVERABLE = -5,
};

typedef enum nix_err nix_err;

/**
 * @brief Verbosity level
 *
 * @note This should be kept in sync with the C++ implementation (nix::Verbosity)
 */
enum nix_verbosity {
    NIX_LVL_ERROR = 0,
    NIX_LVL_WARN,
    NIX_LVL_NOTICE,
    NIX_LVL_INFO,
    NIX_LVL_TALKATIVE,
    NIX_LVL_CHATTY,
    NIX_LVL_DEBUG,
    NIX_LVL_VOMIT,
};

typedef enum nix_verbosity nix_verbosity;

/**
 * @brief This object stores error state.
 * @struct nix_c_context
 *
 * Passed as a first parameter to functions that can fail, to store error
 * information.
 *
 * Optional wherever it can be used, passing NULL instead will throw a C++
 * exception.
 *
 * The struct is laid out so that it can also be cast to nix_err* to inspect
 * directly:
 * @code{.c}
 * assert(*(nix_err*)ctx == NIX_OK);
 * @endcode
 * @note These can be reused between different function calls,
 *  but make sure not to use them for multiple calls simultaneously (which can
 * happen in callbacks).
 */
typedef struct nix_c_context nix_c_context;

/**
 * @brief Called to get the value of a string owned by Nix.
 *
 * The `start` data is borrowed and the function must not assume that the buffer persists after it returns.
 * @warning Don't assume that the string is NUL-terminated.
 *
 * @param[in] start the string to copy.
 * @param[in] n the string length.
 * @param[in] user_data optional, arbitrary data, passed to the nix_get_string_callback when it's called.
 */
typedef void (*nix_get_string_callback)(const char * start, unsigned int n, void * user_data);

// Function prototypes

/**
 * @brief Allocate a new nix_c_context.
 * @throws std::bad_alloc
 * @return allocated nix_c_context, owned by the caller. Free using
 * `nix_c_context_free`.
 */
nix_c_context * nix_c_context_create();
/**
 * @brief Free a nix_c_context. Does not fail.
 * @param[out] context The context to free, mandatory.
 */
void nix_c_context_free(nix_c_context * context);
/**
 *  @}
 */

/**
 * @brief Initializes nix_libutil and its dependencies.
 *
 * This function can be called multiple times, but should be called at least
 * once prior to any other nix function.
 *
 * @param[out] context Optional, stores error information
 * @return NIX_OK if the initialization is successful, or an error code
 * otherwise.
 */
nix_err nix_libutil_init(nix_c_context * context);

/** @defgroup settings Nix configuration settings
 *  @{
 */
/**
 * @brief Retrieves a setting from the nix global configuration.
 *
 * This function requires nix_libutil_init() to be called at least once prior to
 * its use.
 *
 * @param[out] context optional, Stores error information
 * @param[in] key The key of the setting to retrieve.
 * @param[in] callback Called with the setting value.
 * @param[in] user_data optional, arbitrary data, passed to the callback when it's called.
 * @see nix_get_string_callback
 * @return NIX_ERR_KEY if the setting is unknown, or NIX_OK if the setting was retrieved
 * successfully.
 */
nix_err nix_setting_get(nix_c_context * context, const char * key, nix_get_string_callback callback, void * user_data);

/**
 * @brief Sets a setting in the nix global configuration.
 *
 * Use "extra-<setting name>" to append to the setting's value.
 *
 * Settings only apply for new State%s. Call nix_plugins_init() when you are
 * done with the settings to load any plugins.
 *
 * @param[out] context optional, Stores error information
 * @param[in] key The key of the setting to set.
 * @param[in] value The value to set for the setting.
 * @return NIX_ERR_KEY if the setting is unknown, or NIX_OK if the setting was
 * set successfully.
 */
nix_err nix_setting_set(nix_c_context * context, const char * key, const char * value);

/**
 *  @}
 */
// todo: nix_plugins_init()

/**
 * @brief Retrieves the nix library version.
 *
 * Does not fail.
 * @return A static string representing the version of the nix library.
 */
const char * nix_version_get();

/** @addtogroup errors
 *  @{
 */
/**
 * @brief Retrieves the most recent error message from a context.
 *
 * @pre This function should only be called after a previous nix function has
 * returned an error.
 *
 * @param[out] context optional, the context to store errors in if this function
 * fails
 * @param[in] ctx the context to retrieve the error message from
 * @param[out] n optional: a pointer to an unsigned int that is set to the
 * length of the error.
 * @return nullptr if no error message was ever set,
 *         a borrowed pointer to the error message otherwise, which is valid
 *         until the next call to a Nix function, or until the context is
 *         destroyed.
 */
const char * nix_err_msg(nix_c_context * context, const nix_c_context * ctx, unsigned int * n);

/**
 * @brief Retrieves the error message from errorInfo in a context.
 *
 * Used to inspect nix Error messages.
 *
 * @pre This function should only be called after a previous nix function has
 * returned a NIX_ERR_NIX_ERROR
 *
 * @param[out] context optional, the context to store errors in if this function
 * fails
 * @param[in] read_context the context to retrieve the error message from.
 * @param[in] callback Called with the error message.
 * @param[in] user_data optional, arbitrary data, passed to the callback when it's called.
 * @see nix_get_string_callback
 * @return NIX_OK if there were no errors, an error code otherwise.
 */
nix_err nix_err_info_msg(
    nix_c_context * context, const nix_c_context * read_context, nix_get_string_callback callback, void * user_data);

/**
 * @brief Retrieves the error name from a context.
 *
 * Used to inspect nix Error messages.
 *
 * @pre This function should only be called after a previous nix function has
 * returned a NIX_ERR_NIX_ERROR
 *
 * @param context optional, the context to store errors in if this function
 * fails
 * @param[in] read_context the context to retrieve the error message from
 * @param[in] callback Called with the error name.
 * @param[in] user_data optional, arbitrary data, passed to the callback when it's called.
 * @see nix_get_string_callback
 * @return NIX_OK if there were no errors, an error code otherwise.
 */
nix_err nix_err_name(
    nix_c_context * context, const nix_c_context * read_context, nix_get_string_callback callback, void * user_data);

/**
 * @brief Retrieves the most recent error code from a nix_c_context
 *
 * Equivalent to reading the first field of the context.
 *
 * Does not fail
 *
 * @param[in] read_context the context to retrieve the error message from
 * @return most recent error code stored in the context.
 */
nix_err nix_err_code(const nix_c_context * read_context);

/**
 * @brief Set an error message on a nix context.
 *
 * This should be used when you want to throw an error from a PrimOp callback.
 *
 * All other use is internal to the API.
 *
 * @param context context to write the error message to, required unless C++ exceptions are supported
 * @param err The error code to set and return
 * @param msg The error message to set. This string is copied.
 * @returns the error code set
 */
nix_err nix_set_err_msg(nix_c_context * context, nix_err err, const char * msg);

/**
 * @brief Clear the error message from a nix context.
 *
 * This is performed implicitly by all functions that accept a context, so
 * this won't be necessary in most cases.
 * However, if you want to clear the error message without calling another
 * function, you can use this.
 *
 * Example use case: a higher order function that helps with error handling,
 * to make it more robust in the following scenario:
 *
 * 1. A previous call failed, and the error was caught and handled.
 * 2. The context is reused with our error handling helper function.
 * 3. The callback passed to the helper function doesn't actually make a call to
 *    a Nix function.
 * 4. The handled error is raised again, from an unrelated call.
 *
 * This failure can be avoided by clearing the error message after handling it.
 */
void nix_clear_err(nix_c_context * context);

/**
 * @brief Sets the verbosity level
 *
 * @param[out] context Optional, additional error context.
 * @param[in] level Verbosity level
 */
nix_err nix_set_verbosity(nix_c_context * context, nix_verbosity level);

/**
 *  @}
 */

/** @defgroup logger Logger
 * @brief Capture Nix log output via C callbacks
 *
 * The functions in this section let an embedder replace Nix's global
 * logger with one driven by user-supplied callbacks. This is the
 * intended way to surface `builtins.trace` calls, builder output,
 * warnings, and other diagnostic messages produced by libnixutil,
 * libnixstore and libnixexpr to a host language.
 *  @{
 */

/**
 * @brief Activity identifier.
 *
 * `0` means "no activity" (used as the parent of top-level activities).
 */
typedef uint64_t nix_activity_id;

/**
 * @brief Activity type.
 *
 * @note Must be kept in sync with `nix::ActivityType`.
 */
enum nix_activity_type {
    NIX_ACTIVITY_TYPE_NONE = 0,
    NIX_ACTIVITY_TYPE_COPY_PATH = 100,
    NIX_ACTIVITY_TYPE_FILE_TRANSFER = 101,
    NIX_ACTIVITY_TYPE_REALISE = 102,
    NIX_ACTIVITY_TYPE_COPY_PATHS = 103,
    NIX_ACTIVITY_TYPE_BUILDS = 104,
    NIX_ACTIVITY_TYPE_BUILD = 105,
    NIX_ACTIVITY_TYPE_OPTIMISE_STORE = 106,
    NIX_ACTIVITY_TYPE_VERIFY_PATHS = 107,
    NIX_ACTIVITY_TYPE_SUBSTITUTE = 108,
    NIX_ACTIVITY_TYPE_QUERY_PATH_INFO = 109,
    NIX_ACTIVITY_TYPE_POST_BUILD_HOOK = 110,
    NIX_ACTIVITY_TYPE_BUILD_WAITING = 111,
    NIX_ACTIVITY_TYPE_FETCH_TREE = 112,
};
typedef enum nix_activity_type nix_activity_type;

/**
 * @brief Activity result type.
 *
 * @note Must be kept in sync with `nix::ResultType`.
 */
enum nix_result_type {
    NIX_RESULT_TYPE_FILE_LINKED = 100,
    NIX_RESULT_TYPE_BUILD_LOG_LINE = 101,
    NIX_RESULT_TYPE_UNTRUSTED_PATH = 102,
    NIX_RESULT_TYPE_CORRUPTED_PATH = 103,
    NIX_RESULT_TYPE_SET_PHASE = 104,
    NIX_RESULT_TYPE_PROGRESS = 105,
    NIX_RESULT_TYPE_SET_EXPECTED = 106,
    NIX_RESULT_TYPE_POST_BUILD_LOG_LINE = 107,
    NIX_RESULT_TYPE_FETCH_STATUS = 108,
    NIX_RESULT_TYPE_HASH_MISMATCH = 109,
    NIX_RESULT_TYPE_BUILD_RESULT = 110,
};
typedef enum nix_result_type nix_result_type;

/**
 * @brief Logger field type.
 *
 * @note Must be kept in sync with `nix::Logger::Field::type`.
 */
enum nix_logger_field_type {
    NIX_LOGGER_FIELD_TYPE_INT = 0,
    NIX_LOGGER_FIELD_TYPE_STR = 1,
};
typedef enum nix_logger_field_type nix_logger_field_type;

/**
 * @brief String value for a logger field.
 */
struct nix_logger_field_value_string
{
    const char * value;
    unsigned int len;
};
typedef struct nix_logger_field_value_string nix_logger_field_value_string;

/**
 * @brief Value held inside of a logger field.
 */
union nix_logger_field_value
{
    uint64_t i;
    nix_logger_field_value_string str;
};
typedef union nix_logger_field_value nix_logger_field_value;

/**
 * @brief Logger field.
 */
struct nix_logger_field
{
    nix_logger_field_type type;
    nix_logger_field_value value;
};
typedef struct nix_logger_field nix_logger_field;

/**
 * @brief Vtable of callbacks for a custom logger.
 *
 * Any field may be NULL, missing callbacks are treated as no-ops.
 *
 * @warning Callbacks may be invoked concurrently from multiple threads
 * (notably during parallel builds). Implementations are responsible
 * for their own synchronization.
 *
 * @warning Callbacks must not throw exceptions across the C boundary.
 */
typedef struct nix_logger
{
    /**
     * @brief Ordinary log message.
     *
     * Receives `builtins.trace` output, warnings, errors (after
     * formatting), and any message produced through the `printError` /
     * `printInfo` / `debug` / etc. macros in the C++ API.
     *
     * @param[in] userdata The user-supplied opaque pointer.
     * @param[in] level Verbosity level of the message.
     * @param[in] msg Borrowed message body.
     * @param[in] n Number of bytes in msg.
     */
    void (*log)(void * userdata, nix_verbosity level, const char * msg, unsigned int n);

    /**
     * @brief An activity (e.g. a build, a substitution) has started.
     *
     * @param[in] userdata The user-supplied opaque pointer.
     * @param[in] activity_id Unique identifier for the new activity.
     * @param[in] level Verbosity level associated with this activity.
     * @param[in] type Activity type. See ::nix_activity_type.
     * @param[in] s Borrowed description.
     * @param[in] ns Number of bytes in s.
     * @param[in] field Borrowed list of fields.
     * @param[in] nf Number of fields.
     * @param[in] parent_id ID of the parent activity, or `0` if none.
     */
    void (*start_activity)(
        void * userdata,
        nix_activity_id activity_id,
        nix_verbosity level,
        nix_activity_type type,
        const char * s,
        unsigned int ns,
        const nix_logger_field ** fields,
        unsigned int nf,
        nix_activity_id parent_id);

    /**
     * @brief An activity has stopped.
     */
    void (*stop_activity)(void * userdata, nix_activity_id activity_id);

    /**
     * @brief An activity reported a result.
     *
     * Covers in particular:
     * - ::NIX_RESULT_TYPE_BUILD_LOG_LINE - a line of builder output
     * - ::NIX_RESULT_TYPE_POST_BUILD_LOG_LINE - a line of post-build hook output
     * - ::NIX_RESULT_TYPE_SET_PHASE - current build phase
     * - ::NIX_RESULT_TYPE_PROGRESS - progress in an activity
     *
     * @param[in] userdata The user-supplied opaque pointer.
     * @param[in] activity_id The activity reporting this result.
     * @param[in] type The result type.
     * @param[in] field Borrowed list of fields.
     * @param[in] n Number of fields.
     */
    void (*result)(
        void * userdata,
        nix_activity_id activity_id,
        nix_result_type type,
        const nix_logger_field ** fields,
        unsigned int n);

    /**
     * @brief Called when the logger is destroyed.
     *
     * Invoked when the logger is replaced by a subsequent call to
     * ::nix_set_logger, or when Nix's global logger is torn
     * down at program exit. Use this to free resources owned by
     * `userdata`.
     */
    void (*destroy)(void * userdata);
} nix_logger;

/**
 * @brief Replace the global Nix logger with one driven by C callbacks.
 *
 * After this call, log messages, activities and supported results
 * produced anywhere in Nix are routed to the supplied callbacks.
 *
 * The vtable is copied; only the function pointers it contains need to
 * remain valid for the lifetime of the logger. `userdata` is borrowed
 * and passed unmodified to every callback. When the logger is later
 * destroyed (by another call to this function, or at program shutdown),
 * `vtable->destroy(userdata)` is invoked if non-NULL.
 *
 * @param[out] context Optional, stores error information.
 * @param[in] vtable Required, callback vtable.
 * @param[in] userdata Optional opaque pointer passed to every callback.
 * @return NIX_OK on success, an error code otherwise.
 */
nix_err nix_set_logger(nix_c_context * context, const nix_logger * vtable, void * userdata);

/**
 *  @}
 */

// cffi end
#ifdef __cplusplus
}
#endif

/** @} */
#endif // NIX_API_UTIL_H
