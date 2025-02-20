/*
	Glidix Runtime

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

#ifndef _SYS_STAT_H
#define _SYS_STAT_H

#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

#define	S_IFMT				0xF000
#define	S_IFBLK				030000
#define	S_IFCHR				020000
#define	S_IFDIR				010000
#define	S_IFIFO				040000
#define	S_IFREG				0
#define	S_IFLNK				050000

#define	S_ISBLK(m)			((m & 0xF000) == 030000)
#define	S_ISCHR(m)			((m & 0xF000) == 020000)
#define	S_ISDIR(m)			((m & 0xF000) == 010000)
#define	S_ISFIFO(m)			((m & 0xF000) == 040000)
#define	S_ISREG(m)			((m & 0xF000) == 0)
#define	S_ISLNK(m)			((m & 0xF000) == 050000)

#define	S_TYPEISMQ(m)			(0)
#define	S_TYPEISSEM(m)			(0)
#define	S_TYPEISSHM(m)			(0)

#define	S_IRWXU				0700
#define	S_IRUSR				0400
#define	S_IWUSR				0200
#define	S_IXUSR				0100
#define	S_IRWXG				0070
#define	S_IRGRP				0040
#define	S_IWGRP				0020
#define	S_IXGRP				0010
#define	S_IRWXO				0007
#define	S_IROTH				0004
#define	S_IWOTH				0002
#define	S_IXOTH				0001
#define	S_ISUID				04000
#define	S_ISGID				02000
#define	S_ISVTX				01000

struct stat
{
	dev_t				st_dev;
	ino_t				st_ino;
	mode_t				st_mode;
	nlink_t				st_nlink;
	uid_t				st_uid;
	gid_t				st_gid;
	dev_t				st_rdev;
	off_t				st_size;
	blksize_t			st_blksize;
	blkcnt_t			st_blocks;
	time_t				st_atime;
	time_t				st_mtime;
	time_t				st_ctime;
};

/* implemented by libglidix directly */
int	stat(const char *path, struct stat *buf);
int	fstat(int fd, struct stat *buf);
int	lstat(const char *path, struct stat *buf);
int	chmod(const char *path, mode_t mode);
int	fchmod(int fd, mode_t mode);
int	mkdir(const char *path, mode_t mode);
mode_t	umask(mode_t cmask);

#ifdef __cplusplus
}	/* extern "C" */
#endif

#endif
