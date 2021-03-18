/***************************************************************************************************
  Zyan Disassembler Library (Zydis)
  Original Author : Florian Bernd
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
***************************************************************************************************/

/**
 * @file
 * @brief   Demonstrates basic hooking functionality of the `ZydisFormatter`
 * class by implementing a custom symbol-resolver.
 */

#define NDEBUG

#include "pch.h"

#include "Zycore/Format.h"
#include "Zycore/LibC.h"
#include "Zydis/Zydis.h"

#pragma comment(lib, "Zydis.lib")
#pragma comment(lib, "Zycore.lib")

//
// Global Variables
//
extern UINT32 g_DisassemblerSyntax;

/**
 * @brief   Defines the `ZydisSymbol` struct.
 */
typedef struct ZydisSymbol_ {
  /**
   * @brief   The symbol address.
   */
  ZyanU64 address;
  /**
   * @brief   The symbol name.
   */
  const char *name;
} ZydisSymbol;

/**
 * @brief   A static symbol table with some dummy symbols.
 */
static const ZydisSymbol SYMBOL_TABLE[3] = {
    {0x007FFFFFFF401000, "SomeModule.EntryPoint"},
    {0x007FFFFFFF530040, "SomeModule.SomeData"},
    {0x007FFFFFFF401100, "SomeModule.SomeFunction"}};

ZydisFormatterFunc default_print_address_absolute;

/**
 * @brief Print addresses
 *
 * @param formatter
 * @param buffer
 * @param context
 * @return ZyanStatus
 */
static ZyanStatus
ZydisFormatterPrintAddressAbsolute(const ZydisFormatter *formatter,
                                   ZydisFormatterBuffer *buffer,
                                   ZydisFormatterContext *context) {
  ZyanU64 address;
  ZYAN_CHECK(ZydisCalcAbsoluteAddress(context->instruction, context->operand,
                                      context->runtime_address, &address));

  for (ZyanUSize i = 0; i < ZYAN_ARRAY_LENGTH(SYMBOL_TABLE); ++i) {
    if (SYMBOL_TABLE[i].address == address) {
      ZYAN_CHECK(ZydisFormatterBufferAppend(buffer, ZYDIS_TOKEN_SYMBOL));
      ZyanString *string;
      ZYAN_CHECK(ZydisFormatterBufferGetString(buffer, &string));
      return ZyanStringAppendFormat(string, "<%s>", SYMBOL_TABLE[i].name);
    }
  }

  return default_print_address_absolute(formatter, buffer, context);
}

/**
 * @brief Disassemble a user-mode buffer
 *
 * @param decoder
 * @param runtime_address
 * @param data
 * @param length
 */
void DisassembleBuffer(ZydisDecoder *decoder, ZyanU64 runtime_address,
                       ZyanU8 *data, ZyanUSize length, uint32_t maximum_instr) {
  ZydisFormatter formatter;
  int instr_decoded = 0;

  if (g_DisassemblerSyntax == 1) {
    ZydisFormatterInit(&formatter, ZYDIS_FORMATTER_STYLE_INTEL);
  } else if (g_DisassemblerSyntax == 2) {
    ZydisFormatterInit(&formatter, ZYDIS_FORMATTER_STYLE_ATT);
  } else if (g_DisassemblerSyntax == 3) {
    ZydisFormatterInit(&formatter, ZYDIS_FORMATTER_STYLE_INTEL_MASM);
  } else {
    ShowMessages("err, in selecting disassembler syntax\n");
    return;
  }

  ZydisFormatterSetProperty(&formatter, ZYDIS_FORMATTER_PROP_FORCE_SEGMENT,
                            ZYAN_TRUE);
  ZydisFormatterSetProperty(&formatter, ZYDIS_FORMATTER_PROP_FORCE_SIZE,
                            ZYAN_TRUE);

  //
  // Replace the `ZYDIS_FORMATTER_FUNC_PRINT_ADDRESS_ABS` function that formats
  // the absolute addresses
  //
  default_print_address_absolute =
      (ZydisFormatterFunc)&ZydisFormatterPrintAddressAbsolute;
  ZydisFormatterSetHook(&formatter, ZYDIS_FORMATTER_FUNC_PRINT_ADDRESS_ABS,
                        (const void **)&default_print_address_absolute);

  ZydisDecodedInstruction instruction;
  char buffer[256];
  while (ZYAN_SUCCESS(
      ZydisDecoderDecodeBuffer(decoder, data, length, &instruction))) {

    // ZYAN_PRINTF("%016" PRIX64 "  ", runtime_address);
    ShowMessages("%s   ", SeparateTo64BitValue(runtime_address).c_str());
    //
    // We have to pass a `runtime_address` different to
    // `ZYDIS_RUNTIME_ADDRESS_NONE` to enable printing of absolute addresses
    //
    ZydisFormatterFormatInstruction(&formatter, &instruction, &buffer[0],
                                    sizeof(buffer), runtime_address);

    //
    // Show the memory for this instruction
    //
    for (size_t i = 0; i < instruction.length; i++) {
      ZyanU8 MemoryContent = data[i];
      ShowMessages(" %02X", MemoryContent);
    }
    //
    // Add padding (we assume that each instruction should be at least 10 bytes)
    //
#define PaddingLength 12
    if (instruction.length < PaddingLength) {
      for (size_t i = 0; i < PaddingLength - instruction.length; i++) {
        ShowMessages("   ");
      }
    }
    ShowMessages(" %s\n", &buffer[0]);

    data += instruction.length;
    length -= instruction.length;
    runtime_address += instruction.length;
    instr_decoded++;

    if (instr_decoded == maximum_instr) {
      return;
    }
  }
}

