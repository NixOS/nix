# Derivation CBOR format

CBOR version 1 is an experimental interchange representation of existing
Nix derivations. It supports arbitrary bytes in the builder, arguments, and
environment names and values, including strings that cannot be represented
by the current derivation JSON format.

`nix derivation show --cbor` writes a single derivation when given one
derivation without `--recursive`. With multiple derivations or `--recursive`,
it writes a collection. `nix derivation add --cbor` reads a single derivation.

Importing uses the existing derivation validation, storage, and hashing
rules. CBOR bytes are not hashed directly. This format does not change
derivation identity or introduce a new addressing scheme.

## Schema

A derivation is a CBOR map with the following fields. Field names are text
strings. All fields except `structuredAttrs` are required.

| Field | CBOR type | Meaning |
| --- | --- | --- |
| `version` | Unsigned integer | Must be `1`. |
| `name` | Text string | Derivation name. |
| `system` | Text string | Build platform. |
| `builder` | Byte string | Builder path. |
| `args` | Array of byte strings | Ordered builder arguments. |
| `env` | Map of byte strings to byte strings | Environment names and values. |
| `outputs` | Map with text keys | Output specifications. |
| `inputs` | Map with text keys | Source and derivation dependencies. |
| `structuredAttrs` | Byte string, optional | Original structured-attribute JSON document. |

The `inputs` and `outputs` substructures use the field definitions of
[derivation JSON version 4](json/derivation/index.md), with JSON objects
encoded as CBOR maps and their strings encoded as CBOR text strings.
An input node has `outputs` (a set of output names encoded as an array) and
`dynamicOutputs` (a map from output names to further input nodes).
Output variants and experimental-feature requirements are shared between
the JSON and CBOR codecs. Changes to the shared schema must consider each
format's version independently.

Text strings must contain valid UTF-8. Byte-valued fields always use byte
strings, even when their contents happen to be valid UTF-8. Environment
names are byte strings too; a decoder that only supports text map keys
cannot decode this format in general.

A collection is a map containing `version` (unsigned integer `1`) and
`derivations` (a map from store-path base names, as text strings, to
derivation maps). Each derivation includes its own `version` field.

## Structured attributes and identity

The `structuredAttrs` byte string must contain a JSON object accepted by
Nix's structured-attribute parser. Its bytes are preserved verbatim,
including whitespace, key order, escaping, and number spellings.
Numbers inside it are JSON bytes, not CBOR integers or floating-point values.

Existing derivation hashes depend on the original JSON spelling.
For example, `{"a":1}` and `{ "a": 1 }` can belong to different derivations
despite describing equivalent JSON values. Importing and exporting CBOR
must preserve this distinction, as required by the
[ATerm identity rules](derivation-aterm.md#canonical-form).

## Deterministic output

The encoder follows the
[RFC 8949 length-first deterministic encoding requirements](https://www.rfc-editor.org/rfc/rfc8949.html#section-4.2.3):

- Integers and lengths use their shortest available encodings.
- All arrays, maps, and strings have definite lengths.
- Map keys are ordered by the length of their encoded key, then by
  unsigned bytewise lexicographic order of that encoding.
- Sets, including input source paths and input output names, are encoded
  as arrays in ascending bytewise lexical order, without duplicates.
- Argument order and the contents of byte strings are preserved.

Only unsigned integers, byte strings, text strings, arrays, maps, and
booleans are supported at the CBOR layer. The schema further restricts
their locations; for example, `impure` must be `true` when present.
Tags, negative integers, floats, null, and indefinite lengths are rejected.

## Import validation

The reader accepts nonminimal integer and length encodings, unsorted maps,
and unsorted set arrays. Exporting the imported derivation normalizes these
to the deterministic encoding described above. Import does not normalize
the embedded structured-attribute JSON.

Duplicate map keys, duplicate set entries, missing or unknown fields,
incorrect wire types, invalid UTF-8 text, unsupported versions, truncated
input, and trailing data are rejected. Dynamic input nodes are limited to
256 levels below the initial node, and the CBOR reader also limits nesting.
The normal derivation invariants are checked when adding a derivation to
the store.
