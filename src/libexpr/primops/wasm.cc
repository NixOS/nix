#include "nix/expr/primops.hh"
#include "nix/expr/eval-inline.hh"

#include <wasmtime.hh>
#include <boost/unordered/concurrent_flat_map.hpp>

using namespace wasmtime;

namespace nix {

using ValueId = uint32_t;

template<typename T, typename E = Error>
T unwrap(Result<T, E> && res)
{
    if (res)
        return res.ok();
    throw Error(res.err().message());
}

static Engine & getEngine()
{
    static Engine engine = []() {
        wasmtime::Config config;
        config.pooling_allocation_strategy(PoolAllocationConfig());
        config.memory_init_cow(true);
        return Engine(std::move(config));
    }();
    return engine;
}

static std::span<uint8_t> string2span(std::string_view s)
{
    return std::span<uint8_t>((uint8_t *) s.data(), s.size());
}

static std::string_view span2string(std::span<uint8_t> s)
{
    return std::string_view((char *) s.data(), s.size());
}

template<typename T>
static std::span<T> subspan(std::span<uint8_t> s, size_t len)
{
    if (s.size() < len * sizeof(T))
        throw Error("Wasm memory access out of bounds");
    return std::span((T *) s.data(), len);
}

// FIXME: move to wasmtime C++ wrapper.
class InstancePre
{
    WASMTIME_OWN_WRAPPER(InstancePre, wasmtime_instance_pre);

public:
    TrapResult<Instance> instantiate(wasmtime::Store::Context cx)
    {
        wasmtime_instance_t instance;
        wasm_trap_t * trap = nullptr;
        auto * error = wasmtime_instance_pre_instantiate(ptr.get(), cx.capi(), &instance, &trap);
        if (error != nullptr) {
            return TrapError(wasmtime::Error(error));
        }
        if (trap != nullptr) {
            return TrapError(Trap(trap));
        }
        return Instance(instance);
    }
};

TrapResult<InstancePre> instantiate_pre(Linker & linker, const Module & m)
{
    wasmtime_instance_pre_t * instance_pre;
    auto * error = wasmtime_linker_instantiate_pre(linker.capi(), m.capi(), &instance_pre);
    if (error != nullptr) {
        return TrapError(wasmtime::Error(error));
    }
    return InstancePre(instance_pre);
}

static void regFuns(Linker & linker, bool useWasi);

struct NixWasmInstancePre
{
    Engine & engine;
    SourcePath wasmPath;
    bool useWasi;
    InstancePre instancePre;

    NixWasmInstancePre(SourcePath _wasmPath)
        : engine(getEngine())
        , wasmPath(_wasmPath)
        , useWasi(false)
        , instancePre(({
            // Compile the module
            auto module = unwrap(Module::compile(engine, string2span(wasmPath.readFile())));

            // Auto-detect WASI by checking for wasi_snapshot_preview1 imports.
            for (const auto & ref : module.imports())
                if (const_cast<std::decay_t<decltype(ref)> &>(ref).module() == "wasi_snapshot_preview1") {
                    useWasi = true;
                    break;
                }

            // Create linker with appropriate WASI support
            Linker linker(engine);
            if (useWasi)
                unwrap(linker.define_wasi());
            regFuns(linker, useWasi);

            unwrap(instantiate_pre(linker, module));
        }))
    {
    }
};

struct NixWasmInstance
{
    EvalState & state;
    ref<NixWasmInstancePre> pre;
    wasmtime::Store wasmStore;
    wasmtime::Store::Context wasmCtx;
    Instance instance;
    Memory memory_;

    ValueVector values;
    std::exception_ptr ex;

    std::optional<std::string> functionName;

    ValueId resultId = 0;

    std::string logPrefix;

    NixWasmInstance(EvalState & _state, ref<NixWasmInstancePre> _pre)
        : state(_state)
        , pre(_pre)
        , wasmStore(pre->engine)
        , wasmCtx(wasmStore)
        , instance(unwrap(pre->instancePre.instantiate(wasmCtx)))
        , memory_(getExport<Memory>("memory"))
        , logPrefix(pre->wasmPath.baseName())
    {
        wasmCtx.set_data(this);

        /* Reserve value ID 0 so it can be used in functions like get_attr() to denote a missing attribute. */
        values.push_back(nullptr);
    }

    ValueId addValue(Value * v)
    {
        auto id = values.size();
        values.emplace_back(v);
        return id;
    }

    std::pair<ValueId, Value &> allocValue()
    {
        auto v = state.allocValue();
        auto id = addValue(v);
        return {id, *v};
    }

