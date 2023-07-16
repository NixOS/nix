import nix

def greet():
    print("Evaluating 1 + 1 in Nix gives: " + str(nix.callExprString("_: 1 + 1", None)))
