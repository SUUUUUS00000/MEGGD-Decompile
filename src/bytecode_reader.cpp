// bytecode_reader.cpp
#include "bytecode_reader.hpp"

#include <cstring>
#include <fstream>
#include <sstream>

namespace luaudec
{

// ---------------------------------------------------------------------
// Low-level primitives
// ---------------------------------------------------------------------

void BytecodeReader::requireBytes(size_t n) const
{
    if (offset_ + n > size_ || offset_ + n < offset_ /* overflow */)
    {
        std::ostringstream oss;
        oss << "unexpected end of stream at offset " << offset_ << " (need " << n
            << " more byte(s), have " << (size_ > offset_ ? size_ - offset_ : 0) << ")";
        throw BytecodeReadError(oss.str());
    }
}

uint8_t BytecodeReader::readU8()
{
    requireBytes(1);
    return data_[offset_++];
}

uint32_t BytecodeReader::readU32()
{
    requireBytes(4);
    uint32_t v;
    std::memcpy(&v, data_ + offset_, 4);
    offset_ += 4;
    return v;
}

float BytecodeReader::readF32()
{
    requireBytes(4);
    float v;
    std::memcpy(&v, data_ + offset_, 4);
    offset_ += 4;
    return v;
}

double BytecodeReader::readF64()
{
    requireBytes(8);
    double v;
    std::memcpy(&v, data_ + offset_, 8);
    offset_ += 8;
    return v;
}

uint32_t BytecodeReader::readVarInt()
{
    uint32_t result = 0;
    for (int shift = 0; shift < 35; shift += 7)
    {
        uint8_t byte = readU8();
        result |= static_cast<uint32_t>(byte & 0x7f) << shift;
        if (!(byte & 0x80))
            return result;
    }
    throw BytecodeReadError("varint too long at offset " + std::to_string(offset_));
}

uint64_t BytecodeReader::readVarInt64()
{
    uint64_t result = 0;
    for (int shift = 0; shift < 70; shift += 7)
    {
        uint8_t byte = readU8();
        result |= static_cast<uint64_t>(byte & 0x7f) << shift;
        if (!(byte & 0x80))
            return result;
    }
    throw BytecodeReadError("varint64 too long at offset " + std::to_string(offset_));
}

std::string BytecodeReader::readRawString(size_t length)
{
    requireBytes(length);
    std::string s(reinterpret_cast<const char*>(data_ + offset_), length);
    offset_ += length;
    return s;
}

std::optional<uint32_t> BytecodeReader::readStringRef(const Module& /*module*/)
{
    uint32_t id = readVarInt();
    if (id == 0)
        return std::nullopt;
    return id - 1;
}

// ---------------------------------------------------------------------
// Top-level module
// ---------------------------------------------------------------------

Module BytecodeReader::read(const uint8_t* data, size_t size)
{
    BytecodeReader reader(data, size);
    return reader.parseModule();
}

Module BytecodeReader::readFile(const std::string& path)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file)
        throw BytecodeReadError("could not open file: " + path);

    std::streamsize fsize = file.tellg();
    if (fsize < 0)
        throw BytecodeReadError("could not determine file size: " + path);
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> buf(static_cast<size_t>(fsize));
    if (fsize > 0 && !file.read(reinterpret_cast<char*>(buf.data()), fsize))
        throw BytecodeReadError("failed reading file: " + path);

    return read(buf.data(), buf.size());
}

Module BytecodeReader::parseModule()
{
    Module module;

    uint8_t version = readU8();
    if (version == 0)
    {
        // A version byte of 0 means compilation failed and the rest of the
        // stream is a human-readable error message, not bytecode.
        std::string message = readRawString(size_ - offset_);
        throw BytecodeReadError("input is a compile-error payload, not bytecode: " + message);
    }
    if (version < kBytecodeVersionMin || version > kBytecodeVersionMax)
    {
        std::ostringstream oss;
        oss << "unsupported bytecode version " << static_cast<int>(version) << " (supported: "
            << static_cast<int>(kBytecodeVersionMin) << ".." << static_cast<int>(kBytecodeVersionMax) << ")";
        throw BytecodeReadError(oss.str());
    }
    module.version = version;

    uint8_t typesVersion = 0;
    if (version >= 4)
    {
        typesVersion = readU8();
        if (typesVersion < kTypeVersionMin || typesVersion > kTypeVersionMax)
        {
            throw BytecodeReadError("unsupported type-info version " + std::to_string(typesVersion));
        }
    }
    module.typesVersion = typesVersion;

    // String table.
    uint32_t stringCount = readVarInt();
    module.strings.reserve(stringCount);
    for (uint32_t i = 0; i < stringCount; ++i)
    {
        uint32_t len = readVarInt();
        module.strings.push_back(readRawString(len));
    }

    // Userdata type remap table (only present for typesVersion == 3).
    if (typesVersion == 3)
        parseUserdataTypeRemap(module);

    // Function protos.
    uint32_t protoCount = readVarInt();
    module.protos.reserve(protoCount);
    for (uint32_t i = 0; i < protoCount; ++i)
        module.protos.push_back(parseProto(i, module));

    module.mainProtoId = readVarInt();
    if (module.mainProtoId >= module.protos.size())
        throw BytecodeReadError("main proto id out of range");

    return module;
}

