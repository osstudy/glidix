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

#include <glidix/module.h>
#include <glidix/pagetab.h>
#include <glidix/spinlock.h>
#include <glidix/console.h>
#include <glidix/vfs.h>
#include <glidix/string.h>
#include <glidix/memory.h>
#include <glidix/elf64.h>
#include <glidix/isp.h>
#include <glidix/physmem.h>
#include <glidix/symtab.h>
#include <glidix/errno.h>
#include <glidix/sched.h>

#define	MODULE_AREA_BASE			0xFFFF818000000000
#define	MODULE_BLOCK_SIZE			0x0000000040000000
#define	MODULE_SECTOR_SIZE			0x0000000000200000

static PAGE_ALIGN PDPT pdptModuleSpace;
static Spinlock modLock;

static Module *firstModule;
static Module *lastModule;

static FileSystem *moduleFileSystem;

static int getModule(Dir *dir)
{
	spinlockAcquire(&modLock);
	Module *mod = firstModule;

	while (mod != NULL)
	{
		if (mod->block == (int)dir->stat.st_ino)
		{
			strcpy(dir->dirent.d_name, mod->name);
			dir->dirent.d_ino = dir->stat.st_ino;
			dir->stat.st_mode = 0;
			dir->stat.st_nlink = 1;
			dir->stat.st_uid = 0;
			dir->stat.st_gid = 0;
			dir->stat.st_rdev = 0;
			dir->stat.st_size = mod->numSectors * MODULE_SECTOR_SIZE;
			dir->stat.st_blksize = MODULE_SECTOR_SIZE;
			dir->stat.st_blocks = mod->numSectors;
			spinlockRelease(&modLock);
			return 0;
		};

		mod = mod->next;
	};

	spinlockRelease(&modLock);
	return -1;
};

static int modfs_next(Dir *dir)
{
	while (1)
	{
		dir->stat.st_ino++;
		if (dir->stat.st_ino == 512) return -1;
		if (getModule(dir) == 0) return 0;
	};

	return -1;
};

static int modfs_unlink(Dir *dir)
{
	return rmmod(dir->dirent.d_name, 0);
};

static int modfs_openroot(FileSystem *fs, Dir *dir, size_t szdir)
{
	dir->next = modfs_next;
	dir->unlink = modfs_unlink;
	dir->stat.st_ino = 0;

	while (1)
	{
		if (dir->stat.st_ino == 512) return VFS_EMPTY_DIRECTORY;
		if (getModule(dir) == 0) return 0;
		dir->stat.st_ino++;
	};
};

void initModuleInterface()
{
	kprintf("Initializing the module interface... ");
	spinlockRelease(&modLock);
	firstModule = NULL;
	lastModule = NULL;
	memset(&pdptModuleSpace, 0, 0x1000);

	uint64_t pdptPhysAddr = (uint64_t) &pdptModuleSpace - 0xFFFF800000000000;
	if (pdptPhysAddr % 0x1000)
	{
		kprintf("%$\x04" "Failed%#\n");
		panic("pdptModuleSpace is not page-aligned: kernel insane");
	};

	PML4 *pml4 = getPML4();
	pml4->entries[259].present = 1;
	pml4->entries[259].rw = 1;
	pml4->entries[259].pdptPhysAddr = (pdptPhysAddr >> 12);

	refreshAddrSpace();

	moduleFileSystem = (FileSystem*) kmalloc(sizeof(FileSystem));
	memset(moduleFileSystem, 0, sizeof(FileSystem));

	moduleFileSystem->fsname = "";
	moduleFileSystem->openroot = modfs_openroot;

	kprintf("%$\x02" "Done%#\n");
};

static int allocModuleBlock()
{
	int i;
	for (i=0; i<512; i++)
	{
		if (!pdptModuleSpace.entries[i].present)
		{
			return i;
		};
	};

	return -1;
};

