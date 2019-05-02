////////////////////////////////////////////////////////////////////////////////
///
/// @file       main.cpp
///
/// @project
///
/// @brief      Main bcm regiuster tool for decoding BCM5179 registers.
///
////////////////////////////////////////////////////////////////////////////////
///
////////////////////////////////////////////////////////////////////////////////
///
/// @copyright Copyright (c) 2018, Evan Lojewski
/// @cond
///
/// All rights reserved.
///
/// Redistribution and use in source and binary forms, with or without
/// modification, are permitted provided that the following conditions are met:
/// 1. Redistributions of source code must retain the above copyright notice,
/// this list of conditions and the following disclaimer.
/// 2. Redistributions in binary form must reproduce the above copyright notice,
/// this list of conditions and the following disclaimer in the documentation
/// and/or other materials provided with the distribution.
/// 3. Neither the name of the copyright holder nor the
/// names of its contributors may be used to endorse or promote products
/// derived from this software without specific prior written permission.
///
////////////////////////////////////////////////////////////////////////////////
///
/// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
/// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
/// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
/// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
/// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
/// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
/// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
/// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
/// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
/// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
/// POSSIBILITY OF SUCH DAMAGE.
/// @endcond
////////////////////////////////////////////////////////////////////////////////
#include "HAL.hpp"

#include <NVRam.h>
#include <MII.h>
#include <bcm5719_eeprom.h>
#include <dirent.h>
#include <endian.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <OptionParser.h>
#include <vector>
#include <string>
#include <iostream>
#include <bcm5719_GEN.h>
#include <APE.h>
#include <bcm5719_SHM.h>
#include <bcm5719_SHM_CHANNEL0.h>
#include <elfio/elfio.hpp>

#include <APE_FILTERS.h>
#include <APE_NVIC.h>
#include <APE_APE_PERI.h>
#include <APE_TX_PORT.h>

#include <Ethernet.h>
#include <NCSI.h>

#include "../NVRam/bcm5719_NVM.h"

using namespace std;
using namespace ELFIO;
using optparse::OptionParser;

elfio gELFIOReader;

uint8_t ping_packet[] = {
    0x00, 0x0c, 0x29, 0x64, 0x66, 0xe2, 0x2c, 0x09, 0x4d, 0x00, 0x01, 0x4a, 0x08, 0x00, 0x45, 0x00,
    0x00, 0x54, 0x12, 0xe1, 0x40, 0x00, 0x40, 0x01, 0x40, 0xbb, 0xc0, 0xa8, 0x20, 0x62, 0x04, 0x02,
    0x02, 0x01, 0x08, 0x00, 0xa1, 0xae, 0x15, 0x5f, 0x00, 0x52, 0xdc, 0xf1, 0x63, 0xae, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00,
};
uint32_t ping_packet_len = sizeof(ping_packet);

const char* regnames[32] = {
    "$zero", /* Zero register - always 0 */
    "$at",   /* Assembler register */
    "$v0", "$v1",             /* Results */
    "$a0", "$a1", "$a2", "$a3", /* Aguments */
    "$t0", "$t1", "$t2", "$t3", "$t4", "$t5", "$t6", "$t7", /* Temp, not saved */
    "$s0", "$s1", "$s2", "$s3", "$s4", "$s5", "$s6", "$s7", /* Saved registers */
    "$t8", "$t9", /* Temp, not saved */
    "$k0", "$k1", /* Kernel / OS */
    "$gp", "$sp", "$fp", /* Pointers */
    "$ra", /* return address */
};


void boot_ape_loader()
{
    extern unsigned char apeloader_bin[];
    extern unsigned int apeloader_bin_len;

    int numWords = apeloader_bin_len/4;

    RegAPEMode_t mode;
    mode.r32 = 0;
    mode.bits.Halt = 1;
    mode.bits.FastBoot = 1;
    APE.Mode = mode;

    // We hijack the complete SHM here.


    // load file.
    for(int i = 0; i < numWords; i++)
    {
        SHM.write(0x0B00 + i*4, ((uint32_t*)apeloader_bin)[i]);
    }


    // Mark fw as not read.
    SHM.FwStatus.bits.Ready = 0;
    // Start the file
    APE.GpioMessage.r32 = 0x60220B00|2;

    mode.bits.Halt = 0;
    mode.bits.FastBoot = 1;
    mode.bits.Reset = 1;
    APE.Mode = mode;

    // Wait for ready.
    while(0 == SHM.FwStatus.bits.Ready);
}

uint32_t loader_read_mem(uint32_t addr)
{
    SHM.LoaderArg0.r32 = addr;
    SHM.LoaderCommand.bits.Command = SHM_LOADER_COMMAND_COMMAND_READ_MEM;

    // Wait for command to be handled.
    while(0 != SHM.LoaderCommand.bits.Command);

    return (uint32_t)SHM.LoaderArg0.r32;
}

void loader_write_mem(uint32_t addr, uint32_t value)
{
    SHM.LoaderArg0.r32 = addr;
    SHM.LoaderArg1.r32 = value;
    SHM.LoaderCommand.bits.Command = SHM_LOADER_COMMAND_COMMAND_WRITE_MEM;

    // Wait for command to be handled.
    while(0 != SHM.LoaderCommand.bits.Command);
}

