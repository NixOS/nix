# JSON guideline

Nix consumes and produces JSON in a variety of contexts.
These guidelines ensure consistent practices for all our JSON interfaces, for ease of use, and so that experience in one part carries over to another.

## Extensibility

The schema of JSON input and output should allow for backwards compatible extension.
This section explains how to achieve this.

Two definitions are helpful here, because while JSON only defines one "key-value" object type, we use it to cover two use cases:

 - **dictionary**: a map from names to value that all have the same type.
   In C++ this would be a `std::map` with string keys.

 - **record**: a fixed set of attributes each with their own type.
   In C++, this would be represented by a `struct`.

It is best not to mix these use cases, as that may lead to incompatibilities when the schema changes.
For example, adding a record field to a dictionary breaks consumers that assume all JSON object fields to have the same meaning and type, and dictionary items with a colliding name can not be represented anymore.

This leads to the following guidelines:

 - The top-level (root) value must be a record.

   Otherwise, one can not change the structure of a command's output.

 - The value of a dictionary item must be a record.

   Otherwise, the item type can not be extended.

 - List items should be records.

   Otherwise, one can not change the structure of the list items.

   If the order of the items does not matter, and each item has a unique key that is a string, consider representing the list as a dictionary instead.
   If the order of the items needs to be preserved, return a list of records.

 - Streaming JSON should return records.

   An example of a streaming JSON format is [JSON lines](https://jsonlines.org/), where each line represents a JSON value.
   These JSON values can be considered top-level values or list items, and they must be records.

### Examples

This is bad, because all keys must be assumed to be store types:

```json
{
  "local": { ... },
  "remote": { ... },
  "http": { ... }
}
```

This is good, because the it is extensible at the root, and is somewhat self-documenting:

```json
{
  "storeTypes": { "local": { ... }, ... },
  "pluginSupport": true
}
```

While the dictionary of store types seems like a very complete response at first, a use case may arise that warrants returning additional information.
For example, the presence of plugin support may be crucial information for a client to proceed when their desired store type is missing.



The following representation is bad because it is not extensible:

```json
{ "outputs": [ "out" "bin" ] }
```

However, simply converting everything to records is not enough, because the order of outputs must be preserved:

```json
{ "outputs": { "bin": {}, "out": {} } }
```

The first item is the default output. Deriving this information from the outputs ordering is not great, but this is how Nix currently happens to work.
While it is possible for a JSON parser to preserve the order of fields, we can not rely on this capability to be present in all JSON libraries.

This representation is extensible and preserves the ordering:

```json
{ "outputs": [ { "outputName": "out" }, { "outputName": "bin" } ] }
```

## Self-describing values

As described in the previous section, it's crucial that schemas can be extended with new fields without breaking compatibility.
However, that should *not* mean we use the presence/absence of fields to indicate optional information *within* a version of the schema.
Instead, always include the field, and use `null` to indicate the "nothing" case.

### Examples

Here are two JSON objects:

```json
{
  "foo": {}
}
```
```json
{
  "foo": {},
  "bar": {}
}
```

Since they differ in which fields they contain, they should *not* both be valid values of the same schema.
At most, they can match two different schemas where the second (with `foo` and `bar`) is considered a newer version of the first (with just `foo`).
Within each version, all fields are mandatory (always `foo`, and always `foo` and `bar`).
Only *between* each version, `bar` gets added as a new mandatory field.

Here are another two JSON objects:

```json
{ "foo": null }
```
```json
{ "foo": { "bar": 1 } }
```

Since they both contain a `foo` field, they could be valid values of the same schema.
The schema would have `foo` has an optional field, which is either `null` or an object where `bar` is an integer.
