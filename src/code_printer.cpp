// code_printer.cpp
#include "code_printer.hpp"

#include <sstream>
#include <iomanip>
#include <cmath>

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

// Lua/Luau keywords can't be used as bare identifiers even if they'd
// otherwise look like one (e.g. a table field literally named "end").
bool isKeyword(const std::string& s)
{
    static const std::vector<std::string> kw = {
        "and", "break", "do", "else", "elseif", "end", "false", "for", "function", "if", "in",
        "local", "nil", "not", "or", "repeat", "return", "then", "true", "until", "while", "continue"};
    for (const auto& k : kw)
        if (s == k)
            return true;
    return false;
}

int precedence(BinOpKind op)
{
    switch (op)
    {
    case BinOpKind::Or: return 1;
    case BinOpKind::And: return 2;
    case BinOpKind::Lt:
    case BinOpKind::Le:
    case BinOpKind::Gt:
    case BinOpKind::Ge:
    case BinOpKind::Ne:
    case BinOpKind::Eq: return 3;
    case BinOpKind::Concat: return 4;
    case BinOpKind::Add:
    case BinOpKind::Sub: return 5;
    case BinOpKind::Mul:
    case BinOpKind::Div:
    case BinOpKind::Mod:
    case BinOpKind::IDiv: return 6;
    case BinOpKind::Pow: return 8;
    }
    return 0;
}
constexpr int kUnaryPrec = 7;

const char* binOpSymbol(BinOpKind op)
{
    switch (op)
    {
    case BinOpKind::Add: return "+";
    case BinOpKind::Sub: return "-";
    case BinOpKind::Mul: return "*";
    case BinOpKind::Div: return "/";
    case BinOpKind::Mod: return "%";
    case BinOpKind::Pow: return "^";
    case BinOpKind::IDiv: return "//";
    case BinOpKind::Concat: return "..";
    case BinOpKind::Eq: return "==";
    case BinOpKind::Ne: return "~=";
    case BinOpKind::Lt: return "<";
    case BinOpKind::Le: return "<=";
    case BinOpKind::Gt: return ">";
    case BinOpKind::Ge: return ">=";
    case BinOpKind::And: return "and";
    case BinOpKind::Or: return "or";
    }
    return "?";
}

std::string formatNumber(double v)
{
    if (std::isinf(v))
        return v > 0 ? "math.huge" : "-math.huge";
    if (std::isnan(v))
        return "(0/0)";
    std::ostringstream oss;
    if (v == static_cast<int64_t>(v) && std::abs(v) < 1e15)
        oss << static_cast<int64_t>(v);
    else
        oss << std::setprecision(17) << v;
    return oss.str();
}

std::string escapeString(const std::string& s)
{
    std::string out;
    out.reserve(s.size() + 2);
    out += '"';
    for (unsigned char c : s)
    {
        switch (c)
        {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\t': out += "\\t"; break;
        case '\r': out += "\\r"; break;
        default:
            if (c < 0x20 || c == 0x7f)
            {
                std::ostringstream oss;
                oss << "\\" << std::setw(3) << std::setfill('0') << std::dec << int(c);
                out += oss.str();
            }
            else
            {
                out += static_cast<char>(c);
            }
        }
    }
    out += '"';
    return out;
}

} // namespace

std::string CodePrinter::indentStr(int n) { return std::string(static_cast<size_t>(n) * 4, ' '); }

std::vector<std::string> CodePrinter::paramNames(const Proto& proto)
{
    std::vector<std::string> names;
    for (uint8_t i = 0; i < proto.numParams; ++i)
    {
        std::string name;
        for (const LocalVarInfo& lv : proto.locals)
        {
            if (lv.reg == i && lv.startpc == 0 && lv.nameString)
            {
                name = module_.str(*lv.nameString);
                break;
            }
        }
        if (name.empty())
            name = "p" + std::to_string(i);
        names.push_back(name);
    }
    return names;
}

std::string CodePrinter::printExprList(const std::vector<ExprPtr>& es)
{
    std::string out;
    for (size_t i = 0; i < es.size(); ++i)
    {
        if (i)
            out += ", ";
        out += printExpr(es[i], 0);
    }
    return out;
}

std::string CodePrinter::printPrimary(const ExprPtr& e)
{
    switch (e->kind)
    {
    case EK::Binary:
    case EK::Unary:
    {
        std::string s = printExpr(e, 100);
        return "(" + s + ")";
    }
    case EK::Function:
        return "(" + printFunctionLiteral(e->protoIndex, 0) + ")";
    default:
        return printExpr(e, 0);
    }
}

