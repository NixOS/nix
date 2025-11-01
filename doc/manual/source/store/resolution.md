# Derivation Resolution

To *resolve* a derivation is to replace its [inputs] with the simplest inputs --- plain store paths --- that denote the same store objects.

Derivations that only have store paths as inputs are likewise called *resolved derivations*.
(They are called that whether they are in fact the output of derivation resolution, or just made that way without non-store-path inputs to begin with.)

[Deriving paths][deriving-path] intentionally make it possible to refer to the same [store object] in multiple ways.
This is a consequence of content-addressing, since different derivations can produce the same outputs, and the same date can also be manually added to the store.
This is also a consequence even of input-addressing, as an output can be referred to by derivation and output name, or directly by its store path input address.
Since dereferencing deriving paths is thus not injective, it induces an equivalence relation on deriving paths.

Let's call this equivalence relation \\(\\sim\\), where \\(p_1 \\sim p_2\\) means that deriving paths \\(p_1\\) and \\(p_2\\) refer to the same store object.

**Content Equivalence**: Two deriving paths are equivalent if they refer to the same store object:

\\[
\\begin{prooftree}
\\AxiomC{${}^*p_1 = {}^*p_2$}
\\UnaryInfC{$p_1 \\,\\sim_\\mathrm{DP}\\, p_2$}
\\end{prooftree}
\\]

where \\({}^*p\\) denotes the store object that deriving path \\(p\\) refers to.

This also induces an equivalence relation on sets of deriving paths:

\\[
\\begin{prooftree}
\\AxiomC{$\\{ {}^*p | p \\in P_1 \\} = \\{ {}^*p | p \\in P_2 \\}$}
\\UnaryInfC{$P_1 \\,\\sim_{\\mathcal{P}(\\mathrm{DP})}\\, P_2$}
\\end{prooftree}
\\]

**Input Content Equivalence**: This, in turn, induces an equivalence relation on derivations: two derivations are equivalent if their inputs are equivalent, and they are otherwise equal:

\\[
\\begin{prooftree}
\\AxiomC{$\\mathrm{inputs}(d_1) \\,\\sim_{\\mathcal{P}(\\mathrm{DP})}\\, \\mathrm{inputs}(d_2)$}
\\AxiomC{$
  d\_1\left[\\mathrm{inputs} := \\{\\}\right]
  \=
  d\_2\left[\\mathrm{inputs} := \\{\\}\right]
$}
\\BinaryInfC{$d_1 \\,\\sim_\\mathrm{Drv}\\, d_2$}
\\end{prooftree}
\\]

Derivation resolution always maps derivations to input-content-equivalent derivations.

Similar to evaluation, we can also speak of *partial* vs *total* derivation resolution.
Total resolution is the function described above.
For partial resolution, a derivation is related to equivalent derivations with the same or simpler inputs, but not all those inputs will be plain store paths.
This is useful when the input refers to a floating content addressed output we have not yet built --- we don't know what (content-address) store path will used for that derivation, so we are "stuck" trying to resolve derived path in question.
Partial resolution is not a function, but an (assymetic) relation, created by directing the above equivalence relation so the right-side items are always equal or simpler.
(This is the usual practice for evaluation relations.)
Like well-behaved evaluation relations, partial resolution is [*confluent*](https://en.wikipedia.org/wiki/Confluence_(abstract_rewriting)).

[store object]: @docroot@/store/store-object.md
[inputs]: @docroot@/store/derivation/index.md#inputs
[deriving-path]: @docroot@/store/derivation/index.md#deriving-path