static uint64_t *mapModuleArea(int modblock, int numSectors)
{
	ispLock();

	// physical frame indices of page tables we will use; we collect them here to avoid constantly
	// switching the ISP.
	uint64_t *ptFrames = (uint64_t*) kmalloc(8*(numSectors+512*numSectors));

	uint64_t pdPhysFrame = phmAllocFrame();
	ispSetFrame(pdPhysFrame);
	PD *pd = ispGetPointer();
	memset(pd, 0, 0x1000);

	int i;
	for (i=0; i<numSectors; i++)
	{
		uint64_t ptFrame = phmAllocFrame();
		ptFrames[i] = ptFrame;
		pd->entries[i].present = 1;
		pd->entries[i].rw = 1;
		pd->entries[i].ptPhysAddr = ptFrame;
	};

	for (i=0; i<(numSectors*512); i++)
	{
		ptFrames[numSectors+i] = phmAllocFrame();
	};

	// now the module pages to physical frames.
	PT *pt = ispGetPointer();
	for (i=0; i<numSectors; i++)
	{
		ispSetFrame(ptFrames[i]);
		memset(pt, 0, 0x1000);
		int j;
		for (j=0; j<512; j++)
		{
			pt->entries[j].present = 1;
			pt->entries[j].rw = 1;
			pt->entries[j].framePhysAddr = /*phmAllocFrame();*/ ptFrames[numSectors+512*i+j];
		};
	};

	ispUnlock();
	//kfree(ptFrames);

	// now map that into the address space
	ASM("cli");
	pdptModuleSpace.entries[modblock].present = 1;
	pdptModuleSpace.entries[modblock].rw = 1;
	pdptModuleSpace.entries[modblock].pdPhysAddr = pdPhysFrame;
	refreshAddrSpace();
	ASM("sti");

	return ptFrames;
};

static void unmapModuleArea(Module *mod)
{
	pdptModuleSpace.entries[mod->block].present = 0;
	refreshAddrSpace();

	int i;
	for (i=0; i<(mod->numSectors+512*mod->numSectors); i++)
	{
		phmFreeFrame(mod->frames[i]);
	};

	kfree(mod->frames);
	mod->frames = NULL;
};

