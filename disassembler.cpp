// disassembler.cpp
#include "disassembler.hpp"

#include <sstream>
#include <iomanip>
#include <cmath>

namespace luaudec
{

namespace
{

std::string reg(uint8_t r)
{
    return "R" + std::to_string(r);
}

std::string upv(uint8_t idx, const Module& module, const Proto& proto)
{
    std::string s = "U" + std::to_string(idx);
    if (idx < proto.upvalNames.size() && proto.upvalNames[idx].nameString)
        s += " [" + module.str(*proto.upvalNames[idx].nameString) + "]";
    return s;
}

// B/C "count+1, 0=multret" fields used by CALL/RETURN/GETVARARGS/SETLIST.
std::string countField(uint8_t v)
{
    return v == 0 ? std::string("multret") : std::to_string(v - 1);
}

std::string label(int32_t target, int32_t insnCount)
{
    if (target == insnCount)
        return "END";
    return "L" + std::to_string(target);
}

} // namespace

std::string Disassembler::formatNumber(double v)
{
    if (std::isinf(v))
        return v > 0 ? "inf" : "-inf";
    if (std::isnan(v))
        return "nan";
    std::ostringstream oss;
    if (v == static_cast<int64_t>(v) && std::abs(v) < 1e15)
        oss << static_cast<int64_t>(v);
    else
        oss << std::setprecision(17) << v;
    return oss.str();
}

std::string Disassembler::escapeString(const std::string& s)
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
                oss << "\\x" << std::hex << std::setw(2) << std::setfill('0') << int(c);
                out += oss.str();
            }
            else
            {
                out += static_cast<char>(c);
            }
        }
    }
    out += '"';
    if (out.size() > 64)
        out = out.substr(0, 61) + "...\"";
    return out;
}

std::string Disassembler::renderImportPath(const Module& module, const Proto& proto, uint32_t packedId)
{
    uint32_t count = packedId >> 30;
    uint32_t ids[3] = {(packedId >> 20) & 0x3ff, (packedId >> 10) & 0x3ff, packedId & 0x3ff};
    std::string out;
    for (uint32_t i = 0; i < count && i < 3; ++i)
    {
        if (i > 0)
            out += ".";
        uint32_t cidx = ids[i];
        if (cidx < proto.constants.size() && proto.constants[cidx].tag == ConstTag::String && proto.constants[cidx].stringIndex)
            out += module.str(*proto.constants[cidx].stringIndex);
        else
            out += "?";
    }
    return out;
}

std::string Disassembler::renderConstant(const Module& module, const Proto& proto, uint32_t idx)
{
    if (idx >= proto.constants.size())
        return "<bad const " + std::to_string(idx) + ">";
    const Constant& c = proto.constants[idx];
    switch (c.tag)
    {
    case ConstTag::Nil:
        return "nil";
    case ConstTag::Boolean:
        return c.boolValue ? "true" : "false";
    case ConstTag::Number:
        return formatNumber(c.numberValue);
    case ConstTag::Vector:
    {
        std::ostringstream oss;
        oss << "vector(" << c.vecValue[0] << ", " << c.vecValue[1] << ", " << c.vecValue[2];
        if (c.vecValue[3] != 0.0f)
            oss << ", " << c.vecValue[3];
        oss << ")";
        return oss.str();
    }
    case ConstTag::String:
        return c.stringIndex ? escapeString(module.str(*c.stringIndex)) : "\"\"";
    case ConstTag::Import:
        return renderImportPath(module, proto, c.importId);
    case ConstTag::Table:
    case ConstTag::TableWithConstants:
    {
        std::ostringstream oss;
        oss << "{";
        for (size_t i = 0; i < c.tableShape.size(); ++i)
        {
            if (i)
                oss << ", ";
            const TableShapeEntry& e = c.tableShape[i];
            if (e.keyConstant >= 0)
                oss << renderConstant(module, proto, static_cast<uint32_t>(e.keyConstant));
            if (e.valueConstant >= 0)
                oss << "=" << renderConstant(module, proto, static_cast<uint32_t>(e.valueConstant));
        }
        oss << "}";
        return oss.str();
    }
    case ConstTag::Closure:
        return "closure#" + std::to_string(c.closureProto);
    case ConstTag::ClassShape:
    {
        std::string name = c.classShape.classNameConstant >= 0
                                ? renderConstant(module, proto, static_cast<uint32_t>(c.classShape.classNameConstant))
                                : "?";
        return "class " + name;
    }
    case ConstTag::Integer:
        return std::to_string(c.integerValue);
    }
    return "?";
}

