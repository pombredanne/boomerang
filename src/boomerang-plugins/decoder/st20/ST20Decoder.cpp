#pragma region License
/*
 * This file is part of the Boomerang Decompiler.
 *
 * See the file "LICENSE.TERMS" for information on usage and
 * redistribution of this file, and for a DISCLAIMER OF ALL
 * WARRANTIES.
 */
#pragma endregion License
#include "ST20Decoder.h"

#include "boomerang/core/Project.h"
#include "boomerang/core/Settings.h"
#include "boomerang/db/Prog.h"
#include "boomerang/ssl/statements/BranchStatement.h"
#include "boomerang/ssl/statements/CallStatement.h"
#include "boomerang/ssl/statements/ReturnStatement.h"
#include "boomerang/util/log/Log.h"

#include <cassert>
#include <cstring>


#define ST20_INS_J 0
#define ST20_INS_LDLP 1

#define ST20_INS_LDNL 3
#define ST20_INS_LDC 4
#define ST20_INS_LDNLP 5

#define ST20_INS_LDL 7
#define ST20_INS_ADC 8
#define ST20_INS_CALL 9
#define ST20_INS_CJ 10
#define ST20_INS_AJW 11
#define ST20_INS_EQC 12
#define ST20_INS_STL 13
#define ST20_INS_STNL 14

#define OPR_MASK (1 << 16)
#define OPR_SIGN (1 << 17)


static const char *functionNames[] = {
    "j",     //  0
    "ldlp",  //  1
    "pfix",  //  2
    "ldnl",  //  3
    "ldc",   //  4
    "ldnlp", //  5
    "nfix",  //  6
    "ldl",   //  7
    "adc",   //  8
    "call",  //  9
    "cj",    // 10
    "ajw",   // 11
    "eqc",   // 12
    "stl",   // 13
    "stnl",  // 14
    "opr"    // 15
};


ST20Decoder::ST20Decoder(Project *project)
    : IDecoder(project)
    , m_rtlDict(project->getSettings()->debugDecoder)
{
    const Settings *settings = project->getSettings();
    QString realSSLFileName;

    if (!settings->sslFileName.isEmpty()) {
        realSSLFileName = settings->getWorkingDirectory().absoluteFilePath(settings->sslFileName);
    }
    else {
        realSSLFileName = settings->getDataDirectory().absoluteFilePath("ssl/st20.ssl");
    }

    if (!m_rtlDict.readSSLFile(realSSLFileName)) {
        LOG_ERROR("Cannot read SSL file '%1'", realSSLFileName);
        throw std::runtime_error("Cannot read SSL file");
    }
}


bool ST20Decoder::initialize(Project *project)
{
    m_prog = project->getProg();
    return true;
}


bool ST20Decoder::decodeInstruction(Address pc, ptrdiff_t delta, MachineInstruction &result)
{
    int total     = 0; // Total value from all prefixes
    result.m_size = 0;

    while (true) {
        const Byte instructionData = Util::readByte(
            (const void *)(pc.value() + delta + result.m_size));
        const Byte functionCode = (instructionData >> 4) & 0xF;
        const Byte oper         = instructionData & 0xF;

        result.m_size++;

        switch (functionCode) {
        case ST20_INS_J: { // unconditional jump
            total += oper;
            const Address jumpDest = pc + result.m_size + total;

            result.m_addr   = pc;
            result.m_id     = ST20_INS_J;
            result.m_valid  = true;
            result.m_iclass = IClass::NOP;

            std::strcpy(result.m_mnem.data(), "j");
            std::snprintf(result.m_opstr.data(), result.m_opstr.size(), "0x%lx", jumpDest.value());
            result.m_operands.push_back(Const::get(jumpDest));
            result.m_variantID = "J";
        } break;

        case ST20_INS_LDLP:
        case ST20_INS_LDNL:
        case ST20_INS_LDC:
        case ST20_INS_LDNLP:
        case ST20_INS_LDL:
        case ST20_INS_ADC:
        case ST20_INS_AJW:
        case ST20_INS_EQC:
        case ST20_INS_STL:
        case ST20_INS_STNL: {
            total += oper;

            result.m_addr   = pc;
            result.m_id     = functionCode;
            result.m_valid  = true;
            result.m_iclass = IClass::NOP;

            std::strcpy(result.m_mnem.data(), functionNames[functionCode]);
            std::snprintf(result.m_opstr.data(), result.m_opstr.size(), "0x%x", total);

            result.m_operands.push_back(Const::get(total));
            result.m_variantID = QString(functionNames[functionCode]).toUpper();
        } break;

        case 2: { // prefix
            total = (total + oper) << 4;
            continue;
        }
        case 6: { // negative prefix
            total = (total + ~oper) << 4;
            continue;
        }

        case ST20_INS_CALL: { // call
            total += oper;
            const Address callDest = Address(pc + result.m_size + total);

            result.m_addr   = pc;
            result.m_id     = ST20_INS_CALL;
            result.m_valid  = true;
            result.m_iclass = IClass::NOP;

            std::strcpy(result.m_mnem.data(), "call");
            std::snprintf(result.m_opstr.data(), result.m_opstr.size(), "0x%lx", callDest.value());

            result.m_operands.push_back(Const::get(callDest));
            result.m_variantID = "CALL";
        } break;

        case ST20_INS_CJ: { // cond jump
            total += oper;
            const Address jumpDest = pc + result.m_size + total;

            result.m_addr   = pc;
            result.m_id     = ST20_INS_CJ;
            result.m_valid  = true;
            result.m_iclass = IClass::NOP;

            std::strcpy(result.m_mnem.data(), "cj");
            std::snprintf(result.m_opstr.data(), result.m_opstr.size(), "0x%lx", jumpDest.value());

            result.m_operands.push_back(Const::get(jumpDest));
            result.m_variantID = "CJ";
        } break;

        case 15: { // operate
            total += oper;
            const char *insnName = getInstructionName(total);
            if (!insnName) {
                // invalid or unknown instruction
                result.m_valid = false;
                return false;
            }

            result.m_addr = pc;
            result.m_id   = OPR_MASK |
                          (total > 0 ? total : ((~total & ~0xF) | (total & 0xF) | OPR_SIGN));
            result.m_valid  = true;
            result.m_iclass = IClass::NOP;

            std::strcpy(result.m_mnem.data(), insnName);
            std::strcpy(result.m_opstr.data(), "");
            result.m_variantID = QString(insnName).toUpper();
        } break;

        default: assert(false);
        }

        break;
    }

    return result.m_valid;
}