int insmod(const char *modname, const char *path, const char *opt, int flags)
{
	if (strlen(modname) >= 127)
	{
		kprintf("insmod(%s): module name too long\n", modname);
		return -1;
	};

	spinlockAcquire(&modLock);

	Module *scanmod = firstModule;
	while (scanmod != NULL)
	{
		if (strcmp(scanmod->name, modname) == 0)
		{
			spinlockRelease(&modLock);
			kprintf("insmod(%s): a module with this name is already loaded\n", modname);
			return -1;
		};

		scanmod = scanmod->next;
	};

	int error;
	File *fp = vfsOpen(path, 0, &error);
	if (fp == NULL)
	{
		kprintf("insmod(%s): failed to open %s: %d\n", modname, path, error);
		spinlockRelease(&modLock);
		return -1;
	};

	if (fp->seek == NULL)
	{
		kprintf("insmod(%s): %s: cannot seek\n", modname, path);
		vfsClose(fp);
		spinlockRelease(&modLock);
		return -1;
	};

	Elf64_Ehdr elfHeader;
	if (vfsRead(fp, &elfHeader, sizeof(Elf64_Ehdr)) < sizeof(Elf64_Ehdr))
	{
		kprintf("insmod(%s): %s: file too small to contain ELF64 header\n", modname, path);
		vfsClose(fp);
		spinlockRelease(&modLock);
		return -1;
	};

	if (memcmp(elfHeader.e_ident, "\x7f" "ELF", 4) != 0)
	{
		kprintf("insmod(%s): %s: not an ELF64 file: magic number incorrect\n", modname, path);
		vfsClose(fp);
		spinlockRelease(&modLock);
		return -1;
	};

	if (elfHeader.e_ident[EI_CLASS] != ELFCLASS64)
	{
		kprintf("insmod(%s): %s: not an ELF64 file: this is an ELF32 file\n", modname, path);
		vfsClose(fp);
		spinlockRelease(&modLock);
		return -1;
	};

	if (elfHeader.e_ident[EI_DATA] != ELFDATA2LSB)
	{
		kprintf("insmod(%s): %s: not a kernel module image: data is in big endian\n", modname, path);
		vfsClose(fp);
		spinlockRelease(&modLock);
		return -1;
	};

	if (elfHeader.e_ident[EI_VERSION] != 1)
	{
		kprintf("insmod(%s): %s: ELF version should be 1, got %d\n", modname, path, elfHeader.e_ident[EI_VERSION]);
		vfsClose(fp);
		spinlockRelease(&modLock);
		return -1;
	};

	if (elfHeader.e_type != ET_REL)
	{
		kprintf("insmod(%s): %s: not a kernel module image: this is not a relocatable object\n", modname, path);
		vfsClose(fp);
		spinlockRelease(&modLock);
		return -1;
	};

	if (elfHeader.e_shentsize != sizeof(Elf64_Shdr))
	{
		kprintf("insmod(%s): %s: e_shentsize is %d, should be %d\n", modname, path, elfHeader.e_shentsize, sizeof(Elf64_Shdr));
		vfsClose(fp);
		spinlockRelease(&modLock);
		return -1;
	};

	Elf64_Shdr shstr;
	fp->seek(fp, elfHeader.e_shoff + elfHeader.e_shstrndx * sizeof(Elf64_Shdr), SEEK_SET);
	if (vfsRead(fp, &shstr, sizeof(Elf64_Shdr)) < sizeof(Elf64_Shdr))
	{
		kprintf("insmod(%s): %s: EOF while reading string section header\n", modname, path);
		vfsClose(fp);
		spinlockRelease(&modLock);
		return -1;
	};

	fp->seek(fp, shstr.sh_offset, SEEK_SET);
	char *strings = (char*) kmalloc(shstr.sh_size);
	if (vfsRead(fp, strings, shstr.sh_size) < shstr.sh_size)
	{
		kprintf("insmod(%s): %s: EOF while reading string section contents\n", modname, path);
		vfsClose(fp);
		kfree(strings);
		spinlockRelease(&modLock);
		return -1;
	};

	if (flags & INSMOD_VERBOSE)
	{
		kprintf("insmod(%s): string section is %d bytes\n", modname, shstr.sh_size);
		kprintf("insmod(%s): reading kernel module image %s containing %d sections\n", modname, path, elfHeader.e_shnum);
	};

	Elf64_Shdr section;
	fp->seek(fp, elfHeader.e_shoff, SEEK_SET);
	Elf64_Half i;

	Elf64_Word shModbody = 0;
	Elf64_Word shSymtab = 0;
	Elf64_Word shRela = 0;
	Elf64_Word shStrtab = 0;

	for (i=0; i<elfHeader.e_shnum; i++)
	{
		if (vfsRead(fp, &section, sizeof(Elf64_Shdr)) < sizeof(Elf64_Shdr))
		{
			kprintf("insmod(%s): %s: EOF while reading sections\n", modname, path);
			kfree(strings);
			vfsClose(fp);
			spinlockRelease(&modLock);
			return -1;
		};

		const char *name = &strings[section.sh_name];
		if (flags & INSMOD_VERBOSE)
		{
			kprintf("insmod(%s): section [%d]: name=%s\n", modname, i, name);
		};

		if (strcmp(name, ".modbody") == 0)
		{
			if (section.sh_addr != 0)
			{
				kprintf("insmod(%s): section .modbody has nonzero load address %a\n", modname, section.sh_addr);
				vfsClose(fp);
				kfree(strings);
				spinlockRelease(&modLock);
				return -1;
			};

			if (section.sh_type != SHT_PROGBITS)
			{
				kprintf("insmod(%s): section .modbody is not a PROGBITS section\n", modname);
				vfsClose(fp);
				kfree(strings);
				spinlockRelease(&modLock);
				return -1;
			};

			shModbody = i;
		};

		if (strcmp(name, ".rela.modbody") == 0)
		{
			if (section.sh_type != SHT_RELA)
			{
				kprintf("insmod(%s): section .rela.modbody is not a RELA section\n", modname);
				vfsClose(fp);
				kfree(strings);
				spinlockRelease(&modLock);
				return -1;
			};

			shRela = i;
		};

		if (strcmp(name, ".symtab") == 0)
		{
			if (section.sh_type != SHT_SYMTAB)
			{
				kprintf("insmod(%s): section .symtab is not a SYMTAB section\n", modname);
				vfsClose(fp);
				kfree(strings);
				spinlockRelease(&modLock);
				return -1;
			};

			shSymtab = i;
		};

		if (strcmp(name, ".strtab") == 0)
		{
			shStrtab = i;
		};
	};
	kfree(strings);

	if (shModbody == 0)
	{
		kprintf("insmod(%s): section .modbody not found\n", modname);
		vfsClose(fp);
		spinlockRelease(&modLock);
		return -1;
	};

	if (shRela == 0)
	{
		kprintf("insmod(%s): section .rela.modbody not found\n", modname);
		vfsClose(fp);
		spinlockRelease(&modLock);
		return -1;
	};

	if (shSymtab == 0)
	{
		kprintf("insmod(%s): section .symtab not found\n", modname);
		vfsClose(fp);
		spinlockRelease(&modLock);
		return -1;
	};

	if (shStrtab == 0)
	{
		kprintf("insmod(%s): section .strtab not found\n", modname);
		vfsClose(fp);
		spinlockRelease(&modLock);
		return -1;
	};

	if (flags & INSMOD_VERBOSE)
	{
		kprintf("insmod(%s): using section %d for module body\n", modname, shModbody);
		kprintf("insmod(%s): using section %d for symbol table\n", modname, shSymtab);
		kprintf("insmod(%s): using section %d for relocation table\n", modname, shRela);
		kprintf("insmod(%s): using section %d for string table\n", modname, shStrtab);
	};

	Elf64_Shdr strtabSection;
	fp->seek(fp, elfHeader.e_shoff + shStrtab * sizeof(Elf64_Shdr), SEEK_SET);
	vfsRead(fp, &strtabSection, sizeof(Elf64_Shdr));
	fp->seek(fp, strtabSection.sh_offset, SEEK_SET);
	strings = (char*) kmalloc(strtabSection.sh_size);
	vfsRead(fp, strings, strtabSection.sh_size);
	
	int modblock = allocModuleBlock();
	if (modblock == -1)
	{
		kprintf("insmod(%s): module limit exceeded (out of module area)\n", modname);
		vfsClose(fp);
		kfree(strings);
		spinlockRelease(&modLock);
		return -1;
	};

	uint64_t baseAddr = (uint64_t)MODULE_AREA_BASE + (uint64_t)modblock * (uint64_t)MODULE_BLOCK_SIZE;
	if (flags & INSMOD_VERBOSE)
	{
		kprintf("insmod(%s): loading at %a (modblock %d)\n", modname, baseAddr, modblock);
	};

	Elf64_Shdr modbodySection;
	fp->seek(fp, elfHeader.e_shoff + shModbody * sizeof(Elf64_Shdr), SEEK_SET);
	vfsRead(fp, &modbodySection, sizeof(Elf64_Shdr));

	if (flags & INSMOD_VERBOSE)
	{
		kprintf("insmod(%s): size of section .modbody is %d bytes\n", modname, modbodySection.sh_size);
	};

	Module *module = (Module*) kmalloc(sizeof(Module));
	strcpy(module->name, modname);
	module->block = modblock;
	module->numSectors = modbodySection.sh_size / MODULE_SECTOR_SIZE;
	if (modbodySection.sh_size % MODULE_SECTOR_SIZE) module->numSectors++;
	if (flags & INSMOD_VERBOSE)
	{
		kprintf("insmod(%s): mapping %d sectors (2MB blocks) for module body\n", modname, module->numSectors);
	};
	module->frames = mapModuleArea(modblock, module->numSectors);

	if (flags & INSMOD_VERBOSE)
	{
		kprintf("insmod(%s): loading module body into memory\n", modname);
	};

	// not all of the section may be in the file, so read as much as possible and zero the rest.
	memset((void*)baseAddr, 0, modbodySection.sh_size);
	fp->seek(fp, modbodySection.sh_offset, SEEK_SET);
	vfsRead(fp, (void*)baseAddr, modbodySection.sh_size);

	// load the symbol table into memory
	Elf64_Shdr symtabSection;
	fp->seek(fp, elfHeader.e_shoff + shSymtab * sizeof(Elf64_Shdr), SEEK_SET);
	vfsRead(fp, &symtabSection, sizeof(Elf64_Shdr));
	Elf64_Sym *symtab = (Elf64_Sym*) kmalloc(symtabSection.sh_size);
	module->numSymbols = symtabSection.sh_size / sizeof(Elf64_Sym);
	fp->seek(fp, symtabSection.sh_offset, SEEK_SET);
	vfsRead(fp, symtab, symtabSection.sh_size);

	// search for necessary symbols
	int (*moduleInitEvent)(const char*) = NULL;
	for (i=0; i<(symtabSection.sh_size / sizeof(Elf64_Sym)); i++)
	{
		Elf64_Sym *symbol = &symtab[i];
		const char *symname = &strings[symbol->st_name];
		if (symbol->st_shndx == shModbody)
		{
			if (strcmp(symname, "__module_init") == 0)
			{
				uint64_t addr = baseAddr + symbol->st_value;
				if (flags & INSMOD_VERBOSE)
				{
					kprintf("insmod(%s): found __module_init at %a\n", modname, addr);
				};
				moduleInitEvent = (int (*)(const char*)) addr;
			}
			else if (strcmp(symname, "_fini") == 0)
			{
				uint64_t addr = baseAddr + symbol->st_value;
				if (flags & INSMOD_VERBOSE)
				{
					kprintf("insmod(%s): found _fini at %a\n", modname, addr);
				};
				module->fini = (void(*)(void)) addr;
			}
			else if (strcmp(symname, "__module_fini") == 0)
			{
				uint64_t addr = baseAddr + symbol->st_value;
				if (flags & INSMOD_VERBOSE)
				{
					kprintf("insmod(%s): found __module_fini at %a\n", modname, addr);
				};
				module->modfini = (int(*)(void)) addr;
			};
		};
	};

	if (moduleInitEvent == NULL)
	{
		kprintf("insmod(%s): warning: no module init event\n", modname);
	};

	// parse the relocations
	Elf64_Shdr relaSection;
	fp->seek(fp, elfHeader.e_shoff + shRela * sizeof(Elf64_Shdr), SEEK_SET);
	vfsRead(fp, &relaSection, sizeof(Elf64_Shdr));
	uint64_t numRelocs = relaSection.sh_size / sizeof(Elf64_Rela);

	if (flags & INSMOD_VERBOSE)
	{
		kprintf("insmod(%s): the module body has %d relocation entries\n", modname, numRelocs);
	};

	fp->seek(fp, relaSection.sh_offset, SEEK_SET);
	Elf64_Rela rela;
	for (i=0; i<numRelocs; i++)
	{
		vfsRead(fp, &rela, sizeof(Elf64_Rela));
		Elf64_Xword type = ELF64_R_TYPE(rela.r_info);
		Elf64_Xword symidx = ELF64_R_SYM(rela.r_info);

		Elf64_Sym *symbol = &symtab[symidx];
		const char *symname = &strings[symbol->st_name];
		if (symbol->st_name == 0) symname = "<noname>";

		void *symaddr = NULL;
		if (symbol->st_shndx == 0)
		{
			// undefined reference; resolve against kernel symbol table
			if (strcmp(symname, "__this_module") == 0)
			{
				symaddr = module;
			}
			else
			{
				symaddr = getSymbol(symname);
				if (symaddr == NULL)
				{
					kprintf("insmod(%s): undefined reference to '%s'\n", modname, symname);
					vfsClose(fp);
					kfree(strings);
					kfree(symtab);
					unmapModuleArea(module);
					spinlockRelease(&modLock);
					return -1;
				};
			};
		}
		else if (symbol->st_shndx == shModbody)
		{
			// internal reference
			symaddr = (void*) (baseAddr + symbol->st_value);
		}
		else
		{
			// impossible reference
			kprintf("insmod(%s): reference to symbol '%s' contained in non-linked section\n", modname, symname);
			vfsClose(fp);
			kfree(strings);
			kfree(symtab);
			unmapModuleArea(module);
			spinlockRelease(&modLock);
			return -1;
		};

		// address of the field to relocate
		void *reladdr = (void*) (baseAddr + rela.r_offset);

		switch (type)
		{
		case R_X86_64_64:
			if (flags & INSMOD_VERBOSE)
			{
				kprintf("insmod(%s): R_X86_64_64 at .modbody+%d = '%s'+%d = %a+%d\n",
					modname, rela.r_offset, symname, rela.r_addend, symaddr, rela.r_addend);
			};
			*((uint64_t*)reladdr) = (uint64_t) symaddr + rela.r_addend;
			break;
		default:
			kprintf("insmod(%s): found an invalid/unknown relocation entry\n", modname);
			vfsClose(fp);
			kfree(strings);
			kfree(symtab);
			unmapModuleArea(module);
			spinlockRelease(&modLock);
			kfree(module);
			return -1;
		};
	};

	module->symtab = symtab;
	module->strings = strings;
	module->baseAddr = baseAddr;
	module->next = NULL;
	vfsClose(fp);

	if (firstModule == NULL)
	{
		firstModule = module;
		lastModule = module;
	}
	else
	{
		lastModule->next = module;
		lastModule = module;
	};
	spinlockRelease(&modLock);

	if (flags & INSMOD_VERBOSE) kprintf("insmod(%s): running module initializers\n", modname);
	void (*modInitSection)(void) = (void (*)(void)) baseAddr;
	modInitSection();

	if (moduleInitEvent != NULL)
	{
		if (flags & INSMOD_VERBOSE)
		{
			kprintf("insmod(%s): calling module init event\n", modname);
		};
		
		int status = moduleInitEvent(opt);
		if (status != MODINIT_OK)
		{
			// TODO: add rmmod() below but read TODO
			//rmmod(modname, 0);
			if (status == MODINIT_CANCEL)
			{
				if (flags & INSMOD_VERBOSE)
				{
					kprintf("insmod(%s): module not needed\n");
				};
				
				return 0;
			}
			else if (status == MODINIT_FATAL)
			{
				if (flags & INSMOD_VERBOSE)
				{
					kprintf("insmod(%s): module init fatal error\n");
				};
				
				return -1;
			};
		};
	};

	if (flags & INSMOD_VERBOSE)
	{
		kprintf("insmod(%s): module loaded\n", modname);
	};

	return 0;
};