    Value & getValue(ValueId id)
    {
        if (id >= values.size() || id == 0)
            throw Error("invalid ValueId %d", id);
        return *values[id];
    }

    template<typename T>
    T getExport(std::string_view name)
    {
        auto ext = instance.get(wasmCtx, name);
        if (!ext)
            throw Error("Wasm module '%s' does not export '%s'", pre->wasmPath, name);
        auto res = std::get_if<T>(&*ext);
        if (!res)
            throw Error("export '%s' of Wasm module '%s' does not have the right type", name, pre->wasmPath);
        return *res;
    }

    std::vector<Val> runFunction(std::string_view name, const std::vector<Val> & args)
    {
        functionName = name;
        return unwrap(getExport<Func>(name).call(wasmCtx, args));
    }

    auto memory()
    {
        return memory_.data(wasmCtx);
    }

    std::monostate panic(uint32_t ptr, uint32_t len)
    {
        throw Error("Wasm panic: %s", Uncolored(span2string(memory().subspan(ptr, len))));
    }

    std::monostate warn(uint32_t ptr, uint32_t len)
    {
        doWarn(span2string(memory().subspan(ptr, len)));
        return {};
    }

    void doWarn(std::string_view s)
    {
        if (functionName)
            nix::warn("'%s' function '%s': %s", logPrefix, functionName.value_or("<unknown>"), s);
        else
            nix::warn("'%s': %s", logPrefix, s);
    }

    uint32_t get_type(ValueId valueId)
    {
        auto & value = getValue(valueId);
        state.forceValue(value, noPos);
        auto t = value.type();
        return t == nInt        ? 1
               : t == nFloat    ? 2
               : t == nBool     ? 3
               : t == nString   ? 4
               : t == nPath     ? 5
               : t == nNull     ? 6
               : t == nAttrs    ? 7
               : t == nList     ? 8
               : t == nFunction ? 9
                                : []() -> int { throw Error("unsupported type"); }();
    }

    ValueId make_int(int64_t n)
    {
        auto [valueId, value] = allocValue();
        value.mkInt(n);
        return valueId;
    }

    int64_t get_int(ValueId valueId)
    {
        return state.forceInt(getValue(valueId), noPos, "while evaluating a value from Wasm").value;
    }

    ValueId make_float(double x)
    {
        auto [valueId, value] = allocValue();
        value.mkFloat(x);
        return valueId;
    }

    double get_float(ValueId valueId)
    {
        return state.forceFloat(getValue(valueId), noPos, "while evaluating a value from Wasm");
    }

    ValueId make_string(uint32_t ptr, uint32_t len)
    {
        auto [valueId, value] = allocValue();
        value.mkString(span2string(memory().subspan(ptr, len)), state.mem);
        return valueId;
    }

    uint32_t copy_string(ValueId valueId, uint32_t ptr, uint32_t maxLen)
    {
        auto s = state.forceString(getValue(valueId), noPos, "while evaluating a value from Wasm");
        if (s.size() <= maxLen) {
            auto buf = memory().subspan(ptr, maxLen);
            memcpy(buf.data(), s.data(), s.size());
        }
        return s.size();
    }

    ValueId make_path(ValueId baseId, uint32_t ptr, uint32_t len)
    {
        auto & baseValue = getValue(baseId);
        state.forceValue(baseValue, noPos);
        if (baseValue.type() != nPath)
            throw Error("make_path expects a path value");
        auto base = baseValue.path();

        auto [valueId, value] = allocValue();
        value.mkPath({base.accessor, CanonPath(span2string(memory().subspan(ptr, len)), base.path)}, state.mem);
        return valueId;
    }

    uint32_t copy_path(ValueId valueId, uint32_t ptr, uint32_t maxLen)
    {
        auto & v = getValue(valueId);
        state.forceValue(v, noPos);
        if (v.type() != nPath)
            throw Error("copy_path expects a path value");
        auto path = v.path().path;
        auto s = path.abs();
        if (s.size() <= maxLen) {
            auto buf = memory().subspan(ptr, maxLen);
            memcpy(buf.data(), s.data(), s.size());
        }
        return s.size();
    }

    ValueId make_bool(int32_t b)
    {
        return addValue(state.getBool(b));
    }

    int32_t get_bool(ValueId valueId)
    {
        return state.forceBool(getValue(valueId), noPos, "while evaluating a value from Wasm");
    }

    ValueId make_null()
    {
        return addValue(&Value::vNull);
    }