void BytecodeReader::parseUserdataTypeRemap(Module& module)
{
    for (;;)
    {
        uint8_t index = readU8();
        if (index == 0)
            break;
        auto nameRef = readStringRef(module);
        UserdataTypeEntry entry;
        entry.index = static_cast<uint8_t>(index - 1);
        entry.name = nameRef ? module.str(*nameRef) : std::string();
        module.userdataTypes.push_back(std::move(entry));
    }
}

// ---------------------------------------------------------------------
// Proto
// ---------------------------------------------------------------------

Proto BytecodeReader::parseProto(uint32_t index, Module& module)
{
    Proto proto;
    proto.selfIndex = index;

    size_t protoStartOffset = offset_;
    uint32_t protoSize = 0;
    bool hasProtoSize = module.version >= 12;
    if (hasProtoSize)
    {
        protoSize = readVarInt();
        protoStartOffset = offset_; // size is measured *after* the size varint itself
    }

    proto.maxStackSize = readU8();
    proto.numParams = readU8();
    proto.numUpvalues = readU8();
    proto.isVararg = readU8() != 0;

    if (module.version >= 4)
    {
        proto.flags = readU8();

        uint32_t typeSize = readVarInt();
        if (typeSize > 0)
            proto.typeInfoRaw = std::vector<uint8_t>(data_ + offset_, data_ + offset_ + std::min<size_t>(typeSize, size_ - offset_));
        requireBytes(typeSize);
        offset_ += typeSize;
    }

    uint32_t sizecode = readVarInt();
    std::vector<uint32_t> words(sizecode);
    for (uint32_t i = 0; i < sizecode; ++i)
        words[i] = readU32();
    proto.totalWordCount = sizecode;
    decodeInstructions(proto, words);

    uint32_t sizek = readVarInt();
    proto.constants.reserve(sizek);
    for (uint32_t i = 0; i < sizek; ++i)
        proto.constants.push_back(parseConstant(proto, module));

    uint32_t sizep = readVarInt();
    proto.childProtoIds.reserve(sizep);
    for (uint32_t i = 0; i < sizep; ++i)
        proto.childProtoIds.push_back(readVarInt());

    proto.lineDefined = static_cast<int32_t>(readVarInt());
    proto.debugNameString = readStringRef(module);

    uint8_t hasLineInfo = readU8();
    if (hasLineInfo)
        parseLineInfo(proto);

    uint8_t hasDebugInfo = readU8();
    if (hasDebugInfo)
        parseDebugInfo(proto, module);

    if (module.version >= 11)
    {
        uint32_t feedbackVecSize = readVarInt();
        proto.feedbackVecSize = feedbackVecSize;
        for (uint32_t i = 0; i < feedbackVecSize; ++i)
        {
            readU8();       // slot type (LFT_CALLTARGET == 0 currently)
            readVarInt();    // pc
        }
    }

    if (module.version >= 12)
    {
        if (proto.flags & ProtoFlag_Inlinable)
            readVarInt64(); // inlining cost estimate, not needed for decompilation

        // Forward-compatibility resync: each proto's on-disk size is known
        // up front (protoSize), so we can always land exactly on the next
        // proto's start even if a future bytecode revision adds fields
        // this reader doesn't know about yet.
        if (hasProtoSize)
            offset_ = protoStartOffset + protoSize;
    }

    return proto;
}