int rmmod(const char *modname, int flags)
{
	spinlockAcquire(&modLock);
	if (firstModule == NULL)
	{
		spinlockRelease(&modLock);
		getCurrentThread()->therrno = ENOENT;
		return -1;
	};

	Module *mod = firstModule;
	Module *prevmod = NULL;

	while (strcmp(mod->name, modname) != 0)
	{
		if (mod->next == NULL)
		{
			spinlockRelease(&modLock);
			getCurrentThread()->therrno = ENOENT;
			return -1;
		};

		prevmod = mod;
		mod = mod->next;
	};

	if (mod->modfini != NULL)
	{
		if (flags & RMMOD_VERBOSE)
		{
			kprintf("rmmod(%s): calling module fini event\n", modname);
		};

		int status = mod->modfini();

		if (status != 0)
		{
			if (flags & RMMOD_FORCE)
			{
				kprintf("rmmod(%s): WARNING: module refused to unlink but RMMOD_FORCE is set!\n", modname);
			}
			else
			{
				kprintf("rmmod(%s): module refused to unlink\n", modname);
				spinlockRelease(&modLock);
				getCurrentThread()->therrno = EPERM;
				return -1;
			};
		};
	}
	else
	{
		if (flags & RMMOD_FORCE)
		{
			kprintf("rmmod(%s): WARNING: no module fini event by RMMOD_FORCE is set!\n", modname);
		}
		else
		{
			kprintf("rmmod(%s): no module fini event (use RMMOD_FORCE to unload anyway)\n", modname);
			spinlockRelease(&modLock);
			getCurrentThread()->therrno = EPERM;
			return -1;
		};
	};

	if (mod->fini != NULL)
	{
		if (flags & RMMOD_VERBOSE)
		{
			kprintf("rmmod(%s): calling module fininalizers\n", modname);
		};

		mod->fini();
	}
	else if (flags & RMMOD_VERBOSE)
	{
		kprintf("rmmod(%s): WARNING: no module finalizers\n", modname);
	};

	if (flags & RMMOD_VERBOSE)
	{
		kprintf("rmmod(%s): unmapping module memory\n", modname);
	};

	unmapModuleArea(mod);

	if (flags & RMMOD_VERBOSE)
	{
		kprintf("rmmod(%s): unlinking module from kernel\n", modname);
	};

	if (prevmod == NULL)
	{
		firstModule = NULL;
	}
	else
	{
		prevmod->next = mod->next;
	};

	kfree(mod->symtab);
	kfree((void*)mod->strings);
	kfree(mod);

	spinlockRelease(&modLock);

	if (flags & RMMOD_VERBOSE)
	{
		kprintf("rmmod(%s): finished successfully\n", modname);
	};

	return 0;
};

