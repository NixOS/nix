# C++ style guide

Some miscellaneous notes on how we write C++.
Formatting we hope to eventually normalize automatically, so this section is free to just discuss higher-level concerns.

## The `*-impl.hh` pattern

Let's start with some background info first.
Headers, are supposed to contain declarations, not definitions.
This allows us to change a definition without changing the declaration, and have a very small rebuild during development.
Templates, however, need to be specialized to use-sites.
Absent fancier techniques, templates require that the definition, not just mere declaration, must be available at use-sites in order to make that specialization on the fly as part of compiling those use-sites.
Making definitions available like that means putting them in headers, but that is unfortunately means we get all the extra rebuilds we want to avoid by just putting declarations there as described above.

The `*-impl.hh` pattern is a ham-fisted partial solution to this problem.
It constitutes:

- Declaring items only in the main `foo.hh`, including templates.

- Putting template definitions in a companion `foo-impl.hh` header.

Most C++ developers would accompany this by having `foo.hh` include `foo-impl.hh`, to ensure any file getting the template declarations also got the template definitions.
But we've found not doing this has some benefits and fewer than imagined downsides.
The fact remains that headers are rarely as minimal as they could be;
there is often code that needs declarations from the headers but not the templates within them.
With our pattern where `foo.hh` doesn't include `foo-impl.hh`, that means they can just include `foo.hh`
Code that needs both just includes `foo.hh` and `foo-impl.hh`.
This does make linking error possible where something forgets to include `foo-impl.hh` that needs it, but those are build-time only as easy to fix.
