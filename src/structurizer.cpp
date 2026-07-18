// structurizer.cpp
#include "structurizer.hpp"
#include "expr_tracker.hpp"

#include <unordered_map>
#include <optional>

namespace luaudec
{

namespace
{

bool isConditionalJump(Op op)
{
    switch (op)
    {
    case Op::JUMPIF:
    case Op::JUMPIFNOT:
    case Op::JUMPIFEQ:
    case Op::JUMPIFLE:
    case Op::JUMPIFLT:
    case Op::JUMPIFNOTEQ:
    case Op::JUMPIFNOTLE:
    case Op::JUMPIFNOTLT:
    case Op::JUMPXEQKNIL:
    case Op::JUMPXEQKB:
    case Op::JUMPXEQKN:
    case Op::JUMPXEQKS:
    case Op::CMPPROTO:
        return true;
    default:
        return false;
    }
}

bool isFastCall(Op op)
{
    return op == Op::FASTCALL || op == Op::FASTCALL1 || op == Op::FASTCALL2 || op == Op::FASTCALL2K || op == Op::FASTCALL3;
}

bool isForPrep(Op op)
{
    return op == Op::FORGPREP || op == Op::FORGPREP_INEXT || op == Op::FORGPREP_NEXT;
}

// Registers a conditional-jump instruction reads as comparison operands
// (not counting constant/immediate operands). Used to decide which
// registers need pinning to a stable variable before a branch/loop
// condition is built -- see Structurizer::pinConditionRegs.
std::vector<uint8_t> conditionOperandRegs(const Instruction& insn)
{
    switch (insn.op)
    {
    case Op::JUMPIF:
    case Op::JUMPIFNOT:
    case Op::JUMPXEQKNIL:
    case Op::JUMPXEQKB:
    case Op::JUMPXEQKN:
    case Op::JUMPXEQKS:
        return {insn.a};
    case Op::JUMPIFEQ:
    case Op::JUMPIFLE:
    case Op::JUMPIFLT:
    case Op::JUMPIFNOTEQ:
    case Op::JUMPIFNOTLE:
    case Op::JUMPIFNOTLT:
        return {insn.a, auxA(insn.aux)};
    default:
        return {};
    }
}

} // namespace

class Structurizer
{
public:
    Structurizer(const Module& module, const Proto& proto) : module_(module), proto_(proto), tracker_(module, proto)
    {
        buildWhileRepeatMap();
        bindParams();
    }

    // Registers 0..numParams-1 hold the function's parameters from entry
    // (pc 0) with no explicit "write" instruction of their own -- without
    // this, the tracker has no recorded value for them at all and body
    // references would fall back to a raw `R0`-style placeholder. Naming
    // here must match CodePrinter::paramNames exactly so the signature
    // and body agree.
    void bindParams()
    {
        for (uint8_t i = 0; i < proto_.numParams; ++i)
        {
            std::string name = tracker_.localNameAt(i, 0).value_or("p" + std::to_string(i));
            tracker_.bindParam(i, name);
        }
    }

    std::vector<StmtPtr> run() { return structureRange(0, static_cast<int>(proto_.instructions.size()), nullptr); }

private:
    const Module& module_;
    const Proto& proto_;
    ExprTracker tracker_;

    struct LoopCtx
    {
        int32_t exitTarget;
        int32_t continueTarget;
    };

    // Maps a while/repeat loop's header instruction index to the index of
    // the JUMPBACK that closes it (the *last*/outermost one targeting that
    // header, so inner `continue` JUMPBACKs to the same header are found
    // during body structuring instead of being mistaken for the loop's own
    // closing edge).
    std::unordered_map<int, int> whileRepeatMap_;

    void buildWhileRepeatMap()
    {
        for (size_t i = 0; i < proto_.instructions.size(); ++i)
        {
            const Instruction& insn = proto_.instructions[i];
            if (insn.op == Op::JUMPBACK && insn.jumpTarget >= 0 && insn.jumpTarget <= static_cast<int32_t>(i))
            {
                int h = insn.jumpTarget;
                auto it = whileRepeatMap_.find(h);
                if (it == whileRepeatMap_.end() || static_cast<int>(i) > it->second)
                    whileRepeatMap_[h] = static_cast<int>(i);
            }
        }
    }

