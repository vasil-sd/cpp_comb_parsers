# Simple C++ combinatiorial parsers

This is simple, but correct implementation, regarding side effect of actions.

Parsing is done in two stages: 1. parse input, 2. apply effects, if parsing was successful.

So, you may attach to parser actions with side effects and do not worry that they will be triggered many times during backtracking on parsing.

See test.cpp for URI parsing example.