/**
 * @brief Zydis test
 *
 * @return int
 */
int ZydisTest() {
  if (ZydisGetVersion() != ZYDIS_VERSION) {
    fputs("Invalid zydis version\n", ZYAN_STDERR);
    return EXIT_FAILURE;
  }

  ZyanU8 data[] = {
      0x48, 0x8B, 0x05, 0x39, 0x00,
      0x13, 0x00, // mov rax, qword ptr ds:[<SomeModule.SomeData>]
      0x50,       // push rax
      0xFF, 0x15, 0xF2, 0x10, 0x00,
      0x00,       // call qword ptr ds:[<SomeModule.SomeFunction>]
      0x85, 0xC0, // test eax, eax
      0x0F, 0x84, 0x00, 0x00, 0x00,
      0x00,                        // jz 0x007FFFFFFF400016
      0xE9, 0xE5, 0x0F, 0x00, 0x00 // jmp <SomeModule.EntryPoint>
  };

  ZydisDecoder decoder;
  ZydisDecoderInit(&decoder, ZYDIS_MACHINE_MODE_LONG_64,
                   ZYDIS_ADDRESS_WIDTH_64);

  DisassembleBuffer(&decoder, 0x007FFFFFFF400000, &data[0], sizeof(data),
                    0xffffffff);

  return 0;
}

/**
 * @brief Disassemble x64 assemblies
 *
 * @param BufferToDisassemble buffer to disassemble
 * @param BaseAddress the base address of assembly
 * @param Size size of buffer
 * @param MaximumInstrDecoded maximum instructions to decode, 0 means all
 * possible
 * @return int
 */
int HyperDbgDisassembler64(unsigned char *BufferToDisassemble,
                           UINT64 BaseAddress, UINT64 Size,
                           UINT32 MaximumInstrDecoded) {
  if (ZydisGetVersion() != ZYDIS_VERSION) {
    fputs("Invalid zydis version\n", ZYAN_STDERR);
    return EXIT_FAILURE;
  }

  ZydisDecoder decoder;
  ZydisDecoderInit(&decoder, ZYDIS_MACHINE_MODE_LONG_64,
                   ZYDIS_ADDRESS_WIDTH_64);

  DisassembleBuffer(&decoder, BaseAddress, &BufferToDisassemble[0], Size,
                    MaximumInstrDecoded);

  return 0;
}

/**
 * @brief Disassemble 32 bit assemblies
 *
 * @param BufferToDisassemble buffer to disassemble
 * @param BaseAddress the base address of assembly
 * @param Size size of buffer
 * @param MaximumInstrDecoded maximum instructions to decode, 0 means all
 * possible
 * @return int
 */