    // Builds the condition for a conditional-jump instruction.
    // fallthroughIsTrue=true (if-statements, while-loops): normalizes so
    // that *falling through* means "condition true" -- negating/flipping
    // per-opcode as needed, since about half the family jumps on true and
    // half jumps on false.
    // fallthroughIsTrue=false (repeat...until): returns the *raw*
    // jump-is-true reading instead, since "until COND" exits (jumps)
    // exactly when COND holds -- no fallthrough normalization wanted
    // there.
    ExprPtr buildCondition(const Instruction& insn, bool fallthroughIsTrue = true)
    {
        auto pick = [&](BinOpKind whenFallthroughConvention, BinOpKind whenRawConvention) {
            return fallthroughIsTrue ? whenFallthroughConvention : whenRawConvention;
        };
        switch (insn.op)
        {
        case Op::JUMPIF: // jump=truthy
            return fallthroughIsTrue ? Expr::mkUnary(UnOpKind::Not, tracker_.regValue(insn.a)) : tracker_.regValue(insn.a);
        case Op::JUMPIFNOT: // jump=falsy
            return fallthroughIsTrue ? tracker_.regValue(insn.a) : Expr::mkUnary(UnOpKind::Not, tracker_.regValue(insn.a));
        case Op::JUMPIFEQ: // jump=(a==aux)
            return Expr::mkBinary(pick(BinOpKind::Ne, BinOpKind::Eq), tracker_.regValue(insn.a), tracker_.regValue(auxA(insn.aux)));
        case Op::JUMPIFLE: // jump=(a<=aux)
            return Expr::mkBinary(pick(BinOpKind::Gt, BinOpKind::Le), tracker_.regValue(insn.a), tracker_.regValue(auxA(insn.aux)));
        case Op::JUMPIFLT: // jump=(a<aux)
            return Expr::mkBinary(pick(BinOpKind::Ge, BinOpKind::Lt), tracker_.regValue(insn.a), tracker_.regValue(auxA(insn.aux)));
        case Op::JUMPIFNOTEQ: // jump=(a~=aux)
            return Expr::mkBinary(pick(BinOpKind::Eq, BinOpKind::Ne), tracker_.regValue(insn.a), tracker_.regValue(auxA(insn.aux)));
        case Op::JUMPIFNOTLE: // jump=(a>aux)
            return Expr::mkBinary(pick(BinOpKind::Le, BinOpKind::Gt), tracker_.regValue(insn.a), tracker_.regValue(auxA(insn.aux)));
        case Op::JUMPIFNOTLT: // jump=(a>=aux)
            return Expr::mkBinary(pick(BinOpKind::Lt, BinOpKind::Ge), tracker_.regValue(insn.a), tracker_.regValue(auxA(insn.aux)));
        case Op::JUMPXEQKNIL:
        {
            bool jumpMeansEqual = !auxNot(insn.aux); // notFlag=0 => jump when equal
            bool wantEqual = fallthroughIsTrue ? !jumpMeansEqual : jumpMeansEqual;
            return Expr::mkBinary(wantEqual ? BinOpKind::Eq : BinOpKind::Ne, tracker_.regValue(insn.a), Expr::mkNil());
        }
        case Op::JUMPXEQKB:
        {
            ExprPtr b = Expr::mkBool(auxKB(insn.aux) != 0);
            bool jumpMeansEqual = !auxNot(insn.aux);
            bool wantEqual = fallthroughIsTrue ? !jumpMeansEqual : jumpMeansEqual;
            return Expr::mkBinary(wantEqual ? BinOpKind::Eq : BinOpKind::Ne, tracker_.regValue(insn.a), b);
        }
        case Op::JUMPXEQKN:
        case Op::JUMPXEQKS:
        {
            ExprPtr k = tracker_.constantExpr(auxKV(insn.aux));
            bool jumpMeansEqual = !auxNot(insn.aux);
            bool wantEqual = fallthroughIsTrue ? !jumpMeansEqual : jumpMeansEqual;
            return Expr::mkBinary(wantEqual ? BinOpKind::Eq : BinOpKind::Ne, tracker_.regValue(insn.a), k);
        }
        case Op::CMPPROTO:
        default:
            return Expr::mkRaw("--[[ unsupported condition (" + std::string(opInfo(static_cast<uint8_t>(insn.op)).name) + ") ]] true");
        }
    }