void BytecodeReader::decodeInstructions(Proto& proto, const std::vector<uint32_t>& words)
{
    proto.pcToInsnIndex.assign(words.size() + 1, -1);

    size_t pc = 0;
    while (pc < words.size())
    {
        uint32_t word = words[pc];
        uint8_t opByte = insnOp(word);
        const OpInfo& info = opInfo(opByte);

        Instruction insn;
        insn.pc = static_cast<uint32_t>(pc);
        insn.op = opByte < kOpCount ? static_cast<Op>(opByte) : Op::NOP;
        insn.a = insnA(word);

        if (info.mode == Mode::ABC)
        {
            insn.b = insnB(word);
            insn.c = insnC(word);
        }
        else if (info.mode == Mode::AD)
        {
            insn.d = insnD(word);
        }
        else // Mode::E
        {
            insn.e = insnE(word);
        }

        insn.wordCount = info.hasAux ? 2 : 1;
        if (info.hasAux)
        {
            if (pc + 1 >= words.size())
                throw BytecodeReadError("instruction at pc=" + std::to_string(pc) + " expects an AUX word past end of code");
            insn.aux = words[pc + 1];
            insn.hasAux = true;
        }

        size_t insnIndex = proto.instructions.size();
        proto.pcToInsnIndex[pc] = static_cast<int32_t>(insnIndex);
        proto.instructions.push_back(insn);

        pc += insn.wordCount;
    }
    // Trailing slot: pc == words.size() means "fell off the end" (valid
    // target for e.g. a loop-exit jump that lands exactly past the last
    // instruction); pcToInsnIndex is sized words.size()+1 with that final
    // slot left at -1, resolved on demand via Proto::resolveTarget().

    // Second pass: resolve jump targets now that pc->index mapping exists.
    for (Instruction& insn : proto.instructions)
    {
        bool isJump = false;
        int32_t offsetWords = 0;
        int64_t base = static_cast<int64_t>(insn.pc) + 1; // default base for D/E-encoded jumps
        switch (insn.op)
        {
        case Op::JUMP:
        case Op::JUMPBACK:
        case Op::JUMPIF:
        case Op::JUMPIFNOT:
        case Op::JUMPIFEQ:
        case Op::JUMPIFLE:
        case Op::JUMPIFLT:
        case Op::JUMPIFNOTEQ:
        case Op::JUMPIFNOTLE:
        case Op::JUMPIFNOTLT:
        case Op::FORNPREP:
        case Op::FORNLOOP:
        case Op::FORGLOOP:
        case Op::FORGPREP_INEXT:
        case Op::FORGPREP_NEXT:
        case Op::FORGPREP:
        case Op::JUMPXEQKNIL:
        case Op::JUMPXEQKB:
        case Op::JUMPXEQKN:
        case Op::JUMPXEQKS:
        case Op::CMPPROTO:
            isJump = true;
            offsetWords = insn.d;
            break;
        case Op::JUMPX:
            isJump = true;
            offsetWords = insn.e;
            break;
        case Op::FASTCALL:
        case Op::FASTCALL1:
        case Op::FASTCALL2:
        case Op::FASTCALL2K:
        case Op::FASTCALL3:
            // ABC-mode: the jump distance lives in C, not D, and it jumps
            // over the fallback call sequence *and* the CALL itself when
            // the fast path is taken inline. Unlike the D-encoded jump
            // family, this offset is relative to pc+wordCount (empirically
            // verified: it lands exactly past the fallback CALL
            // instruction, matching the documented "jumps over the
            // instructions and over the next CALL" behavior).
            isJump = true;
            offsetWords = insn.c;
            base = static_cast<int64_t>(insn.pc) + insn.wordCount;
            break;
        default:
            break;
        }
        if (isJump)
        {
            // Jump offsets for the D/E-encoded family are relative to the
            // word right after the header word (pc+1), even for 2-word AUX
            // instructions: the compiler already folds the "skip the AUX
            // word" adjustment into the encoded D/E value itself, so the
            // reader must NOT additionally offset by wordCount there.
            // (Verified empirically against luau-compile's own --text
            // output for both families; see commit message.)
            int64_t targetPc = base + offsetWords;
            if (targetPc < 0 || static_cast<uint64_t>(targetPc) > proto.totalWordCount)
                throw BytecodeReadError("jump target out of range at pc=" + std::to_string(insn.pc));
            insn.jumpTarget = proto.resolveTarget(static_cast<uint32_t>(targetPc));
        }
    }
}

// ---------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------

