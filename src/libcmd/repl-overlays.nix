info:
initial:
functions:
let final = builtins.foldl'
              (prev: function: prev // (function info final prev))
              initial
              functions;
in final
