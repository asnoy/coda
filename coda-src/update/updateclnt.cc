/* BLURB gpl

                           Coda File System
                              Release 6

          Copyright (c) 1987-2003 Carnegie Mellon University
                  Additional copyrights listed below

This  code  is  distributed "AS IS" without warranty of any kind under
the terms of the GNU General Public Licence Version 2, as shown in the
file  LICENSE.  The  technical and financial  contributors to Coda are
listed in the file CREDITS.

                        Additional copyrights

#*/

/*
                         IBM COPYRIGHT NOTICE

                          Copyright (C) 1986
             International Business Machines Corporation
                         All Rights Reserved

This  file  contains  some  code identical to or derived from the 1986
version of the Andrew File System ("AFS"), which is owned by  the  IBM
Corporation.   This  code is provided "AS IS" and IBM does not warrant
that it is free of infringement of  any  intellectual  rights  of  any
third  party.    IBM  disclaims  liability of any kind for any damages
whatsoever resulting directly or indirectly from use of this  software
or  of  any  derivative work.  Carnegie Mellon University has obtained
permission to  modify,  distribute and sublicense this code,  which is
based on Version 2  of  AFS  and  does  not  contain  the features and
enhancements that are part of  Version 3 of  AFS.  Version 3 of AFS is
commercially   available   and  supported  by   Transarc  Corporation,
Pittsburgh, PA.

*/



/************************************************************************/
/*								*/
/*  updateclnt.c  - Client to update server data bases			*/
/*								*/
/*  Function  	- This is the client to update file servers dbs	*/
/*		  It checks every waitinterval (default 30 secs)	*/
/*		  to see if any of the file listed in the files        */
/*		  file has changed.  It fetches any new files.         */
/************************************************************************/

#ifdef __cplusplus
extern "C" {
#endif

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <sys/types.h>
#include <sys/param.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/file.h>
#include <netinet/in.h>
#include "coda_assert.h"
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdarg.h>
#include <fcntl.h>
#include "coda_string.h"
#include "coda_flock.h"

#include <lwp/lwp.h>
#include <lwp/lock.h>
#include <lwp/timer.h>
#include <rpc2/rpc2.h>
#include <rpc2/se.h>
#include <rpc2/sftp.h>
#include <map.h>
#include <portmapper.h>
#include <vice.h>
#include <util.h>

#ifdef sun
int utimes(const char *, const struct timeval *);
#endif

#ifdef __cplusplus
}
#endif

#include <volutil.h>
#include <codaconf.h>
#include <vice_file.h>
#include "update.h"
#include "getsecret.h"

extern long VolUpdateDB(RPC2_Handle);

extern char *ViceErrorMsg(int errorCode);   /* should be in libutil */

#define UPDATENAME "updateclnt"
#define MONITORNAME "UpdateMonitor"
#define NEEDNEW 232
#define LISTNAME "files"
#define LISTLEN 4096

static void CheckLibStructure();
static int CheckDir(char *prefix, int mode);
static int CheckFile(char *fileName, int mode);
static void ProcessArgs(int argc, char ** argv);
static void ReadConfigFile();
static void ReConnect();
static void SetDebug();
static void ResetDebug();
static void Terminate();
static void SetCheck();
static void SwapLog();
static void U_InitRPC();
static int U_BindToServer(char *fileserver, RPC2_Handle *RPCid);

static int ReadOnlyAllowed = 0;
static int CheckAll = 1;

static RPC2_Integer operatorSecs = 0;
static RPC2_Integer operatorUsecs = 0;

static RPC2_Handle con = 0;
static char host[256];
static int waitinterval = 30;	/* 5 min */
static int reps = 6;

static struct timeval  tp;
static struct timezone tsp;

static char **hostlist;		/* List of hosts to notify of changes. */
static RPC2_EncryptionKey vkey;	/* Encryption key for bind authentication */

static char *vicedir = NULL;
static int   nservers = 0;

static RPC2_Unsigned timestamp = 0;  /* last transfered time. */

