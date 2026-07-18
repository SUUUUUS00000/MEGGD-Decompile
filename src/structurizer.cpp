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

} // namespace

class Structurizer
{
public:
    Structurizer(const Module& module, const Proto& proto) : module_(module), proto_(proto), tracker_(module, proto)
    {
        buildWhileRepeatMap();
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

    // Builds the *normalized* condition for a conditional-jump instruction,
    // such that "fallthrough" always means "condition true" (negating or
    // flipping the comparison as needed per-opcode -- see structurizer.cpp
    // commit message / design notes for the polarity table this follows).
    ExprPtr buildCondition(const Instruction& insn)
    {
        switch (insn.op)
        {
        case Op::JUMPIF:
            return Expr::mkUnary(UnOpKind::Not, tracker_.regValue(insn.a));
        case Op::JUMPIFNOT:
            return tracker_.regValue(insn.a);
        case Op::JUMPIFEQ:
            return Expr::mkBinary(BinOpKind::Ne, tracker_.regValue(insn.a), tracker_.regValue(auxA(insn.aux)));
        case Op::JUMPIFLE:
            return Expr::mkBinary(BinOpKind::Gt, tracker_.regValue(insn.a), tracker_.regValue(auxA(insn.aux)));
        case Op::JUMPIFLT:
            return Expr::mkBinary(BinOpKind::Ge, tracker_.regValue(insn.a), tracker_.regValue(auxA(insn.aux)));
        case Op::JUMPIFNOTEQ:
            return Expr::mkBinary(BinOpKind::Eq, tracker_.regValue(insn.a), tracker_.regValue(auxA(insn.aux)));
        case Op::JUMPIFNOTLE:
            return Expr::mkBinary(BinOpKind::Le, tracker_.regValue(insn.a), tracker_.regValue(auxA(insn.aux)));
        case Op::JUMPIFNOTLT:
            return Expr::mkBinary(BinOpKind::Lt, tracker_.regValue(insn.a), tracker_.regValue(auxA(insn.aux)));
        case Op::JUMPXEQKNIL:
            return auxNot(insn.aux) ? Expr::mkBinary(BinOpKind::Eq, tracker_.regValue(insn.a), Expr::mkNil())
                                     : Expr::mkBinary(BinOpKind::Ne, tracker_.regValue(insn.a), Expr::mkNil());
        case Op::JUMPXEQKB:
        {
            ExprPtr b = Expr::mkBool(auxKB(insn.aux) != 0);
            return auxNot(insn.aux) ? Expr::mkBinary(BinOpKind::Eq, tracker_.regValue(insn.a), b)
                                     : Expr::mkBinary(BinOpKind::Ne, tracker_.regValue(insn.a), b);
        }
        case Op::JUMPXEQKN:
        case Op::JUMPXEQKS:
        {
            ExprPtr k = tracker_.constantExpr(auxKV(insn.aux));
            return auxNot(insn.aux) ? Expr::mkBinary(BinOpKind::Eq, tracker_.regValue(insn.a), k)
                                     : Expr::mkBinary(BinOpKind::Ne, tracker_.regValue(insn.a), k);
        }
        case Op::CMPPROTO:
        default:
            return Expr::mkRaw("--[[ unsupported condition (" + std::string(opInfo(static_cast<uint8_t>(insn.op)).name) + ") ]] true");
        }
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
        ExprPtr cond = buildCondition(jinsn);
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

        std::vector<StmtPtr> thenBody = structureRange(J + 1, hasElse ? (T - 1) : T, enclosing);
        std::vector<StmtPtr> elseBody;
        int32_t resumeAt = T;
        if (hasElse)
        {
            elseBody = structureRange(T, elseSkipTarget, enclosing);
            resumeAt = elseSkipTarget;
        }

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
            // process them (should yield no statements) then build cond.
            std::vector<StmtPtr> setupStmts = structureRange(H, scan, nullptr);
            for (StmtPtr& s : setupStmts)
                result.push_back(std::move(s)); // rare: keep correctness if setup wasn't pure

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
                LoopCtx ctx{static_cast<int32_t>(proto_.instructions[condIdx].jumpTarget), H};
                std::vector<StmtPtr> body = structureRange(H, condIdx, &ctx);
                ExprPtr cond = buildCondition(proto_.instructions[condIdx]);

                StmtPtr s = std::make_shared<Stmt>();
                s->kind = SK::Repeat;
                s->body = std::move(body);
                s->cond = cond;
                result.push_back(s);
                return static_cast<int>(proto_.instructions[condIdx].jumpTarget);
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
