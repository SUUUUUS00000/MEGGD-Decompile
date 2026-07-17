#include "bytecode_reader.hpp"
#include "disassembler.hpp"
#include <iostream>
#include <string>

using namespace luaudec;

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::cerr << "usage: " << argv[0] << " <bytecode.bin>\n";
        return 1;
    }
    try
    {
        Module m = BytecodeReader::readFile(argv[1]);
        std::cout << "version=" << int(m.version) << " typesVersion=" << int(m.typesVersion)
                  << " strings=" << m.strings.size() << " protos=" << m.protos.size()
                  << " main=" << m.mainProtoId << "\n";
        for (const Proto& p : m.protos)
        {
            std::cout << "proto[" << p.selfIndex << "] "
                      << "name=" << m.strOr(p.debugNameString, "?")
                      << " line=" << p.lineDefined
                      << " params=" << int(p.numParams)
                      << " vararg=" << p.isVararg
                      << " upvals=" << int(p.numUpvalues)
                      << " maxstack=" << int(p.maxStackSize)
                      << " insns=" << p.instructions.size()
                      << " consts=" << p.constants.size()
                      << " children=" << p.childProtoIds.size()
                      << " hasLineInfo=" << p.hasLineInfo
                      << " hasDebugInfo=" << p.hasDebugInfo
                      << " locals=" << p.locals.size()
                      << "\n";
            for (const LocalVarInfo& lv : p.locals)
                std::cout << "    local " << m.strOr(lv.nameString, "?") << " reg=R" << int(lv.reg)
                          << " [" << lv.startpc << "," << lv.endpc << ")\n";
        }
        if (argc > 2 && std::string(argv[2]) == "--asm")
            std::cout << "\n" << Disassembler::disassembleModule(m) << "\n";
    }
    catch (const BytecodeReadError& e)
    {
        std::cerr << "bytecode read error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