std::string Disassembler::formatInstruction(const Module& module, const Proto& proto, const Instruction& insn, const std::vector<bool>& /*isLabel*/)
{
    const OpInfo& info = opInfo(static_cast<uint8_t>(insn.op));
    std::ostringstream out;
    int32_t insnCount = static_cast<int32_t>(proto.instructions.size());

    auto K = [&](uint32_t idx) { return "K" + std::to_string(idx) + " [" + renderConstant(module, proto, idx) + "]"; };
    auto J = [&]() { return "-> " + label(insn.jumpTarget, insnCount); };

    out << info.name;
    switch (insn.op)
    {
    case Op::NOP:
    case Op::BREAK:
    case Op::NATIVECALL:
        break;

    case Op::LOADNIL:
        out << " " << reg(insn.a);
        break;
    case Op::LOADB:
        out << " " << reg(insn.a) << " " << int(insn.b);
        if (insn.c != 0)
            out << " +" << int(insn.c);
        break;
    case Op::LOADN:
        out << " " << reg(insn.a) << " " << insn.d;
        break;
    case Op::LOADK:
        out << " " << reg(insn.a) << " " << K(static_cast<uint32_t>(insn.d));
        break;
    case Op::LOADKX:
        out << " " << reg(insn.a) << " " << K(insn.aux);
        break;
    case Op::MOVE:
        out << " " << reg(insn.a) << " " << reg(insn.b);
        break;

    case Op::GETGLOBAL:
    case Op::SETGLOBAL:
        out << " " << reg(insn.a) << " " << K(insn.aux);
        break;

    case Op::GETUPVAL:
    case Op::SETUPVAL:
        out << " " << reg(insn.a) << " " << upv(insn.b, module, proto);
        break;

    case Op::CLOSEUPVALS:
        out << " " << reg(insn.a) << "+";
        break;

    case Op::GETIMPORT:
        out << " " << reg(insn.a) << " K" << insn.d << " [" << renderImportPath(module, proto, insn.aux) << "]";
        break;

    case Op::GETTABLE:
    case Op::SETTABLE:
        out << " " << reg(insn.a) << " " << reg(insn.b) << " " << reg(insn.c);
        break;

    case Op::GETTABLEKS:
    case Op::SETTABLEKS:
    case Op::GETUDATAKS:
    case Op::SETUDATAKS:
        out << " " << reg(insn.a) << " " << reg(insn.b) << " " << K(auxKV(insn.aux));
        break;

    case Op::GETTABLEN:
    case Op::SETTABLEN:
        out << " " << reg(insn.a) << " " << reg(insn.b) << " " << (int(insn.c) + 1);
        break;

    case Op::NEWCLOSURE:
    {
        out << " " << reg(insn.a) << " P" << insn.d;
        if (insn.d >= 0 && static_cast<size_t>(insn.d) < proto.childProtoIds.size())
        {
            uint32_t globalIdx = proto.childProtoIds[insn.d];
            std::string name = globalIdx < module.protos.size() ? module.strOr(module.protos[globalIdx].debugNameString, "") : "";
            out << " [proto " << globalIdx << (name.empty() ? "" : (" " + name)) << "]";
        }
        break;
    }
    case Op::DUPCLOSURE:
        out << " " << reg(insn.a) << " " << K(static_cast<uint32_t>(insn.d));
        break;

    case Op::NAMECALL:
    case Op::NAMECALLUDATA:
        out << " " << reg(insn.a) << " " << reg(insn.b) << " " << K(auxKV(insn.aux));
        break;

    case Op::CALL:
        out << " " << reg(insn.a) << " nargs=" << countField(insn.b) << " nres=" << countField(insn.c);
        break;
    case Op::RETURN:
        out << " " << reg(insn.a) << " n=" << countField(insn.b);
        break;

    case Op::JUMP:
    case Op::JUMPBACK:
        out << " " << J();
        break;
    case Op::JUMPIF:
    case Op::JUMPIFNOT:
        out << " " << reg(insn.a) << " " << J();
        break;
    case Op::JUMPIFEQ:
    case Op::JUMPIFLE:
    case Op::JUMPIFLT:
    case Op::JUMPIFNOTEQ:
    case Op::JUMPIFNOTLE:
    case Op::JUMPIFNOTLT:
        out << " " << reg(insn.a) << " " << reg(auxA(insn.aux)) << " " << J();
        break;
    case Op::JUMPX:
        out << " " << J();
        break;
    case Op::JUMPXEQKNIL:
        out << " " << reg(insn.a) << " " << (auxNot(insn.aux) ? "~= nil" : "== nil") << " " << J();
        break;
    case Op::JUMPXEQKB:
        out << " " << reg(insn.a) << " " << (auxNot(insn.aux) ? "~= " : "== ") << (auxKB(insn.aux) ? "true" : "false") << " " << J();
        break;
    case Op::JUMPXEQKN:
    case Op::JUMPXEQKS:
        out << " " << reg(insn.a) << " " << (auxNot(insn.aux) ? "~= " : "== ") << K(auxKV(insn.aux)) << " " << J();
        break;
    case Op::CMPPROTO:
        out << " " << reg(insn.a) << " protoK=" << auxKV(insn.aux) << " " << J();
        break;

    case Op::ADD: case Op::SUB: case Op::MUL: case Op::DIV: case Op::MOD: case Op::POW:
    case Op::AND: case Op::OR:
    case Op::IDIV:
        out << " " << reg(insn.a) << " " << reg(insn.b) << " " << reg(insn.c);
        break;

    case Op::ADDK: case Op::SUBK: case Op::MULK: case Op::DIVK: case Op::MODK: case Op::POWK:
    case Op::ANDK: case Op::ORK:
    case Op::IDIVK:
        out << " " << reg(insn.a) << " " << reg(insn.b) << " " << K(insn.c);
        break;

    case Op::SUBRK:
    case Op::DIVRK:
        out << " " << reg(insn.a) << " " << K(insn.b) << " " << reg(insn.c);
        break;

    case Op::CONCAT:
        out << " " << reg(insn.a) << " " << reg(insn.b) << ".." << reg(insn.c);
        break;

    case Op::NOT:
    case Op::MINUS:
    case Op::LENGTH:
        out << " " << reg(insn.a) << " " << reg(insn.b);
        break;

    case Op::NEWTABLE:
        out << " " << reg(insn.a) << " arraySize=" << insn.aux;
        break;
    case Op::DUPTABLE:
        out << " " << reg(insn.a) << " " << K(static_cast<uint32_t>(insn.d));
        break;
    case Op::SETLIST:
        out << " " << reg(insn.a) << " " << reg(insn.b) << " n=" << countField(insn.c) << " startIdx=" << insn.aux;
        break;

    case Op::FORNPREP:
    case Op::FORNLOOP:
        out << " " << reg(insn.a) << " " << J() << "  ; limit=" << reg(insn.a) << " step=" << reg(insn.a + 1)
            << " var=" << reg(insn.a + 2);
        break;

    case Op::FORGPREP:
    case Op::FORGPREP_INEXT:
    case Op::FORGPREP_NEXT:
        out << " " << reg(insn.a) << " " << J() << "  ; gen=" << reg(insn.a) << " state=" << reg(insn.a + 1)
            << " ctrl=" << reg(insn.a + 2);
        break;
    case Op::FORGLOOP:
        out << " " << reg(insn.a) << " " << J() << " nvars=" << int(auxA(insn.aux));
        break;

    case Op::GETVARARGS:
        out << " " << reg(insn.a) << " n=" << countField(insn.b);
        break;
    case Op::PREPVARARGS:
        out << " nparams=" << int(insn.a);
        break;

    case Op::FASTCALL:
        out << " " << builtinName(insn.a) << " " << J();
        break;
    case Op::FASTCALL1:
        out << " " << builtinName(insn.a) << " " << reg(insn.b) << " " << J();
        break;
    case Op::FASTCALL2:
        out << " " << builtinName(insn.a) << " " << reg(insn.b) << " " << reg(auxA(insn.aux)) << " " << J();
        break;
    case Op::FASTCALL2K:
        out << " " << builtinName(insn.a) << " " << reg(insn.b) << " " << K(auxKV(insn.aux)) << " " << J();
        break;
    case Op::FASTCALL3:
        out << " " << builtinName(insn.a) << " " << reg(insn.b) << " " << reg(auxA(insn.aux)) << " " << reg(auxB(insn.aux)) << " " << J();
        break;

    case Op::COVERAGE:
        out << " hits=" << insn.e;
        break;

    case Op::CAPTURE:
    {
        static const char* kinds[] = {"VAL", "REF", "UPVAL"};
        const char* k = insn.a < 3 ? kinds[insn.a] : "?";
        out << " " << k << " " << (insn.a == 2 ? upv(insn.b, module, proto) : reg(insn.b));
        break;
    }

    case Op::NEWCLASSMEMBER:
        out << " " << reg(insn.a) << " " << reg(insn.b) << " " << K(auxKV(insn.aux));
        break;
    case Op::CALLFB:
        out << " " << reg(insn.a) << " " << reg(insn.b) << " " << K(auxKV(insn.aux));
        break;

    default:
        // Generic fallback for anything not special-cased above.
        if (info.mode == Mode::ABC)
            out << " " << reg(insn.a) << " " << int(insn.b) << " " << int(insn.c);
        else if (info.mode == Mode::AD)
            out << " " << reg(insn.a) << " " << insn.d;
        else
            out << " " << insn.e;
        if (insn.hasAux)
            out << " aux=0x" << std::hex << insn.aux << std::dec;
        break;
    }

    return out.str();
}

