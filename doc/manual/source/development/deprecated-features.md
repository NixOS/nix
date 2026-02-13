This section describes the notion of *deprecated features*, and how it fits into the big picture of the development of Nix.

# What are deprecated features?

Deprecated features are legacy features that are scheduled for removal.
They are disabled by default but can be re-enabled by toggling the associated [deprecated feature flags](@docroot@/command-ref/conf-file.md#conf-deprecated-features).
This allows for a transition period where users can adapt their code.

# Deprecated feature descriptions

{{#include @generated@/development/deprecated-feature-descriptions.md}}