    ValueId make_list(uint32_t ptr, uint32_t len)
    {
        auto vs = subspan<ValueId>(memory().subspan(ptr), len);

        auto [valueId, value] = allocValue();

        auto list = state.buildList(len);
        for (const auto & [n, v] : enumerate(list))
            v = &getValue(vs[n]); // FIXME: endianness
        value.mkList(list);

        return valueId;
    }

    uint32_t copy_list(ValueId valueId, uint32_t ptr, uint32_t maxLen)
    {
        auto & value = getValue(valueId);
        state.forceList(value, noPos, "while getting a list from Wasm");

        if (value.listSize() <= maxLen) {
            auto out = subspan<ValueId>(memory().subspan(ptr), value.listSize());

            for (const auto & [n, elem] : enumerate(value.listView()))
                out[n] = addValue(elem);
        }

        return value.listSize();
    }

    ValueId make_attrset(uint32_t ptr, uint32_t len)
    {
        auto mem = memory();

        struct Attr
        {
            // FIXME: endianness
            uint32_t attrNamePtr;
            uint32_t attrNameLen;
            ValueId value;
        };

        auto attrs = subspan<Attr>(mem.subspan(ptr), len);

        auto [valueId, value] = allocValue();
        auto builder = state.buildBindings(len);
        for (auto & attr : attrs)
            builder.insert(
                state.symbols.create(span2string(mem.subspan(attr.attrNamePtr, attr.attrNameLen))),
                &getValue(attr.value));
        value.mkAttrs(builder);

        return valueId;
    }

    uint32_t copy_attrset(ValueId valueId, uint32_t ptr, uint32_t maxLen)
    {
        auto & value = getValue(valueId);
        state.forceAttrs(value, noPos, "while copying an attrset into Wasm");

        if (value.attrs()->size() <= maxLen) {
            // FIXME: endianness.
            struct Attr
            {
                ValueId value;
                uint32_t nameLen;
            };

            auto buf = subspan<Attr>(memory().subspan(ptr), maxLen);

            // FIXME: for determinism, we should return attributes in lexicographically sorted order.
            for (const auto & [n, attr] : enumerate(*value.attrs())) {
                buf[n].value = addValue(attr.value);
                buf[n].nameLen = state.symbols[attr.name].size();
            }
        }

        return value.attrs()->size();
    }

    std::monostate copy_attrname(ValueId valueId, uint32_t attrIdx, uint32_t ptr, uint32_t len)
    {
        auto & value = getValue(valueId);
        state.forceAttrs(value, noPos, "while copying an attr name into Wasm");

        auto & attrs = *value.attrs();

        if ((size_t) attrIdx >= attrs.size())
            throw Error("copy_attrname: attribute index out of bounds");

        std::string_view name = state.symbols[attrs[attrIdx].name];

        if ((size_t) len != name.size())
            throw Error("copy_attrname: buffer length does not match attribute name length");

        memcpy(memory().subspan(ptr, len).data(), name.data(), name.size());

        return {};
    }

    ValueId get_attr(ValueId valueId, uint32_t ptr, uint32_t len)
    {
        auto attrName = span2string(memory().subspan(ptr, len));

        auto & value = getValue(valueId);
        state.forceAttrs(value, noPos, "while getting an attribute from Wasm");

        auto attr = value.attrs()->get(state.symbols.create(attrName));

        return attr ? addValue(attr->value) : 0;
    }

    ValueId call_function(ValueId funId, uint32_t ptr, uint32_t len)
    {
        auto & fun = getValue(funId);
        state.forceFunction(fun, noPos, "while calling a function from Wasm");

        ValueVector args;
        for (auto argId : subspan<ValueId>(memory().subspan(ptr), len))
            args.push_back(&getValue(argId));

        auto [valueId, value] = allocValue();

        state.callFunction(fun, args, value, noPos);

        return valueId;
    }

    ValueId make_app(ValueId funId, uint32_t ptr, uint32_t len)
    {
        if (!len)
            return funId;

        auto args = subspan<ValueId>(memory().subspan(ptr), len);

        auto res = &getValue(funId);

        while (!args.empty()) {
            auto arg = &getValue(args[0]);
            auto tmp = state.allocValue();
            tmp->mkApp(res, {arg});
            res = tmp;
            args = args.subspan(1);
        }

        return addValue(res);
    }