    // Forces every register the condition instruction reads to a stable
    // named-variable binding valid through [pc, untilPc), before the
    // condition expression itself is built. Without this, an unnamed
    // register that's read here and *also* read or reassigned inside the
    // branch/loop this condition guards would either appear frozen at its
    // pre-branch value (if never consumed) or leak a raw register
    // placeholder (if a compound value got consumed here and the branch
    // tries to read it again).
    void pinConditionRegs(const Instruction& condInsn, uint32_t fromPc, uint32_t untilPc, std::vector<StmtPtr>& result, bool pinAtomsToo)
    {
        for (uint8_t reg : conditionOperandRegs(condInsn))
        {
            if (!pinAtomsToo && !tracker_.isCompoundPending(reg))
                continue; // safe to leave as a freely re-readable atom (e.g. a literal constant)
            std::string name = tracker_.localNameAt(reg, fromPc).value_or(tracker_.freshSyntheticName());
            tracker_.pinAsVariable(reg, fromPc, untilPc, name);
        }
        for (StmtPtr& s : tracker_.takeStmts())
            result.push_back(std::move(s));
    }

    // Main recursive worker: structures instructions [lo, hi) into a
    // statement list. `enclosing` (nullable) gives the break/continue
    // targets of the nearest enclosing loop.
    std::vector<StmtPtr> structureRange(int lo, int hi, const LoopCtx* enclosing)
    {
        std::vector<StmtPtr> result;
        int i = lo;

        auto drain = [&]() {
            for (StmtPtr& s : tracker_.takeStmts())
                result.push_back(std::move(s));
        };

        while (i < hi)
        {
            const Instruction& insn = proto_.instructions[i];

            if (isFastCall(insn.op))
            {
                // The fast path's guaranteed fallback (GETIMPORT/MOVE/
                // GETUPVAL + CALL) computes the same value; skip the hint
                // entirely and let the fallback be processed normally.
                ++i;
                continue;
            }

            // --- Loop headers (checked before generic branch handling) ---
            if (insn.op == Op::FORNPREP)
            {
                drain();
                i = handleNumericFor(i, result);
                continue;
            }
            if (isForPrep(insn.op))
            {
                drain();
                i = handleGenericFor(i, result);
                continue;
            }
            {
                auto it = whileRepeatMap_.find(i);
                if (it != whileRepeatMap_.end())
                {
                    drain();
                    i = handleWhileOrRepeat(i, it->second, result);
                    continue;
                }
            }

            // --- Terminators / branches ---
            if (insn.op == Op::RETURN)
            {
                drain();
                std::vector<ExprPtr> values = tracker_.gatherRange(insn.a, insn.b);
                result.push_back(Stmt::mkReturn(std::move(values)));
                return result; // RETURN always ends the enclosing block
            }

            if (insn.op == Op::LOADB && insn.c != 0)
            {
                // Boolean-from-comparison codegen: set + unconditional
                // skip. Not collapsed into an inline ternary (future
                // polish); just respect the skip so we don't also process
                // the alternate branch's LOADB as if it always ran too.
                tracker_.step(insn, (i + 1 < static_cast<int>(proto_.instructions.size())) ? proto_.instructions[i + 1].pc : proto_.totalWordCount);
                i += 1 + insn.c;
                continue;
            }

            if (insn.op == Op::JUMP || insn.op == Op::JUMPBACK)
            {
                drain();
                int32_t target = insn.jumpTarget;
                if (enclosing && target == enclosing->exitTarget)
                {
                    result.push_back(Stmt::mkBreak());
                    ++i;
                    continue;
                }
                if (enclosing && target == enclosing->continueTarget)
                {
                    result.push_back(Stmt::mkContinue());
                    ++i;
                    continue;
                }
                // Unmatched jump (dead code, or a pattern this structurizer
                // doesn't model) -- degrade gracefully instead of guessing.
                result.push_back(Stmt::mkComment("unresolved jump to instruction " + std::to_string(target)));
                ++i;
                continue;
            }

            if (isConditionalJump(insn.op))
            {
                drain();
                i = handleIfElse(i, hi, enclosing, result);
                continue;
            }

            // Ordinary straight-line instruction.
            uint32_t nextPc = (i + 1 < static_cast<int>(proto_.instructions.size())) ? proto_.instructions[i + 1].pc : proto_.totalWordCount;
            tracker_.step(insn, nextPc);
            ++i;
        }

        drain();
        return result;
    }

