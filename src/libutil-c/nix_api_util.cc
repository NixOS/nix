#include "nix_api_util.h"
#include "nix/util/config-global.hh"
#include "nix/util/error.hh"
#include "nix/util/logging.hh"
#include "nix_api_util_internal.h"
#include "nix/util/util.hh"

#include <boost/container/small_vector.hpp>

#include <cxxabi.h>
#include <sstream>
#include <typeinfo>

#include "nix_api_util_config.h"

namespace {

void collectFields(
    boost::container::small_vector<nix_logger_field, 4> & storage,
    boost::container::small_vector<const nix_logger_field *, 4> & cfields,
    const nix::Logger::Fields & fields)
{
    storage.reserve(fields.size());
    for (const auto & field : fields) {
        nix_logger_field_type t;
        nix_logger_field_value v{};
        if (field.type == nix::Logger::Field::tString) {
            t = NIX_LOGGER_FIELD_TYPE_STR;

            nix_logger_field_value_string str;
            str.value = field.s.data();
            str.len = field.s.size();

            v.str = str;
        } else if (field.type == nix::Logger::Field::tInt) {
            t = NIX_LOGGER_FIELD_TYPE_INT;
            v.i = field.i;
        } else {
            nix::unreachable();
        }

        storage.push_back(
            nix_logger_field{
                .type = t,
                .value = v,
            });
    }

    cfields.reserve(storage.size());
    for (const auto & f : storage)
        cfields.push_back(&f);
}

/**
 * Logger that delegates every supported method to a C callback vtable.
 */
class CallbackLogger final : public nix::Logger
{
public:
    nix_logger vtable;
    void * userdata;

    CallbackLogger(const nix_logger & vtable, void * userdata)
        : vtable(vtable)
        , userdata(userdata)
    {
    }

    ~CallbackLogger() override
    {
        if (vtable.destroy)
            vtable.destroy(userdata);
    }

    void log(nix::Verbosity lvl, std::string_view s) override
    {
        if (!vtable.log)
            return;
        vtable.log(userdata, static_cast<nix_verbosity>(lvl), s.data(), s.size());
    }

    void logEI(const nix::ErrorInfo & ei) override
    {
        if (!vtable.log)
            return;
        std::ostringstream oss;
        nix::showErrorInfo(oss, ei, nix::loggerSettings.showTrace.get());
        auto formatted = oss.str();
        vtable.log(userdata, static_cast<nix_verbosity>(ei.level), formatted.data(), formatted.size());
    }

    void startActivity(
        nix::ActivityId act,
        nix::Verbosity lvl,
        nix::ActivityType type,
        const std::string & s,
        const Fields & fields,
        nix::ActivityId parent) override
    {
        if (!vtable.start_activity)
            return;

        boost::container::small_vector<nix_logger_field, 4> storage;
        boost::container::small_vector<const nix_logger_field *, 4> cfields;
        collectFields(storage, cfields, fields);

        vtable.start_activity(
            userdata,
            act,
            static_cast<nix_verbosity>(lvl),
            static_cast<nix_activity_type>(type),
            s.c_str(),
            s.size(),
            cfields.data(),
            cfields.size(),
            parent);
    }

    void stopActivity(nix::ActivityId act) override
    {
        if (vtable.stop_activity)
            vtable.stop_activity(userdata, act);
    }

    void result(nix::ActivityId act, nix::ResultType type, const Fields & fields) override
    {
        if (!vtable.result)
            return;

        boost::container::small_vector<nix_logger_field, 4> storage;
        boost::container::small_vector<const nix_logger_field *, 4> cfields;
        collectFields(storage, cfields, fields);

        vtable.result(userdata, act, static_cast<nix_result_type>(type), cfields.data(), cfields.size());
    }
};

} // namespace

