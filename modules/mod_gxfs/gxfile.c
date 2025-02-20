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

#include "gxfs.h"
#include <glidix/console.h>
#include <glidix/memory.h>
#include <glidix/string.h>
#include <glidix/time.h>

ssize_t gxfile_read(File *fp, void *buffer, size_t size)
{
	GXFile *gxfile = (GXFile*) fp->fsdata;
	semWait(&gxfile->gxfs->sem);
	ssize_t ret = GXReadInode(&gxfile->gxino, buffer, size);
	semSignal(&gxfile->gxfs->sem);
	return ret;
};

ssize_t gxfile_write(File *fp, const void *buffer, size_t size)
{
	GXFile *gxfile = (GXFile*) fp->fsdata;
	semWait(&gxfile->gxfs->sem);
	ssize_t ret = GXWriteInode(&gxfile->gxino, buffer, size);
	gxfile->dirty = 1;
	semSignal(&gxfile->gxfs->sem);
	return ret;
};

void gxfile_close(File *fp)
{
	GXFile *gxfile = (GXFile*) fp->fsdata;
	semWait(&gxfile->gxfs->sem);
	gxfile->gxfs->numOpenInodes--;
	semSignal(&gxfile->gxfs->sem);
	fp->fsync(fp);
	kfree(fp->fsdata);
};

off_t gxfile_seek(File *fp, off_t off, int whence)
{
	GXFile *gxfile = (GXFile*) fp->fsdata;
	semWait(&gxfile->gxfs->sem);
	gxfsInode inode;
	GXReadInodeHeader(&gxfile->gxino, &inode);

	off_t newPos;
	off_t currentPos = gxfile->gxino.pos;

	switch (whence)
	{
	case SEEK_SET:
		newPos = off;
		break;
	case SEEK_CUR:
		newPos = currentPos + off;
		break;
	case SEEK_END:
		newPos = inode.inoSize + off;
		break;
	};

	if (newPos < 0) newPos = 0;
	gxfile->gxino.pos = newPos;
	semSignal(&gxfile->gxfs->sem);
	return newPos;
};

int gxfile_dup(File *me, File *fp, size_t szfile)
{
	GXFile *gxfile = (GXFile*) me->fsdata;
	semWait(&gxfile->gxfs->sem);
	gxfile->gxfs->numOpenInodes++;

	memcpy(fp, me, szfile);
	fp->fsdata = kmalloc(sizeof(GXFile));
	memcpy(fp->fsdata, me->fsdata, sizeof(GXFile));

	semSignal(&gxfile->gxfs->sem);
	return 0;
};

int gxfile_fstat(File *fp, struct stat *st)
{
	GXFile *gxfile = (GXFile*) fp->fsdata;
	semWait(&gxfile->gxfs->sem);

	gxfsInode inode;
	GXReadInodeHeader(&gxfile->gxino, &inode);

	st->st_dev = 0;
	st->st_ino = gxfile->gxino.ino;
	st->st_mode = inode.inoMode;
	st->st_nlink = inode.inoLinks;
	st->st_uid = inode.inoOwner;
	st->st_gid = inode.inoGroup;
	st->st_rdev = 0;
	st->st_size = inode.inoSize;
	st->st_blksize = gxfile->gxfs->cis.cisBlockSize;
	st->st_blocks = st->st_size / st->st_blksize + 1;
	st->st_atime = inode.inoATime;
	st->st_ctime = inode.inoCTime;
	st->st_mtime = inode.inoMTime;

	semSignal(&gxfile->gxfs->sem);
	return 0;
};

int gxfile_fchmod(File *fp, mode_t mode)
{
	GXFile *gxfile = (GXFile*) fp->fsdata;
	semWait(&gxfile->gxfs->sem);

	gxfsInode inode;
	GXReadInodeHeader(&gxfile->gxino, &inode);
	inode.inoMode = (inode.inoMode & 0xF000) | (mode & 0x0FFF);
	inode.inoCTime = time();
	GXWriteInodeHeader(&gxfile->gxino, &inode);

	semSignal(&gxfile->gxfs->sem);
	return 0;
};

int gxfile_fchown(File *fp, uid_t uid, gid_t gid)
{
	GXFile *gxfile = (GXFile*) fp->fsdata;
	semWait(&gxfile->gxfs->sem);

	gxfsInode inode;
	GXReadInodeHeader(&gxfile->gxino, &inode);
	inode.inoOwner = uid;
	inode.inoGroup = gid;
	inode.inoCTime = time();
	GXWriteInodeHeader(&gxfile->gxino, &inode);

	semSignal(&gxfile->gxfs->sem);
	return 0;
};

void gxfile_fsync(File *fp)
{
	GXFile *gxfile = (GXFile*) fp->fsdata;
	semWait(&gxfile->gxfs->sem);

	gxfsInode inode;
	GXReadInodeHeader(&gxfile->gxino, &inode);
	time_t now = time();
	inode.inoATime = now;
	if (gxfile->dirty) inode.inoMTime = now;
	GXWriteInodeHeader(&gxfile->gxino, &inode);
	if (gxfile->gxfs->fp->fsync != NULL) gxfile->gxfs->fp->fsync(gxfile->gxfs->fp);

	semSignal(&gxfile->gxfs->sem);
};

void gxfile_truncate(File *fp, off_t length)
{
	GXFile *gxfile = (GXFile*) fp->fsdata;
	semWait(&gxfile->gxfs->sem);

	gxfsInode inode;
	GXReadInodeHeader(&gxfile->gxino, &inode);

	size_t newSize = (size_t) length;
	if (newSize > inode.inoSize)
	{
		off_t oldPos = gxfile->gxino.pos;
		gxfile->gxino.pos = inode.inoSize;
		uint8_t zero = 0;
		size_t toAppend = newSize - inode.inoSize;
		while (toAppend--) GXWriteInode(&gxfile->gxino, &zero, 1);
		gxfile->gxino.pos = oldPos;
	}
	else
	{
		GXShrinkInode(&gxfile->gxino, inode.inoSize-newSize, &inode);
	};

	GXWriteInodeHeader(&gxfile->gxino, &inode);
	gxfile->dirty = 1;

	semSignal(&gxfile->gxfs->sem);
};

int GXOpenFile(GXFileSystem *gxfs, File *fp, ino_t ino)
{
	semWait(&gxfs->sem);
	//kprintf_debug("GXOpenFile(%d)\n", ino);
	GXFile *gxfile = (GXFile*) kmalloc(sizeof(GXFile));
	fp->fsdata = gxfile;

	gxfile->gxfs = gxfs;
	gxfile->dirty = 0;
	GXOpenInode(gxfs, &gxfile->gxino, ino);

	fp->read = gxfile_read;
	fp->write = gxfile_write;
	fp->close = gxfile_close;
	fp->seek = gxfile_seek;
	fp->dup = gxfile_dup;
	fp->fstat = gxfile_fstat;
	fp->fchmod = gxfile_fchmod;
	fp->fchown = gxfile_fchown;
	fp->fsync = gxfile_fsync;
	fp->truncate = gxfile_truncate;

	gxfs->numOpenInodes++;
	semSignal(&gxfs->sem);
	return 0;
};
