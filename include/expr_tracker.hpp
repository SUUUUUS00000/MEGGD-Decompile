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
    // if any (checks synthetic pins from pinAsVariable first, then real
    // debug-info locals).
    std::optional<std::string> localNameAt(uint8_t r, uint32_t pc) const;

    // Directly binds register r to a stable named-local reference (used by
    // the structurizer for for-loop control variables, whose lifetime
    // spans the whole loop body rather than being written by a single
    // instruction the tracker would otherwise see).
    void bindLocal(uint8_t r, const std::string& name);

    // Like bindLocal, but also registers a synthetic local spanning
    // [0, functionEnd) with no initializer statement (the function's own
    // signature is the declaration) -- used for parameters, so that a
    // later reassignment to a parameter (e.g. `function f(x) x = x or 1
    // end`) is correctly emitted as `x = x or 1` instead of being
    // silently treated as an anonymous compiler temp.
    void bindParam(uint8_t r, const std::string& name);

    // Forces register r to be treated as a real, stably-named variable for
    // word-pc range [fromPc, toPc), regardless of debug info: materializes
    // its current value (consuming it if compound) into `local name =
    // <current value>` right now, and remembers the binding so any later
    // write to r within that pc range is emitted as a real `name = ...`
    // assignment instead of being silently deferred/frozen. Used when a
    // register is read by a loop/branch condition and therefore needs a
    // stable identity across the construct it guards (e.g. an unnamed
    // loop counter that's both tested and incremented, in bytecode with no
    // debug info to tell us its real name).
    void pinAsVariable(uint8_t r, uint32_t fromPc, uint32_t toPc, const std::string& name);

    void registerSyntheticLocalRange(uint8_t r, uint32_t fromPc, uint32_t toPc, const std::string& name);

    // True if register r currently holds a pending *compound* value (one
    // that regValue() would consume/clear on read) rather than a freely
    // re-readable atom or nothing at all. The structurizer uses this to
    // decide which condition-operand registers actually need pinning:
    // compounds always do (reading them once already clears them, so a
    // second read inside a branch would otherwise see nothing); bare
    // atoms don't unless they're also going to be *reassigned* inside a
    // loop this condition guards (a separate, loop-specific concern).
    bool isCompoundPending(uint8_t r) const;

    // True if register r currently holds *any* pending value (atom or
    // compound) -- used to detect loop-carried variables: a register
    // written before a loop and reassigned inside it needs pinning even
    // though it isn't a condition operand (e.g. an accumulator).
    bool hasLiveValue(uint8_t r) const;

    // Synthesizes a fresh v0/v1/... name (exposed so the structurizer can
    // generate one for pinAsVariable when no debug name is available).
    std::string freshSyntheticName();

    // Explicitly drops any pending value for register r. Used by the
    // structurizer after a loop construct is fully processed to clear its
    // internal control registers (limit/step, generator/state/control),
    // which are otherwise indistinguishable from a genuinely live value
    // if the compiler later reuses the same register number for something
    // in completely unrelated code.
    void clearRegister(uint8_t r);

private:
    const Module& module_;
    const Proto& proto_;

    std::vector<ExprPtr> regExpr_;      // pending symbolic values per register
    std::vector<bool> regIsAtom_;       // true if regExpr_[r] is safe to re-read without consuming
    std::vector<bool> regIsMultretTail_;
    std::vector<std::optional<std::string>> declaredName_; // name last declared (via `local`) for reg r, if any
    std::vector<StmtPtr> stmts_;
    int freshCounter_ = 0;

    struct SyntheticLocal
    {
        uint8_t reg;
        uint32_t startpc;
        uint32_t endpc;
        std::string name;
    };
    std::vector<SyntheticLocal> syntheticLocals_;

    // Unified lookup used internally by produceValue/emitNamedWrite: checks
    // syntheticLocals_ (most specific / most recently pinned) before
    // falling back to proto_.locals.
    std::optional<std::string> activeLocalName(uint8_t r, uint32_t pc) const;

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
