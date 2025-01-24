assert { a.b = [ { c.d = true; } ]; } == { a.b = [ { c.d = false; } ]; };

abort "unreachable"