    // Handles a conditional jump at index J as if/elseif/else, returning
    // the index to resume scanning from.
    int handleIfElse(int J, int hi, const LoopCtx* enclosing, std::vector<StmtPtr>& result)
    {
        const Instruction& jinsn = proto_.instructions[J];
        int32_t T = jinsn.jumpTarget;

        int32_t elseSkipTarget = -1;
        bool hasElse = false;
        if (T - 1 >= J + 1 && T - 1 < hi && proto_.instructions[T - 1].op == Op::JUMP)
        {
            int32_t t2 = proto_.instructions[T - 1].jumpTarget;
            bool looksLikeBreakOrContinue = enclosing && (t2 == enclosing->exitTarget || t2 == enclosing->continueTarget);
            if (t2 > T && !looksLikeBreakOrContinue)
            {
                hasElse = true;
                elseSkipTarget = t2;
            }
        }
        int32_t resumeAt = hasElse ? elseSkipTarget : T;
        uint32_t untilPc = (resumeAt < static_cast<int32_t>(proto_.instructions.size())) ? proto_.instructions[resumeAt].pc : proto_.totalWordCount;

        pinConditionRegs(jinsn, jinsn.pc, untilPc, result, /*pinAtomsToo=*/false);
        ExprPtr cond = buildCondition(jinsn);

        std::vector<StmtPtr> thenBody = structureRange(J + 1, hasElse ? (T - 1) : T, enclosing);
        std::vector<StmtPtr> elseBody;
        if (hasElse)
            elseBody = structureRange(T, elseSkipTarget, enclosing);

        result.push_back(Stmt::mkIf(cond, std::move(thenBody), std::move(elseBody)));
        return resumeAt;
    }

    int handleNumericFor(int P, std::vector<StmtPtr>& result)
    {
        const Instruction& prep = proto_.instructions[P];
        int32_t E = prep.jumpTarget;
        int fornLoopIdx = static_cast<int>(E) - 1;
        bool wellFormed = fornLoopIdx > P && fornLoopIdx < static_cast<int>(proto_.instructions.size()) &&
                           proto_.instructions[fornLoopIdx].op == Op::FORNLOOP;
        if (!wellFormed)
        {
            // Fall back: treat as an opaque region rather than crash.
            result.push_back(Stmt::mkComment("unrecognized FORNPREP shape at instruction " + std::to_string(P)));
            return P + 1;
        }

        ExprPtr startExpr = tracker_.regValue(static_cast<uint8_t>(prep.a + 2));
        ExprPtr limitExpr = tracker_.regValue(prep.a);
        ExprPtr stepExpr = tracker_.regValue(static_cast<uint8_t>(prep.a + 1));
        bool stepIsDefaultOne = stepExpr->kind == EK::Number && ((stepExpr->isInt && stepExpr->intValue == 1) || (!stepExpr->isInt && stepExpr->number == 1.0));

        uint32_t bodyStartPc = (P + 1 < static_cast<int>(proto_.instructions.size())) ? proto_.instructions[P + 1].pc : proto_.totalWordCount;
        auto nameOpt = tracker_.localNameAt(static_cast<uint8_t>(prep.a + 2), bodyStartPc);
        std::string loopVar = nameOpt.value_or("i");
        tracker_.bindLocal(static_cast<uint8_t>(prep.a + 2), loopVar);

        LoopCtx ctx{E, fornLoopIdx};
        std::vector<StmtPtr> body = structureRange(P + 1, fornLoopIdx, &ctx);

        StmtPtr s = std::make_shared<Stmt>();
        s->kind = SK::NumericFor;
        s->loopVar = loopVar;
        s->forStart = startExpr;
        s->forLimit = limitExpr;
        s->forStep = stepIsDefaultOne ? nullptr : stepExpr;
        s->body = std::move(body);
        result.push_back(s);
        return E;
    }

