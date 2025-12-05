# Derivation Resolution

*See [this chapter's appendix](@docroot@/store/math-notation.md) on grammar and metavariable conventions.*

To *resolve* a derivation is to replace its [inputs] with the simplest inputs &mdash; plain store paths &mdash; that denote the same store objects.

Derivations that only have store paths as inputs are likewise called *resolved derivations*.
(They are called that whether they are in fact the output of derivation resolution, or just made that way without non-store-path inputs to begin with.)

## Input Content Equivalence of Derivations

[Deriving paths][deriving-path] intentionally make it possible to refer to the same [store object] in multiple ways.
This is a consequence of content-addressing, since different derivations can produce the same outputs, and the same data can also be manually added to the store.
This is also a consequence even of input-addressing, as an output can be referred to by derivation and output name, or directly by its [computed](./derivation/outputs/input-address.md) store path.
Since dereferencing deriving paths is thus not injective, it induces an equivalence relation on deriving paths.

Let's call this equivalence relation \\(\\sim\\), where \\(p_1 \\sim p_2\\) means that deriving paths \\(p_1\\) and \\(p_2\\) refer to the same store object.

**Content Equivalence**: Two deriving paths are equivalent if they refer to the same store object:

\\[
\\begin{prooftree}
\\AxiomC{${}^*p_1 = {}^*p_2$}
\\UnaryInfC{$p_1 \\,\\sim_\\mathrm{DP}\\, p_2$}
\\end{prooftree}
\\]

where \\({}^\*p\\) denotes the store object that deriving path \\(p\\) refers to.

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

## Resolution relation

Dereferencing a derived path &mdash; \\({}^\*p\\) above &mdash; was just introduced as a black box.
But actually it is a multi-step process of looking up build results in the [build trace] that itself depends on resolving the lookup keys.
Resolution is thus a recursive multi-step process that is worth diagramming formally.

We can do this with a small-step binary transition relation; let's call it \\(\rightsquigarrow\\).
We can then conclude dereferenced equality like this:

\\[
\\begin{prooftree}
\\AxiomC{$p\_1 \\rightsquigarrow^* p$}
\\AxiomC{$p\_2 \\rightsquigarrow^* p$}
\\BinaryInfC{${}^*p\_1 = {}^*p\_2$}
\\end{prooftree}
\\]

I.e. by showing that both original items resolve (over 0 or more small steps, hence the \\({}^*\\)) to the same exact item.

With this motivation, let's now formalize a [small-step](https://en.wikipedia.org/wiki/Operational_semantics#Small-step_semantics) system of reduction rules for resolution.

### Formal rules

### \\(\text{resolved}\\) unary relation

\\[
\\begin{prooftree}
\\AxiomC{$s \in \text{store-path}$}
\\UnaryInfC{$s$ resolved}
\\end{prooftree}
\\]

\\[
\\begin{prooftree}
\\AxiomC{$\forall i \in \mathrm{inputs}(d). i \text{ resolved}$}
\\UnaryInfC{$d$ resolved}
\\end{prooftree}
\\]

### \\(\rightsquigarrow\\) binary relation

> **Remark**
>
> Actually, to be completely formal we would need to keep track of the build trace we are choosing to resolve against.
>
> We could do that by making \\(\rightsquigarrow\\) a ternary relation, which would pass the build trace to itself until it finally uses it in that one rule.
> This would add clutter more than insight, so we didn't bother to write it.
>
> There are other options too, like saying the whole reduction rule system is parameterized on the build trace, essentially [currying](https://en.wikipedia.org/wiki/Currying) the ternary \\(\rightsquigarrow\\) into a function from build traces to the binary relation written above.

#### Core build trace lookup rule

\\[
\\begin{prooftree}
\\AxiomC{$s \in \text{store-path}$}
\\AxiomC{${}^*s \in \text{derivation}$}
\\AxiomC{${}^*s$ resolved}
\\AxiomC{$\mathrm{build\text{-}trace}[s][o] = t$}
\\QuaternaryInfC{$(s, o) \rightsquigarrow t$}
\\RightLabel{\\scriptsize output path resolution}
\\end{prooftree}
\\]

#### Inductive rules

\\[
\\begin{prooftree}
\\AxiomC{$i \\rightsquigarrow i'$}
\\AxiomC{$i \\in \\mathrm{inputs}(d)$}
\\BinaryInfC{$d \\rightsquigarrow d[i \\mapsto i']$}
\\end{prooftree}
\\]

\\[
\\begin{prooftree}
\\AxiomC{$d \\rightsquigarrow d'$}
\\UnaryInfC{$(\\mathrm{path}(d), o) \\rightsquigarrow (\\mathrm{path}(d'), o)$}
\\end{prooftree}
\\]

\\[
\\begin{prooftree}
\\AxiomC{$p \\rightsquigarrow p'$}
\\UnaryInfC{$(p, o) \\rightsquigarrow (p', o)$}
\\end{prooftree}
\\]

### Properties

Like all well-behaved evaluation relations, partial resolution is [*confluent*](https://en.wikipedia.org/wiki/Confluence_(abstract_rewriting)).
Also, if we take the symmetric closure of \\(\\rightsquigarrow^\*\\), we end up with the equivalence relations of the previous section.
Resolution respects content equivalence for deriving paths, and input content equivalence for derivations.

> **Remark**
>
> We chose to define from scratch an "resolved" unary relation explicitly above.
> But it can also be defined as the normal forms of the \\(\\rightsquigarrow^\*\\) relation:
>
> \\[ a \text{ resolved} \Leftrightarrow \forall b. b \rightsquigarrow^* a \Rightarrow b = a\\]
>
> In prose, resolved terms are terms which \\(\\rightsquigarrow^\*\\) only relates on the left side to the same term on the right side; they are the terms which can be resolved no further.

## Partial versus Complete Resolution

Similar to evaluation, we can also speak of *partial* versus *complete* derivation resolution.
Partial derivation resolution is what we've actually formalized above with \\(\\rightsquigarrow^\*\\).
Complete resolution is resolution ending in a resolved term (deriving path or derivation).
(Which is a normal form of the relation, per the remark above.)

With partial resolution, a derivation is related to equivalent derivations with the same or simpler inputs, but not all those inputs will be plain store paths.
This is useful when the input refers to a floating content addressed output we have not yet built &mdash; we don't know what (content-address) store path will used for that derivation, so we are "stuck" trying to resolve the deriving path in question.
(In the above formalization, this happens when the build trace is missing the keys we wish to look up in it.)

Complete resolution is a *functional* relation, i.e. values on the left are uniquely related with values on the right.
It is not however, a *total* relation (in general, assuming arbitrary build traces).
This is discussed in the next section.

## Termination

For static derivations graphs, complete resolution is indeed total, because it always terminates for all inputs.
(A relation that is both total and functional is a function.)

For [dynamic][xp-feature-dynamic-derivations] derivation graphs, however, this is not the case &mdash; resolution is not guaranteed to terminate.
The issue isn't rewriting deriving paths themselves:
a single rewrite to normalize an output deriving path to a constant one always exists, and always proceeds in one step.
The issue is that dynamic derivations (i.e. those that are filled-in the graph by a previous resolution) may have more transitive dependencies than the original derivation.

> **Example**
>
> Suppose we have this deriving path
> ```json
> {
>   "drvPath": {
>     "drvPath": "...-foo.drv",
>     "output": "bar.drv"
>   },
>   "output": "baz"
> }
> ```
> and derivation `foo` is already resolved.
> When we resolve deriving path we'll end up with something like.
> ```json
> {
>   "drvPath": "...-foo-bar.drv",
>   "output": "baz"
> }
> ```
> So far is just an atomic single rewrite, with no termination issues.
> But the derivation `foo-bar` may have its *own* dynamic derivation inputs.
> Resolution must resolve that derivation first before the above deriving path can finally be normalized to a plain `...-foo-bar-baz` store path.

The important thing to notice is that while "build trace" *keys* must be resolved.
The *value* those keys are mapped to have no such constraints.
An arbitrary store object has no notion of being resolved or not.
But, an arbitrary store object can be read back as a derivation (as will in fact be done in case for dynamic derivations / nested output deriving paths).
And those derivations need *not* be resolved.

It is those dynamic non-resolved derivations which are the source of non-termination.
By the same token, they are also the reason why dynamic derivations offer greater expressive power.

[store object]: @docroot@/store/store-object.md
[inputs]: @docroot@/store/derivation/index.md#inputs
[build trace]: @docroot@/store/build-trace.md
[deriving-path]: @docroot@/store/derivation/index.md#deriving-path
[xp-feature-dynamic-derivations]: @docroot@/development/experimental-features.md#xp-feature-dynamic-derivations
