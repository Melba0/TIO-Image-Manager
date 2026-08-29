#pragma once
class Context;

// Register all built-in macros into the shared macro table:
//   math:          max, min, abs, sqrt, pow, log, exp
//   geometric:     big, small, left, right, top, bottom, square
//   relational:    left_of, above, inside
//   atmosphere:    warm, cool, bright, dark, smooth, rough
void registerBuiltinMacros(Context& ctx);