    /**
     * Read the contents of a file into Wasm memory. This is like calling `builtins.readFile`, except that it can handle
     * binary files that cannot be represented as Nix strings.
     */
    uint32_t read_file(ValueId pathId, uint32_t ptr, uint32_t len)
    {
        auto & pathValue = getValue(pathId);
        auto path = state.realisePath(noPos, pathValue);

        auto contents = path.readFile();

        if (contents.size() > std::numeric_limits<uint32_t>::max())
            throw Error("file '%s' is too large to process in Wasm (size: %d)", path, contents.size());

        // FIXME: this is an inefficient interface since it may cause the file to be read twice.
        if (contents.size() <= len) {
            auto buf = memory().subspan(ptr, len);
            memcpy(buf.data(), contents.data(), contents.size());
        }

        return contents.size();
    }
};

template<typename R, typename... Args>
static void regFun(Linker & linker, std::string_view name, R (NixWasmInstance::*f)(Args...))
{
    unwrap(linker.func_wrap("env", name, [f](Caller caller, Args... args) -> Result<R, Trap> {
        try {
            auto instance = std::any_cast<NixWasmInstance *>(caller.context().get_data());
            return (*instance.*f)(args...);
        } catch (std::exception & e) {
            return Trap(e.what());
        } catch (...) {
            return Trap("unknown exception");
        }
    }));
}

static void regFuns(Linker & linker, bool useWasi)
{
    regFun(linker, "panic", &NixWasmInstance::panic);
    regFun(linker, "warn", &NixWasmInstance::warn);
    regFun(linker, "get_type", &NixWasmInstance::get_type);
    regFun(linker, "make_int", &NixWasmInstance::make_int);
    regFun(linker, "get_int", &NixWasmInstance::get_int);
    regFun(linker, "make_float", &NixWasmInstance::make_float);
    regFun(linker, "get_float", &NixWasmInstance::get_float);
    regFun(linker, "make_string", &NixWasmInstance::make_string);
    regFun(linker, "copy_string", &NixWasmInstance::copy_string);
    regFun(linker, "make_path", &NixWasmInstance::make_path);
    regFun(linker, "copy_path", &NixWasmInstance::copy_path);
    regFun(linker, "make_bool", &NixWasmInstance::make_bool);
    regFun(linker, "get_bool", &NixWasmInstance::get_bool);
    regFun(linker, "make_null", &NixWasmInstance::make_null);
    regFun(linker, "make_list", &NixWasmInstance::make_list);
    regFun(linker, "copy_list", &NixWasmInstance::copy_list);
    regFun(linker, "make_attrset", &NixWasmInstance::make_attrset);
    regFun(linker, "copy_attrset", &NixWasmInstance::copy_attrset);
    regFun(linker, "copy_attrname", &NixWasmInstance::copy_attrname);
    regFun(linker, "get_attr", &NixWasmInstance::get_attr);
    regFun(linker, "call_function", &NixWasmInstance::call_function);
    regFun(linker, "make_app", &NixWasmInstance::make_app);
    regFun(linker, "read_file", &NixWasmInstance::read_file);

    if (useWasi) {
        unwrap(linker.func_wrap(
            "env", "return_to_nix", [](Caller caller, ValueId resultId) -> Result<std::monostate, Trap> {
                auto instance = std::any_cast<NixWasmInstance *>(caller.context().get_data());
                instance->resultId = resultId;
                return Trap("return_to_nix");
            }));
    }
}

static NixWasmInstance instantiateWasm(EvalState & state, const SourcePath & wasmPath)
{
    // FIXME: make this a weak Boehm GC pointer so that it can be freed during GC.
    // FIXME: move to EvalState?
    // Note: InstancePre in Rust is Send+Sync so it should be safe to share between threads.
    static boost::concurrent_flat_map<SourcePath, std::shared_ptr<NixWasmInstancePre>> instancesPre;

    std::shared_ptr<NixWasmInstancePre> instancePre;

    instancesPre.try_emplace_and_cvisit(
        wasmPath,
        nullptr,
        [&](auto & i) { instancePre = i.second = std::make_shared<NixWasmInstancePre>(wasmPath); },
        [&](auto & i) { instancePre = i.second; });

    return NixWasmInstance{state, ref(instancePre)};
}

/**
 * Callback for WASI stdout/stderr writes. It splits the output into lines and logs each line separately.
 */
struct WasiLogger
{
    NixWasmInstance & instance;

    std::string data;

    ~WasiLogger()
    {
        if (!data.empty())
            instance.doWarn(data);
    }

