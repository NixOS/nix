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

#ifdef __cplusplus
extern "C" {
#endif
// cffi start

/** @defgroup errors Handling errors
 * @brief Dealing with errors from the Nix side
 *
 * To handle errors that can be returned from the Nix API
 * nix_c_context can be passed any function that potentially returns an error.
 *
 * Error information will be stored in this context, and can be retrieved
 * using nix_err_code, nix_err_msg.
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
 * @brief Type for error codes in the NIX system
 *
 * This type can have one of several predefined constants:
 * - NIX_OK: No error occurred (0)
 * - NIX_ERR_UNKNOWN: An unknown error occurred (-1)
 * - NIX_ERR_OVERFLOW: An overflow error occurred (-2)
 * - NIX_ERR_KEY: A key error occurred (-3)
 * - NIX_ERR_NIX_ERROR: A generic Nix error occurred (-4)
 */
typedef int nix_err;

/**
 * @brief No error occurred.
 *
 * This error code is returned when no error has occurred during the function
 * execution.
 */
#define NIX_OK 0

/**
 * @brief An unknown error occurred.
 *
 * This error code is returned when an unknown error occurred during the
 * function execution.
 */
#define NIX_ERR_UNKNOWN -1

/**
 * @brief An overflow error occurred.
 *
 * This error code is returned when an overflow error occurred during the
 * function execution.
 */
#define NIX_ERR_OVERFLOW -2

/**
 * @brief A key error occurred.
 *
 * This error code is returned when a key error occurred during the function
 * execution.
 */
#define NIX_ERR_KEY -3

/**
 * @brief A generic Nix error occurred.
 *
 * This error code is returned when a generic Nix error occurred during the
 * function execution.
 */
#define NIX_ERR_NIX_ERROR -4

/**
 * @brief This object stores error state.
 * @struct nix_c_context
 *
 * Passed as a first parameter to C functions that can fail, will store error
 * information. Optional wherever it is used, passing NULL will throw a C++
 * exception instead. The first field is a nix_err, that can be read directly to
 * check for errors.
 * @note These can be reused between different function calls,
 *  but make sure not to use them for multiple calls simultaneously (which can
 * happen in callbacks).
 */
typedef struct nix_c_context nix_c_context;

// Function prototypes

/**
 * @brief Allocate a new nix_c_context.
 * @throws std::bad_alloc
 * @return allocated nix_c_context, owned by the caller. Free using
 * `nix_c_context_free`.
 */
nix_c_context *nix_c_context_create();
/**
 * @brief Free a nix_c_context. Does not fail.
 * @param[out] context The context to free, mandatory.
 */
void nix_c_context_free(nix_c_context *context);
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
nix_err nix_libutil_init(nix_c_context *context);

/** @defgroup settings
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
 * @param[out] value A pointer to a buffer where the value of the setting will
 * be stored.
 * @param[in] n The size of the buffer pointed to by value.
 * @return NIX_ERR_KEY if the setting is unknown, NIX_ERR_OVERFLOW if the
 * provided buffer is too short, or NIX_OK if the setting was retrieved
 * successfully.
 */
nix_err nix_setting_get(nix_c_context *context, const char *key, char *value,
                        int n);

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
nix_err nix_setting_set(nix_c_context *context, const char *key,
                        const char *value);

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
const char *nix_version_get();

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
 *         a borrowed pointer to the error message otherwise.
 */
const char *nix_err_msg(nix_c_context *context, const nix_c_context *ctx,
                        unsigned int *n);

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
 * @param[in] read_context the context to retrieve the error message from
 * @param[out] value The allocated area to write the error string to.
 * @param[in] n Maximum size of the returned string.
 * @return NIX_OK if there were no errors, an error code otherwise.
 */
nix_err nix_err_info_msg(nix_c_context *context,
                         const nix_c_context *read_context, char *value, int n);

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
 * @param[out] value The allocated area to write the error string to.
 * @param[in] n Maximum size of the returned string.
 * @return NIX_OK if there were no errors, an error code otherwise.
 */
nix_err nix_err_name(nix_c_context *context, const nix_c_context *read_context,
                     char *value, int n);

/**
 * @brief Retrieves the most recent error code from a nix_c_context
 *
 * Equivalent to reading the first field of the context.
 *
 * @param[out] context optional, the context to store errors in if this function
 * fails
 * @param[in] read_context the context to retrieve the error message from
 * @return most recent error code stored in the context.
 */
nix_err nix_err_code(nix_c_context *context, const nix_c_context *read_context);

/**
 *  @}
 */

// cffi end
#ifdef __cplusplus
}
#endif

/** @} */
#endif // NIX_API_UTIL_H