const string symbol_for_address(uint32_t address, uint32_t &offset)
{
    Elf_Half sec_num = gELFIOReader.sections.size();

    for ( int i = 0; i < sec_num; ++i ) {
        section* psec = gELFIOReader.sections[i];
        // Check section type
        if ( psec->get_type() == SHT_SYMTAB ) {
            const symbol_section_accessor symbols( gELFIOReader, psec );
            for ( unsigned int j = 0; j < symbols.get_symbols_num(); ++j ) {
                std::string   name;
                Elf64_Addr    value;
                Elf_Xword     size;
                unsigned char bind;
                unsigned char type;
                Elf_Half      section_index;
                unsigned char other;

                // Read symbol properties
                symbols.get_symbol( j, name, value, size, bind,
                                       type, section_index, other );


                if(value <= address &&
                    value + size > address)
                {
                    offset = address - value;
                    return name;
                }
            }
        }
    }

    return string("unknown");
}

void print_context(void)
{
    uint32_t r[32];
    uint32_t pc;
    uint32_t opcode;

    // Read out the device context.
    pc = DEVICE.RxRiscProgramCounter.r32;
    opcode = DEVICE.RxRiscCurrentInstruction.r32;
    r[0] = DEVICE.RxRiscRegister0.r32;
    r[1] = DEVICE.RxRiscRegister1.r32;
    r[2] = DEVICE.RxRiscRegister2.r32;
    r[3] = DEVICE.RxRiscRegister3.r32;
    r[4] = DEVICE.RxRiscRegister4.r32;
    r[5] = DEVICE.RxRiscRegister5.r32;
    r[6] = DEVICE.RxRiscRegister6.r32;
    r[7] = DEVICE.RxRiscRegister7.r32;
    r[8] = DEVICE.RxRiscRegister8.r32;
    r[9] = DEVICE.RxRiscRegister9.r32;
    r[10] = DEVICE.RxRiscRegister10.r32;
    r[11] = DEVICE.RxRiscRegister11.r32;
    r[12] = DEVICE.RxRiscRegister12.r32;
    r[13] = DEVICE.RxRiscRegister13.r32;
    r[14] = DEVICE.RxRiscRegister14.r32;
    r[15] = DEVICE.RxRiscRegister15.r32;
    r[16] = DEVICE.RxRiscRegister16.r32;
    r[17] = DEVICE.RxRiscRegister17.r32;
    r[18] = DEVICE.RxRiscRegister18.r32;
    r[19] = DEVICE.RxRiscRegister19.r32;
    r[20] = DEVICE.RxRiscRegister20.r32;
    r[21] = DEVICE.RxRiscRegister21.r32;
    r[22] = DEVICE.RxRiscRegister22.r32;
    r[23] = DEVICE.RxRiscRegister23.r32;
    r[24] = DEVICE.RxRiscRegister24.r32;
    r[25] = DEVICE.RxRiscRegister25.r32;
    r[26] = DEVICE.RxRiscRegister26.r32;
    r[27] = DEVICE.RxRiscRegister27.r32;
    r[28] = DEVICE.RxRiscRegister28.r32;
    r[29] = DEVICE.RxRiscRegister29.r32;
    r[30] = DEVICE.RxRiscRegister30.r32;
    r[31] = DEVICE.RxRiscRegister31.r32;

    printf("==== Context ===\n");
    uint32_t sym_offset = 0;
    string symbol = symbol_for_address(pc, sym_offset);
    printf("   pc: 0x%08X (%s+%d)   opcode: 0x%08X \n", pc, symbol.c_str(), sym_offset, opcode);
    int numCols = 4;
    int offset = 32 / numCols;
    for(int i = 0; i < ARRAY_ELEMENTS(r)/4; i++)
    {
        for(int j = 0; j < numCols; j++)
        {
            printf("$%d(%5s): 0x%08X    ", i + j*offset, regnames[i + j*offset], r[i + j*offset]);
        }
        printf("\n");
    }
}