std::string Disassembler::disassembleProto(const Module& module, const Proto& proto)
{
    std::ostringstream out;
    std::string name = module.strOr(proto.debugNameString, "?");
    out << "-- proto " << proto.selfIndex << " \"" << name << "\" line=" << proto.lineDefined
        << " params=" << int(proto.numParams) << " upvals=" << int(proto.numUpvalues)
        << " vararg=" << (proto.isVararg ? "yes" : "no") << " maxstack=" << int(proto.maxStackSize) << "\n";

    std::vector<bool> isLabel(proto.instructions.size() + 1, false);
    for (const Instruction& insn : proto.instructions)
        if (insn.jumpTarget >= 0)
            isLabel[insn.jumpTarget] = true;

    for (size_t i = 0; i < proto.instructions.size(); ++i)
    {
        const Instruction& insn = proto.instructions[i];
        std::string lbl = isLabel[i] ? ("L" + std::to_string(i) + ":") : "";
        out << std::left << std::setw(6) << lbl << " " << formatInstruction(module, proto, insn, isLabel);
        int32_t line = proto.lineForPc(insn.pc);
        if (line >= 0)
            out << "    ; line " << line;
        out << "\n";
    }
    if (isLabel.back())
        out << "END:\n";

    return out.str();
}

std::string Disassembler::disassembleModule(const Module& module)
{
    std::ostringstream out;
    out << "; Luau bytecode version=" << int(module.version) << " typesVersion=" << int(module.typesVersion)
        << " main=" << module.mainProtoId << "\n\n";
    for (const Proto& proto : module.protos)
        out << disassembleProto(module, proto) << "\n";
    return out.str();
}

} // namespace luaudec
