# Wasm Host Interface

Nix provides a builtin for calling WebAssembly modules: `builtins.wasm`. This allows extending Nix with custom functionality written in languages that compile to WebAssembly (such as Rust).

## Overview

WebAssembly modules can interact with Nix values through a host interface that provides functions for creating and inspecting Nix values. The WASM module receives Nix values as opaque `ValueId` handles and uses host functions to work with them.

The `builtins.wasm` builtin takes two arguments:
1. A configuration attribute set with the following attributes:
   - `path` - Path to the WebAssembly module (required)
   - `function` - Name of the Wasm function to call (required for non-WASI modules, not allowed for WASI modules)
2. The argument value to pass to the function

WASI mode is automatically detected by checking if the module imports from `wasi_snapshot_preview1`. There are two calling conventions:

- **Non-WASI mode** (no WASI imports) calls the Wasm export specified by `function` directly. The function receives its input as a `ValueId` parameter and returns a `ValueId`.
- **WASI mode** (when the module imports from `wasi_snapshot_preview1`) runs the WASI module's `_start` entry point. The input `ValueId` is passed as a command-line argument (`argv[1]`), and the result is returned by calling the `return_to_nix` host function.

## Value IDs

Nix values are represented in Wasm code as a `u32` referred to below as a `ValueId`. These are opaque handles that reference values managed by the Nix evaluator. Value ID 0 is reserved to represent a missing attribute lookup result.

## Entry Points

### Non-WASI Mode

Non-WASI mode is used when the module does **not** import from `wasi_snapshot_preview1`.

Usage:
```nix
builtins.wasm {
  path = <module>;
  function = <function-name>;
} <arg>
```

Every Wasm module used in non-WASI mode must export:
- A `memory` object that the host can use to read/write data.
- `nix_wasm_init_v1()`, a function that is called once when the module is instantiated.
- The entry point function, whose name is specified by the `function` attribute. It takes a single `ValueId` and returns a single `ValueId` (i.e. it has type `fn(arg: u32) -> u32`).

### WASI Mode

WASI mode is automatically used when the module imports a `wasi_snapshot_preview1` function.

Usage:
```nix
builtins.wasm {
  path = <module>;
} <arg>
```

Every WASI module must export:
- A `memory` object that the host can use to read/write data.
- `_start()`, the standard WASI entry point. This function takes no parameters.

The input value is passed as a command-line argument: `argv[1]` is set to the decimal representation of the `ValueId` of the input value.

To return a result to Nix, the module must call the `return_to_nix` host function (see below) with the `ValueId` of the result. If `_start` finishes without calling `return_to_nix`, an error is raised.

Standard output and standard error from the WASI module are captured and emitted as Nix warnings (one warning per line).

## Host Functions

All host functions are imported from the `env` module.

### Error Handling

#### `panic(ptr: u32, len: u32)`

Aborts execution with an error message.

**Parameters:**
- `ptr` - Pointer to UTF-8 encoded error message in Wasm memory
- `len` - Length of the error message in bytes

#### `warn(ptr: u32, len: u32)`

Emits a warning message.

**Parameters:**
- `ptr` - Pointer to UTF-8 encoded warning message in Wasm memory
- `len` - Length of the warning message in bytes

### Type Inspection

#### `get_type(value: ValueId) -> u32`

Returns the type of a Nix value.

**Parameters:**
- `value` - ID of a Nix value

**Return values:**
- `1` - Integer
- `2` - Float
- `3` - Boolean
- `4` - String
- `5` - Path
- `6` - Null
- `7` - Attribute set
- `8` - List
- `9` - Function

**Note:** Forces evaluation of the value.

### Integer Operations

#### `make_int(n: i64) -> ValueId`

Creates a Nix integer value.

**Parameters:**
- `n` - The integer value

**Returns:** Value ID of the created integer

#### `get_int(value: ValueId) -> i64`

Extracts an integer from a Nix value. Throws an error if the value is not an integer.

**Parameters:**
- `value` - ID of a Nix integer value

**Returns:** The integer value

### Float Operations

#### `make_float(x: f64) -> ValueId`

Creates a Nix float value.

**Parameters:**
- `x` - The float value

**Returns:** Value ID of the created float

#### `get_float(value: ValueId) -> f64`

Extracts a float from a Nix value. Throws an error if the value is not a float.

**Parameters:**
- `value` - ID of a Nix float value

**Returns:** The float value

### Boolean Operations

#### `make_bool(b: i32) -> ValueId`

Creates a Nix Boolean value.

**Parameters:**
- `b` - Boolean value (0 = false, non-zero = true)

**Returns:** Value ID of the created Boolean

#### `get_bool(value: ValueId) -> i32`

Extracts a Boolean from a Nix value. Throws an error if the value is not a Boolean.

**Parameters:**
- `value` - ID of a Nix Boolean value

**Returns:** 0 for false, 1 for true

### Null Operations

#### `make_null() -> ValueId`

Creates a Nix null value.

**Returns:** Value ID of the null value

### String Operations

#### `make_string(ptr: u32, len: u32) -> ValueId`

Creates a Nix string value from Wasm memory.

**Parameters:**
- `ptr` - Pointer to a string in Wasm memory
- `len` - Length of the string in bytes

**Note:** Strings do not require a null terminator.

**Returns:** Value ID of the created string

#### `copy_string(value: ValueId, ptr: u32, max_len: u32) -> u32`

Copies a Nix string value into Wasm memory.

**Parameters:**
- `value` - ID of a string value
- `ptr` - Pointer to buffer in Wasm memory
- `max_len` - Maximum number of bytes to copy

**Returns:** The actual length of the string in bytes

**Note:** If the returned length is greater than `max_len`, no data is copied. Call again with a larger buffer to get the full string.

