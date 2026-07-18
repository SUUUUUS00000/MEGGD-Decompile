// main.cpp
#include "bytecode_reader.hpp"
#include "disassembler.hpp"
#include "decompiler.hpp"

#include <iostream>
#include <fstream>

using namespace luaudec;

namespace
{
void printUsage(const char* prog)
{
    std::cerr << "usage: " << prog << " [--asm|--decompile] <bytecode.bin> [-o output.lua]\n"
              << "  --decompile   emit reconstructed Luau-like source (default)\n"
              << "  --asm         emit a disassembly listing instead\n";
}
} // namespace

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        printUsage(argv[0]);
        return 1;
    }

    std::string mode = "--decompile";
    std::string inputPath;
    std::string outputPath;

    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];
        if (arg == "--asm" || arg == "--decompile")
            mode = arg;
        else if (arg == "-o" && i + 1 < argc)
            outputPath = argv[++i];
        else if (inputPath.empty())
            inputPath = arg;
        else
        {
            printUsage(argv[0]);
            return 1;
        }
    }

    if (inputPath.empty())
    {
        printUsage(argv[0]);
        return 1;
    }

    Module module;
    try
    {
        module = BytecodeReader::readFile(inputPath);
    }
    catch (const BytecodeReadError& e)
    {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }

    std::string result = (mode == "--asm") ? Disassembler::disassembleModule(module) : decompileModule(module);

    if (outputPath.empty())
    {
        std::cout << result;
    }
    else
    {
        std::ofstream out(outputPath, std::ios::binary);
        if (!out)
        {
            std::cerr << "error: could not open output file: " << outputPath << "\n";
            return 1;
        }
        out << result;
    }
    return 0;
}