    int handleGenericFor(int P, std::vector<StmtPtr>& result)
    {
        const Instruction& prep = proto_.instructions[P];
        int loopIdx = prep.jumpTarget;
        bool wellFormed = loopIdx > P && loopIdx < static_cast<int>(proto_.instructions.size()) && proto_.instructions[loopIdx].op == Op::FORGLOOP;
        if (!wellFormed)
        {
            result.push_back(Stmt::mkComment("unrecognized generic-for prep shape at instruction " + std::to_string(P)));
            return P + 1;
        }
        const Instruction& loopInsn = proto_.instructions[loopIdx];
        int32_t bodyStart = loopInsn.jumpTarget;
        int32_t E = loopIdx + 1;
        uint32_t nvars = auxA(loopInsn.aux);
        if (nvars == 0)
            nvars = 1;

        ExprPtr genExpr = tracker_.regValue(prep.a);
        ExprPtr stateExpr = tracker_.regValue(static_cast<uint8_t>(prep.a + 1));
        ExprPtr ctrlExpr = tracker_.regValue(static_cast<uint8_t>(prep.a + 2));

        // Peephole: if gen/state/ctrl were literally just materialized by
        // an immediately preceding `local g, s, c = expr()` statement,
        // fold back into `for ... in expr() do` instead of `for ... in
        // g, s, c do`.
        if (!result.empty() && result.back()->kind == SK::Local && result.back()->targets.size() >= 1 &&
            result.back()->targets.size() <= 3 && result.back()->values.size() == 1)
        {
            const std::vector<ExprPtr>& tgt = result.back()->targets;
            bool matches = tgt.size() >= 1 && genExpr->kind == EK::Local && tgt[0]->kind == EK::Local && tgt[0]->str == genExpr->str;
            if (matches && tgt.size() >= 2)
                matches = stateExpr->kind == EK::Local && tgt[1]->str == stateExpr->str;
            if (matches && tgt.size() >= 3)
                matches = ctrlExpr->kind == EK::Local && tgt[2]->str == ctrlExpr->str;
            if (matches)
            {
                ExprPtr call = result.back()->values[0];
                result.pop_back();
                genExpr = call;
                stateExpr = nullptr;
                ctrlExpr = nullptr;
            }
        }

        std::vector<std::string> loopVars;
        uint32_t bodyStartPc = proto_.instructions[bodyStart].pc;
        for (uint32_t k = 0; k < nvars; ++k)
        {
            uint8_t reg = static_cast<uint8_t>(prep.a + 3 + k);
            std::string name = tracker_.localNameAt(reg, bodyStartPc).value_or("v" + std::to_string(k));
            loopVars.push_back(name);
            tracker_.bindLocal(reg, name);
        }

        LoopCtx ctx{E, loopIdx};
        std::vector<StmtPtr> body = structureRange(bodyStart, loopIdx, &ctx);

        StmtPtr s = std::make_shared<Stmt>();
        s->kind = SK::GenericFor;
        s->loopVars = loopVars;
        s->forExprs.push_back(genExpr);
        if (stateExpr)
            s->forExprs.push_back(stateExpr);
        if (ctrlExpr)
            s->forExprs.push_back(ctrlExpr);
        s->body = std::move(body);
        result.push_back(s);
        return E;
    }

