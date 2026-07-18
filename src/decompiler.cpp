// decompiler.cpp
#include "decompiler.hpp"

#include "bytecode_reader.hpp"
#include "structurizer.hpp"
#include "code_printer.hpp"

#include <vector>

namespace luaudec
{

std::string decompile_bytecode(const uint8_t* data, size_t size)
{
    Module m = BytecodeReader::read(data, size);

    std::vector<std::vector<StmtPtr>> bodies;
    bodies.reserve(m.protos.size());
    for (const auto& proto : m.protos)
    {
        bodies.push_back(structureFunction(m, proto));
    }

    CodePrinter printer(m, bodies);
    return printer.printChunk(m.mainProtoId);
}

} // namespace luaudec
