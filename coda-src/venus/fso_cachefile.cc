/* BLURB gpl

                           Coda File System
                              Release 5

          Copyright (c) 1987-1999 Carnegie Mellon University
                  Additional copyrights listed below

This  code  is  distributed "AS IS" without warranty of any kind under
the terms of the GNU General Public Licence Version 2, as shown in the
file  LICENSE.  The  technical and financial  contributors to Coda are
listed in the file CREDITS.

                        Additional copyrights
                           none currently

#*/

/*
 *    Cache file management
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/types.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <errno.h>
#include <stdio.h>
#include <setjmp.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>

#ifdef __cplusplus
}
#endif

/* interfaces */
#include <vice.h>
#include <mkpath.h>
#include <copyfile.h>

/* from venus */
#include "fso.h"
#include "venus.private.h"

/*  *****  CacheFile Members  *****  */

/* Pre-allocation routine. */
/* MUST be called from within transaction! */
CacheFile::CacheFile(int i)
{
    /* Assume caller has done RVMLIB_REC_OBJECT! */
    /* RVMLIB_REC_OBJECT(*this); */
    sprintf(name, "%02X/%02X/%02X/%02X",
	    (i>>24) & 0xff, (i>>16) & 0xff, (i>>8) & 0xff, i & 0xff);
    inode = (ino_t)-1;
    length = validdata = 0;
    refcnt = 1;
    numopens = 0;
    /* Container reset will be done by eventually by FSOInit()! */
    LOG(100, ("CacheFile::CacheFile(%d): %s (this=0x%x)\n", i, name, this));
}


CacheFile::CacheFile()
{
    CODA_ASSERT(inode == (ino_t)-1 || length == 0);
    refcnt = 1;
    numopens = 0;
}


CacheFile::~CacheFile()
{
    LOG(10, ("CacheFile::~CacheFile: %s (this=0x%x)\n", name, this));
    CODA_ASSERT(inode == (ino_t)-1 || length == 0);
}


/* MUST NOT be called from within transaction! */
void CacheFile::Validate()
{
    if (!ValidContainer())
	Reset();
}


/* MUST NOT be called from within transaction! */
void CacheFile::Reset()
{
    if (inode != (ino_t)-1 && length != 0) {
        Recov_BeginTrans();
	Truncate(0);
	Recov_EndTrans(MAXFP);
    }
}

int CacheFile::ValidContainer()
{
    int code = 0;
    struct stat tstat;
    
    if (inode == (ino_t)-1)
        return 0;
    int valid = (code = ::stat(name, &tstat)) == 0 &&
#if !defined(DJGPP) && !defined(__CYGWIN32__)
      tstat.st_uid == (uid_t)V_UID &&
      tstat.st_gid == (gid_t)V_GID &&
      (tstat.st_mode & ~S_IFMT) == V_MODE &&
      tstat.st_ino == inode &&
#endif
      tstat.st_size == (off_t)length;

    if (!valid && LogLevel >= 0) {
	dprint("CacheFile::ValidContainer: %s invalid\n", name);
	if (code == 0)
	    dprint("\t(%u, %u), (%u, %u), (%o, %o), (%d, %d), (%d, %d)\n",
		   tstat.st_uid, (uid_t)V_UID, tstat.st_gid, (gid_t)V_GID,
		   (tstat.st_mode & ~S_IFMT), V_MODE,
		   tstat.st_ino, inode, tstat.st_size, length);
	else
	    dprint("\tstat failed (%d)\n", errno);
    }

    return(valid);
}

/* Must be called from within a transaction!  Assume caller has done
   RVMLIB_REC_OBJECT() */

void CacheFile::Create(int newlength)
{
    LOG(10, ("CacheFile::Create: %s, %d\n", name, newlength));

    int tfd;
    struct stat tstat;
    if (mkpath(name, V_MODE | 0100)<0)
        CHOKE("CacheFile::Create: could not make path for %s", name);
    if ((tfd = ::open(name, O_RDWR | O_CREAT | O_TRUNC | O_BINARY, V_MODE)) < 0)
	CHOKE("CacheFile::Create: open failed (%d)", errno);

#ifndef DJGPP
#ifndef __CYGWIN32__
    if (::fchown(tfd, (uid_t)V_UID, (gid_t)V_GID) < 0)
	CHOKE("CacheFile::Create: fchown failed (%d)", errno);
#else
    if (::chown(name, (uid_t)V_UID, (gid_t)V_GID) < 0)
	CHOKE("CacheFile::Create: fchown failed (%d)", errno);
#endif
#endif
    if (::ftruncate(tfd, newlength) < 0)
      CHOKE("CacheFile::Create: ftruncate failed (%d)", errno);
    if (::fstat(tfd, &tstat) < 0)
	CHOKE("CacheFile::ResetContainer: fstat failed (%d)", errno);
    if (::close(tfd) < 0)
	CHOKE("CacheFile::ResetContainer: close failed (%d)", errno);

    inode = tstat.st_ino;
    validdata = 0;
    length = newlength;
    refcnt = 1;
    numopens = 0;
}


