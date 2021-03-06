////////////////////////////////////////////////////////////////////////////////
///
/// @file       NVRam.h
///
/// @project
///
/// @brief      NVRam Support Routines
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
#ifndef NVRAM_H
#define NVRAM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool NVRam_acquireLock(void);
bool NVRam_releaseLock(void);

uint32_t NVRam_readWord(uint32_t address);
void NVRam_read(uint32_t address, uint32_t *buffer, size_t words);

void NVRam_writeWord(uint32_t address, uint32_t data);
void NVRam_write(uint32_t address, uint32_t *buffer, size_t words);

void NVRam_enable(void);
void NVRam_enableWrites(void);
void NVRam_disable(void);
void NVRam_disableWrites(void);

uint32_t NVRam_crc(const uint8_t *pcDatabuf, // Pointer to data buffer
                   uint32_t ulDatalen, // Length of data buffer in bytes
                   uint32_t crc);      // Initial value

#endif /* NVRAM_H */