void writeMemory(uint32_t rxAddr, uint32_t value)
{
    cout << "Updating " << std::hex << rxAddr << " to " << std::hex << value << endl;
    // Halt.
    RegDEVICERxRiscMode_t mode;
    mode.r32 = 0;
    mode.bits.Halt = 1;
    DEVICE.RxRiscMode = mode;

    DEVICE.RxRiscMode.print();

    // Save old state that we will clobber so we can restore it afterwards.
    uint32_t oldIP = DEVICE.RxRiscProgramCounter.r32;
    uint32_t oldT6 = DEVICE.RxRiscRegister14.r32;
    uint32_t oldT7 = DEVICE.RxRiscRegister15.r32;

    // Check that the instructions we are expecting to use are correct. This will
    // break if the ROM is different.
    DEVICE.RxRiscProgramCounter.r32 = 0x40000038;


    cout << "PC is now " << (uint32_t)DEVICE.RxRiscProgramCounter.r32 << endl;
    uint32_t iw = DEVICE.RxRiscCurrentInstruction.r32;
    if (iw != 0xADCF0000)
    { // sw $t7, 0($t6)
        fprintf(stderr, "cannot set RX word via forced store because the device has an unknown ROM (got 0x%08X)\n", iw);
        return;
    }

    DEVICE.RxRiscRegister14.r32 = rxAddr;
    DEVICE.RxRiscRegister15.r32 = value;

    mode.bits.SingleStep = 1;
    DEVICE.RxRiscMode = mode;

    // Don't remove this, it creates a small delay which seems to sometimes be
    // necessary.
    uint32_t pc = DEVICE.RxRiscProgramCounter.r32;
    if (pc != 0x4000003C)
    {
        fprintf(stderr, "  bad2 0x%08x\n", pc);
    }

    // Restore.
    DEVICE.RxRiscRegister15.r32 = oldT7;
    DEVICE.RxRiscRegister14.r32 = oldT6;
    DEVICE.RxRiscProgramCounter.r32 = oldIP;

    mode.bits.SingleStep = 0;
    DEVICE.RxRiscMode = mode;
}


uint32_t allocationBlocksNeeded(uint32_t frame_size)
{
    static const uint32_t first_size = 20 * sizeof(uint32_t);
    static const uint32_t remaining_size = 30 * sizeof(uint32_t);
    uint32_t blocks = 1;
    // Block size is 128 bytes, first one has 12 words used, so 20 words available.
    if(frame_size > first_size)
    {
        frame_size -= 20 * sizeof(uint32_t);

        // Remaining blocks can hold 30 words;
        blocks += ((frame_size + remaining_size - 1) / remaining_size);
    }

    printf("%d blocks needed for packet\n", blocks);
    return blocks;
}
int32_t allocateTXBlock(void)
{
#if 1
    int32_t block = -1;

    // Wait for allocator state machine to finish.
    // while(APE_TX_TO_NET_BUFFER_ALLOCATOR_0_STATE_PROCESSING == APE.TxToNetBufferAllocator0.bits.State);

    // Set the alloc bit.
    RegAPETxToNetBufferAllocator0_t alloc;
    alloc.r32 = 0;
    alloc.bits.RequestAllocation = 1;
    APE.TxToNetBufferAllocator0 = alloc;

    // Wait for state machine to finish
    RegAPETxToNetBufferAllocator0_t status;
    do {
        status = APE.TxToNetBufferAllocator0;
    } while(APE_TX_TO_NET_BUFFER_ALLOCATOR_0_STATE_PROCESSING == status.bits.State);

    if(APE_TX_TO_NET_BUFFER_ALLOCATOR_0_STATE_ALLOCATION_OK != status.bits.State)
    {
        block = -1;
        printf("Error: Failed to allocate TX block.\n");
    }
    else
    {
        block = status.bits.Index;
        printf("Allocated block %d\n", block);
    }

    return block;
#else
    static int block = 1;
    int this_block = block;
    block++;
    printf("Allocated block %d\n", this_block);
    return this_block;
#endif
}

uint32_t initFirstBlock(RegTX_PORTOut_t* block, uint32_t length, int32_t blocks, int32_t next_block, uint32_t* packet)
{
    int copy_length;
    int i;
    union {
        uint32_t r32;
        struct {
            uint32_t payload_length:7;
            uint32_t next_block:23;
            uint32_t first:1;
            uint32_t not_last:1;
        } bits;
    } control;

    control.r32 = 0;
    control.bits.next_block = next_block >= 0 ? next_block : 0;
    control.bits.first = 1;

    if(length > ((TX_PORT_OUT_ALL_BLOCK_WORDS - TX_PORT_OUT_ALL_FIRST_PAYLOAD_WORD)*4))
    {
        copy_length = (TX_PORT_OUT_ALL_BLOCK_WORDS - TX_PORT_OUT_ALL_FIRST_PAYLOAD_WORD)*4;
        control.bits.not_last = 1;
    }
    else
    {
        // Last.
        copy_length = length;
        control.bits.not_last = 0;
    }


    // Copy Payload Data.
    int num_words = (copy_length + sizeof(uint32_t) - 1)/sizeof(uint32_t);
    for(i = 0; i < num_words; i++)
    {
        block[TX_PORT_OUT_ALL_FIRST_PAYLOAD_WORD + i].r32 = be32toh(packet[i]);
        printf("Block[%d]: 0x%08X\n", i+TX_PORT_OUT_ALL_FIRST_PAYLOAD_WORD, packet[i]);
    }

    // Pad if too small.
    if(copy_length < ETHERNET_FRAME_MIN)
    {
        copy_length = ETHERNET_FRAME_MIN;
        length = ETHERNET_FRAME_MIN;
    }
    num_words = (copy_length + sizeof(uint32_t) - 1)/sizeof(uint32_t);
    for(; i < num_words; i++)
    {
        // Pad remaining with 0's
        block[TX_PORT_OUT_ALL_FIRST_PAYLOAD_WORD + i].r32 = 0;
        printf("Block[%d]: pad(0)\n", i+TX_PORT_OUT_ALL_FIRST_PAYLOAD_WORD);
    }
    // block[1] = uninitialized;
    block[2].r32 = 0;
    block[TX_PORT_OUT_ALL_FRAME_LEN_WORD].r32 = length;
    block[4].r32 = 0;
    block[5].r32 = 0;
    block[6].r32 = 0;
    block[7].r32 = 0;
    block[8].r32 = 0;
    block[TX_PORT_OUT_ALL_NUM_BLOCKS_WORD].r32 = blocks;
    // block[10] = uninitialized;
    // block[11] = uninitialized;

    for(i = 0; i < 12; i++)
    {
        // printf("Block[%d]: 0x%08X\n", i, (uint32_t)block[i].r32);
    }

    control.bits.payload_length = copy_length;

    printf("Control0: %x\n", control.r32);
    block[TX_PORT_OUT_ALL_CONTROL_WORD].r32 = control.r32;

    length -= control.bits.payload_length;

    return copy_length;
}

