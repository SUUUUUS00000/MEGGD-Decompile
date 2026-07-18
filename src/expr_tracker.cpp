// expr_tracker.cpp
#include "expr_tracker.hpp"

#include <algorithm>

namespace luaudec
{

namespace
{
bool looksLikeIdentifier(const std::string& s)
{
    if (s.empty() || (!std::isalpha((unsigned char)s[0]) && s[0] != '_'))
        return false;
    for (char c : s)
        if (!std::isalnum((unsigned char)c) && c != '_')
            return false;
    return true;
}
} // namespace

std::string ExprTracker::constantNameOnly(uint32_t idx) const
{
    if (idx < proto_.constants.size() && proto_.constants[idx].tag == ConstTag::String && proto_.constants[idx].stringIndex)
        return module_.str(*proto_.constants[idx].stringIndex);
    return "?";
}

ExprTracker::ExprTracker(const Module& module, const Proto& proto) : module_(module), proto_(proto)
{
    size_t n = std::max<size_t>(proto.maxStackSize, 1) + 8; // small margin
    regExpr_.assign(n, nullptr);
    regIsAtom_.assign(n, false);
    declaredName_.assign(n, std::nullopt);
    tableAccum_.assign(n, std::nullopt);
    nameHint_.assign(n, std::nullopt);
}

static int findLocalIndex(const Proto& proto, uint8_t r, uint32_t pc)
{
    for (size_t i = 0; i < proto.locals.size(); ++i)
    {
        const LocalVarInfo& lv = proto.locals[i];
        if (lv.reg == r && lv.startpc <= pc && pc < lv.endpc)
            return static_cast<int>(i);
    }
    return -1;
}

std::optional<std::string> ExprTracker::localNameAt(uint8_t r, uint32_t pc) const
{
    return activeLocalName(r, pc);
}

std::optional<std::string> ExprTracker::activeLocalName(uint8_t r, uint32_t pc) const
{
    for (const SyntheticLocal& sl : syntheticLocals_)
        if (sl.reg == r && sl.startpc <= pc && pc < sl.endpc)
            return sl.name;
    int idx = findLocalIndex(proto_, r, pc);
    if (idx >= 0 && proto_.locals[idx].nameString)
        return module_.str(*proto_.locals[idx].nameString);
    return std::nullopt;
}

std::string ExprTracker::freshSyntheticName() { return freshName(); }

bool ExprTracker::isCompoundPending(uint8_t r) const
{
    return r < regExpr_.size() && regExpr_[r] != nullptr && !regIsAtom_[r];
}

void ExprTracker::pinAsVariable(uint8_t r, uint32_t fromPc, uint32_t toPc, const std::string& name)
{
    if (r < regExpr_.size() && regExpr_[r] && regExpr_[r]->kind == EK::Local && regExpr_[r]->str == name &&
        r < declaredName_.size() && declaredName_[r] && *declaredName_[r] == name)
    {
        return; // already pinned to this exact name
    }
    ExprPtr current = regValue(r); // consumes if compound; safe re-read if already an atom/Local
    stmts_.push_back(Stmt::mkLocal({Expr::mkLocal(name)}, {current}));
    syntheticLocals_.push_back(SyntheticLocal{r, fromPc, toPc, name});
    if (r < declaredName_.size())
        declaredName_[r] = name;
    setReg(r, Expr::mkLocal(name), true);
}

std::string ExprTracker::freshName()
{
    return "v" + std::to_string(freshCounter_++);
}

ExprPtr ExprTracker::constantExpr(uint32_t idx) const
{
    if (idx >= proto_.constants.size())
        return Expr::mkRaw("<bad const>");
    const Constant& c = proto_.constants[idx];
    switch (c.tag)
    {
    case ConstTag::Nil:
        return Expr::mkNil();
    case ConstTag::Boolean:
        return Expr::mkBool(c.boolValue);
    case ConstTag::Number:
        return Expr::mkNumber(c.numberValue);
    case ConstTag::Integer:
        return Expr::mkInt(c.integerValue);
    case ConstTag::String:
        return Expr::mkStr(c.stringIndex ? module_.str(*c.stringIndex) : std::string());
    case ConstTag::Vector:
    {
        std::vector<ExprPtr> args{Expr::mkNumber(c.vecValue[0]), Expr::mkNumber(c.vecValue[1]), Expr::mkNumber(c.vecValue[2])};
        if (c.vecValue[3] != 0.0f)
            args.push_back(Expr::mkNumber(c.vecValue[3]));
        return Expr::mkCall(Expr::mkGlobal("vector"), std::move(args));
    }
    case ConstTag::Import:
    {
        uint32_t count = c.importId >> 30;
        uint32_t ids[3] = {(c.importId >> 20) & 0x3ff, (c.importId >> 10) & 0x3ff, c.importId & 0x3ff};
        ExprPtr e;
        for (uint32_t i = 0; i < count && i < 3; ++i)
        {
            std::string seg = "?";
            if (ids[i] < proto_.constants.size() && proto_.constants[ids[i]].tag == ConstTag::String && proto_.constants[ids[i]].stringIndex)
                seg = module_.str(*proto_.constants[ids[i]].stringIndex);
            e = e ? Expr::mkIndex(e, Expr::mkStr(seg), true) : Expr::mkGlobal(seg);
        }
        return e ? e : Expr::mkRaw("<bad import>");
    }
    case ConstTag::Table:
    case ConstTag::TableWithConstants:
    {
        std::vector<TableItem> items;
        for (const TableShapeEntry& entry : c.tableShape)
        {
            TableItem item;
            if (entry.keyConstant >= 0)
            {
                ExprPtr keyExpr = constantExpr(static_cast<uint32_t>(entry.keyConstant));
                if (keyExpr->kind == EK::Str && looksLikeIdentifier(keyExpr->str))
                {
                    item.dotStyle = true;
                    item.key = keyExpr;
                }
                else
                {
                    item.key = keyExpr;
                }
            }
            item.value = entry.valueConstant >= 0 ? constantExpr(static_cast<uint32_t>(entry.valueConstant)) : Expr::mkNil();
            items.push_back(std::move(item));
        }
        return Expr::mkTable(std::move(items));
    }
    case ConstTag::Closure:
        return Expr::mkFunction(c.closureProto);
    case ConstTag::ClassShape:
        return Expr::mkRaw("--[[ class shape constant, unsupported ]]");
    }
    return Expr::mkRaw("<?>");
}

void ExprTracker::produceValue(uint8_t r, ExprPtr value, bool isAtom, uint32_t nextPc)
{
    if (activeLocalName(r, nextPc).has_value())
    {
        emitNamedWrite(r, nextPc, std::move(value));
        return;
    }
    setReg(r, std::move(value), isAtom);
}

void ExprTracker::bindLocal(uint8_t r, const std::string& name)
{
    setReg(r, Expr::mkLocal(name), true);
}

void ExprTracker::bindParam(uint8_t r, const std::string& name)
{
    setReg(r, Expr::mkLocal(name), true);
    syntheticLocals_.push_back(SyntheticLocal{r, 0, proto_.totalWordCount, name});
    if (r < declaredName_.size())
        declaredName_[r] = name;
}

void ExprTracker::setReg(uint8_t r, ExprPtr e, bool isAtom)
{
    if (r >= regExpr_.size())
        return;
    regExpr_[r] = std::move(e);
    regIsAtom_[r] = isAtom;
    tableAccum_[r] = std::nullopt;
    nameHint_[r] = std::nullopt;
}

void ExprTracker::clearReg(uint8_t r)
{
    if (r >= regExpr_.size())
        return;
    regExpr_[r] = nullptr;
    regIsAtom_[r] = false;
    tableAccum_[r] = std::nullopt;
    nameHint_[r] = std::nullopt;
}

ExprPtr ExprTracker::peekRegValue(uint8_t r) const
{
    if (r < regExpr_.size() && regExpr_[r])
        return regExpr_[r];
    return Expr::mkReg(r);
}

ExprPtr ExprTracker::regValue(uint8_t r)
{
    if (r < regExpr_.size() && regExpr_[r])
    {
        ExprPtr v = regExpr_[r];
        if (!regIsAtom_[r])
        {
            if (r < nameHint_.size() && nameHint_[r])
            {
                std::string name = *nameHint_[r];
                stmts_.push_back(Stmt::mkLocal({Expr::mkLocal(name)}, {v}));
                clearReg(r);
                return Expr::mkLocal(name);
            }
            clearReg(r); // compound: single-use, prevent accidental duplication downstream
        }
        return v;
    }
    return Expr::mkReg(r);
}

void ExprTracker::emitNamedWrite(uint8_t r, uint32_t nextPc, ExprPtr value)
{
    std::string name = activeLocalName(r, nextPc).value_or(freshName());
    bool alreadyDeclared = (r < declaredName_.size() && declaredName_[r] && *declaredName_[r] == name);
    if (alreadyDeclared)
    {
        stmts_.push_back(Stmt::mkAssign({Expr::mkLocal(name)}, {value}));
    }
    else
    {
        stmts_.push_back(Stmt::mkLocal({Expr::mkLocal(name)}, {value}));
        if (r < declaredName_.size())
            declaredName_[r] = name;
    }
    setReg(r, Expr::mkLocal(name), true);
}

void ExprTracker::step(const Instruction& insn, uint32_t nextPc)
{
    auto& M = module_;
    (void)M;

    switch (insn.op)
    {
    // ---- pure atoms ----
    case Op::LOADNIL:
        produceValue(insn.a, Expr::mkNil(), true, nextPc);
        break;
    case Op::LOADB:
        // Note: if insn.c != 0 this instruction *also* acts as a jump
        // (skip the next instruction); the structurizer intercepts that
        // case before it ever reaches step(). Here we only handle the
        // plain "set A to a boolean" effect.
        produceValue(insn.a, Expr::mkBool(insn.b != 0), true, nextPc);
        break;
    case Op::LOADN:
        produceValue(insn.a, Expr::mkInt(insn.d), true, nextPc);
        break;
    case Op::LOADK:
        produceValue(insn.a, constantExpr(static_cast<uint32_t>(insn.d)), true, nextPc);
        break;
    case Op::LOADKX:
        produceValue(insn.a, constantExpr(insn.aux), true, nextPc);
        break;
    case Op::GETUPVAL:
    {
        std::string name = "up" + std::to_string(insn.b);
        if (insn.b < proto_.upvalNames.size() && proto_.upvalNames[insn.b].nameString)
            name = module_.str(*proto_.upvalNames[insn.b].nameString);
        produceValue(insn.a, Expr::mkUpvalue(name), true, nextPc);
        break;
    }
    case Op::GETIMPORT:
    {
        uint32_t count = insn.aux >> 30;
        uint32_t ids[3] = {(insn.aux >> 20) & 0x3ff, (insn.aux >> 10) & 0x3ff, insn.aux & 0x3ff};
        ExprPtr e;
        for (uint32_t i = 0; i < count && i < 3; ++i)
        {
            std::string seg = "?";
            if (ids[i] < proto_.constants.size() && proto_.constants[ids[i]].tag == ConstTag::String && proto_.constants[ids[i]].stringIndex)
                seg = module_.str(*proto_.constants[ids[i]].stringIndex);
            e = e ? Expr::mkIndex(e, Expr::mkStr(seg), true) : Expr::mkGlobal(seg);
        }
        produceValue(insn.a, e ? e : Expr::mkRaw("<bad import>"), true, nextPc);
        break;
    }
    case Op::MOVE:
    {
        bool atom = insn.b < regIsAtom_.size() ? regIsAtom_[insn.b] : true;
        ExprPtr v = peekRegValue(insn.b);
        if (!atom)
            clearReg(insn.b); // ownership transfers to `a`
        produceValue(insn.a, v, atom, nextPc);
        break;
    }
    case Op::GETVARARGS:
    {
        uint32_t n = insn.b; // countField convention: 0 => multret
        if (n == 0)
        {
            produceValue(insn.a, Expr::mkVararg(), true, nextPc);
        }
        else if (n == 2) // 1 actual value (n=count+1)
        {
            produceValue(insn.a, Expr::mkVararg(), true, nextPc);
        }
        else
        {
            uint32_t count = n - 1;
            std::vector<ExprPtr> names;
            for (uint32_t i = 0; i < count; ++i)
            {
                uint8_t reg = static_cast<uint8_t>(insn.a + i);
                std::string nm = activeLocalName(reg, nextPc).value_or(freshName());
                names.push_back(Expr::mkLocal(nm));
                setReg(reg, Expr::mkLocal(nm), true);
                if (reg < declaredName_.size())
                    declaredName_[reg] = nm;
            }
            stmts_.push_back(Stmt::mkLocal(names, {Expr::mkVararg()}));
        }
        break;
    }

    // ---- compound (single-use) pure expressions ----
    case Op::GETGLOBAL:
        produceValue(insn.a, Expr::mkGlobal(constantNameOnly(insn.aux)), false, nextPc);
        break;
    case Op::GETTABLE:
        produceValue(insn.a, Expr::mkIndex(regValue(insn.b), regValue(insn.c), false), false, nextPc);
        break;
    case Op::GETTABLEN:
        produceValue(insn.a, Expr::mkIndex(regValue(insn.b), Expr::mkInt(insn.c + 1), false), false, nextPc);
        break;
    case Op::GETTABLEKS:
    case Op::GETUDATAKS:
    {
        uint32_t kidx = auxKV(insn.aux);
        ExprPtr key = constantExpr(kidx);
        bool dot = key->kind == EK::Str && looksLikeIdentifier(key->str);
        produceValue(insn.a, Expr::mkIndex(regValue(insn.b), key, dot), false, nextPc);
        break;
    }
    case Op::ADD: produceValue(insn.a, Expr::mkBinary(BinOpKind::Add, regValue(insn.b), regValue(insn.c)), false, nextPc); break;
    case Op::SUB: produceValue(insn.a, Expr::mkBinary(BinOpKind::Sub, regValue(insn.b), regValue(insn.c)), false, nextPc); break;
    case Op::MUL: produceValue(insn.a, Expr::mkBinary(BinOpKind::Mul, regValue(insn.b), regValue(insn.c)), false, nextPc); break;
    case Op::DIV: produceValue(insn.a, Expr::mkBinary(BinOpKind::Div, regValue(insn.b), regValue(insn.c)), false, nextPc); break;
    case Op::MOD: produceValue(insn.a, Expr::mkBinary(BinOpKind::Mod, regValue(insn.b), regValue(insn.c)), false, nextPc); break;
    case Op::POW: produceValue(insn.a, Expr::mkBinary(BinOpKind::Pow, regValue(insn.b), regValue(insn.c)), false, nextPc); break;
    case Op::IDIV: produceValue(insn.a, Expr::mkBinary(BinOpKind::IDiv, regValue(insn.b), regValue(insn.c)), false, nextPc); break;
    case Op::AND: produceValue(insn.a, Expr::mkBinary(BinOpKind::And, regValue(insn.b), regValue(insn.c)), false, nextPc); break;
    case Op::OR: produceValue(insn.a, Expr::mkBinary(BinOpKind::Or, regValue(insn.b), regValue(insn.c)), false, nextPc); break;

    case Op::ADDK: produceValue(insn.a, Expr::mkBinary(BinOpKind::Add, regValue(insn.b), constantExpr(insn.c)), false, nextPc); break;
    case Op::SUBK: produceValue(insn.a, Expr::mkBinary(BinOpKind::Sub, regValue(insn.b), constantExpr(insn.c)), false, nextPc); break;
    case Op::MULK: produceValue(insn.a, Expr::mkBinary(BinOpKind::Mul, regValue(insn.b), constantExpr(insn.c)), false, nextPc); break;
    case Op::DIVK: produceValue(insn.a, Expr::mkBinary(BinOpKind::Div, regValue(insn.b), constantExpr(insn.c)), false, nextPc); break;
    case Op::MODK: produceValue(insn.a, Expr::mkBinary(BinOpKind::Mod, regValue(insn.b), constantExpr(insn.c)), false, nextPc); break;
    case Op::POWK: produceValue(insn.a, Expr::mkBinary(BinOpKind::Pow, regValue(insn.b), constantExpr(insn.c)), false, nextPc); break;
    case Op::IDIVK: produceValue(insn.a, Expr::mkBinary(BinOpKind::IDiv, regValue(insn.b), constantExpr(insn.c)), false, nextPc); break;
    case Op::ANDK: produceValue(insn.a, Expr::mkBinary(BinOpKind::And, regValue(insn.b), constantExpr(insn.c)), false, nextPc); break;
    case Op::ORK: produceValue(insn.a, Expr::mkBinary(BinOpKind::Or, regValue(insn.b), constantExpr(insn.c)), false, nextPc); break;
    case Op::SUBRK: produceValue(insn.a, Expr::mkBinary(BinOpKind::Sub, constantExpr(insn.b), regValue(insn.c)), false, nextPc); break;
    case Op::DIVRK: produceValue(insn.a, Expr::mkBinary(BinOpKind::Div, constantExpr(insn.b), regValue(insn.c)), false, nextPc); break;

    case Op::CONCAT:
    {
        ExprPtr chain = regValue(insn.b);
        for (int r = insn.b + 1; r <= insn.c; ++r)
            chain = Expr::mkBinary(BinOpKind::Concat, chain, regValue(static_cast<uint8_t>(r)));
        produceValue(insn.a, chain, false, nextPc);
        break;
    }
    case Op::NOT: produceValue(insn.a, Expr::mkUnary(UnOpKind::Not, regValue(insn.b)), false, nextPc); break;
    case Op::MINUS: produceValue(insn.a, Expr::mkUnary(UnOpKind::Neg, regValue(insn.b)), false, nextPc); break;
    case Op::LENGTH: produceValue(insn.a, Expr::mkUnary(UnOpKind::Len, regValue(insn.b)), false, nextPc); break;

    // ---- table constructors ----
    case Op::NEWTABLE:
    {
        ExprPtr t = Expr::mkTable({});
        setReg(insn.a, t, false);
        tableAccum_[insn.a] = TableAccum{t, 1};
        {
            auto name = activeLocalName(insn.a, nextPc);
            if (name)
                nameHint_[insn.a] = *name;
        }
        break;
    }
    case Op::DUPTABLE:
    {
        ExprPtr t = constantExpr(static_cast<uint32_t>(insn.d));
        setReg(insn.a, t, false);
        tableAccum_[insn.a] = TableAccum{t, 1};
        {
            auto name = activeLocalName(insn.a, nextPc);
            if (name)
                nameHint_[insn.a] = *name;
        }
        break;
    }
    case Op::SETLIST:
    {
        uint32_t count = insn.c == 0 ? 0 : (insn.c - 1);
        if (count == 0 && insn.c == 0)
        {
            // MULTRET: scan consecutive live registers from B.
            uint8_t r = insn.b;
            while (r < regExpr_.size() && regExpr_[r])
                ++r;
            count = r - insn.b;
        }
        if (insn.a < tableAccum_.size() && tableAccum_[insn.a])
        {
            for (uint32_t i = 0; i < count; ++i)
            {
                TableItem item;
                item.value = regValue(static_cast<uint8_t>(insn.b + i));
                tableAccum_[insn.a]->expr->items.push_back(std::move(item));
            }
        }
        break;
    }

    default:
        // NOP/BREAK(debugger)/COVERAGE/CAPTURE/FASTCALL*/JUMPX/NATIVECALL
        // and other structural/no-textual-effect opcodes: nothing to do.
        // FASTCALL* in particular is skipped entirely by the structurizer
        // (never reaches step()) so its guaranteed fallback GETIMPORT/MOVE/
        // GETUPVAL + CALL sequence is processed as ordinary instructions.
        break;

    case Op::SETTABLE:
    {
        ExprPtr value = regValue(insn.a);
        if (insn.b < tableAccum_.size() && tableAccum_[insn.b])
        {
            TableItem item;
            item.key = regValue(insn.c);
            item.value = value;
            tableAccum_[insn.b]->expr->items.push_back(std::move(item));
        }
        else
        {
            stmts_.push_back(Stmt::mkAssign({Expr::mkIndex(regValue(insn.b), regValue(insn.c), false)}, {value}));
        }
        break;
    }
    case Op::SETTABLEN:
    {
        ExprPtr value = regValue(insn.a);
        if (insn.b < tableAccum_.size() && tableAccum_[insn.b])
        {
            TableItem item;
            item.key = Expr::mkInt(insn.c + 1);
            item.value = value;
            tableAccum_[insn.b]->expr->items.push_back(std::move(item));
        }
        else
        {
            stmts_.push_back(Stmt::mkAssign({Expr::mkIndex(regValue(insn.b), Expr::mkInt(insn.c + 1), false)}, {value}));
        }
        break;
    }
    case Op::SETTABLEKS:
    case Op::SETUDATAKS:
    {
        ExprPtr value = regValue(insn.a);
        uint32_t kidx = auxKV(insn.aux);
        ExprPtr key = constantExpr(kidx);
        bool dot = key->kind == EK::Str && looksLikeIdentifier(key->str);
        if (insn.b < tableAccum_.size() && tableAccum_[insn.b])
        {
            TableItem item;
            item.dotStyle = dot;
            item.key = key;
            item.value = value;
            tableAccum_[insn.b]->expr->items.push_back(std::move(item));
        }
        else
        {
            stmts_.push_back(Stmt::mkAssign({Expr::mkIndex(regValue(insn.b), key, dot)}, {value}));
        }
        break;
    }
    case Op::SETGLOBAL:
        stmts_.push_back(Stmt::mkAssign({Expr::mkGlobal(constantNameOnly(insn.aux))}, {regValue(insn.a)}));
        break;
    case Op::SETUPVAL:
    {
        std::string name = "up" + std::to_string(insn.b);
        if (insn.b < proto_.upvalNames.size() && proto_.upvalNames[insn.b].nameString)
            name = module_.str(*proto_.upvalNames[insn.b].nameString);
        stmts_.push_back(Stmt::mkAssign({Expr::mkUpvalue(name)}, {regValue(insn.a)}));
        break;
    }
    case Op::CLOSEUPVALS:
        // Scope-exit bookkeeping; no source-level representation needed.
        break;

    case Op::NEWCLOSURE:
    {
        uint32_t globalIdx = (insn.d >= 0 && static_cast<size_t>(insn.d) < proto_.childProtoIds.size()) ? proto_.childProtoIds[insn.d] : 0;
        // Always materialize (even without a debug name) rather than
        // defer: an inlined anonymous function literal at its own call
        // site (`(function() ... end)(...)`) is technically correct but
        // reads nothing like the original `local f = function() ... end;
        // f(...)` or `local function f() ... end`.
        emitNamedWrite(insn.a, nextPc, Expr::mkFunction(globalIdx));
        break;
    }
    case Op::DUPCLOSURE:
    {
        uint32_t globalIdx = 0;
        uint32_t cidx = static_cast<uint32_t>(insn.d);
        if (cidx < proto_.constants.size() && proto_.constants[cidx].tag == ConstTag::Closure)
            globalIdx = proto_.constants[cidx].closureProto;
        emitNamedWrite(insn.a, nextPc, Expr::mkFunction(globalIdx));
        break;
    }

    case Op::NAMECALL:
    {
        uint32_t kidx = auxKV(insn.aux);
        pendingNamecall_ = PendingNamecall{insn.a, regValue(insn.b), constantNameOnly(kidx)};
        break;
    }

    case Op::CALL:
    {
        ExprPtr call = buildCallExpr(insn);
        uint32_t nres = insn.c; // count+1, 0=multret
        if (nres == 1)
        {
            // No results kept: bare call statement.
            stmts_.push_back(Stmt::mkCallStmt(call));
        }
        else if (nres == 2)
        {
            produceValue(insn.a, call, false, nextPc);
        }
        else if (nres == 0)
        {
            // MULTRET: keep as a deferred compound; a later multret-aware
            // consumer (another CALL's trailing arg, RETURN, or SETLIST)
            // will pick it up via regValue/gatherRange.
            setReg(insn.a, call, false);
        }
        else
        {
            materializeMultiWrite(insn.a, nres - 1, call, nextPc);
        }
        break;
    }
    }
}

std::vector<ExprPtr> ExprTracker::gatherRange(uint8_t startReg, uint8_t countPlus1)
{
    std::vector<ExprPtr> out;
    if (countPlus1 == 0)
    {
        uint8_t r = startReg;
        while (r < regExpr_.size() && regExpr_[r])
        {
            out.push_back(regValue(r));
            ++r;
        }
    }
    else
    {
        uint32_t n = countPlus1 - 1;
        for (uint32_t i = 0; i < n; ++i)
            out.push_back(regValue(static_cast<uint8_t>(startReg + i)));
    }
    return out;
}

ExprPtr ExprTracker::buildCallExpr(const Instruction& insn)
{
    std::vector<ExprPtr> args;
    if (pendingNamecall_ && pendingNamecall_->funcReg == insn.a)
    {
        PendingNamecall pn = *pendingNamecall_;
        pendingNamecall_.reset();
        // Skip the implicit self arg at insn.a+1; real args start at +2.
        uint32_t nargs = insn.b;
        if (nargs == 0)
            args = gatherRange(static_cast<uint8_t>(insn.a + 2), 0);
        else if (nargs >= 2)
        {
            uint32_t n = nargs - 2; // nargs-1 total args minus the self slot
            for (uint32_t i = 0; i < n; ++i)
                args.push_back(regValue(static_cast<uint8_t>(insn.a + 2 + i)));
        }
        return Expr::mkCall(pn.base, std::move(args), true, pn.method);
    }
    pendingNamecall_.reset();
    ExprPtr callee = regValue(insn.a);
    args = gatherRange(static_cast<uint8_t>(insn.a + 1), static_cast<uint8_t>(insn.b));
    return Expr::mkCall(callee, std::move(args));
}

void ExprTracker::materializeMultiWrite(uint8_t startReg, uint32_t count, ExprPtr multiValueExpr, uint32_t nextPc)
{
    std::vector<ExprPtr> names;
    bool allDeclared = true;
    for (uint32_t i = 0; i < count; ++i)
    {
        uint8_t reg = static_cast<uint8_t>(startReg + i);
        std::string nm = activeLocalName(reg, nextPc).value_or(freshName());
        names.push_back(Expr::mkLocal(nm));
        bool declared = reg < declaredName_.size() && declaredName_[reg] && *declaredName_[reg] == nm;
        allDeclared = allDeclared && declared;
        setReg(reg, Expr::mkLocal(nm), true);
        if (reg < declaredName_.size())
            declaredName_[reg] = nm;
    }
    std::vector<ExprPtr> values{multiValueExpr};
    if (count > 0 && allDeclared)
        stmts_.push_back(Stmt::mkAssign(names, values));
    else
        stmts_.push_back(Stmt::mkLocal(names, values));
}

std::vector<StmtPtr> ExprTracker::takeStmts()
{
    std::vector<StmtPtr> out;
    out.swap(stmts_);
    return out;
}

std::vector<StmtPtr> ExprTracker::flushAndTakeStmts()
{
    for (size_t r = 0; r < regExpr_.size(); ++r)
    {
        if (regExpr_[r] && !regIsAtom_[r])
        {
            std::string name = (r < nameHint_.size() && nameHint_[r]) ? *nameHint_[r] : freshName();
            stmts_.push_back(Stmt::mkLocal({Expr::mkLocal(name)}, {regExpr_[r]}));
        }
        clearReg(static_cast<uint8_t>(r));
    }
    return takeStmts();
}

} // namespace luaudec