    void operator()(std::string_view s)
    {
        data.append(s);

        while (true) {
            auto pos = data.find('\n');
            if (pos == std::string_view::npos)
                break;
            instance.doWarn(data.substr(0, pos));
            data.erase(0, pos + 1);
        }
    }
};

static void prim_wasm(EvalState & state, const PosIdx pos, Value ** args, Value & v)
{
    state.forceAttrs(*args[0], pos, "while evaluating the first argument to `builtins.wasm`");

    // Extract 'path' attribute
    auto pathAttr = args[0]->attrs()->get(state.symbols.create("path"));
    if (!pathAttr)
        throw Error("missing required 'path' attribute in first argument to `builtins.wasm`");
    auto wasmPath = state.realisePath(pos, *pathAttr->value);

    // Check for unknown attributes
    for (auto & attr : *args[0]->attrs()) {
        auto name = state.symbols[attr.name];
        if (name != "path" && name != "function")
            throw Error("unknown attribute '%s' in first argument to `builtins.wasm`", name);
    }

    // Second argument is the value to pass to the function
    auto argValue = args[1];

    try {
        auto instance = instantiateWasm(state, wasmPath);

        // Extract 'function' attribute (optional for wasi, required for non-wasi)
        std::string functionName;
        auto functionAttr = args[0]->attrs()->get(state.symbols.create("function"));
        if (instance.pre->useWasi) {
            functionName = "_start";
            if (functionAttr)
                throw Error("'function' attribute is not allowed for WASI modules");
        } else {
            if (!functionAttr)
                throw Error(
                    "missing required 'function' attribute in first argument to `builtins.wasm` for non-WASI modules");
            functionName = std::string(
                state.forceStringNoCtx(*functionAttr->value, pos, "while evaluating the 'function' attribute"));
        }

        debug("calling wasm module");

        auto argId = instance.addValue(argValue);

        if (instance.pre->useWasi) {
            WasiLogger logger{instance};

            auto loggerTrampoline = [](void * data, const unsigned char * buf, size_t len) -> ptrdiff_t {
                auto logger = static_cast<WasiLogger *>(data);
                (*logger)(std::string_view((const char *) buf, len));
                return len;
            };

            WasiConfig wasiConfig;
            wasi_config_set_stdout_custom(wasiConfig.capi(), loggerTrampoline, &logger, nullptr);
            wasi_config_set_stderr_custom(wasiConfig.capi(), loggerTrampoline, &logger, nullptr);
            wasiConfig.argv({"wasi", std::to_string(argId)});
            unwrap(instance.wasmStore.context().set_wasi(std::move(wasiConfig)));

            auto res = instance.getExport<Func>(functionName).call(instance.wasmCtx, {});
            if (!instance.resultId) {
                unwrap(std::move(res));
                throw Error("Wasm function '%s' from '%s' finished without returning a value", functionName, wasmPath);
            }

            auto & vRes = instance.getValue(instance.resultId);
            state.forceValue(vRes, pos);
            v = vRes;
        } else {
            // FIXME: use the "start" function if present.
            instance.runFunction("nix_wasm_init_v1", {});

            auto res = instance.runFunction(functionName, {(int32_t) argId});
            if (res.size() != 1)
                throw Error("Wasm function '%s' from '%s' did not return exactly one value", functionName, wasmPath);
            if (res[0].kind() != ValKind::I32)
                throw Error("Wasm function '%s' from '%s' did not return an i32 value", functionName, wasmPath);
            auto & vRes = instance.getValue(res[0].i32());
            state.forceValue(vRes, pos);
            v = vRes;
        }
    } catch (Error & e) {
        e.addTrace(state.positions[pos], "while executing the Wasm function from '%s'", wasmPath);
        throw;
    }
}

static RegisterPrimOp primop_wasm(
    {.name = "__wasm",
     .args = {"config", "arg"},
     .doc = R"(
      Call a Wasm function with the specified argument.

      The first argument must be an attribute set with the following attributes:
      - `path`: Path to the Wasm module (required)
      - `function`: Function name to call (required for non-WASI modules, not allowed for WASI modules)

      The second argument is the value to pass to the function.

      WASI mode is automatically enabled if the module imports from `wasi_snapshot_preview1`.

      Example (non-WASI):
      ```nix
      builtins.wasm {
        path = ./foo.wasm;
        function = "fib";
      } 33
      ```

      Example (WASI):
      ```nix
      builtins.wasm {
        path = ./bar.wasm;
      } { x = 42; }
      ```
     )",
     .impl = prim_wasm,
     .experimentalFeature = Xp::WasmBuiltin});

} // namespace nix