uint32_t initAdditionalBlock(RegTX_PORTOut_t* block, int32_t next_block, uint32_t length, uint32_t* packet)
{
    int i;
    union {
        uint32_t r32;
        struct {
            uint32_t payload_length:7;
            uint32_t next_block:23;
            uint32_t first:1;
            uint32_t not_last:1;
        } bits;
    } control;

    control.r32 = 0;
    control.bits.first = 0;
    control.bits.next_block = next_block;

    if(length > ((TX_PORT_OUT_ALL_BLOCK_WORDS - TX_PORT_OUT_ALL_ADDITIONAL_PAYLOAD_WORD)*4))
    {
        control.bits.payload_length = (TX_PORT_OUT_ALL_BLOCK_WORDS - TX_PORT_OUT_ALL_ADDITIONAL_PAYLOAD_WORD)*4;
        control.bits.not_last = 1;
    }
    else
    {
        // Last
        control.bits.payload_length = length;
        control.bits.not_last = 0;
    }

    // block[1] = uninitialized;

    for(i = 0; i < 2; i++)
    {
        // printf("ABlock[%d]: 0x%08X\n", i, (uint32_t)block[i].r32);
    }

    // Copy payload data.
    int num_words = (length + sizeof(uint32_t) - 1)/sizeof(uint32_t);
    for(i = 0; i < num_words; i++)
    {
        block[TX_PORT_OUT_ALL_ADDITIONAL_PAYLOAD_WORD + i].r32 = be32toh(packet[i]);
        printf("ABlock[%d]: 0x%08X\n", i+TX_PORT_OUT_ALL_ADDITIONAL_PAYLOAD_WORD, packet[i]);

    }

    printf("Control: %x\n", control.r32);
    block[TX_PORT_OUT_ALL_CONTROL_WORD].r32 = control.r32;

    length -= control.bits.payload_length;

    return length;
}


void transmitPacket(uint8_t* packet, uint32_t length)
{
    uint32_t* packet_32 = (uint32_t*)packet;
    uint32_t consumed = 0;
    uint32_t blocks = allocationBlocksNeeded(length);
    int total_blocks = blocks;;

    // First block
    int32_t first = allocateTXBlock();
    int32_t next_block = -1;
    if(blocks > 1)
    {
        next_block = allocateTXBlock();
    }
    RegTX_PORTOut_t* block = &TX_PORT.Out[TX_PORT_OUT_ALL_BLOCK_WORDS * first];

    consumed += initFirstBlock(block, length, blocks, next_block, &packet_32[consumed/4]);
    blocks -= 1;
    while(blocks)
    {

        block = &TX_PORT.Out[TX_PORT_OUT_ALL_BLOCK_WORDS * next_block];
        blocks--;
        if(blocks)
        {
            next_block = allocateTXBlock();
            consumed += initAdditionalBlock(block, next_block, length-consumed, &packet_32[consumed/4]);
        }
        else
        {
            initAdditionalBlock(block, 0, length - consumed, &packet_32[consumed/4]);
        }
    }

    int tail = next_block;
    if(next_block == -1)
    {
        tail = first;
    }

    printf("Head: %d, Tail: %d\n", first, tail);

    RegAPETxToNetDoorbellFunc0_t doorbell;
    doorbell.r32 = 0;
    doorbell.bits.Head = first;
    doorbell.bits.Tail = tail;
    doorbell.bits.Length = total_blocks;
    doorbell.print();

    APE.TxToNetDoorbellFunc0 = doorbell;
    APE.TxToNetDoorbellFunc0.print();
    APE.TxToNetBufferReturn0.print();
}

void step(void)
{
    uint32_t oldPC = DEVICE.RxRiscProgramCounter.r32;

    RegDEVICERxRiscMode_t mode;
    mode.r32 = 0;
    mode.bits.SingleStep = 1;
    mode.bits.Halt = 1;
    DEVICE.RxRiscMode = mode;

    // Force a re-load of the next word.
    uint32_t newPC = DEVICE.RxRiscProgramCounter.r32;

    if(oldPC + 4 != newPC)
    {
        // branched. Re-read PC to re-read opcode
        DEVICE.RxRiscProgramCounter.r32 = DEVICE.RxRiscProgramCounter.r32;
    }
}