bool ST20Decoder::liftInstruction(const MachineInstruction &insn, DecodeResult &lifted)
{
    lifted.iclass   = IClass::NOP;
    lifted.reDecode = false;
    lifted.rtl      = instantiateRTL(insn);

    return lifted.valid();
}


const char *ST20Decoder::getInstructionName(int prefixTotal) const
{
    if (prefixTotal >= 0) {
        switch (prefixTotal) {
        case 0x00: return "rev";
        case 0x01: return "lb";
        case 0x02: return "bsub";
        case 0x03: return "endp";
        case 0x04: return "diff";
        case 0x05: return "add";
        case 0x06: return "gcall";
        case 0x07: return "in";
        case 0x08: return "prod";
        case 0x09: return "gt";
        case 0x0A: return "wsub";
        case 0x0B: return "out";
        case 0x0C: return "sub";
        case 0x0D: return "startp";
        case 0x0E: return "outbyte";
        case 0x0F: return "outword";
        case 0x10: return "seterr";
        case 0x12: return "resetch";
        case 0x13: return "csub0";
        case 0x15: return "stopp";
        case 0x16: return "ladd";
        case 0x17: return "stlb";
        case 0x18: return "sthf";
        case 0x19: return "norm";
        case 0x1A: return "ldiv";
        case 0x1B: return "ldpi";
        case 0x1C: return "stlf";
        case 0x1D: return "xdble";
        case 0x1E: return "ldpri";
        case 0x1F: return "rem";
        case 0x20: return "ret";
        case 0x21: return "lend";
        case 0x22: return "ldtimer";
        case 0x29: return "testerr";
        case 0x2A: return "testpranal";
        case 0x2B: return "tin";
        case 0x2C: return "div";
        case 0x2E: return "dist";
        case 0x2F: return "disc";
        case 0x30: return "diss";
        case 0x31: return "lmul";
        case 0x32: return "not";
        case 0x33: return "xor";
        case 0x34: return "bcnt";
        case 0x35: return "lshr";
        case 0x36: return "lshl";
        case 0x37: return "lsum";
        case 0x38: return "lsub";
        case 0x39: return "runp";
        case 0x3A: return "xword";
        case 0x3B: return "sb";
        case 0x3C: return "gajw";
        case 0x3D: return "savel";
        case 0x3E: return "saveh";
        case 0x3F: return "wcnt";
        case 0x40: return "shr";
        case 0x41: return "shl";
        case 0x42: return "mint";
        case 0x43: return "alt";
        case 0x44: return "altwt";
        case 0x45: return "altend";
        case 0x46: return "and";
        case 0x47: return "enbt";
        case 0x48: return "enbc";
        case 0x49: return "enbs";
        case 0x4A: return "move";
        case 0x4B: return "or";
        case 0x4C: return "csngl";
        case 0x4D: return "ccnt1";
        case 0x4E: return "talt";
        case 0x4F: return "ldiff";
        case 0x50: return "sthb";
        case 0x51: return "taltwt";
        case 0x52: return "sum";
        case 0x53: return "mul";
        case 0x54: return "sttimer";
        case 0x55: return "stoperr";
        case 0x56: return "cword";
        case 0x57: return "clrhalterr";
        case 0x58: return "sethalterr";
        case 0x59: return "testhalterr";
        case 0x5A: return "dup";
        case 0x5B: return "move2dinit";
        case 0x5C: return "move2dall";
        case 0x5D: return "move2dnonzero";
        case 0x5E: return "move2dzero";
        case 0x5F: return "gtu";
        case 0x63: return "unpacksn";
        case 0x64: return "slmul";
        case 0x65: return "sulmul";
        case 0x68: return "satadd";
        case 0x69: return "satsub";
        case 0x6A: return "satmul";
        case 0x6C: return "postnormsn";
        case 0x6D: return "roundsn";
        case 0x6E: return "ldtraph";
        case 0x6F: return "sttraph";
        case 0x71: return "ldinf";
        case 0x72: return "fmul";
        case 0x73: return "cflerr";
        case 0x74: return "crcword";
        case 0x75: return "crcbyte";
        case 0x76: return "bitcnt";
        case 0x77: return "bitrevword";
        case 0x78: return "bitrevnbits";
        case 0x79: return "pop";
        case 0x7E: return "ldmemstartval";
        case 0x81: return "wsubdb";
        case 0x9C: return "fptesterr";
        case 0xB0: return "settimeslice";
        case 0xB8: return "xbword";
        case 0xB9: return "lbx";
        case 0xBA: return "cb";
        case 0xBB: return "cbu";
        case 0xC1: return "ssub";
        case 0xC4: return "intdis";
        case 0xC5: return "intenb";
        case 0xC6: return "ldtrapped";
        case 0xC7: return "cir";
        case 0xC8: return "ss";
        case 0xCA: return "ls";
        case 0xCB: return "sttrapped";
        case 0xCC: return "ciru";
        case 0xCD: return "gintdis";
        case 0xCE: return "gintenb";
        case 0xF0: return "devlb";
        case 0xF1: return "devsb";
        case 0xF2: return "devls";
        case 0xF3: return "devss";
        case 0xF4: return "devlw";
        case 0xF5: return "devsw";
        case 0xF6: return "null";
        case 0xF7: return "null";
        case 0xF8: return "xsword";
        case 0xF9: return "lsx";
        case 0xFA: return "cs";
        case 0xFB: return "csu";
        case 0x17C: return "lddevid";
        }
    }
    else {
        // Total is negative, as a result of nfixes
        prefixTotal = (~prefixTotal & ~0xF) | (prefixTotal & 0xF);

        switch (prefixTotal) {
        case 0x00: return "swapqueue";
        case 0x01: return "swaptimer";
        case 0x02: return "insertqueue";
        case 0x03: return "timeslice";
        case 0x04: return "signal";
        case 0x05: return "wait";
        case 0x06: return "trapdis";
        case 0x07: return "trapenb";
        case 0x0B: return "tret";
        case 0x0C: return "ldshadow";
        case 0x0D: return "stshadow";
        case 0x1F: return "iret";
        case 0x24: return "devmove";
        case 0x2E: return "restart";
        case 0x2F: return "causeerror";
        case 0x30: return "nop";
        case 0x4C: return "stclock";
        case 0x4D: return "ldclock";
        case 0x4E: return "clockdis";
        case 0x4F: return "clockenb";
        case 0x8C: return "ldprodid";
        case 0x8D: return "reboot";
        }
    }

    return nullptr;
}


