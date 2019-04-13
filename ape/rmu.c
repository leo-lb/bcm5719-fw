////////////////////////////////////////////////////////////////////////////////
///
/// @file       rx_from_network.c
///
/// @project
///
/// @brief      Initialization code for RX from network.
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

#include "ape.h"

#include <APE_APE.h>

void initRMU(void)
{
    RegAPEMode_t mode;
    mode.r32 = APE.Mode.r32;
    mode.bits.MemoryECC = 1;
    mode.bits.SwapATBdword = 1;
    mode.bits.SwapARBdword = 1;
    mode.bits.ICodePIPRdDisable = 1;
    APE.Mode = mode;

    // Optionally, set REG_APE__RMU_CONTROL to RST_RX|RST_TX. This can help unwedge the state machines if you wedged them previously due to a bug in your code.
    RegAPERmuControl_t rmuControl;
    rmuControl.r32 = 0;
    rmuControl.bits.ResetTX = 1;
    rmuControl.bits.ResetRX = 1;
    APE.RmuControl = rmuControl;

    // Now set REG_APE__RMU_CONTROL to AUTO_DRV|RX|TX. Also set bits 19 and 20 (meaning unknown).
    rmuControl.r32 = 0;
    rmuControl.bits.AutoDrv = 1;
    rmuControl.bits.RX = 1;
    rmuControl.bits.TX = 1;
    rmuControl.r32 |= (1 << 19) | (1 << 20);
    APE.RmuControl = rmuControl;

    APE.RmuControl = rmuControl;

    // Set REG_APE__BMC_NC_RX_CONTROL to FLOW_CONTROL=0 or 1, HWM=0x240, XON_THRESHOLD=0x201F.
    // Note: FLOW_CONTROL=1 enables the hardware to automatically send PAUSE frames to the BMC. tcpdump can detect these, so keeping flow control on gives you a way to detect when the RX state machine has gotten wedged.
    RegAPEBmcToNcRxControl_t rxControl;
    rxControl.r32 = 0;
    rxControl.bits.FlowControl = 1;
    rxControl.bits.HWM = 0x240;


    // Set REG_APE__NC_BMC_TX_CONTROL to 0.
    RegAPEBmcToNcTxControl_t txControl;
    txControl.r32 = 0;
    APE.BmcToNcTxControl = txControl;

    // Set all eight REG_APE__BMC_NC_RX_SRC_MAC_MATCHN_{HIGH,LOW} to zero.
    APE.BmcToNcSourceMacMatch0High.r32 = 0;
    APE.BmcToNcSourceMacMatch0Low.r32  = 0;
    APE.BmcToNcSourceMacMatch1High.r32 = 0;
    APE.BmcToNcSourceMacMatch1Low.r32  = 0;
    APE.BmcToNcSourceMacMatch2High.r32 = 0;
    APE.BmcToNcSourceMacMatch2Low.r32  = 0;
    APE.BmcToNcSourceMacMatch3High.r32 = 0;
    APE.BmcToNcSourceMacMatch3Low.r32  = 0;
    APE.BmcToNcSourceMacMatch4High.r32 = 0;
    APE.BmcToNcSourceMacMatch4Low.r32  = 0;
    APE.BmcToNcSourceMacMatch5High.r32 = 0;
    APE.BmcToNcSourceMacMatch5Low.r32  = 0;
    APE.BmcToNcSourceMacMatch6High.r32 = 0;
    APE.BmcToNcSourceMacMatch6Low.r32  = 0;
    APE.BmcToNcSourceMacMatch7High.r32 = 0;
    APE.BmcToNcSourceMacMatch7Low.r32  = 0;

    // Set REG_APE__ARB_CONTROL as desired. Suggest PACKAGE_ID=0, TKNREL=0x14, START, and setting unknown bit 26 to 1.
    RegAPEArbControl_t arbControl;
    arbControl.r32 = (1 << 26);
    arbControl.bits.PackageID = 0; /* TODO: allow to be configured as per NC-SI spec. */
    arbControl.bits.Start = 1;
    arbControl.bits.TKNREL = 0x14;
}