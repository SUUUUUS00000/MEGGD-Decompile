// decompiler.cpp
#include "decompiler.hpp"
#include "structurizer.hpp"
#include "code_printer.hpp"

namespace luaudec
{

std::string decompileModule(const Module& module)
{
    std::vector<std::vector<StmtPtr>> bodiesByProto(module.protos.size());
    for (const Proto& proto : module.protos)
        bodiesByProto[proto.selfIndex] = structureFunction(module, proto);

    CodePrinter printer(module, bodiesByProto);
    return printer.printChunk(module.mainProtoId);
}

} // namespace luaudec