bool ST20Decoder::isSPARCRestore(const MachineInstruction &) const
{
    return false;
}


std::unique_ptr<RTL> ST20Decoder::instantiateRTL(const MachineInstruction &insn)
{
    // Take the argument, convert it to upper case and remove any .'s
    const QString sanitizedName = QString(insn.m_variantID).remove(".").toUpper();

    if (m_prog && m_prog->getProject()->getSettings()->debugDecoder) {
        OStream q_cout(stdout);
        // Display a disassembly of this instruction if requested
        q_cout << insn.m_addr << ": " << insn.m_variantID << " ";

        for (const SharedExp &itd : insn.m_operands) {
            if (itd->isIntConst()) {
                int val = itd->access<Const>()->getInt();

                if ((val > 100) || (val < -100)) {
                    q_cout << "0x" << QString::number(val, 16);
                }
                else {
                    q_cout << val;
                }
            }
            else {
                itd->print(q_cout);
            }

            q_cout << " ";
        }

        q_cout << '\n';
    }

    return m_rtlDict.instantiateRTL(sanitizedName, insn.m_addr, insn.m_operands);
}


QString ST20Decoder::getRegNameByNum(RegNum regNum) const
{
    return m_rtlDict.getRegDB()->getRegNameByNum(regNum);
}


int ST20Decoder::getRegSizeByNum(RegNum regNum) const
{
    return m_rtlDict.getRegDB()->getRegSizeByNum(regNum);
}


BOOMERANG_DEFINE_PLUGIN(PluginType::Decoder, ST20Decoder, "ST20 decoder plugin", BOOMERANG_VERSION,
                        "Boomerang developers")
