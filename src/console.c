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

#include <glidix/console.h>
#include <glidix/port.h>
#include <glidix/spinlock.h>
#include <stdint.h>

#define	VRAM_BASE 0xFFFF8000000B8000

static struct
{
	uint64_t curX, curY;
	uint64_t curColor;
	uint8_t putcon;
} consoleState;

static Spinlock consoleLock;

void initConsole()
{
	//consoleState.curX = 0;
	//consoleState.curY = 0;
	//consoleState.curColor = 0x07;
	//consoleState.putcon = 1;
	spinlockRelease(&consoleLock);
	clearScreen();
};

static void updateVGACursor()
{
	uint64_t pos = consoleState.curY * 80 + consoleState.curX;
	outb(0x3D4, 0x0F);
	outb(0x3D5, pos & 0xFF);
	outb(0x3D4, 0x0E);
	outb(0x3D5, (pos >> 8) & 0xFF);
};

void clearScreen()
{
	spinlockAcquire(&consoleLock);
	consoleState.curX = 0;
	consoleState.curY = 0;
	consoleState.curColor = 0x07;
	consoleState.putcon = 1;
	
	uint8_t *videoram = (uint8_t*) VRAM_BASE;
	uint64_t i;
	
	for (i=0; i<80*25; i++)
	{
		videoram[2*i+0] = 0;
		videoram[2*i+1] = 0x07;
	};
	
	updateVGACursor();
	spinlockRelease(&consoleLock);
};

void setCursorPos(uint8_t x, uint8_t y)
{
	spinlockAcquire(&consoleLock);
	consoleState.curX = x;
	consoleState.curY = y;
	updateVGACursor();
	spinlockRelease(&consoleLock);
};

void setConsoleColor(uint8_t col)
{
	spinlockAcquire(&consoleLock);
	consoleState.curColor = col;
	spinlockRelease(&consoleLock);
};

static void scroll()
{
	uint8_t *vidmem = (uint8_t*) VRAM_BASE;

	uint64_t i;
	for (i=0; i<2*80*24; i++)
	{
		vidmem[i] = vidmem[i+160];
	};

	for (i=80*24; i<80*25; i++)
	{
		vidmem[2*i+0] = 0;
		vidmem[2*i+1] = 0x07;
	};

	consoleState.curY--;
	updateVGACursor();
};

static void kputch(char c)
{
	outb(0xE9, c);
	if (!consoleState.putcon) return;

	if (consoleState.curY == 25) scroll();
	
	if (c == '\n')
	{
		consoleState.curX = 0;
		consoleState.curY++;
		if (consoleState.curY == 25) scroll();
	}
	else if (c == '\r')
	{
		consoleState.curX = 0;
	}
	else if (c == '\b')
	{
		if (consoleState.curX == 0)
		{
			if (consoleState.curY == 0) return;
			consoleState.curY--;
			consoleState.curX = 80;
		};
		consoleState.curX--;
		uint8_t *vidmem = (uint8_t*) (VRAM_BASE + 2 * (consoleState.curY * 80 + consoleState.curX));
		*vidmem++ = 0;
		*vidmem++ = consoleState.curColor;
	}
	else if (c == '\t')
	{
		consoleState.curX = (consoleState.curX/8+1)*8;
		if (consoleState.curX >= 80)
		{
			consoleState.curY++;
			consoleState.curX -= 80;
			if (consoleState.curY == 25) scroll();
		};
	}
	else
	{
		uint8_t *vidmem = (uint8_t*) (VRAM_BASE + 2 * (consoleState.curY * 80 + consoleState.curX));
		*vidmem++ = c;
		*vidmem++ = consoleState.curColor;
		consoleState.curX++;

		if (consoleState.curX == 80)
		{
			consoleState.curX = 0;
			consoleState.curY++;
			//if (consoleState.curY == 25) scroll();
		};
	};
};

static void kputs(const char *str)
{
	while (*str != 0)
	{
		kputch(*str++);
	};

	updateVGACursor();
};

static void put_d(int d)
{
	if (d < 0)
	{
		kputch('-');
		d = -d;
	};

	char buffer[20];
	buffer[19] = 0;

	char *ptr = &buffer[18];
	do
	{
		*ptr = '0' + (d % 10);
		ptr--;
		d /= 10;
	} while (d != 0);

	kputs(ptr+1);
};

static void put_x(int d)
{
	if (d < 0)
	{
		kputch('-');
		d = -d;
	};

	char buffer[20];
	buffer[19] = 0;

	char *ptr = &buffer[18];
	do
	{
		if ((d % 16) < 10) *ptr = '0' + (d % 16);
		else *ptr = 'a' + ((d % 16) - 10);
		ptr--;
		d /= 16;
	} while (d != 0);

	kputs(ptr+1);
};