int main(int argc, char const *argv[])
{
    OptionParser parser = OptionParser().description("BCM Register Utility");

    parser.add_option("--elf")
            .dest("debugfile")
            .metavar("DEBUG_FILE")
            .help("Elf file used for improved context decoding.");

    parser.add_option("-f", "--function")
            .dest("function")
            .type("int")
            .set_default("0")
            .metavar("FUNCTION")
            .help("Read registers from the specified pci function.");

    parser.add_option("-s", "--step")
            .dest("step")
            .set_default("0")
            .action("store_true")
            .help("Single step the CPU.");

    parser.add_option("-t", "--stepto")
            .dest("stepto")
            .metavar("ADDR")
            .help("Single step the CPU.");

    parser.add_option("--halt")
            .dest("halt")
            .set_default("0")
            .action("store_true")
            .help("Halt the CPU.");

    parser.add_option("-pc", "--pc")
            .dest("pc")
            .help("Force the PC to the specified value.");

    parser.add_option("-c", "--context")
            .dest("context")
            .set_default("0")
            .action("store_true")
            .help("Print the current CPU context.");

    parser.add_option("-g", "--run")
            .dest("run")
            .set_default("0")
            .action("store_true")
            .help("Continue CPU execution.");

    parser.add_option("-i", "--info")
            .dest("info")
            .set_default("0")
            .action("store_true")
            .help("Print device information registers.");

    parser.add_option("-a", "--ape")
            .dest("ape")
            .set_default("0")
            .action("store_true")
            .help("Print ape information registers.");

    parser.add_option("-rx", "--rx")
            .dest("rx")
            .set_default("0")
            .action("store_true")
            .help("Print rx information registers.");

    parser.add_option("-tx", "--tx")
            .dest("tx")
            .set_default("0")
            .action("store_true")
            .help("Print tx information registers.");

    parser.add_option("-p", "--apeboot")
            .dest("apeboot")
            .metavar("APE_FILE")
            .help("File to boot on the APE.");


    parser.add_option("-apereset", "--apereset")
            .dest("apereset")
            .set_default("0")
            .action("store_true")
            .help("File to boot on the APE.");

    parser.add_option("-reset", "--reset")
            .dest("reset")
            .set_default("0")
            .action("store_true")
            .help("File to boot on the APE.");

    parser.add_option("-m", "--mii")
            .dest("mii")
            .set_default("0")
            .action("store_true")
            .help("Print MII information registers.");


    optparse::Values options = parser.parse_args(argc, argv);
    vector<string> args = parser.args();


    if(!initHAL(NULL, options.get("function")))
    {
        cerr << "Unable to locate pci device with function " << (int)options.get("function") << endl;
        exit(-1);
    }

    if(options.is_set("debugfile"))
    {
        if(!gELFIOReader.load(options["debugfile"]))
        {
            cerr << "Unablt to read elf file " << options["debugfile"] << endl;
            exit(-1);
        }
    }

    if(options.get("step"))
    {
        do {
            cout << "Stepping...\n";
            step();
            print_context();

        } while(DEVICE.RxRiscProgramCounter.r32 > 0x40000000);
        exit(0);
    }

    if(options.is_set("stepto"))
    {
        uint32_t addr = stoi(options["stepto"], nullptr, 0);
        do {
            cout << "Stepping...\n";

            step();
            print_context();

        } while(DEVICE.RxRiscProgramCounter.r32 != addr);
        exit(0);

    }



    if(options.get("halt"))
    {
        cout << "Halting...\n";
        RegDEVICERxRiscMode_t mode;
        mode.r32 = 0;
        mode.bits.Halt = 1;
        DEVICE.RxRiscMode = mode;

        print_context();
        exit(0);
    }

    if(options.is_set("pc"))
    {
        uint32_t pc = stoi(options["pc"], nullptr, 0);
        cout << "Updating PC to " << std::hex << pc << endl;
        RegDEVICERxRiscMode_t mode;
        mode.r32 = 0;
        mode.bits.Halt = 1;
        DEVICE.RxRiscMode = mode;

        DEVICE.RxRiscProgramCounter.r32 = pc;
        print_context();
        exit(0);
    }

    if(options.get("context"))
    {
        print_context();
        exit(0);
    }

    if(options.get("run"))
    {
        cout << "Running...\n";
        RegDEVICERxRiscMode_t mode;
        mode.r32 = 0; // Ensure single-step and halt are cleared
        DEVICE.RxRiscMode = mode;
        exit(0);
    }

    if(options.get("mii"))
    {
        uint8_t phy = MII_getPhy();

        printf("MII Phy:          %d\n", phy);
                printf("MII Status:       0x%04X\n", MII_readRegister(phy, (mii_reg_t)REG_MII_STATUS));
        printf("MII PHY ID[high]: 0x%04X\n", MII_readRegister(phy, (mii_reg_t)REG_MII_PHY_ID_HIGH));
        printf("MII PHY ID[low]:  0x%04X\n", MII_readRegister(phy, (mii_reg_t)REG_MII_PHY_ID_LOW));

        RegMIIControl_t control;
        control.r16 = MII_readRegister(phy, (mii_reg_t)REG_MII_CONTROL);
        control.print();

        RegMIIAutonegotiationAdvertisement_t auto_neg_advert;
        auto_neg_advert.r16 = MII_readRegister(phy, (mii_reg_t)REG_MII_AUTONEGOTIATION_ADVERTISEMENT);
        auto_neg_advert.print();

        RegMII1000baseTControl_t gig_control;
        gig_control.r16 = MII_readRegister(phy, (mii_reg_t)REG_MII_1000BASE_T_CONTROL);
        gig_control.print();

        RegMIISpareControl3_t sc3;
        sc3.r16 = MII_readRegister(phy, (mii_reg_t)REG_MII_SPARE_CONTROL_3);
        sc3.print();

        exit(0);
    }

    if(options.is_set("apeboot"))
    {
        boot_ape_loader();

        int fileLength = 0;
        int fileWords = 0;
        #define NVRAM_SIZE      (1024u * 256u) /* 256KB */

        union {
            uint8_t         bytes[NVRAM_SIZE];
            uint32_t        words[NVRAM_SIZE/4];
        } ape;

        string &file = options["apeboot"];

        fstream infile;
        infile.open(file, fstream::in | fstream::binary);
        if(infile.is_open())
        {
            // get length of file:
            infile.seekg(0, infile.end);
            fileLength = infile.tellg();
            fileWords = fileLength / 4;
            infile.seekg(0, infile.beg);

            // Read in file
            infile.read((char*)ape.bytes, fileLength);

            infile.close();
        }
        else
        {
            cerr << " Unable to open file '" << file << "'" << endl;
            exit(-1);
        }

        if(ape.words[0] == be32toh(APE_HEADER_MAGIC))
        {
            // The file is swapped... fix it.
            for(int i = 0; i < sizeof(ape)/sizeof(ape.words[0]); i++)
            {
                ape.words[i] = be32toh(ape.words[i]);
            }
        }


        // load file.
        for(int i = 0; i < fileWords; i++)
        {
            uint32_t addr = 0x10D800 + i*4;
            loader_write_mem(addr, ape.words[i]);
        }


        RegAPEMode_t mode;
        mode.r32 = 0;
        mode.bits.Halt = 1;
        mode.bits.FastBoot = 1;
        APE.Mode = mode;


        // Set the payload address
        APE.GpioMessage.r32 = 0x10D800|2;

        // Clear the signature.
        SHM.SegSig.r32 = 0xBAD0C0DE;

        // Boot
        mode.bits.Halt = 0;
        mode.bits.FastBoot = 1;
        mode.bits.Reset = 1;
        APE.Mode = mode;

        exit(0);
    }

    if(options.get("apereset"))
    {

        // Halt
        RegAPEMode_t mode;
        mode.r32 = 0;
        mode.bits.Halt = 1;
        mode.bits.FastBoot = 0;
        APE.Mode = mode;

        // Boot
        mode.bits.Halt = 0;
        mode.bits.FastBoot = 0;
        mode.bits.Reset = 1;
        APE.Mode = mode;

        exit(0);
    }

    if(options.get("reset"))
    {

        DEVICE.MiscellaneousConfig.bits.GRCReset = 1;
        exit(0);
    }



    if(options.get("rx"))
    {
        DEVICE.ReceiveMacMode.print();
        DEVICE.EmacMode.print();
        APE.RxPoolModeStatus0.print();
        APE.RxbufoffsetFunc0.print();
        exit(0);
    }

    if(options.get("tx"))
    {
        // allocateTXBlock();
        DEVICE.EmacMode.print();
        APE.Mode.print();
        APE.Status.print();
        APE.TxState0.print();
        APE.TxToNetPoolModeStatus0.print();
        APE.TxToNetBufferAllocator0.print();
        APE.TxToNetBufferRing0.print();
        APE.TxToNetBufferReturn0.print();
        APE.TxToNetDoorbellFunc0.print();
        if(APE.TxToNetDoorbellFunc0.bits.TXQueueFull)
        {
            fprintf(stderr, "TX Queue Full\n");
            abort();
        }
        transmitPacket(ping_packet, 68);
        APE.TxState0.print();
        APE.TxToNetPoolModeStatus0.print();
        APE.TxToNetBufferAllocator0.print();
        APE.TxToNetBufferRing0.print();
        APE.TxToNetBufferReturn0.print();
        APE.TxToNetDoorbellFunc0.print();

        exit(0);

    }

    if(options.get("ape"))
    {
        APE.Mode.print();
        APE.Status.print();
        APE.Gpio.print();
        SHM.FwStatus.print();
        SHM.FwFeatures.print();
        SHM.FwVersion.print();

        printf("APE SegSig: 0x%08X\n", (uint32_t)SHM.SegSig.r32);
        printf("APE SegLen: 0x%08X\n", (uint32_t)SHM.ApeSegLength.r32);
        printf("APE RcpuApeResetCount: 0x%08X\n", (uint32_t)SHM.RcpuApeResetCount.r32);

        printf("APE RCPU SegSig: 0x%08X\n", (uint32_t)SHM.RcpuSegSig.r32);
        printf("APE RCPU SegLen: 0x%08X\n", (uint32_t)SHM.RcpuSegLength.r32);
        printf("APE RCPU Init Count: 0x%08X\n", (uint32_t)SHM.RcpuInitCount.r32);
        printf("APE RCPU FW Version: 0x%08X\n", (uint32_t)SHM.RcpuFwVersion.r32);

        printf("APE RCPU CfgFeature: 0x%08X\n", (uint32_t)SHM.RcpuCfgFeature.r32);
        printf("APE RCPU PCI Vendor/Device ID: 0x%08X\n", (uint32_t)SHM.RcpuPciVendorDeviceId.r32);
        printf("APE RCPU PCI Subsystem ID: 0x%08X\n", (uint32_t)SHM.RcpuPciSubsystemId.r32);

        // boot_ape_loader();
        // printf("Loader Command: 0x%08X\n", (uint32_t)SHM.LoaderCommand.r32);
        // printf("Loader Arg0: 0x%08X\n", (uint32_t)SHM.LoaderArg0.r32);
        // printf("Loader Arg1: 0x%08X\n", (uint32_t)SHM.LoaderArg1.r32);

        // SHM.LoaderArg0.r32 = 0x00100040;
        // SHM.LoaderCommand.bits.Command = SHM_LOADER_COMMAND_COMMAND_READ_MEM;

        // // Wait for command to be handled.
        // while(0 != SHM.LoaderCommand.bits.Command);
        // printf("Loader Command: 0x%08X\n", (uint32_t)SHM.LoaderCommand.r32);
        // printf("Loader Arg0: 0x%08X\n", (uint32_t)SHM.LoaderArg0.r32);
        // printf("Loader Arg1: 0x%08X\n", (uint32_t)SHM.LoaderArg1.r32);

        // // boot_ape_loader();
        // FILTERS.ElementConfig[0].print();
        // FILTERS.ElementConfig[15].print();
        // FILTERS.ElementConfig[16].print();
        // FILTERS.RuleConfiguration.print();
        // FILTERS.RuleSet[0].print();
        // FILTERS.RuleSet[1].print();
        // FILTERS.RuleSet[2].print();

        // NVIC.VectorTableOffset.print();

        // NVIC.InterruptControlType.print();
        // NVIC.SystickControlAndStatus.print();
        // NVIC.SystickControlAndStatus.bits.ENABLE = 1;
        // NVIC.SystickControlAndStatus.print();

        // NVIC.SystickReloadValue.print();
        // // NVIC.SystickReloadValue.bits.RELOAD=0xFFFFFFFF;
        // NVIC.SystickCurrentValue.print();
        // NVIC.SystickCurrentValue.print();
        // NVIC.SystickCalibrationValue.print();
        // APE_PERI.RmuControl.bits.AutoDrv =1;
        APE_PERI.RmuControl.print();
#if 0
        uint32_t buffer[1024];

        RegAPE_PERIBmcToNcRxStatus_t stat;
        stat.r32 = APE_PERI.BmcToNcRxStatus.r32;
        stat.print();
        
        if(stat.bits.New)
        {
            int32_t bytes = stat.bits.PacketLength;
            int i = 0;
            while(bytes > 0)
            {
                uint32_t word = (APE_PERI.BmcToNcReadBuffer.r32);
                buffer[i] = word;
                printf("Word %d: 0x%08X\n", i, word);
                i++;
                bytes -= 4;
            }

            NetworkFrame_t *frame = ((NetworkFrame_t*)buffer);

            if(stat.bits.Bad)
            {
                // TODO: ACK bad packet.
                APE_PERI.BmcToNcRxControl.bits.ResetBad = 1;
                while(APE_PERI.BmcToNcRxControl.bits.ResetBad);
            }
            else if(!stat.bits.Passthru)
            {
                handleNCSIFrame(frame);
            }
            else
            {
                // Pass through to network
            }
        }

        // printf("REG_APE__NC_BMC_TX_STATUS__IN_FIFO")
        // APE_PERI.BmcToNcTxStatus.print();
        // APE_PERI.BmcToNcTxControl.print();
        // APE_PERI.BmcToNcRxControl.print();
        // APE_PERI.ArbControl.print();
        printf("RegAPENcsiChannel0CtrlstatRx: 0x%08X\n", (uint32_t)SHM_CHANNEL0.NcsiChannelCtrlstatRx.r32);
#endif
        exit(0);
    }


    if(options.get("info"))
    {
        GEN.GenDataSig.print();
        GEN.GenFwMbox.print();
        GEN.GenAsfStatusMbox.print();
        GEN.GenFwVersion.print();
        DEVICE.ChipId.print();
        DEVICE.PciVendorDeviceId.print();
        DEVICE.PciSubsystemId.print();
        DEVICE.PciClassCodeRevision.print();
        DEVICE.Status.print();


        // GenCfgFeature
        // GenCfgHw
        // GenCfgShared
        // GenCfgHw2
        // GenCfg5

        printf("\n");

        uint64_t serial = (((uint64_t)(DEVICE.PciSerialNumberHigh.r32)) << 32) | DEVICE.PciSerialNumberLow.r32;
        printf("Serial Number:   0x%016lX\n", serial);

        uint64_t genmac = (((uint64_t)(GEN.GenMacAddrHighMbox.r32)) << 32) | GEN.GenMacAddrLowMbox.r32;
        printf("GEN Mac Addr:   0x%016lX\n", genmac);

        printf("\n");

        printf("Power Budget[0]: 0x%08X\n", (uint32_t)DEVICE.PciPowerBudget0.r32);
        printf("Power Budget[1]: 0x%08X\n", (uint32_t)DEVICE.PciPowerBudget1.r32);
        printf("Power Budget[2]: 0x%08X\n", (uint32_t)DEVICE.PciPowerBudget2.r32);
        printf("Power Budget[3]: 0x%08X\n", (uint32_t)DEVICE.PciPowerBudget3.r32);
        printf("Power Budget[4]: 0x%08X\n", (uint32_t)DEVICE.PciPowerBudget4.r32);
        printf("Power Budget[5]: 0x%08X\n", (uint32_t)DEVICE.PciPowerBudget5.r32);
        printf("Power Budget[6]: 0x%08X\n", (uint32_t)DEVICE.PciPowerBudget6.r32);
        printf("Power Budget[7]: 0x%08X\n", (uint32_t)DEVICE.PciPowerBudget7.r32);

        printf("\n");

        uint64_t mac0 = (((uint64_t)(DEVICE.EmacMacAddresses0High.r32)) << 32) | DEVICE.EmacMacAddresses0Low.r32;
        uint64_t mac1 = (((uint64_t)(DEVICE.EmacMacAddresses1High.r32)) << 32) | DEVICE.EmacMacAddresses1Low.r32;
        uint64_t mac2 = (((uint64_t)(DEVICE.EmacMacAddresses2High.r32)) << 32) | DEVICE.EmacMacAddresses2Low.r32;
        uint64_t mac3 = (((uint64_t)(DEVICE.EmacMacAddresses3High.r32)) << 32) | DEVICE.EmacMacAddresses3Low.r32;
        printf("MAC0: 0x%012lX\n", mac0);
        printf("MAC1: 0x%012lX\n", mac1);
        printf("MAC2: 0x%012lX\n", mac2);
        printf("MAC3: 0x%012lX\n", mac3);

        printf("\n");

        printf("Reg 6408: 0x%08X\n", (uint32_t)DEVICE._6408.r32);
        printf("Reg 64c0: 0x%08X\n", (uint32_t)DEVICE._64c0.r32);
        printf("Reg 64c8: 0x%08X\n", (uint32_t)DEVICE._64c8.r32);
        printf("Reg 64dc: 0x%08X\n", (uint32_t)DEVICE._64dc.r32);
        printf("Reg 6530: 0x%08X\n", (uint32_t)DEVICE._6530.r32);
        printf("Reg 6550: 0x%08X\n", (uint32_t)DEVICE._6550.r32);
        printf("Reg 65f4: 0x%08X\n", (uint32_t)DEVICE._65f4.r32);
        printf("Reg 7c04: 0x%08X\n", (uint32_t)DEVICE._7c04.r32);

        printf("LedControl:         0x%08X\n", (uint32_t)DEVICE.LedControl.r32);
        printf("GrcModeControl:     0x%08X\n", (uint32_t)DEVICE.GrcModeControl.r32);
        DEVICE.GrcModeControl.bits.NVRAMWriteEnable = 1;
        DEVICE.GrcModeControl.print();
        printf("GphyControlStatus:  0x%08X\n", (uint32_t)DEVICE.GphyControlStatus.r32);
        printf("TopLevelMiscellaneousControl1: 0x%08X\n", (uint32_t)DEVICE.TopLevelMiscellaneousControl1.r32);
        printf("MiscellaneousLocalControl:     0x%08X\n", (uint32_t)DEVICE.MiscellaneousLocalControl.r32);

        DEVICE.RxRiscMode.print();
        DEVICE.RxRiscStatus.print();

        DEVICE.RxCpuEventEnable.print();
        DEVICE.RxCpuEvent.print();
        exit(0);
    }


    printf("APEChipId: %x\n", (uint32_t)SHM.ChipId.r32);

    printf("EmacMode.PortMode: %0x\n", (uint32_t)DEVICE.EmacMode.bits.PortMode);
    printf("RxRiscMode: %0x\n", (uint32_t)DEVICE.RxRiscMode.r32);

    // printf("HostDriverId: %0x\n", APE.HostDriverId.r32);
    // printf("RcpuPciSubsystemId: %0x\n", APE.RcpuPciSubsystemId.r32);

    print_context();

    return 0;
}
