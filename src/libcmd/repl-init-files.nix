info:
initial:
functions:
let final = builtins.foldl'
              (prev: function: prev // (function info prev final))
              initial
              functions;
in final