void dumpModules()
{
	// please call this with interrupts disabled
	Module *module;
	kprintf("BASE\t\t\tBODY\tNAME\n");

	for (module=firstModule; module!=NULL; module=module->next)
	{
		kprintf("%a\t%dMB\t%s\n", module->baseAddr, module->numSectors*2, module->name);
	};
};

void findDebugSymbolInModules(uint64_t addr, SymbolInfo *info)
{
	spinlockAcquire(&modLock);

	Module *module;
	for (module=firstModule; module!=NULL; module=module->next)
	{
		uint64_t end = module->baseAddr + 0x200000 * module->numSectors;
		if ((addr >= module->baseAddr) && (addr < end))
		{
			uint64_t i;
			Elf64_Sym *best = module->symtab;
			for (i=0; i<module->numSymbols; i++)
			{
				Elf64_Sym *symbol = &module->symtab[i];
				uint64_t saddr = module->baseAddr + symbol->st_value;
				if ((symbol->st_value > best->st_value) && (saddr < addr))
				{
					best = symbol;
				};
			};

			info->modname = module->name;
			info->symname = &module->strings[best->st_name];
			info->offset = addr - module->baseAddr - best->st_value;
			spinlockRelease(&modLock);
			return;
		};
	};

	spinlockRelease(&modLock);

	Symbol *sym = findSymbolForAddr(addr);
	info->modname = "kernel";
	info->symname = sym->name;
	info->offset = addr - (uint64_t)sym->ptr;
};

void rmmodAll()
{
	if (firstModule == NULL) return;

	Module *mod = firstModule;

	while (mod != NULL)
	{
		if (mod->modfini != NULL)
		{
			int status = mod->modfini();

			if (status != 0)
			{
				kprintf("WARNING: Removing module %s by force\n", mod->name);
			};
		};

		if (mod->fini != NULL)
		{
			mod->fini();
		};

		mod = mod->next;
	};
};

FileSystem *getModulefs()
{
	return moduleFileSystem;
};