Constant BytecodeReader::parseConstant(Proto& proto, Module& module)
{
    Constant c;
    uint8_t tag = readU8();
    c.tag = static_cast<ConstTag>(tag);

    switch (c.tag)
    {
    case ConstTag::Nil:
        break;
    case ConstTag::Boolean:
        c.boolValue = readU8() != 0;
        break;
    case ConstTag::Number:
        c.numberValue = readF64();
        break;
    case ConstTag::Vector:
        c.vecValue[0] = readF32();
        c.vecValue[1] = readF32();
        c.vecValue[2] = readF32();
        c.vecValue[3] = readF32();
        break;
    case ConstTag::String:
        c.stringIndex = readStringRef(module);
        break;
    case ConstTag::Import:
        c.importId = readU32(); // fixed-width, NOT a varint
        break;
    case ConstTag::Table:
    {
        uint32_t keys = readVarInt();
        c.tableShape.reserve(keys);
        for (uint32_t i = 0; i < keys; ++i)
        {
            uint32_t key = readVarInt();
            c.tableShape.push_back(TableShapeEntry{static_cast<int32_t>(key), -1});
        }
        break;
    }
    case ConstTag::TableWithConstants:
    {
        uint32_t keys = readVarInt();
        c.tableShape.reserve(keys);
        for (uint32_t i = 0; i < keys; ++i)
        {
            uint32_t key = readVarInt();
            int32_t constantIdx = static_cast<int32_t>(readU32()); // fixed-width int32
            c.tableShape.push_back(TableShapeEntry{static_cast<int32_t>(key), constantIdx});
        }
        break;
    }
    case ConstTag::Closure:
        c.closureProto = readVarInt();
        break;
    case ConstTag::ClassShape:
    {
        uint32_t nameIdx = readVarInt();
        c.classShape.classNameConstant = static_cast<int32_t>(nameIdx);
        c.classShape.numProperties = readVarInt();
        c.classShape.numMethods = readVarInt();
        uint32_t total = c.classShape.numProperties + c.classShape.numMethods;
        c.classShape.memberNameConstants.reserve(total);
        for (uint32_t i = 0; i < total; ++i)
            c.classShape.memberNameConstants.push_back(readVarInt());
        break;
    }
    case ConstTag::Integer:
    {
        uint8_t negative = readU8();
        uint64_t magnitude = readVarInt64();
        c.integerValue = negative ? static_cast<int64_t>(~magnitude + 1) : static_cast<int64_t>(magnitude);
        break;
    }
    default:
        throw BytecodeReadError("unknown constant tag " + std::to_string(tag) + " in proto " + std::to_string(proto.selfIndex));
    }

    return c;
}

// ---------------------------------------------------------------------
// Line info
// ---------------------------------------------------------------------

void BytecodeReader::parseLineInfo(Proto& proto)
{
    proto.hasLineInfo = true;
    proto.lineGapLog2 = readU8();

    uint32_t sizecode = proto.totalWordCount;
    proto.lineInfoDeltas.resize(sizecode);
    uint8_t last = 0;
    for (uint32_t i = 0; i < sizecode; ++i)
    {
        last = static_cast<uint8_t>(last + readU8());
        proto.lineInfoDeltas[i] = last;
    }

    uint32_t intervals = ((sizecode - 1) >> proto.lineGapLog2) + 1;
    proto.lineInfoAbs.resize(intervals);
    int32_t lastLine = 0;
    for (uint32_t i = 0; i < intervals; ++i)
    {
        lastLine += static_cast<int32_t>(readU32());
        proto.lineInfoAbs[i] = lastLine;
    }
}

// ---------------------------------------------------------------------
// Debug info (locals / upvalue names)
// ---------------------------------------------------------------------

void BytecodeReader::parseDebugInfo(Proto& proto, Module& module)
{
    proto.hasDebugInfo = true;

    uint32_t sizelocvars = readVarInt();
    proto.locals.reserve(sizelocvars);
    for (uint32_t i = 0; i < sizelocvars; ++i)
    {
        LocalVarInfo lv;
        lv.nameString = readStringRef(module);
        lv.startpc = readVarInt();
        lv.endpc = readVarInt();
        lv.reg = readU8();
        proto.locals.push_back(lv);
    }

    uint32_t sizeupvalues = readVarInt();
    proto.upvalNames.reserve(sizeupvalues);
    for (uint32_t i = 0; i < sizeupvalues; ++i)
    {
        UpvalInfo uv;
        uv.nameString = readStringRef(module);
        proto.upvalNames.push_back(uv);
    }
}

} // namespace luaudec
