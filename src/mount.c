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

#include <glidix/mount.h>
#include <glidix/spinlock.h>
#include <glidix/console.h>
#include <glidix/string.h>
#include <glidix/memory.h>
#include <glidix/initrdfs.h>
#include <glidix/devfs.h>
#include <glidix/module.h>
#include <glidix/sched.h>
#include <glidix/errno.h>
#include <glidix/procfs.h>

static Spinlock mountLock;
static MountPoint *mountTable;

void initMount()
{
	kprintf("Initializing the mountpoint table... ");
	spinlockRelease(&mountLock);
	mountTable = NULL;
	kprintf("%$\x02" "Done%#\n");

	kprintf("Mounting the initrdfs at /initrd... ");
	int errno = mount("/initrd/", getInitrdfs(), 0);
	if (errno != 0)
	{
		kprintf("%$\x04" "Failed%#\n");
		panic("failed to mount the initrdfs (errno %d)", errno);
	};
	kprintf("%$\x02" "Done%#\n");

	kprintf("Mounting the devfs at /dev... ");
	errno = mount("/dev/", getDevfs(), 0);
	if (errno != 0)
	{
		kprintf("%$\x04", "Failed%#\n");
		panic("failed to mount the devfs (errno %d)", errno);
	};

	kprintf("%$\x02" "Done%#\n");

	kprintf("Mounting the modfs at /sys/mod... ");
	errno = mount("/sys/mod/", getModulefs(), 0);
	if (errno != 0)
	{
		kprintf("%$\x04", "Failed%#\n");
		panic("failed to mount the modfs (errno %d)", errno);
	};
	kprintf("%$\x02" "Done%#\n");

	kprintf("Mounting the procfs at /proc... ");
	errno = mount("/proc/", getProcfs(), 0);
	if (errno != 0)
	{
		kprintf("%$\x04", "Failed%#\n");
		panic("failed to mount the procfs (errno %d)", errno);
	};
	kprintf("%$\x02" "Done%#\n");
};

static dev_t nextDev = 1;
int mount(const char *prefix, FileSystem *fs, int flags)
{
	if (flags != 0)
	{
		return MOUNT_BAD_FLAGS;
	};

	if ((prefix[0] != '/') || (prefix[strlen(prefix)-1] != '/'))
	{
		return MOUNT_BAD_PREFIX;
	};

	size_t szprefix = strlen(prefix);

	// create the mountpoint
	MountPoint *mp = (MountPoint*) kmalloc(sizeof(MountPoint));
	strcpy(mp->prefix, prefix);
	mp->fs = fs;

	// link the mountpoint (and sort...)
	spinlockAcquire(&mountLock);

	if (mountTable == NULL)
	{
		mp->prev = mp->next = NULL;
		mountTable = mp;
	}
	else
	{
		MountPoint *scan = mountTable;
		while (1)
		{
			if (strlen(scan->prefix) < szprefix)
			{
				if (scan->prev == NULL)
				{
					mp->next = scan;
					mp->prev = NULL;
					scan->prev = mp;
					mountTable = mp;
				}
				else
				{
					mp->prev = scan->prev;
					if (scan->prev != NULL) scan->prev->next = mp;
					if (scan->next != NULL) scan->next->prev = mp;
					mp->next = scan;
					scan->prev = mp;
				};
				break;
			};

			if (scan->next == NULL)
			{
				scan->next = mp;
				mp->prev = scan;
				mp->next = NULL;
				break;
			};

			scan = scan->next;
		};
	};

	fs->dev = nextDev++;
	spinlockRelease(&mountLock);
	return 0;
};

int unmount(const char *prefix)
{
	if (getCurrentThread()->euid != 0)
	{
		getCurrentThread()->therrno = EPERM;
		return -1;
	};

	spinlockAcquire(&mountLock);
	MountPoint *mp = mountTable;

	int status = -1;

	while (mp != NULL)
	{
		if (strcmp(prefix, mp->prefix) == 0)
		{
			if (mp->fs->unmount != NULL)
			{
				if (mp->fs->unmount(mp->fs) != 0)
				{
					getCurrentThread()->therrno = EBUSY;
					spinlockRelease(&mountLock);
					return -1;
				};
			}
			else
			{
				getCurrentThread()->therrno = EINVAL;
				spinlockRelease(&mountLock);
				return -1;
			};

			// unlink
			if (mp->prev != NULL) mp->prev->next = mp->next;
			if (mp->next != NULL) mp->next->prev = mp->prev;
			if (mp == mountTable)
			{
				mountTable = mp->next;
			};

			kfree(mp->fs);
			kfree(mp);
			status = 0;
			break;
		};

		mp = mp->next;
	};

	spinlockRelease(&mountLock);

	if (status == -1)
	{
		getCurrentThread()->therrno = EINVAL;
	};

	return status;
};

int resolveMounts(const char *path, SplitPath *out)
{
	spinlockAcquire(&mountLock);
	MountPoint *mp = mountTable;

	size_t szpath = strlen(path);
	while (mp != NULL)
	{
		if (strlen(mp->prefix) <= szpath)
		{
			if (memcmp(mp->prefix, path, strlen(mp->prefix)) == 0)
			{
				out->fs = mp->fs;
				strcpy(out->filename, &path[strlen(mp->prefix)]);
				memset(out->parent, 0, 512);
				memcpy(out->parent, path, strlen(mp->prefix)-1);
				spinlockRelease(&mountLock);
				return 0;
			};
		};

		mp = mp->next;
	};

	spinlockRelease(&mountLock);
	return -1;
};

void dumpMountTable()
{
	spinlockAcquire(&mountLock);
	MountPoint *mp = mountTable;
	while (mp != NULL)
	{
		const char *prev = "<null>";
		if (mp->prev != NULL)
		{
			prev = mp->prev->prefix;
		};
		kprintf("%s (type=%s, prev=%s)\n", mp->prefix, mp->fs->fsname, prev);
		mp = mp->next;
	};
	spinlockRelease(&mountLock);
};

void unmountAll()
{
	MountPoint *mp = mountTable;
	while (mp != NULL)
	{
		if (mp->fs->unmount != NULL)
		{
			int status = mp->fs->unmount(mp->fs);
			if (status != 0)
			{
				kprintf("WARNING: unmounting %s by force\n", mp->prefix);
			};
		};

		mp = mp->next;
	};
};
