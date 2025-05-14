---
synopsis: "Consistently preserve error messages from cached evaluation"
issues: [12762]
prs: [12809]
---

In one code path, we are not returning the errors cached from prior evaluation, but instead throwing generic errors stemming from the lack of value (due to the error).
These generic error messages were far less informative.
Now we consistently return the original error message.