/* 
 * copies a cache file, data and attributes, to a new one.  
 */
int CacheFile::Copy(CacheFile *destination)
{
    ino_t ino;

    Copy(destination->name, &ino);

    destination->inode  = ino;
    destination->length = length;
    destination->validdata = validdata;

    return 0;
}

int CacheFile::Copy(char *destname, ino_t *ino, int recovering)
{
    LOG(10, ("CacheFile::Copy: from %s, %d, %d/%d, to %s\n",
	     name, inode, validdata, length, destname));

    int tfd, ffd;
    struct stat tstat;

    if (mkpath(destname, V_MODE | 0100) < 0) {
        LOG(0, ("CacheFile::Copy: could not make path for %s\n", name));
	return -1;
    }
    if ((tfd = ::open(destname, O_RDWR|O_CREAT|O_TRUNC|O_BINARY, V_MODE)) < 0) {
	LOG(0, ("CacheFile::Copy: open failed (%d)\n", errno));
	return -1;
    }
#ifndef DJGPP
    if (::fchmod(tfd, V_MODE) < 0)
	CHOKE("CacheFile::Copy: fchmod failed (%d)\n", errno);
#ifdef __CYGWIN32__
    if (::chown(destname, (uid_t)V_UID, (gid_t)V_GID) < 0)
	CHOKE("CacheFile::ResetCopy: fchown failed (%d)\n", errno);
#else
    if (::fchown(tfd, (uid_t)V_UID, (gid_t)V_GID) < 0)
	CHOKE("CacheFile::Copy: fchown failed (%d)\n", errno);
#endif
#endif
    if ((ffd = ::open(name, O_RDONLY| O_BINARY, V_MODE)) < 0)
	CHOKE("CacheFile::Copy: source open failed (%d)\n", errno);

    if (copyfile(ffd, tfd) < 0) {
	LOG(0, ("CacheFile::Copy failed! (%d)\n", errno));
	::close(ffd);
	::close(tfd);
	return -1;
    }

    if (::fstat(tfd, &tstat) < 0)
	CHOKE("CacheFile::Copy: fstat failed (%d)\n", errno);
    if (::close(tfd) < 0)
	CHOKE("CacheFile::Copy: close failed (%d)\n", errno);
    if (::close(ffd) < 0)
	CHOKE("CacheFile::Copy: source close failed (%d)\n", errno);
    
    CODA_ASSERT(recovering || (off_t)length == tstat.st_size);
    if (ino)
        *ino = tstat.st_ino;

    return 0;
}


int CacheFile::DecRef()
{
    if (--refcnt == 0)
    {
	length = validdata = 0;
	if (::unlink(name) < 0)
	    CHOKE("CacheFile::DecRef: unlink failed (%d)", errno);
    }
    return refcnt;
}


/* N.B. length member is NOT updated as side-effect! */
void CacheFile::Stat(struct stat *tstat)
{
    CODA_ASSERT(inode != (ino_t)-1);

    CODA_ASSERT(::stat(name, tstat) == 0);
#if ! defined(DJGPP) && ! defined(__CYGWIN32__)
    CODA_ASSERT(tstat->st_ino == inode);
#endif
}


/* MUST be called from within transaction! */
void CacheFile::Truncate(long newlen)
{
    CODA_ASSERT(inode != (ino_t)-1);

    if (length != newlen) {
	RVMLIB_REC_OBJECT(*this);
	length = validdata = newlen;
    }
#ifndef __CYGWIN32__
    CODA_ASSERT(::truncate(name, length) == 0);
#else 
    {
	int fd = open(name, O_WRONLY | O_BINARY);
	if ( fd < 0 ) CODA_ASSERT(0);
	CODA_ASSERT(::ftruncate(fd, length) == 0);
	close(fd);
    }
#endif
}


/* MUST be called from within transaction! */
void CacheFile::SetLength(long newlen)
{
    CODA_ASSERT(inode != (ino_t)-1);

    LOG(0, ("Cachefile::SetLength %d\n", newlen));

    if (length != newlen) {
	RVMLIB_REC_OBJECT(*this);
	length = validdata = newlen;
    }
}

/* MUST be called from within transaction! */
void CacheFile::SetValidData(long newoffset)
{
    CODA_ASSERT(inode != (ino_t)-1);

    LOG(0, ("Cachefile::SetValidData %d\n", newoffset));

    if (validdata != newoffset) {
	RVMLIB_REC_OBJECT(validdata);
	validdata = newoffset;
    }
}

void CacheFile::print(int fdes)
{
    fdprint(fdes, "[ %s, %d, %d/%d ]\n", name, inode, validdata, length);
}

int CacheFile::Open(int flags)
{
    int fd;

    fd = ::open(name, flags|O_BINARY, V_MODE);

    CODA_ASSERT (fd != -1);
    numopens++;
    
    return fd;
}

int CacheFile::Close(int fd)
{
    int ret;

    if (fd == -1)
        return 0;

    ret = ::close(fd);
    numopens--;

    return ret;
}

