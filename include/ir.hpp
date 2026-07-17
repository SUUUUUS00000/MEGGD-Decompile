// ir.hpp
//
// A small AST for *decompiled* code, distinct from the raw bytecode
// Instruction stream. The structurizer builds this; code_printer renders
// it as Luau-like source text.

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace luaudec
{

struct Expr;
struct Stmt;
using ExprPtr = std::shared_ptr<Expr>;
using StmtPtr = std::shared_ptr<Stmt>;

enum class EK
{
    Nil, True, False, Number, Str, Vararg,
    Local, Global, Upvalue, Reg, // variable-like references
    Index,                        // t.k or t[k]
    Call,                         // f(...) or obj:method(...)
    Binary, Unary,
    TableCtor,
    Function,                     // inline closure literal
    Paren,
    Raw,                          // opaque fallback text
};

enum class BinOpKind
{
    Add, Sub, Mul, Div, Mod, Pow, IDiv,
    Concat,
    Eq, Ne, Lt, Le, Gt, Ge,
    And, Or,
};

enum class UnOpKind { Neg, Not, Len };

// One array item or one keyed item ({expr}, or {[k]=v} / {k=v}).
struct TableItem
{
    ExprPtr key;   // null => positional/array-style item
    bool dotStyle = false; // true => render as `name = value` instead of `[key] = value`
    ExprPtr value;
};

struct Expr
{
    EK kind;

    double number = 0.0;
    bool isInt = false;
    int64_t intValue = 0;

    std::string str; // string literal text / identifier name (Local/Global/Upvalue/Raw)
    int32_t regIndex = -1; // for EK::Reg fallback display

    // Index: base[key] or base.key (dotStyle if key is EK::Str and looks like an identifier)
    ExprPtr base;
    ExprPtr key;
    bool dotStyle = false;

    // Call
    ExprPtr callee;
    std::vector<ExprPtr> args;
    bool isMethodCall = false;
    std::string methodName;

    // Binary / Unary
    BinOpKind binOp{};
    UnOpKind unOp{};
    ExprPtr lhs, rhs; // binary; unary uses lhs

    // TableCtor
    std::vector<TableItem> items;

    // Function
    uint32_t protoIndex = 0;

    static ExprPtr mkNil() { auto e = std::make_shared<Expr>(); e->kind = EK::Nil; return e; }
    static ExprPtr mkBool(bool v) { auto e = std::make_shared<Expr>(); e->kind = v ? EK::True : EK::False; return e; }
    static ExprPtr mkNumber(double v)
    {
        auto e = std::make_shared<Expr>();
        e->kind = EK::Number;
        e->number = v;
        return e;
    }
    static ExprPtr mkInt(int64_t v)
    {
        auto e = std::make_shared<Expr>();
        e->kind = EK::Number;
        e->isInt = true;
        e->intValue = v;
        return e;
    }
    static ExprPtr mkStr(std::string s)
    {
        auto e = std::make_shared<Expr>();
        e->kind = EK::Str;
        e->str = std::move(s);
        return e;
    }
    static ExprPtr mkVararg() { auto e = std::make_shared<Expr>(); e->kind = EK::Vararg; return e; }
    static ExprPtr mkLocal(std::string name)
    {
        auto e = std::make_shared<Expr>();
        e->kind = EK::Local;
        e->str = std::move(name);
        return e;
    }
    static ExprPtr mkGlobal(std::string name)
    {
        auto e = std::make_shared<Expr>();
        e->kind = EK::Global;
        e->str = std::move(name);
        return e;
    }
    static ExprPtr mkUpvalue(std::string name)
    {
        auto e = std::make_shared<Expr>();
        e->kind = EK::Upvalue;
        e->str = std::move(name);
        return e;
    }
    static ExprPtr mkReg(int32_t idx)
    {
        auto e = std::make_shared<Expr>();
        e->kind = EK::Reg;
        e->regIndex = idx;
        return e;
    }
    static ExprPtr mkIndex(ExprPtr base, ExprPtr key, bool dotStyle)
    {
        auto e = std::make_shared<Expr>();
        e->kind = EK::Index;
        e->base = std::move(base);
        e->key = std::move(key);
        e->dotStyle = dotStyle;
        return e;
    }
    static ExprPtr mkCall(ExprPtr callee, std::vector<ExprPtr> args, bool isMethod = false, std::string methodName = "")
    {
        auto e = std::make_shared<Expr>();
        e->kind = EK::Call;
        e->callee = std::move(callee);
        e->args = std::move(args);
        e->isMethodCall = isMethod;
        e->methodName = std::move(methodName);
        return e;
    }
    static ExprPtr mkBinary(BinOpKind op, ExprPtr l, ExprPtr r)
    {
        auto e = std::make_shared<Expr>();
        e->kind = EK::Binary;
        e->binOp = op;
        e->lhs = std::move(l);
        e->rhs = std::move(r);
        return e;
    }
    static ExprPtr mkUnary(UnOpKind op, ExprPtr v)
    {
        auto e = std::make_shared<Expr>();
        e->kind = EK::Unary;
        e->unOp = op;
        e->lhs = std::move(v);
        return e;
    }
    static ExprPtr mkTable(std::vector<TableItem> items)
    {
        auto e = std::make_shared<Expr>();
        e->kind = EK::TableCtor;
        e->items = std::move(items);
        return e;
    }
    static ExprPtr mkFunction(uint32_t protoIndex)
    {
        auto e = std::make_shared<Expr>();
        e->kind = EK::Function;
        e->protoIndex = protoIndex;
        return e;
    }
    static ExprPtr mkParen(ExprPtr v)
    {
        auto e = std::make_shared<Expr>();
        e->kind = EK::Paren;
        e->lhs = std::move(v);
        return e;
    }
    static ExprPtr mkRaw(std::string s)
    {
        auto e = std::make_shared<Expr>();
        e->kind = EK::Raw;
        e->str = std::move(s);
        return e;
    }
};

enum class SK
{
    Local, Assign, CallStmt, If, NumericFor, GenericFor, While, Repeat,
    Break, Continue, Return, Do, Comment,
};

struct Stmt
{
    SK kind;

    // Local / Assign
    std::vector<ExprPtr> targets; // Local: just names via ExprPtr(Local); Assign: lvalues
    std::vector<ExprPtr> values;

    // CallStmt
    ExprPtr call;

    // If
    ExprPtr cond;
    std::vector<StmtPtr> thenBody;
    std::vector<StmtPtr> elseBody; // may contain a single nested If for "elseif"

    // NumericFor
    std::string loopVar;
    ExprPtr forStart, forLimit, forStep; // forStep may be null (default 1)

    // GenericFor
    std::vector<std::string> loopVars;
    std::vector<ExprPtr> forExprs;

    // shared body for While/Repeat/NumericFor/GenericFor/Do
    std::vector<StmtPtr> body;

    std::string comment;

    static StmtPtr mkLocal(std::vector<ExprPtr> names, std::vector<ExprPtr> values)
    {
        auto s = std::make_shared<Stmt>();
        s->kind = SK::Local;
        s->targets = std::move(names);
        s->values = std::move(values);
        return s;
    }
    static StmtPtr mkAssign(std::vector<ExprPtr> targets, std::vector<ExprPtr> values)
    {
        auto s = std::make_shared<Stmt>();
        s->kind = SK::Assign;
        s->targets = std::move(targets);
        s->values = std::move(values);
        return s;
    }
    static StmtPtr mkCallStmt(ExprPtr call)
    {
        auto s = std::make_shared<Stmt>();
        s->kind = SK::CallStmt;
        s->call = std::move(call);
        return s;
    }
    static StmtPtr mkIf(ExprPtr cond, std::vector<StmtPtr> thenBody, std::vector<StmtPtr> elseBody)
    {
        auto s = std::make_shared<Stmt>();
        s->kind = SK::If;
        s->cond = std::move(cond);
        s->thenBody = std::move(thenBody);
        s->elseBody = std::move(elseBody);
        return s;
    }
    static StmtPtr mkBreak() { auto s = std::make_shared<Stmt>(); s->kind = SK::Break; return s; }
    static StmtPtr mkContinue() { auto s = std::make_shared<Stmt>(); s->kind = SK::Continue; return s; }
    static StmtPtr mkReturn(std::vector<ExprPtr> values)
    {
        auto s = std::make_shared<Stmt>();
        s->kind = SK::Return;
        s->values = std::move(values);
        return s;
    }
    static StmtPtr mkComment(std::string c)
    {
        auto s = std::make_shared<Stmt>();
        s->kind = SK::Comment;
        s->comment = std::move(c);
        return s;
    }
};

} // namespace luaudec