static void put_a(uint64_t addr)
{
	kputs("0x");
	int count = 16;
	while (count--)
	{
		uint8_t hexd = (uint8_t) (addr >> 60);
		if (hexd < 10)
		{
			kputch('0'+hexd);
		}
		else
		{
			kputch('A'+(hexd-10));
		};
		addr <<= 4;
	};

	updateVGACursor();
};

void kvprintf_gen(uint8_t putcon, const char *fmt, va_list ap)
{
	if (putcon) spinlockAcquire(&consoleLock);
	consoleState.putcon = putcon;

	while (*fmt != 0)
	{
		char c = *fmt++;
		if (c != '%')
		{
			kputch(c);
		}
		else
		{
			c = *fmt++;
			if (c == '%')
			{
				kputch('%');
			}
			else if (c == 's')
			{
				const char *str = va_arg(ap, char*);
				kputs(str);
			}
			else if (c == 'd')
			{
				int d = va_arg(ap, int);
				put_d(d);
			}
			else if (c == 'x')
			{
				int d = va_arg(ap, int);
				put_x(d);
			}
			else if (c == 'a')			// 64-bit unsigned
			{
				uint64_t d = va_arg(ap, uint64_t);
				put_a(d);
			}
			else if (c == 'c')
			{
				char pc = (char) va_arg(ap, int);
				kputch(pc);
			}
			else if (c == '$')
			{
				c = *fmt++;
				consoleState.curColor = c;
			}
			else if (c == '#')
			{
				consoleState.curColor = 0x07;
			};
		};
	};

	updateVGACursor();

	if (putcon) spinlockRelease(&consoleLock);
};

void kvprintf(const char *fmt, va_list ap)
{
	kvprintf_gen(1, fmt, ap);
};

void kprintf(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	kvprintf(fmt, ap);
	va_end(ap);
};

void kvprintf_debug(const char *fmt, va_list ap)
{
	kvprintf_gen(0, fmt, ap);
};

void kprintf_debug(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	kvprintf_debug(fmt, ap);
	va_end(ap);
};

void kputbuf(const char *buf, size_t size)
{
	spinlockAcquire(&consoleLock);
	consoleState.putcon = 1;
	while (size--)
	{
		kputch(*buf++);
	};
	updateVGACursor();
	spinlockRelease(&consoleLock);
};

void kputbuf_debug(const char *buf, size_t size)
{
	spinlockAcquire(&consoleLock);
	consoleState.putcon = 0;
	while (size--)
	{
		kputch(*buf++);
	};
	updateVGACursor();
	spinlockRelease(&consoleLock);
};

typedef struct
{
	char on;
	char off;
	uint64_t mask;
} Flag;

static Flag flagList[11] = {
	{'C', 'c', (1 << 0)},
	{'P', 'p', (1 << 2)},
	{'A', 'a', (1 << 4)},
	{'Z', 'z', (1 << 6)},
	{'S', 's', (1 << 7)},
	{'T', 't', (1 << 8)},
	{'I', 'i', (1 << 9)},
	{'D', 'd', (1 << 10)},
	{'O', 'o', (1 << 11)},
	{'N', 'n', (1 << 14)},
	{'R', 'r', (1 << 16)}
};

static void printFlags(uint64_t flags)
{
	int i;
	for (i=0; i<11; i++)
	{
		//char c = (flags & flagList[i].mask) ? flagList[i].on : flagList[i].off;
		//kprintf("%c", c);
		if (flags & flagList[i].mask)
		{
			kprintf("%$\x02%c%#", flagList[i].on);
		}
		else
		{
			kprintf("%$\x04%c%#", flagList[i].on);
		};
	};
};

void kdumpregs(Regs *regs)
{
	kprintf("DS:  %a\tSS:  %a\n", regs->ds, regs->ss);
	kprintf("RDI: %a\tRSI: %a\n", regs->rdi, regs->rsi);
	kprintf("RBP: %a\tRSP: %a\n", regs->rbp, regs->rsp);
	kprintf("RAX: %a\tRBX: %a\n", regs->rax, regs->rbx);
	kprintf("RCX: %a\tRDX: %a\n", regs->rcx, regs->rdx);
	kprintf("R8:  %a\tR9:  %a\n", regs->r8, regs->r9);
	kprintf("R10: %a\tR11: %a\n", regs->r10, regs->r11);
	kprintf("R12: %a\tR13: %a\n", regs->r12, regs->r13);
	kprintf("R14: %a\tR15: %a\n", regs->r14, regs->r15);
	kprintf("INO: %a\tERR: %a\n", regs->intNo, regs->errCode);
	kprintf("CS:  %a\tRIP: %a\n", regs->cs, regs->rip);
	kprintf("RFLAGS: %a (", regs->rflags); printFlags(regs->rflags); kprintf(")\n");
};

void unlockConsole()
{
	spinlockRelease(&consoleLock);
};
