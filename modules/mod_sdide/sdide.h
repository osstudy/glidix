/*
	Glidix kernel

	Copyright (c) 2014-2015, Madd Games.
	All rights reserved.
	
	Redistribution and use in source and binary forms, with or without
	modification, are permitted provided that the following conditions are met:
	
	* Redistributions of source code must retain the above copyright notice, this
	  list of conditions and the following disclaimer.
	
	* Redistributions in binary form must reproduce the above copyright notice,
	  this list of conditions and the following disclaimer in the documentation
	  and/or other materials provided with the distribution.
	
	THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
	AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
	IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
	DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
	FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
	DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
	SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
	CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
	OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
	OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#ifndef SDIDE_H
#define SDIDE_H

#include <glidix/storage.h>
#include <glidix/common.h>

// IRQs
#define	ATA_IRQ_PRIMARY					0x0E
#define	ATA_IRQ_SECONDARY				0x0F

// status
#define ATA_SR_BSY					0x80
#define ATA_SR_DRDY					0x40
#define ATA_SR_DF					0x20
#define ATA_SR_DSC					0x10
#define ATA_SR_DRQ					0x08
#define ATA_SR_CORR					0x04
#define ATA_SR_IDX					0x02
#define ATA_SR_ERR					0x01

// error
#define ATA_ER_BBK					0x80
#define ATA_ER_UNC					0x40
#define ATA_ER_MC					0x20
#define ATA_ER_IDNF					0x10
#define ATA_ER_MCR					0x08
#define ATA_ER_ABRT					0x04
#define ATA_ER_TK0NF					0x02
#define ATA_ER_AMNF					0x01

// command
#define ATA_CMD_READ_PIO				0x20
#define ATA_CMD_READ_PIO_EXT				0x24
#define ATA_CMD_READ_DMA				0xC8
#define ATA_CMD_READ_DMA_EXT				0x25
#define ATA_CMD_WRITE_PIO				0x30
#define ATA_CMD_WRITE_PIO_EXT				0x34
#define ATA_CMD_WRITE_DMA				0xCA
#define ATA_CMD_WRITE_DMA_EXT				0x35
#define ATA_CMD_CACHE_FLUSH				0xE7
#define ATA_CMD_CACHE_FLUSH_EXT				0xEA
#define ATA_CMD_PACKET					0xA0
#define ATA_CMD_IDENTIFY_PACKET				0xA1
#define ATA_CMD_IDENTIFY				0xEC

// ATAPI commands
#define	ATAPI_CMD_READ					0xA8
#define	ATAPI_CMD_EJECT					0x1B

// identity space
#define ATA_IDENT_DEVICETYPE				0
#define ATA_IDENT_CYLINDERS				2
#define ATA_IDENT_HEADS					6
#define ATA_IDENT_SECTORS				12
#define ATA_IDENT_SERIAL				20
#define ATA_IDENT_MODEL					54
#define ATA_IDENT_CAPABILITIES				98
#define ATA_IDENT_FIELDVALID				106
#define ATA_IDENT_MAX_LBA				120
#define ATA_IDENT_COMMANDSETS				164
#define ATA_IDENT_MAX_LBA_EXT				200

// interfaces types
#define IDE_ATA						0x00
#define IDE_ATAPI					0x01

// device types
#define ATA_MASTER					0x00
#define ATA_SLAVE					0x01

// channels
#define	ATA_PRIMARY					0x00
#define	ATA_SECONDARY					0x01

// channel registers
#define ATA_REG_DATA					0x00
#define ATA_REG_ERROR					0x01
#define ATA_REG_FEATURES				0x01
#define ATA_REG_SECCOUNT0				0x02
#define ATA_REG_LBA0					0x03
#define ATA_REG_LBA1					0x04
#define ATA_REG_LBA2					0x05
#define ATA_REG_HDDEVSEL				0x06
#define ATA_REG_COMMAND					0x07
#define ATA_REG_STATUS					0x07
#define ATA_REG_SECCOUNT1				0x08
#define ATA_REG_LBA3					0x09
#define ATA_REG_LBA4					0x0A
#define ATA_REG_LBA5					0x0B
#define ATA_REG_CONTROL					0x0C
#define ATA_REG_ALTSTATUS				0x0C
#define ATA_REG_DEVADDRESS				0x0D

// transfer directions
#define	ATA_READ					0x00
#define	ATA_WRITE					0x01

// some macros for Glidix
#define	ATA_BLOCK_SIZE					512
#define	ATAPI_BLOCK_SIZE				2048

typedef struct
{
	uint16_t					base;			// I/O Base.
	uint16_t					ctrl;			// Control Base
	uint16_t					bmide;			// Bus Master IDE
	uint8_t						nIEN;			// nIEN (No Interrupt);
} IDEChannelRegs;

typedef struct
{
	uint8_t						exists;
	uint8_t						channel;
	uint8_t						drive;
	uint16_t					type;
	uint16_t					sig;
	uint16_t					cap;
	uint32_t					cmdset;
	uint32_t					size;			// in sectors
	uint8_t						model[41];
} IDEDevice;

typedef struct
{
	IDEChannelRegs					channels[2];
	uint8_t						idbuf[2048];
	uint8_t						irqwait;
	uint8_t						atapiPacket[12];
	IDEDevice					devices[4];
} IDEController;

typedef struct
{
	IDEController*					ctrl;
	int						devidx;
	StorageDevice*					sd;
} IDEThreadParams;

#endif