int main(int argc, char **argv)
{
    struct timeval  time;
    FILE * file;
    int     i, rc;
    int     len;
    char    errmsg[MAXPATHLEN];
    
    *host = '\0';

    ProcessArgs(argc, argv);

    ReadConfigFile();

    /* Check if host has been set.  If not, try to read it out of
       vice_sharefile("db/scm").  */
    if ( host[0] == '\0' ) {
        file = fopen (vice_sharedfile("db/scm"), "r");
	if (!file || !fgets(host, 256, file)) {
	    LogMsg(0, SrvDebugLevel, stdout, "No host given!\n");
	    if (file)
	        fclose(file);
	    exit(-1);
	}
	fclose(file);
	if (host[strlen(host)-1] == '\n')
	    host[strlen(host)-1] = '\0';
    }

    LogMsg(1, SrvDebugLevel, stdout, "Using host '%s' for updatesrv.\n",
	   host);

    CheckLibStructure();

    LogMsg(2, SrvDebugLevel, stdout, "Changing to directory %s.\n",
	   vice_sharedfile(NULL));
    rc = chdir(vice_sharedfile(NULL));
    if ( rc ) {
        snprintf(errmsg, MAXPATHLEN, "Cannot cd to %s", vice_sharedfile(NULL));
	perror(errmsg);
	exit(1);
    }

    UtilDetach();

    (void) signal(SIGQUIT, (void (*)(int))Terminate);
    (void) signal(SIGHUP, (void (*)(int))ResetDebug);
    (void) signal(SIGUSR1, (void (*)(int))SetDebug);
#ifndef __CYGWIN32__
    (void) signal(SIGXFSZ, (void (*)(int))SetCheck);
    (void) signal(SIGXCPU, (void (*)(int))SwapLog);
#endif

    len = strlen(argv[0]);
    if (len > (int) strlen(UPDATENAME)) {
	for (i = 0; i < len; i++) {
	    *(argv[0] + i) = ' ';
	}
	strcpy(argv[0], UPDATENAME);
    }

    freopen(vice_sharedfile("misc/UpdateClntLog"), "a+", stdout);
    freopen(vice_sharedfile("misc/UpdateClntLog"), "a+", stderr);

    file = fopen(vice_sharedfile("misc/updateclnt.pid"), "w");
    if (!file) {
        snprintf (errmsg, MAXPATHLEN, "Could not open %s",
		  vice_sharedfile("misc/updateclnt.pid"));
	perror (errmsg);
	exit(-1);
    }
    fprintf(file, "%d", getpid());
    fclose(file);
    RPC2_DebugLevel = SrvDebugLevel / 10;

    U_InitRPC();

    gettimeofday(&tp, &tsp);
    LogMsg(0, SrvDebugLevel, stdout, 
	   "Update Client pid = %d started at %s", 
	   getpid(), ctime((time_t*)&tp.tv_sec));

    time.tv_sec = waitinterval;
    time.tv_usec = 0;
    i = reps;

    while (1) {

	if (!con) {
	    ReConnect();
	    i = reps;
	}
	if (!con)
	    goto retry;

	/* Checking "db" relative to updatesrv working directory. */
	if (CheckDir("db", 0644)) {
	    operatorSecs = 0;  /* if something changed time has elapsed */
	    for (int i=0; i<nservers; i++) {
	        if (nservers != 1)
		    vice_dir_init (vicedir, i+1);
		/* signal file server to check data bases */
		file = fopen(vice_file("srv/pid"), "r");
		if (file == NULL) {
		    LogMsg(0, SrvDebugLevel, stdout, 
			   "Fopen failed for file %s with %s\n",
			   vice_file("srv/pid"),
			   ViceErrorMsg(errno));
		} else {
		    RPC2_Handle rpcid;
		    if (U_BindToServer(hostlist[i], &rpcid) == RPC2_SUCCESS) {
		        if (VolUpdateDB(rpcid) == RPC2_SUCCESS) {
			    LogMsg(0, SrvDebugLevel, stdout, 
			         "Notifying server %s of database updates\n",
				 hostlist[i]);
			} else {
			    LogMsg(0, SrvDebugLevel, stdout, 
				   "VolUpdateDB failed for host %s\n",
				   hostlist[i]);
			}
		    } else {
		        LogMsg(0, SrvDebugLevel, stdout, 
			     "Bind to server %s for database update failed\n",
			     hostlist[i]);
		    }
		    RPC2_Unbind(rpcid);
		}
	    }
	}
	if (operatorSecs > 0) {
	    gettimeofday(&tp, &tsp);
	    if ((tp.tv_sec < operatorSecs) || 
		(tp.tv_sec > (operatorSecs + 2))) {
		tp.tv_sec = operatorSecs + 1;
		tp.tv_usec = operatorUsecs;
		LogMsg(0, SrvDebugLevel, stdout,
		    "Time between servers differs, "
		    "use `xntpd' to keep the time synchronized");
		/*
		LogMsg(0, SrvDebugLevel, stdout, 
		       "Settime to %s", ctime(&tp.tv_sec));
		settimeofday(&tp, &tsp);
		*/
	    }
	}

	CheckAll = 1;

retry:
	IOMGR_Select(0, 0, 0, 0, &time);
    }
}

