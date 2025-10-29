# Appendix: Math notation

A few times in this manual, formal "proof trees" are used for [natural deduction](https://en.wikipedia.org/wiki/Natural_deduction)-style definition of various [relations](https://en.wikipedia.org/wiki/Relation_(mathematics)).

The following grammar and assignment of metavariables to syntactic categories is used in these sections.

\\begin{align}
s, t &\in \text{store-path} \\\\
o &\in \text{output-name} \\\\
i, p &\in \text{deriving-path} \\\\
d &\in \text{derivation}
\\end{align}

\\begin{align}
\text{deriving-path} \quad p &::= s \mid (p, o)
\\end{align}
