# Experimental Python Bindings

## callExprString

```python
nix.callExprString(expression: str, arg)
```
Parse a nix expression, then call it as a nix function.

Note that this function is experimental and subject to change based on known issues and feedback.

**Parameters:**,
    `expression` (str): The string containing a nix expression.
    `arg`: the argument to pass to the function

**Returns:**
    `result`: the result of the function invocation, converted to python datatypes.