std::string CodePrinter::printExpr(const ExprPtr& e, int minPrec)
{
    if (!e)
        return "nil";
    switch (e->kind)
    {
    case EK::Nil: return "nil";
    case EK::True: return "true";
    case EK::False: return "false";
    case EK::Number: return e->isInt ? std::to_string(e->intValue) : formatNumber(e->number);
    case EK::Str: return escapeString(e->str);
    case EK::Vararg: return "...";
    case EK::Local: return e->str;
    case EK::Global: return e->str;
    case EK::Upvalue: return e->str;
    case EK::Reg: return "R" + std::to_string(e->regIndex);
    case EK::Raw: return e->str;
    case EK::Paren: return "(" + printExpr(e->lhs, 0) + ")";

    case EK::Index:
    {
        std::string base = printPrimary(e->base);
        if (e->dotStyle && e->key->kind == EK::Str && looksLikeIdentifier(e->key->str) && !isKeyword(e->key->str))
            return base + "." + e->key->str;
        return base + "[" + printExpr(e->key, 0) + "]";
    }

    case EK::Call:
    {
        std::string base = printPrimary(e->callee);
        std::string args = printExprList(e->args);
        if (e->isMethodCall)
            return base + ":" + e->methodName + "(" + args + ")";
        return base + "(" + args + ")";
    }

    case EK::Unary:
    {
        const char* sym = e->unOp == UnOpKind::Not ? "not " : e->unOp == UnOpKind::Len ? "#" : "-";
        std::string operand = printExpr(e->lhs, kUnaryPrec);
        std::string s = std::string(sym) + operand;
        return (kUnaryPrec < minPrec) ? "(" + s + ")" : s;
    }

    case EK::Binary:
    {
        int prec = precedence(e->binOp);
        bool rightAssoc = (e->binOp == BinOpKind::Concat || e->binOp == BinOpKind::Pow);
        std::string lhs = printExpr(e->lhs, rightAssoc ? prec + 1 : prec);
        std::string rhs = printExpr(e->rhs, rightAssoc ? prec : prec + 1);
        std::string s = lhs + " " + binOpSymbol(e->binOp) + " " + rhs;
        return (prec < minPrec) ? "(" + s + ")" : s;
    }

    case EK::TableCtor:
    {
        if (e->items.empty())
            return "{}";
        std::string out = "{";
        for (size_t i = 0; i < e->items.size(); ++i)
        {
            if (i)
                out += ", ";
            const TableItem& item = e->items[i];
            if (!item.key)
            {
                out += printExpr(item.value, 0);
            }
            else if (item.dotStyle && item.key->kind == EK::Str && looksLikeIdentifier(item.key->str) && !isKeyword(item.key->str))
            {
                out += item.key->str + " = " + printExpr(item.value, 0);
            }
            else
            {
                out += "[" + printExpr(item.key, 0) + "] = " + printExpr(item.value, 0);
            }
        }
        out += "}";
        return out;
    }

    case EK::Function:
        return printFunctionLiteral(e->protoIndex, 0);
    }
    return "--[[?]]";
}

std::string CodePrinter::printFunctionLiteral(uint32_t protoIndex, int indent)
{
    if (protoIndex >= module_.protos.size())
        return "function() --[[ bad proto ref ]] end";
    const Proto& proto = module_.protos[protoIndex];
    std::vector<std::string> params = paramNames(proto);
    std::string sig = "function(";
    for (size_t i = 0; i < params.size(); ++i)
    {
        if (i)
            sig += ", ";
        sig += params[i];
    }
    if (proto.isVararg)
        sig += params.empty() ? "..." : ", ...";
    sig += ")";

    std::string body;
    const std::vector<StmtPtr>& stmts = (protoIndex < bodiesByProto_.size()) ? bodiesByProto_[protoIndex] : std::vector<StmtPtr>{};
    printBlock(stmts, indent + 1, body);

    std::string out = sig + "\n" + body + indentStr(indent) + "end";
    return out;
}

void CodePrinter::printBlock(const std::vector<StmtPtr>& body, int indent, std::string& out)
{
    for (const StmtPtr& s : body)
        printStmt(s, indent, out);
}

