// expr_tracker.hpp
//
// Walks a straight-line run of instructions (no branches) and:
//  - builds Expr nodes for pure/value-producing instructions, deferring
//    them in a per-register slot so a later consumer can inline them
//    (turning `ADD R2 R0 R1` followed by a use of R2 into `a + b` directly
//    instead of `local tmp = a + b; ...tmp...`);
//  - immediately emits Stmt nodes for anything with a visible effect
//    (calls, table/global/upvalue writes) or anything that debug info
//    identifies as a named local being written;
//  - accumulates in-progress table constructors (NEWTABLE/DUPTABLE +
//    SETLIST/SETTABLEKS/SETTABLEN) into a single TableCtor expression.
//
// This is *not* a general dataflow engine: it assumes compiler-generated,
// non-adversarial bytecode where a temporary register is read at most once
// before being overwritten (true for luau-compile's own output). See
// regValue()'s fallback behavior for what happens when that assumption is
// violated.

#pragma once

#include "bytecode_types.hpp"
#include "ir.hpp"

#include <optional>
#include <vector>

namespace luaudec
{

class ExprTracker
{
public:
    ExprTracker(const Module& module, const Proto& proto);

    // Feeds one non-branching instruction to the tracker. May append to
    // the accumulated statement list. `nextPc` is the word-pc of the
    // instruction immediately following (used to resolve which named
    // local, if any, a register write corresponds to).
    void step(const Instruction& insn, uint32_t nextPc);

    // Returns (and does not clear) the current symbolic value of register
    // r: an inlineable Expr if one is pending, else a reference to the
    // active named local for r (if debug info covers it at this point),
    // else a raw register placeholder. Marks compound (non-atom) values
    // as consumed so a second read falls back to the register placeholder
    // instead of duplicating a possibly-effectful expression.
    ExprPtr regValue(uint8_t r);

    // Peek without consuming (used for loop-header extraction where the
    // same setup registers may need to be read without side effects).
    ExprPtr peekRegValue(uint8_t r) const;

    // Drains and returns all statements accumulated so far (call sites,
    // assignments to named locals/globals/upvalues/table fields).
    std::vector<StmtPtr> takeStmts();

    // Flushes any still-pending compound register expressions into
    // `local vN = expr` statements (used at region/branch boundaries so
    // nothing with potential side effects is silently dropped or
    // duplicated across a control-flow split), then drains statements.
    std::vector<StmtPtr> flushAndTakeStmts();

    // Gathers `startReg .. startReg+n-1` as an expression list, per the
    // "count+1, 0=multret" convention shared by CALL/RETURN/SETLIST/
    // GETVARARGS. For MULTRET, scans consecutive live registers.
    std::vector<ExprPtr> gatherRange(uint8_t startReg, uint8_t countPlus1);

    // Builds a Call expression for the CALL instruction at `insn`, folding
    // in a preceding NAMECALL if one is pending for this call's function
    // register (renders as obj:method(...) instead of obj.method(obj,...)).
    // Used by the structurizer, which owns statement emission for CALL
    // (bare-call statement vs. deferred single value vs. multi-result
    // local/assign) since that depends on surrounding context (debug
    // names for multiple results) the tracker alone doesn't need.
    ExprPtr buildCallExpr(const Instruction& insn);

    // Emits `local a, b, ... = value` or, if all targets already have an
    // active local declared for their prior entry, `a, b, ... = value`.
    // Used for multi-result CALL/GETVARARGS.
    void materializeMultiWrite(uint8_t startReg, uint32_t count, ExprPtr multiValueExpr, uint32_t nextPc);

    // Resolves constant `idx` in this proto to an Expr.
    ExprPtr constantExpr(uint32_t idx) const;

    // Resolves a String-tagged constant to its raw text (used for
    // GETGLOBAL/SETGLOBAL, which reference a name, not a full value).
    std::string constantNameOnly(uint32_t idx) const;

    // Looks up the active named local covering register r at word-pc pc,
    // if any.
    std::optional<std::string> localNameAt(uint8_t r, uint32_t pc) const;

private:
    const Module& module_;
    const Proto& proto_;

    std::vector<ExprPtr> regExpr_;      // pending symbolic values per register
    std::vector<bool> regIsAtom_;       // true if regExpr_[r] is safe to re-read without consuming
    std::vector<int> declaredLocalIdx_; // which proto_.locals[] entry (if any) is the last one declared for reg r; -1 if none yet
    std::vector<StmtPtr> stmts_;
    int freshCounter_ = 0;

    void setReg(uint8_t r, ExprPtr e, bool isAtom);
    void clearReg(uint8_t r);
    std::string freshName();

    // Central policy point: if register r is an active named local as of
    // nextPc, immediately emits `local name = value` / `name = value` (so
    // named source variables always appear as real statements, never get
    // silently inlined into something else); otherwise defers via setReg
    // per the atom/compound rule.
    void produceValue(uint8_t r, ExprPtr value, bool isAtom, uint32_t nextPc);

    // Emits either `local name = value` (first time this local entry is
    // seen) or `name = value` (subsequent writes to the same local).
    void emitNamedWrite(uint8_t r, uint32_t nextPc, ExprPtr value);

    // Table-constructor-in-progress registers (NEWTABLE/DUPTABLE target).
    // Tracks the running array index for SETLIST-free single-item appends.
    struct TableAccum
    {
        ExprPtr expr; // EK::TableCtor
        uint32_t nextArrayIndex = 1;
    };
    std::vector<std::optional<TableAccum>> tableAccum_;

    // Debug-name hint recorded at NEWTABLE/DUPTABLE time (table
    // constructors are never immediately materialized like other named
    // writes, since they need to keep accumulating via tableAccum_; this
    // lets later materialization still use the right name when available).
    std::vector<std::optional<std::string>> nameHint_;

    struct PendingNamecall
    {
        uint8_t funcReg;
        ExprPtr base;
        std::string method;
    };
    std::optional<PendingNamecall> pendingNamecall_;
};

} // namespace luaudec