static void ProcessArgs(int argc, char **argv)
{
    int i;
    for (i = 1; i < argc; i++) {
	if (!strcmp(argv[i], "-d"))
	    SrvDebugLevel = atoi(argv[++i]);
	else
	    if (!strcmp(argv[i], "-h"))
		strcpy(host, argv[++i]);
	else
	  if (!strcmp(argv[i], "-q")) {
	        fprintf (stderr, "Old argument -q to update clnt.\n");
		++i;
	  }
	else
	    if (!strcmp(argv[i], "-w"))
		waitinterval = atoi(argv[++i]);
	else
	    if (!strcmp(argv[i], "-r"))
		reps = atoi(argv[++i]);
	else {
	    LogMsg(0, SrvDebugLevel, stdout, 
		   "usage: update [-d (debug level)] ");
	    LogMsg(0, SrvDebugLevel, stdout, 
		   "[-h (operator console hostname)] ");
	    LogMsg(0, SrvDebugLevel, stdout, 
		   "[-r (reps of w for long wait time)] [-w (short wait time)]\n");
	    exit(-1);
	}
    }
}

static void
ReadConfigFile()
{
    /* Load configuration file to get vice dir. */
    codaconf_init("server.conf");

    CONF_STR(vicedir, "vicedir", "/vice");
    CONF_INT(nservers, "numservers", 1); 

    vice_dir_init(vicedir, 0);

    /* Host name list from multiple configs. */
    hostlist = new char*[nservers];
    if (nservers == 1) {
        hostlist[0] = new char[256];
	hostname(hostlist[0]);
    }
    else {
	char confname[80];
        for (int i = 0; i<nservers; i++) {
	    hostlist[i] = new char[256];
	    hostlist[i][0] = '\0';
	    sprintf (confname, "server_%d.conf", i+1);
	    codaconf_init(confname);
	    CONF_STR(hostlist[i],  "hostname",  "");
	    if (hostlist[i][0] == '\0') {
	        LogMsg(0, SrvDebugLevel, stdout,
		       "No host name for server %d.\n",
		       i+1);
		exit(1);
	    }
	}
    }

    /*    for (int i = 0; i<nservers; i++) printf ("hostlist[%d] is %s\n", i, hostlist[i]); */
}

static void CheckLibStructure()
{
    struct stat lbuf;

    if((stat(vice_sharedfile(NULL),&lbuf)) && (errno == ENOENT)) {
	printf("Creating %s structure\n", vice_sharedfile(NULL));
	mkdir(vice_sharedfile(NULL),0755);
	mkdir(vice_sharedfile("db"),0755);
	mkdir(vice_sharedfile("misc"),0755);
	
	for (int i=1; i<=nservers; i++) {
	    if (nservers != 1)
	        vice_dir_init (vicedir, i);
	    mkdir(vice_file("srv"),0755);
	    mkdir(vice_file("vol"),0755);
	    mkdir(vice_file("spool"),0755);
	}
    }
    else {
	if((stat(vice_sharedfile("db"),&lbuf)) && (errno == ENOENT)) {
	    printf("Creating %s\n", vice_sharedfile("db"));
	    mkdir(vice_sharedfile("db"),0755);
	}
	for (int i=1; i<=nservers; i++) {
	    if (nservers != 1)
	        vice_dir_init (vicedir, i);
	    if ((stat(vice_file("srv"),&lbuf)) && (errno == ENOENT)) {
	        printf("Creating %s\n",vice_file("srv"));
		mkdir(vice_file("srv"),0755);
	    }
	    if ((stat(vice_file("vol"),&lbuf)) && (errno == ENOENT)) {
	      printf("Creating %s\n",vice_file("vol"));
	      mkdir(vice_file("vol"),0755);
	    }
	    if ((stat(vice_file("spool"),&lbuf)) && (errno == ENOENT)) {
	      printf("Creating %s\n",vice_file("spool"));
	      mkdir(vice_file("spool"),0755);
	    }
	}
    }
}


