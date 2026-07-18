// structurizer.hpp
//
// Recovers structured control flow (if/elseif/else, numeric/generic for,
// while, repeat-until, break, continue) from a Proto's flat instruction
// list + resolved jump targets, driving an ExprTracker to turn
// straight-line runs into statements/expressions along the way.
//
// This is a pattern-matcher tuned to the specific, regular jump shapes
// luau-compile's own (single-pass, non-adversarial) codegen produces --
// documented and verified against real compiler output in expr_tracker.cpp
// and bytecode_reader.cpp's commit history. Hand-crafted or obfuscated
// bytecode with irreducible control flow will degrade gracefully to
// comment-annotated raw jumps rather than crash, but won't be "pretty".

#pragma once

#include "bytecode_types.hpp"
#include "ir.hpp"

#include <vector>

namespace luaudec
{

// Structures the full body of `proto` into a statement list.
std::vector<StmtPtr> structureFunction(const Module& module, const Proto& proto);

} // namespace luaudec