### Path Operations

#### `make_path(base: ValueId, ptr: u32, len: u32) -> ValueId`

Creates a Nix path value relative to a base path.

**Parameters:**
- `base` - ID of a path value
- `ptr` - Pointer to a string in Wasm memory
- `len` - Length of the path string in bytes

**Returns:** ID of a new path value

**Note:** The path string is interpreted relative to the base path. The resulting path is in the same source tree ("source accessor") as the original path.

#### `copy_path(value: ValueId, ptr: u32, max_len: u32) -> u32`

Copies a Nix path value into Wasm memory as an absolute path string.

**Parameters:**
- `value` - ID of a path value
- `ptr` - Pointer to buffer in Wasm memory
- `max_len` - Maximum number of bytes to copy

**Returns:** The actual length of the path string in bytes

**Note:** If the returned length is greater than `max_len`, no data is copied.

### List Operations

#### `make_list(ptr: u32, len: u32) -> ValueId`

Creates a Nix list from an array of value IDs in Wasm memory.

**Parameters:**
- `ptr` - Pointer to array of `ValueId` (u32) in Wasm memory
- `len` - Number of elements in the array

**Returns:** Value ID of the created list

**Note:** The array must contain `len * 4` bytes (each ValueId is 4 bytes).

#### `copy_list(value: ValueId, ptr: u32, max_len: u32) -> u32`

Copies a Nix list into Wasm memory as an array of value IDs.

**Parameters:**
- `value` - ID of a list value
- `ptr` - Pointer to buffer in Wasm memory
- `max_len` - Maximum number of elements to copy

**Returns:** The actual number of elements in the list

**Note:** If the returned length is greater than `max_len`, no data is copied. Each element is written as a `ValueId` (4 bytes). The buffer must be `max_len * 4` bytes large.

### Attribute Set Operations

#### `make_attrset(ptr: u32, len: u32) -> ValueId`

Creates a Nix attribute set from an array of attributes in Wasm memory.

**Parameters:**
- `ptr` - Pointer to array of attribute structures in Wasm memory
- `len` - Number of attributes

**Returns:** Value ID of the created attribute set

**Attribute structure format:**
```c
struct Attr {
    name_ptr: u32,   // Pointer to attribute name
    name_len: u32,   // Length of attribute name in bytes
    value_id: u32,   // ID of the attribute value
}
```

Each `Attr` element is 12 bytes (3 × 4 bytes).

#### `copy_attrset(value: ValueId, ptr: u32, max_len: u32) -> u32`

Copies a Nix attribute set into Wasm memory as an array of attribute structures.

**Parameters:**
- `value` - ID of a Nix attribute set value
- `ptr` - Pointer to buffer in Wasm memory
- `max_len` - Maximum number of attributes to copy

**Returns:** The actual number of attributes in the set

**Note:** If the returned length is greater than `max_len`, no data is copied.

**Output structure format:**
```c
struct Attr {
    value_id: u32,   // ID of the attribute value
    name_len: u32,   // Length of attribute name in bytes
}
```

Each attribute is 8 bytes (2 × 4 bytes). Use `copy_attrname` to retrieve attribute names.

#### `copy_attrname(value: ValueId, attr_idx: u32, ptr: u32, len: u32)`

Copies an attribute name into Wasm memory.

**Parameters:**
- `value` - ID of a Nix attribute set value
- `attr_idx` - Index of the attribute (from `copy_attrset`)
- `ptr` - Pointer to buffer in Wasm memory
- `len` - Length of the buffer (must exactly match the attribute name length)

**Note:** Throws an error if `len` doesn't match the attribute name length or if `attr_idx` is out of bounds.

#### `get_attr(value: ValueId, ptr: u32, len: u32) -> ValueId`

Gets an attribute value from an attribute set by name.

**Parameters:**
- `value` - ID of a Nix attribute set value
- `ptr` - Pointer to the attribute name in Wasm memory
- `len` - Length of the attribute name in bytes

**Returns:** Value ID of the attribute value, or 0 if the attribute doesn't exist

### Function Operations

#### `call_function(fun: ValueId, ptr: u32, len: u32) -> ValueId`

Calls a Nix function with arguments.

**Parameters:**
- `fun` - ID of a Nix function value
- `ptr` - Pointer to array of `ValueId` arguments in Wasm memory
- `len` - Number of arguments

**Returns:** Value ID of the function result

#### `make_app(fun: ValueId, ptr: u32, len: u32) -> ValueId`

Creates a lazy or partially applied function application.

**Parameters:**
- `fun` - ID of a Nix function value
- `ptr` - Pointer to array of `ValueId` arguments in Wasm memory
- `len` - Number of arguments

**Returns:** Value ID of the unevaluated application

### Returning Results (WASI mode only)

#### `return_to_nix(value: ValueId)`

Returns a result value to the Nix evaluator from a WASI module. This function is only available in WASI mode.

**Parameters:**
- `value` - ID of the Nix value to return as the result of the `builtins.wasm` call

**Note:** Calling this function immediately terminates the WASI module's execution. The module must call `return_to_nix` before finishing; otherwise, an error is raised.

### File I/O

#### `read_file(path: ValueId, ptr: u32, len: u32) -> u32`

Reads a file into Wasm memory.

**Parameters:**
- `path` - Value ID of a Nix path value
- `ptr` - Pointer to buffer in Wasm memory
- `len` - Maximum number of bytes to read

**Returns:** The actual file size in bytes

**Note:** Similar to `builtins.readFile`, but can handle files that cannot be represented as Nix strings (in particular, files containing NUL bytes). If the returned size is greater than `len`, no data is copied.

## Example Usage

For Rust bindings to this interface and several examples, see https://github.com/DeterminateSystems/nix-wasm-rust/.