static int CheckDir(char *prefix, int mode)
{
    static char list[LISTLEN],
	name[256],
	newname[1024];
    int     i,
	j,
	fd,
	len,
	rc = 0;
    struct stat buff;

    LogMsg(1, SrvDebugLevel, stdout, "Checking directory %s\n", prefix);
    if ((!(CheckFile(prefix, 0755))) && !CheckAll)
	return(0);

    LogMsg(1, SrvDebugLevel, stdout, 
	   "Directory %s changed, check files\n", prefix);

    strcpy(newname, prefix);
    strcat(newname, "/");
    strcat(newname, LISTNAME);

    if (CheckFile(newname, 0644))
	LogMsg(0, SrvDebugLevel, stdout, "Updated %s\n", newname);

    fd = open(newname, O_RDONLY, 0666);
    if (fd <= 0) {
	perror("Open failed for list data set");
	return(0);
    }
    if (fstat(fd, &buff)) {
	perror("Stat failed for list data set");
	return(0);
    }

    for (len = LISTLEN; len == LISTLEN;) {
	if ((len = read(fd, list, LISTLEN)) <= 0)
	    break;
	for (i = j = 0; i < len; i++) {
	    if (list[i] == '\n' || list[i] == '\0') {
		name[j] = '\0';
		if (j > 0) {
		    if (name[0] == '-') {
			strcpy(newname, prefix);
			strcat(newname, "/");
			strcat(newname, &name[1]);
			LogMsg(0, SrvDebugLevel, stdout, "Removing %s\n", newname);
			unlink(newname);
		    }
		    else {
			strcpy(newname, prefix);
			strcat(newname, "/");
			strcat(newname, name);
			if (CheckFile(newname, mode)) {
			    rc = 1;
			    LogMsg(0, SrvDebugLevel, stdout, "Updated %s\n", newname);
			}
		    }
		}
		j = 0;
	    }
	    else {
		name[j++] = list[i];
	    }
	}
    }

    close(fd);
    return(rc);
}


/* fileName is the full pathname */
static int CheckFile(char *fileName, int mode)
{
    static char oldname[1024];
    static char tmpname[1024];
    struct stat buff;
    struct timeval  times[2];
    RPC2_Unsigned time, newtime, currentsecs;
    RPC2_Integer currentusecs;
    long     rc;
    SE_Descriptor sed;

    LogMsg(1, SrvDebugLevel, stdout, "Checking file %s", fileName); 

    if (stat(fileName, &buff)) {
	time = 0;
    }
    else {
	time = buff.st_mtime;
    /* do not update if owner cannot write and it is a normal file */
	if (!(buff.st_mode & S_IWRITE) && (buff.st_mode & S_IFREG) && ReadOnlyAllowed) {
	    LogMsg(0, SrvDebugLevel, stdout, "%s write protected against owner, not updated\n",fileName);
	    return(0);
	}
    }

    strcpy(tmpname, fileName);
    strcat(tmpname, ".UPD");
    strcpy(oldname, fileName);
    strcat(oldname, ".BAK");

    memset(&sed, 0, sizeof(SE_Descriptor));
    sed.Tag = SMARTFTP;
    sed.Value.SmartFTPD.SeekOffset = 0;
    sed.Value.SmartFTPD.Tag = FILEBYNAME;
    if (SrvDebugLevel > 0)
	sed.Value.SmartFTPD.hashmark = '#';
    else
	sed.Value.SmartFTPD.hashmark = '\0';
    sed.Value.SmartFTPD.FileInfo.ByName.ProtectionBits = 0666;
    sed.Value.SmartFTPD.TransmissionDirection = SERVERTOCLIENT;
    strcpy(sed.Value.SmartFTPD.FileInfo.ByName.LocalFileName, tmpname);

    rc = UpdateFetch(con, (RPC2_String)fileName, time, &newtime, &currentsecs, &currentusecs, &sed);

    if (rc) {
	operatorSecs = 0;
	unlink(tmpname);
	LogMsg(0, SrvDebugLevel, stdout, "Fetch failed for '%s' with %s\n",
	       fileName, ViceErrorMsg((int)rc));
	if (rc <= RPC2_ELIMIT) {
	    RPC2_Unbind(con);
	    con = 0;
	}
	return(0);
    }

    if (currentsecs > newtime) {
        operatorSecs = currentsecs;
	operatorUsecs = currentusecs;
    }
    else {
        operatorSecs = 0;
    }

    if ((newtime != 0) && (time != newtime)) {
	rc = NEEDNEW;
	times[0].tv_usec = times[1].tv_usec = 0;
	times[0].tv_sec = times[1].tv_sec = newtime;
	if (((buff.st_mode & S_IFMT) == S_IFREG) || (time == 0)) {
	    unlink(oldname);
	    if ((time != 0) && (rename(fileName, oldname))) {
		LogMsg(0, SrvDebugLevel, stdout,
		       "rename %s to %s failed: %s\n", fileName, oldname,
		       ViceErrorMsg(errno));
	    }
	    if (rename(tmpname, fileName)) {
		LogMsg(0, SrvDebugLevel, stdout,
		       "rename %s to %s failed: %s\n", tmpname, fileName,
		       ViceErrorMsg(errno));
	    }
	}
	chmod(fileName, mode);
	if (utimes(fileName, times)) {
	    perror("utimes failed with");
	}
    }
    else {
        unlink(tmpname);
        if (newtime > timestamp && (buff.st_mode & S_IFMT) == S_IFREG) {
	    timestamp = newtime;
	    rc = NEEDNEW;
	} else
	   rc = 0;
    }

    return(rc);
}