extern "C" {

nix_c_context * nix_c_context_create()
{
    try {
        return new nix_c_context();
    } catch (...) {
        return nullptr;
    }
}

void nix_c_context_free(nix_c_context * context)
{
    delete context;
}

nix_err nix_context_error(nix_c_context * context)
{
    if (context == nullptr) {
        throw;
    }
    try {
        throw;
    } catch (nix::Error & e) {
        /* Storing this exception is annoying, take what we need here */
        context->last_err = e.what();
        context->info = e.info();
        int status;
        const char * demangled = abi::__cxa_demangle(typeid(e).name(), 0, 0, &status);
        if (demangled) {
            context->name = demangled;
            free((void *) demangled);
        } else {
            context->name = typeid(e).name();
        }
        context->last_err_code = NIX_ERR_NIX_ERROR;
        return context->last_err_code;
    } catch (const std::exception & e) {
        context->last_err = e.what();
        context->last_err_code = NIX_ERR_UNKNOWN;
        return context->last_err_code;
    }
    // unreachable
}

nix_err nix_set_err_msg(nix_c_context * context, nix_err err, const char * msg)
{
    if (context == nullptr) {
        // todo last_err_code
        throw nix::Error("Nix C api error: %s", msg);
    }
    context->last_err_code = err;
    context->last_err = msg;
    return err;
}

void nix_clear_err(nix_c_context * context)
{
    if (context)
        context->last_err_code = NIX_OK;
}

const char * nix_version_get()
{
    return PACKAGE_VERSION;
}

// Implementations

nix_err nix_setting_get(nix_c_context * context, const char * key, nix_get_string_callback callback, void * user_data)
{
    if (context)
        context->last_err_code = NIX_OK;
    try {
        std::map<std::string, nix::AbstractConfig::SettingInfo> settings;
        nix::globalConfig.getSettings(settings);
        if (settings.contains(key)) {
            return call_nix_get_string_callback(settings[key].value, callback, user_data);
        } else {
            return nix_set_err_msg(context, NIX_ERR_KEY, "Setting not found");
        }
    }
    NIXC_CATCH_ERRS
}

nix_err nix_setting_set(nix_c_context * context, const char * key, const char * value)
{
    if (context)
        context->last_err_code = NIX_OK;
    if (nix::globalConfig.set(key, value))
        return NIX_OK;
    else {
        return nix_set_err_msg(context, NIX_ERR_KEY, "Setting not found");
    }
}

nix_err nix_libutil_init(nix_c_context * context)
{
    if (context)
        context->last_err_code = NIX_OK;
    try {
        nix::initLibUtil();
        return NIX_OK;
    }
    NIXC_CATCH_ERRS
}

const char * nix_err_msg(nix_c_context * context, const nix_c_context * read_context, unsigned int * n)
{
    if (context)
        context->last_err_code = NIX_OK;
    if (read_context->last_err && read_context->last_err_code != NIX_OK) {
        if (n)
            *n = read_context->last_err->size();
        return read_context->last_err->c_str();
    }
    nix_set_err_msg(context, NIX_ERR_UNKNOWN, "No error message");
    return nullptr;
}

nix_err nix_err_name(
    nix_c_context * context, const nix_c_context * read_context, nix_get_string_callback callback, void * user_data)
{
    if (context)
        context->last_err_code = NIX_OK;
    if (read_context->last_err_code != NIX_ERR_NIX_ERROR) {
        return nix_set_err_msg(context, NIX_ERR_UNKNOWN, "Last error was not a nix error");
    }
    return call_nix_get_string_callback(read_context->name, callback, user_data);
}

nix_err nix_err_info_msg(
    nix_c_context * context, const nix_c_context * read_context, nix_get_string_callback callback, void * user_data)
{
    if (context)
        context->last_err_code = NIX_OK;
    if (read_context->last_err_code != NIX_ERR_NIX_ERROR) {
        return nix_set_err_msg(context, NIX_ERR_UNKNOWN, "Last error was not a nix error");
    }
    return call_nix_get_string_callback(read_context->info->msg.str(), callback, user_data);
}

nix_err nix_err_code(const nix_c_context * read_context)
{
    return read_context->last_err_code;
}

// internal
nix_err call_nix_get_string_callback(const std::string_view str, nix_get_string_callback callback, void * user_data)
{
    callback(str.data(), str.size(), user_data);
    return NIX_OK;
}

nix_err nix_set_verbosity(nix_c_context * context, nix_verbosity level)
{
    if (context)
        context->last_err_code = NIX_OK;
    if (level > NIX_LVL_VOMIT || level < NIX_LVL_ERROR)
        return nix_set_err_msg(context, NIX_ERR_UNKNOWN, "Invalid verbosity level");
    try {
        nix::verbosity = static_cast<nix::Verbosity>(level);
    }
    NIXC_CATCH_ERRS
}

nix_err nix_set_logger(nix_c_context * context, const nix_logger * vtable, void * userdata)
{
    if (context)
        context->last_err_code = NIX_OK;
    if (vtable == nullptr)
        return nix_set_err_msg(context, NIX_ERR_UNKNOWN, "logger vtable is null");
    try {
        nix::logger = std::make_unique<CallbackLogger>(*vtable, userdata);
    }
    NIXC_CATCH_ERRS
}

} // extern "C"