    int handleWhileOrRepeat(int H, int BJ, std::vector<StmtPtr>& result)
    {
        // Recursive structureRange calls below may start exactly at H
        // (the while-condition setup range, or the repeat body range) --
        // erase the map entry first so those calls don't immediately
        // re-detect this same loop and recurse forever.
        whileRepeatMap_.erase(H);

        // Scan forward from H through straight-line instructions looking
        // for an early conditional exit (a `while` loop's condition
        // check). If we hit anything else first (another branch, or reach
        // BJ itself), treat it as `repeat ... until`.
        int scan = H;
        while (scan < BJ && !isConditionalJump(proto_.instructions[scan].op) && !isFastCall(proto_.instructions[scan].op) &&
               proto_.instructions[scan].op != Op::JUMP && proto_.instructions[scan].op != Op::JUMPBACK &&
               proto_.instructions[scan].op != Op::FORNPREP && !isForPrep(proto_.instructions[scan].op) &&
               !(proto_.instructions[scan].op == Op::LOADB && proto_.instructions[scan].c != 0))
        {
            ++scan;
        }

        bool isWhile = scan < BJ && isConditionalJump(proto_.instructions[scan].op) && proto_.instructions[scan].jumpTarget > BJ;

        if (isWhile)
        {
            // Setup instructions [H, scan) are pure condition computation;
            // process them (should yield no statements) then pin+build cond.
            std::vector<StmtPtr> setupStmts = structureRange(H, scan, nullptr);
            for (StmtPtr& s : setupStmts)
                result.push_back(std::move(s)); // rare: keep correctness if setup wasn't pure

            uint32_t exitPc = (BJ + 1 < static_cast<int>(proto_.instructions.size())) ? proto_.instructions[BJ + 1].pc : proto_.totalWordCount;
            pinConditionRegs(proto_.instructions[scan], proto_.instructions[scan].pc, exitPc, result, /*pinAtomsToo=*/true);
            ExprPtr cond = buildCondition(proto_.instructions[scan]);
            LoopCtx ctx{BJ + 1, H};
            std::vector<StmtPtr> body = structureRange(scan + 1, BJ, &ctx);

            StmtPtr s = std::make_shared<Stmt>();
            s->kind = SK::While;
            s->cond = cond;
            s->body = std::move(body);
            result.push_back(s);
            return BJ + 1;
        }
        else
        {
            // repeat ... until: body is [H, BJ-1) if the instruction right
            // before BJ is the until-condition; otherwise fall back to
            // treating the whole thing as the body of an infinite loop
            // (while true do ... end) -- still correct, just not
            // reconstructing the exact until-condition.
            int condIdx = BJ - 1;
            if (condIdx > H && isConditionalJump(proto_.instructions[condIdx].op) && proto_.instructions[condIdx].jumpTarget > BJ)
            {
                int32_t exitIdx = proto_.instructions[condIdx].jumpTarget;
                uint32_t exitPc = (exitIdx < static_cast<int32_t>(proto_.instructions.size())) ? proto_.instructions[exitIdx].pc : proto_.totalWordCount;
                // Pin from the loop's start (not the condition's own pc):
                // the condition's operand registers are typically written
                // *inside* the body (e.g. `until j >= 5` after `j = j +
                // 1`), which precedes the condition check in program
                // order for repeat-until.
                pinConditionRegs(proto_.instructions[condIdx], proto_.instructions[H].pc, exitPc, result, /*pinAtomsToo=*/true);

                LoopCtx ctx{exitIdx, H};
                std::vector<StmtPtr> body = structureRange(H, condIdx, &ctx);
                ExprPtr cond = buildCondition(proto_.instructions[condIdx], /*fallthroughIsTrue=*/false);

                StmtPtr s = std::make_shared<Stmt>();
                s->kind = SK::Repeat;
                s->body = std::move(body);
                s->cond = cond;
                result.push_back(s);
                return exitIdx;
            }
            else
            {
                LoopCtx ctx{BJ + 1, H};
                std::vector<StmtPtr> body = structureRange(H, BJ, &ctx);
                StmtPtr s = std::make_shared<Stmt>();
                s->kind = SK::While;
                s->cond = Expr::mkBool(true);
                s->body = std::move(body);
                result.push_back(s);
                return BJ + 1;
            }
        }
    }
};

std::vector<StmtPtr> structureFunction(const Module& module, const Proto& proto)
{
    Structurizer s(module, proto);
    return s.run();
}

} // namespace luaudec