static void ReConnect()
{
    long     rc;
    RPC2_PortIdent sid;
    RPC2_SubsysIdent ssid;
    RPC2_HostIdent hid;
    RPC2_CountedBS cident;
    RPC2_EncryptionKey secret;
    char hostname[64];
    long portmapid;
    long port;

    if (con) {
	LogMsg(0, SrvDebugLevel, stdout, "Unbinding\n");
	RPC2_Unbind(con);
	con = 0;
    }

    portmapid = portmap_bind(host);
    if ( !portmapid ) {
	fprintf(stderr, "Cannot bind to rpc2portmap; exiting\n");
	return;
    }

    rc = portmapper_client_lookup_pbynvp(portmapid,
					 (unsigned char *)"codaupdate", 0, 17,
					 &port);
    RPC2_Unbind(portmapid);
    if (rc) {
	fprintf(stderr, "Cannot get port from rpc2portmap; exiting\n");
	return;
    }

    hid.Tag = RPC2_HOSTBYNAME;
    strcpy(hid.Value.Name, host);
    sid.Tag = RPC2_PORTBYINETNUMBER;
    sid.Value.InetPortNumber = htons(port);
    ssid.Tag = RPC2_SUBSYSBYID;
    ssid.Value.SubsysId= SUBSYS_UPDATE;

    RPC2_BindParms bparms;
    memset((void *)&bparms, 0, sizeof(bparms));
    bparms.SecurityLevel = RPC2_AUTHONLY;
    bparms.EncryptionType = RPC2_XOR;
    bparms.SideEffectType = SMARTFTP;

    gethostname(hostname, 63); hostname[63] = '\0';
    cident.SeqBody = (RPC2_ByteSeq)&hostname;
    cident.SeqLen = strlen(hostname) + 1;
    bparms.ClientIdent = &cident;

    GetSecret(vice_sharedfile("db/update.tk"), secret);
    bparms.SharedSecret = &secret;

    rc = RPC2_NewBinding(&hid, &sid, &ssid, &bparms, &con);
    if (rc) {
	LogMsg(0, SrvDebugLevel, stdout, "Bind failed with %s\n", 
	       (char *)ViceErrorMsg((int)rc));
	return;
    }
    /* success, we have a new connection */
}


static void SetDebug()
{
    if (SrvDebugLevel > 0) {
	SrvDebugLevel *= 5;
    }
    else {
	SrvDebugLevel = 1;
    }
    RPC2_DebugLevel = SrvDebugLevel/10;
    LogMsg(0, SrvDebugLevel, stdout, "Set Debug On level = %d, RPC level = %d\n",SrvDebugLevel, RPC2_DebugLevel);
}