int HyperDbgDisassembler32(unsigned char *BufferToDisassemble,
                           UINT64 BaseAddress, UINT64 Size,
                           UINT32 MaximumInstrDecoded) {
  if (ZydisGetVersion() != ZYDIS_VERSION) {
    fputs("Invalid zydis version\n", ZYAN_STDERR);
    return EXIT_FAILURE;
  }

  ZydisDecoder decoder;
  ZydisDecoderInit(&decoder, ZYDIS_MACHINE_MODE_LONG_COMPAT_32,
                   ZYDIS_ADDRESS_WIDTH_32);

  DisassembleBuffer(&decoder, (UINT32)BaseAddress, &BufferToDisassemble[0],
                    Size, MaximumInstrDecoded);

  return 0;
}

/**
 * @brief Check whether the jump is taken or not taken (in debugger)
 *
 * @param BufferToDisassemble Current Bytes of assembly
 * @param BuffLength Length of buffer
 * @param Rflag The kernel's currnet RFLAG
 * @param Isx86_64 Whether it's an x86 or x64
 *
 * @return DEBUGGER_NEXT_INSTRUCTION_FINDER_STATUS
 */
DEBUGGER_CONDITIONAL_JUMP_STATUS
HyperDbgIsConditionalJumpTaken(unsigned char *BufferToDisassemble,
                               UINT64 BuffLength, RFLAGS Rflag,
                               BOOLEAN Isx86_64) {

  ZydisDecoder decoder;
  ZydisFormatter formatter;
  UINT64 CurrentRip = 0;
  int instr_decoded = 0;
  ZydisDecodedInstruction instruction;
  char buffer[256];
  UINT32 MaximumInstrDecoded = 1;

  if (ZydisGetVersion() != ZYDIS_VERSION) {
    ShowMessages("Invalid zydis version\n");
    return DEBUGGER_CONDITIONAL_JUMP_STATUS_ERROR;
  }

  if (Isx86_64) {
    ZydisDecoderInit(&decoder, ZYDIS_MACHINE_MODE_LONG_64,
                     ZYDIS_ADDRESS_WIDTH_64);
  } else {
    ZydisDecoderInit(&decoder, ZYDIS_MACHINE_MODE_LONG_COMPAT_32,
                     ZYDIS_ADDRESS_WIDTH_32);
  }

  ZydisFormatterInit(&formatter, ZYDIS_FORMATTER_STYLE_INTEL);

  ZydisFormatterSetProperty(&formatter, ZYDIS_FORMATTER_PROP_FORCE_SEGMENT,
                            ZYAN_TRUE);
  ZydisFormatterSetProperty(&formatter, ZYDIS_FORMATTER_PROP_FORCE_SIZE,
                            ZYAN_TRUE);

  //
  // Replace the `ZYDIS_FORMATTER_FUNC_PRINT_ADDRESS_ABS` function that formats
  // the absolute addresses
  //
  default_print_address_absolute =
      (ZydisFormatterFunc)&ZydisFormatterPrintAddressAbsolute;
  ZydisFormatterSetHook(&formatter, ZYDIS_FORMATTER_FUNC_PRINT_ADDRESS_ABS,
                        (const void **)&default_print_address_absolute);

  while (ZYAN_SUCCESS(ZydisDecoderDecodeBuffer(&decoder, BufferToDisassemble,
                                               BuffLength, &instruction))) {

    //
    // We have to pass a `runtime_address` different to
    // `ZYDIS_RUNTIME_ADDRESS_NONE` to enable printing of absolute addresses
    //

    /* ZydisFormatterFormatInstruction(&formatter, &instruction, &buffer[0],
                                       sizeof(buffer), (ZyanU64)CurrentRip); */


    //
    // Add padding (we assume that each instruction should be at least 10 bytes)
    //

    ShowMessages("Buffer is : %s\n", &buffer[0]);

    BufferToDisassemble += instruction.length;
    BuffLength -= instruction.length;
    CurrentRip += instruction.length;
    instr_decoded++;

    if (instr_decoded == MaximumInstrDecoded) {
      return DEBUGGER_CONDITIONAL_JUMP_STATUS_NOT_CONDITIONAL_JUMP;
    }
  }

  return DEBUGGER_CONDITIONAL_JUMP_STATUS_ERROR;
}