void CodePrinter::printStmt(const StmtPtr& s, int indent, std::string& out)
{
    std::string ind = indentStr(indent);
    switch (s->kind)
    {
    case SK::Local:
    {
        if (s->targets.size() == 1 && s->values.size() == 1 && s->values[0]->kind == EK::Function)
        {
            out += ind; // placeholder to keep structure; replaced below
        }
        if (s->targets.size() == 1 && s->values.size() == 1 && s->values[0]->kind == EK::Function)
        {
            out += ind + "local function " + s->targets[0]->str + printFunctionLiteral(s->values[0]->protoIndex, indent).substr(std::string("function").size()) + "\n";
        }
        else
        {
            std::string names;
            for (size_t i = 0; i < s->targets.size(); ++i)
                names += (i ? ", " : "") + s->targets[i]->str;
            out += ind + "local " + names;
            if (!s->values.empty())
                out += " = " + printExprList(s->values);
            out += "\n";
        }
        break;
    }
    case SK::Assign:
    {
        if (s->targets.size() == 1 && s->values.size() == 1 && s->values[0]->kind == EK::Function && s->targets[0]->kind == EK::Index)
        {
            const Proto& fnProto = (s->values[0]->protoIndex < module_.protos.size()) ? module_.protos[s->values[0]->protoIndex] : module_.protos[0];
            std::vector<std::string> params = paramNames(fnProto);
            bool isMethod = !params.empty() && params[0] == "self";
            std::string target = printExpr(s->targets[0]->base, 0);
            std::string field = s->targets[0]->key->kind == EK::Str ? s->targets[0]->key->str : "?";
            std::string sig = "function " + target + (isMethod ? ":" : ".") + field + "(";
            for (size_t i = (isMethod ? 1 : 0); i < params.size(); ++i)
                sig += (i > (isMethod ? 1u : 0u) ? ", " : "") + params[i];
            if (fnProto.isVararg)
                sig += (params.size() > (isMethod ? 1u : 0u)) ? ", ..." : "...";
            sig += ")";
            std::string body;
            const std::vector<StmtPtr>& stmts = (s->values[0]->protoIndex < bodiesByProto_.size()) ? bodiesByProto_[s->values[0]->protoIndex] : std::vector<StmtPtr>{};
            printBlock(stmts, indent + 1, body);
            out += ind + sig + "\n" + body + ind + "end\n";
        }
        else
        {
            std::string names;
            for (size_t i = 0; i < s->targets.size(); ++i)
                names += (i ? ", " : "") + printExpr(s->targets[i], 0);
            out += ind + names + " = " + printExprList(s->values) + "\n";
        }
        break;
    }
    case SK::CallStmt:
        out += ind + printExpr(s->call, 0) + "\n";
        break;
    case SK::If:
    {
        out += ind + "if " + printExpr(s->cond, 0) + " then\n";
        printBlock(s->thenBody, indent + 1, out);
        const Stmt* cur = s.get();
        while (cur->elseBody.size() == 1 && cur->elseBody[0]->kind == SK::If)
        {
            const Stmt* nxt = cur->elseBody[0].get();
            out += ind + "elseif " + printExpr(nxt->cond, 0) + " then\n";
            printBlock(nxt->thenBody, indent + 1, out);
            cur = nxt;
        }
        if (!cur->elseBody.empty())
        {
            out += ind + "else\n";
            printBlock(cur->elseBody, indent + 1, out);
        }
        out += ind + "end\n";
        break;
    }
    case SK::NumericFor:
    {
        out += ind + "for " + s->loopVar + " = " + printExpr(s->forStart, 0) + ", " + printExpr(s->forLimit, 0);
        if (s->forStep)
            out += ", " + printExpr(s->forStep, 0);
        out += " do\n";
        printBlock(s->body, indent + 1, out);
        out += ind + "end\n";
        break;
    }
    case SK::GenericFor:
    {
        std::string vars;
        for (size_t i = 0; i < s->loopVars.size(); ++i)
            vars += (i ? ", " : "") + s->loopVars[i];
        out += ind + "for " + vars + " in " + printExprList(s->forExprs) + " do\n";
        printBlock(s->body, indent + 1, out);
        out += ind + "end\n";
        break;
    }
    case SK::While:
        out += ind + "while " + printExpr(s->cond, 0) + " do\n";
        printBlock(s->body, indent + 1, out);
        out += ind + "end\n";
        break;
    case SK::Repeat:
        out += ind + "repeat\n";
        printBlock(s->body, indent + 1, out);
        out += ind + "until " + printExpr(s->cond, 0) + "\n";
        break;
    case SK::Break:
        out += ind + "break\n";
        break;
    case SK::Continue:
        out += ind + "continue\n";
        break;
    case SK::Return:
        out += ind + "return";
        if (!s->values.empty())
            out += " " + printExprList(s->values);
        out += "\n";
        break;
    case SK::Do:
        out += ind + "do\n";
        printBlock(s->body, indent + 1, out);
        out += ind + "end\n";
        break;
    case SK::Comment:
        out += ind + "-- " + s->comment + "\n";
        break;
    }
}

std::string CodePrinter::printChunk(uint32_t protoIndex)
{
    std::string out;
    const std::vector<StmtPtr>& stmts = (protoIndex < bodiesByProto_.size()) ? bodiesByProto_[protoIndex] : std::vector<StmtPtr>{};
    printBlock(stmts, 0, out);
    return out;
}

} // namespace luaudec