static void ResetDebug()
{
    RPC2_DebugLevel = SrvDebugLevel = 0;
    LogMsg(0, SrvDebugLevel, stdout, "Reset Debug levels to 0\n");
}


static void Terminate()
{
    LogMsg(0, SrvDebugLevel, stdout, "Exiting updateclnt\n");
    exit(0);
}


static void SwapLog()
{
    struct timeval tv;
    struct timezone tz;

    if(rename("UpdateLog","UpdateLog.old")) {
	LogMsg(0, SrvDebugLevel, stdout, "Rename for UpdateLog failed with a %s\n", ViceErrorMsg(errno));
    }
    else {
	gettimeofday(&tv, &tz);
	LogMsg(0, SrvDebugLevel, stdout, "Moving UpdateLog to UpdateLog.old at %s", ctime((time_t*)&tv.tv_sec));
	freopen("UpdateLog","a+",stdout);
	freopen("UpdateLog","a+",stderr);
	LogMsg(0, SrvDebugLevel, stdout, "New UpdateLog started at %s", ctime((time_t*)&tv.tv_sec));
    }
}


static void SetCheck()
{
    CheckAll = 1;
    LogMsg(0, SrvDebugLevel, stdout, "Check all files at next interval\n");
}

static void U_InitRPC()
{
    PROCESS mylpid;
    FILE *tokfile;
    SFTP_Initializer sftpi;
    long rcode;

    /* store authentication key */
    tokfile = fopen(vice_sharedfile(VolTKFile), "r");
    memset(vkey, 0, RPC2_KEYSIZE);
    fread(vkey, 1, RPC2_KEYSIZE, tokfile);
    fclose(tokfile);

    CODA_ASSERT(LWP_Init(LWP_VERSION, LWP_MAX_PRIORITY-1, &mylpid) == LWP_SUCCESS);

    SFTP_SetDefaults(&sftpi);
    sftpi.PacketSize = 1024;
    sftpi.WindowSize = 16;
    sftpi.SendAhead = 4;
    sftpi.AckPoint = 4;
    SFTP_Activate(&sftpi);
    rcode = RPC2_Init(RPC2_VERSION, 0, NULL, -1, 0);
    if (rcode != RPC2_SUCCESS) {
	LogMsg(0, SrvDebugLevel, stdout, "RPC2_Init failed with %s\n", RPC2_ErrorMsg((int)rcode));
	exit(-1);
    }
}

static int U_BindToServer(char *fileserver, RPC2_Handle *RPCid)
{
 /* Binds to File Server on volume utility port on behalf of uName.
    Sets RPCid to the value of the connection id.    */

    RPC2_HostIdent hident;
    RPC2_PortIdent pident;
    RPC2_SubsysIdent sident;
    RPC2_EncryptionKey secret;
    long     rcode;

    hident.Tag = RPC2_HOSTBYNAME;
    strcpy(hident.Value.Name, fileserver);
    pident.Tag = RPC2_PORTBYNAME;
    strcpy(pident.Value.Name, "codasrv");
    sident.Tag = RPC2_SUBSYSBYID;
    sident.Value.SubsysId = UTIL_SUBSYSID;

    RPC2_BindParms bparms;
    memset((void *)&bparms, 0, sizeof(bparms));
    bparms.SecurityLevel = RPC2_AUTHONLY;
    bparms.EncryptionType = RPC2_XOR;
    bparms.SideEffectType = SMARTFTP;

    GetSecret(vice_sharedfile(VolTKFile), secret);
    bparms.SharedSecret = &secret;

    LogMsg(9, SrvDebugLevel, stdout, "V_BindToServer: binding to host %s\n",
	   fileserver);
    rcode = RPC2_NewBinding(&hident, &pident, &sident, &bparms, RPCid);
    if (rcode < 0 && rcode > RPC2_ELIMIT)
	rcode = 0;
    if (rcode == 0 || rcode == RPC2_NOTAUTHENTICATED || rcode == RPC2_NOBINDING)
	return(rcode);
    else {
	LogMsg(0, SrvDebugLevel, stdout, "RPC2_NewBinding to server %s failed with %s\n",
				fileserver, RPC2_ErrorMsg((int)rcode));
	exit(-1);
    }
}

