// code_printer.hpp
//
// Renders the IR built by ExprTracker/Structurizer as Luau-like source
// text: operator precedence-aware expression printing, indentation,
// elseif-chain flattening, and `local function` / `function t:method()`
// sugar detection.

#pragma once

#include "bytecode_types.hpp"
#include "ir.hpp"

#include <string>
#include <vector>

namespace luaudec
{

class CodePrinter
{
public:
    // `bodiesByProto[i]` must hold the already-structured statement list
    // for module.protos[i] (see decompiler.cpp, which structures every
    // proto up front so nested-closure references can be expanded
    // recursively here without re-running the structurizer per reference).
    CodePrinter(const Module& module, const std::vector<std::vector<StmtPtr>>& bodiesByProto)
        : module_(module), bodiesByProto_(bodiesByProto)
    {
    }

    // Prints proto `index`'s body as a top-level chunk (no enclosing
    // `function ... end`; used for the module's main proto).
    std::string printChunk(uint32_t protoIndex);

    // Prints proto `index` as `function(params) ... end` (no leading
    // keyword/name -- callers wanting `local function f(...)` or
    // `function t:m(...)` prepend that themselves; see printStmt's
    // handling of Local/Assign with a Function value for where this is
    // used automatically).
    std::string printFunctionLiteral(uint32_t protoIndex, int indent);

private:
    const Module& module_;
    const std::vector<std::vector<StmtPtr>>& bodiesByProto_;

    void printBlock(const std::vector<StmtPtr>& body, int indent, std::string& out);
    void printStmt(const StmtPtr& s, int indent, std::string& out);
    std::string printExpr(const ExprPtr& e, int minPrec);
    std::string printPrimary(const ExprPtr& e); // wraps non-primary exprs in parens
    std::string printExprList(const std::vector<ExprPtr>& es);
    std::string indentStr(int n);
    std::vector<std::string> paramNames(const Proto& proto);
};

} // namespace luaudec
