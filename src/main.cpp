// main.cpp
#include "bytecode_reader.hpp"
#include "disassembler.hpp"
#include "decompiler.hpp"
#include "escape_decode.hpp"

#include <iostream>
#include <fstream>
#include <sstream>

using namespace luaudec;

namespace
{
void printUsage(const char* prog)
{
    std::cerr << "usage: " << prog << " [--asm|--decompile] <bytecode.bin> [-o output.lua] [--diag]\n"
              << "  --decompile   emit reconstructed Luau-like source (default)\n"
              << "  --asm         emit a disassembly listing instead\n"
              << "  --diag        print structural parse progress to stderr (sizes/counts only,\n"
              << "                never string/constant content) -- useful for reporting a parse\n"
              << "                failure without needing to share the file itself\n";
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
    bool diagnostic = false;

    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];
        if (arg == "--asm" || arg == "--decompile")
            mode = arg;
        else if (arg == "--diag")
            diagnostic = true;
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

    std::ifstream file(inputPath, std::ios::binary | std::ios::ate);
    if (!file)
    {
        std::cerr << "error: could not open file: " << inputPath << "\n";
        return 1;
    }
    std::streamsize fsize = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<uint8_t> rawBytes(static_cast<size_t>(fsize > 0 ? fsize : 0));
    if (fsize > 0 && !file.read(reinterpret_cast<char*>(rawBytes.data()), fsize))
    {
        std::cerr << "error: failed reading file: " << inputPath << "\n";
        return 1;
    }

    Module module;
    try
    {
        module = BytecodeReader::read(rawBytes.data(), rawBytes.size(), diagnostic);
    }
    catch (const BytecodeReadError& rawError)
    {
        // Bytecode obtained from a Lua string value is very often
        // saved/pasted as *text* (a string literal, or just a bare escape
        // dump) rather than as the raw bytes themselves -- try decoding
        // that before giving up.
        std::string asText(reinterpret_cast<const char*>(rawBytes.data()), rawBytes.size());
        auto decoded = tryDecodeLuaEscapedBytes(asText);
        if (!decoded)
        {
            std::cerr << "error: " << rawError.what() << "\n";
            return 1;
        }
        try
        {
            module = BytecodeReader::read(decoded->data(), decoded->size(), diagnostic);
            std::cerr << "note: input didn't parse as raw bytecode; detected and decoded a Lua-escaped "
                      << "string literal instead (" << decoded->size() << " bytes recovered)\n";
        }
        catch (const BytecodeReadError& decodedError)
        {
            std::cerr << "error: input isn't valid raw bytecode (" << rawError.what() << ")\n"
                      << "error: also tried decoding it as a Lua-escaped string literal, but that didn't parse "
                      << "as valid bytecode either (" << decodedError.what() << ")\n";
            return 1;
        }
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
