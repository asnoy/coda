/* BLURB gpl

                           Coda File System
                              Release 6

          Copyright (c) 1987-2016 Carnegie Mellon University
                  Additional copyrights listed below

This  code  is  distributed "AS IS" without warranty of any kind under
the terms of the GNU General Public Licence Version 2, as shown in the
file  LICENSE.  The  technical and financial  contributors to Coda are
listed in the file CREDITS.

                        Additional copyrights
                           none currently

*/

#include <codaconf.h>
#include "repair.h"

int allowclear = 0, session = NOT_IN_SESSION, repair_DebugFlag = 0;
struct conflict *ConflictObj = NULL;
char cfix[MAXPATHLEN];

static const char *coda_mountpoint;

/* Relax, command parser allows abbreviations of command names */
command_t list[] = {
    {"beginrepair",      rep_BeginRepair,      0, ""}, /* <reppathname> */
    {"endrepair",        rep_EndRepair,        0, ""}, /* no args */
    {"dorepair",         rep_DoRepair,         0, ""}, /* <reppathname> <fixfilename> */
    {"comparedirs",      rep_CompareDirs,      0, ""}, /* <reppathname> <fixfile> */
    {"clearinc",         rep_ClearInc,         0, ""}, /* <reppathname> */
    {"removeinc",        rep_RemoveInc,        0, ""}, /* <reppathname> */
    {"replaceinc",       rep_ReplaceInc,       0, ""}, /* <reppathname> <mergedfile> */
    {"quit",             rep_Exit,             0, ""}, /* no args */
    {"help",             rep_Help,             0, ""}, /* no args */
    { NULL, NULL, 0, ""},
};

#if defined(DJGPP) || defined(__CYGWIN32__)
#define DFLT_MNT "N:"
#else
#define DFLT_MNT "/coda"
#endif

int main(int argc, char **argv)
{
    struct conflict *conf;
    char msgbuf[DEF_BUF];

    memset(cfix, 0, sizeof(cfix));
    signal(SIGINT, (void (*)(int))INT); /* catch SIGINT */

    codaconf_init("venus.conf");
    CODACONF_STR(coda_mountpoint, "mountpoint", DFLT_MNT);

    if ((argc > 1) && (strcmp(argv[1], "-remove") == 0)) {
	if (argc != 3) {
	    fprintf(stderr, "Usage:  %s [-remove <pathname>]\n", argv[0]);
	    exit(EXIT_FAILURE);
	}
	else {
	    if ((BeginRepair(argv[2], &conf, msgbuf, sizeof(msgbuf))) < 0) {
		fprintf(stderr, "%s\nError beginning repair\n", msgbuf);
		exit(EXIT_FAILURE);
	    }

	    if (conf->local) {
	      /* local/global conflict, used to be discardalllocal */
	      fprintf(stderr, "Use purgeml instead.\n");
	    }
	    else { /* server/server conflict */
		if (RemoveInc(conf, msgbuf, sizeof(msgbuf)) < 0)
		    fprintf(stderr, "%s\nError removing inconsistency\n",
			    msgbuf);
	    }
	    if (EndRepair(conf, 1, msgbuf, sizeof(msgbuf)) < 0)
		fprintf(stderr, "%s\nError ending repair\n", msgbuf);
	    Parser_exit(0, NULL);
	    execlp("rm", "rm", "-Rf", argv[2], (char *)NULL);
	    fprintf(stderr, "\nError removing %s: %s\n", argv[2], strerror(errno));
	    exit(EXIT_FAILURE);
	}
    }

    /* Sit in command loop */
    if ( argc >= 3 ) {
	rep_BeginRepair(2, &(argv[0]));
	rep_CompareDirs(argc - 1 , &(argv[1]));
	rep_DoRepair(2, &(argv[1]));
	rep_EndRepair(0, NULL);
	rep_Exit(0, NULL);
    }
    else if ( argc != 3 ) {
	printf(INITHELPMSG);
#if 0
	if (GetTokens()) /* warn user if no tokens are found */
	    fprintf(stderr, "\n\n\nWARNING: YOU DON'T HAVE TOKENS.  YOU MIGHT WANT TO AUTHENTICATE FIRST\n\n\a");
#endif
	GetArgs(argc, argv);
	Parser_init("repair > ", list);
	Parser_commands();
    } 
    else {
	fprintf(stderr, "Usage: %s { object fixfile }\n", argv[0]);
	exit(EXIT_FAILURE);
    }
}

void GetArgs(int argc, char *argv[]) {
    int i;

    for (i = 1; i < argc; i++) {
	if (strcmp(argv[i], "-d") == 0)
	    repair_DebugFlag = 1;
	else if (strcmp(argv[i], "-allowclear") == 0)
	    allowclear = 1;
	else {
	    printf("Usage: repair [-d]\n");
	    exit(EXIT_FAILURE);
	}
    }
}

int getcompareargs(int largc, char **largv, char **filepath, struct repinfo *inf)
{
    int j;

    if (largc == 1) goto exit;

    inf->user = inf->rights = inf->owner = inf->mode = NULL;
    for ( j = 2; j < largc ; j++ ) {
	if ( strcmp(largv[j], "-acl") == 0 ) {
	    if ( largc < j+3 ) 
		goto exit;
	    inf->user = largv[j+1];
	    inf->rights = largv[j+2];
	    j = j + 2;
	}
	else if ( strcmp(largv[j], "-owner") == 0 ) {
	    if ( largc < j+2 ) 
		goto exit;
	    inf->owner = largv[j+1];
	    j = j+1;
	}
	else if ( strcmp(largv[j], "-mode") == 0) {
	    if ( largc < j+2 ) 
		goto exit;
	    inf->mode = largv[j+1];
	    j = j+1;
	}
	else goto exit;
    }

    *filepath = largv[1];
    if (!repair_getfid(*filepath, NULL, NULL, NULL, NULL, 0)) {
	fprintf(stderr, "%s is in %s and cannot be used as the fix file\n", *filepath, coda_mountpoint);
	return(-1);
    }
    strncpy(cfix, *filepath, sizeof(cfix));

    return(0);

  exit:
    printf("%s  <fixfile> { -acl user rights } { -owner uid} {-mode mode}\n", largv[0]);
    return(-1);
}

int getrepairargs(int largc, char **largv, char *fixpath)
{
    if (largc == 1) Parser_getstr("Pathname of fixfile?", cfix, fixpath, MAXPATHLEN);
    else if (largc == 2) strncpy(fixpath, largv[1], MAXPATHLEN);
    else {
	fprintf(stderr, "%s {object fixfile }\n", largv[0]);
	return(-1);
    }
    if (!repair_getfid(fixpath, NULL, NULL, NULL, NULL, 0)) {
	fprintf(stderr, "%s is in %s and cannot be used as the fixfile\n", fixpath, coda_mountpoint);
	return(-1);
    }
    strncpy(cfix, fixpath, sizeof(cfix));
    return(0);
}

/* return zero if user has valid tokens */
int GetTokens()
{
    ClearToken clear;
    EncryptedSecretToken secret;
    return (U_GetLocalTokens(&clear, secret, ""));
}

void INT(int, int, struct sigcontext *) {
    /* force an end to the current repair session when ^C is hit */
    fprintf(stderr, "abnormal exit of repair tool\n");
    rep_Exit(0, NULL);
}

void rep_BeginRepair(int largc, char **largv) {
    char userpath[MAXPATHLEN], msgbuf[DEF_BUF];
    int rc;

    switch(session) {
    case FILE_SESSION:
      printf("File repair session already in progress.\n");
      return;
      break;

    case DIRECTORY_SESSION:
      printf("Directory repair session already in progress.\n");
      return;
      break;

    case NOT_IN_SESSION:
      break;

    default:
      printf("Unknown session type. Restart the repair tool.\n");
      return;
      break;
    }

    if (largc == 1)
	Parser_getstr("Pathname of object in conflict?", "",
		      userpath, MAXPATHLEN);
    else if (largc == 2)
	strncpy(userpath, largv[1], MAXPATHLEN);
    else {
	printf("usage: beginrepair <reppathname>\n");
	return;
    }

    /* Begin the repair */
    if ((rc = BeginRepair(userpath, &ConflictObj, msgbuf, sizeof(msgbuf))) < 0) {
	fprintf(stderr, "%s\nbeginrepair failed.\n", msgbuf);
	return;
    }

    if(ConflictObj->dirconf) {
      session = DIRECTORY_SESSION;
      printf("Directory repair session started\n");
      printf("Available commands:\n\t%s\n\t%s\n\t%s\n\n", "comparedirs",
	     "dorepair", "removeinc");
    }
    else {
      session = FILE_SESSION;
      printf("File repair session started.\n");
      printf("Available commands:\n\t%s\n\t%s\n\n", "replaceinc", "removeinc");
    }

    fflush(stdout);
}


void rep_ClearInc(int largc, char **largv) {
    char msgbuf[DEF_BUF];

    if (session == NOT_IN_SESSION) {
	printf("You must do \"beginrepair\" first\n");
	return;
    }

    if (!allowclear) {
	printf("Clear Inconsistency: This command is obsolete.");
	printf("You don't need to use this anymore\n");
    }
    else if (ClearInc(ConflictObj, msgbuf, sizeof(msgbuf)) < 0)
	fprintf(stderr, "Error clearing inconsistency: %s\n", msgbuf);
}

void rep_CompareDirs(int largc, char **largv) {
    char msgbuf[DEF_BUF];
    struct repinfo inf;
    char *fixfile = NULL;
    int ret;

    if (session == NOT_IN_SESSION) {
	printf("You must do \"beginrepair\" first\n");
	return;
    }

    memset(&inf, 0, sizeof(inf));
    inf.interactive = 1;

    /* Obtain parameters from user */
    if (getcompareargs(largc, largv, &fixfile, &inf) < 0)
      return;

    msgbuf[0] = '\0';
    ret = CompareDirs(ConflictObj, fixfile, &inf, msgbuf, sizeof(msgbuf));
    if (msgbuf[0])
	printf("%s\n", msgbuf);
    if(ret == -1)
	fprintf(stderr, "comparedirs failed!\n");
}

void rep_DoRepair(int largc, char **largv) {
    char msgbuf[DEF_BUF], ufixpath[MAXPATHLEN];

    if (session == NOT_IN_SESSION) {
	printf("You must do \"beginrepair\" first\n");
	return;
    }

    /* Obtain parameters and confirmation from user */
    if (getrepairargs(largc, largv, ufixpath) < 0) return;

    if (DoRepair(ConflictObj, ufixpath, stdout, msgbuf, sizeof(msgbuf)) < 0)
      fprintf(stderr, "%s\nRepair failed.\n", msgbuf);
}

void rep_EndRepair(int largc, char **largv) {
    int commit = 0;
    char msgbuf[DEF_BUF];

    switch (session) {
    case NOT_IN_SESSION:
      printf("There is no repair session to end.\n");
      return;
      break;
    case FILE_SESSION:
    case DIRECTORY_SESSION:
      if (EndRepair(ConflictObj, commit, msgbuf, sizeof(msgbuf)) < 0) {
	fprintf(stderr, "%s\nError ending repair session\n", msgbuf);
	exit(EXIT_FAILURE);
      }
      break;
    default:
      fprintf(stderr, "Unknown session type\n");
      exit(EXIT_FAILURE);
      break;
    }
    session = NOT_IN_SESSION;
    printf("Repair session completed.\n");
    fflush(stdout);
}

void rep_Exit(int largc, char **largv)
{
    /* terminate the current session, if there is one */
    if (session != NOT_IN_SESSION)
	rep_EndRepair(largc, largv);

    Parser_exit(0, NULL); /* exit the repair tool */
}

void rep_Help(int largc, char **largv) {
    printf("See the Coda manual or repair.1 for help.\nPerhaps try doing a \"beginrepair\".\n");
    fflush(stdout);
}

void rep_RemoveInc(int largc, char **largv) {
  int dirconf,rc;
  char msgbuf[DEF_BUF],rodir[DEF_BUF];
  if (session == NOT_IN_SESSION) {
    printf("You must do \"beginrepair\" first\n");
    return;
  }
  printf("\"removeinc\" will terminate the current repair session\n");

  printf("Completely remove %s?", ConflictObj->rodir);
  if (!Parser_getbool("", 1)) {
    printf("Operation aborted.\n");
    return;
  }

  /* remember conflict type and path (since Endrepair will free them) */
  dirconf = ConflictObj->dirconf;
  strcpy(rodir, ConflictObj->rodir);

  /* remove the inconsistency */
  if ((rc = RemoveInc(ConflictObj, msgbuf, sizeof(msgbuf))) < 0)
    fprintf(stderr, "%s\nError removing inconsistency\n", msgbuf);
  /* end the repair session */
  if ((rc = EndRepair(ConflictObj, 0, msgbuf, sizeof(msgbuf))) < 0)
    fprintf(stderr, "%s\nError ending repair session.\n", msgbuf);

  if (!rc) { /* no error - try to remove the object */
    if (((dirconf) ? rmdir(rodir)
	 : unlink(rodir)) < 0)
      fprintf(stderr, "%s\nCould not remove %s\n",
	      strerror(errno), rodir);
    else
      printf("Repair successful.\n");
  }
  exit(EXIT_FAILURE);
}

void rep_ReplaceInc(int largc, char **largv)
{
    int rc;
    char fixpath[MAXPATHLEN], mergefile[MAXPATHLEN], msgbuf[DEF_BUF];
    ViceFid fixfid;
    char fixrealm[MAXHOSTNAMELEN];
    ViceVersionVector fixvv;
    struct stat sbuf;

    if (session == NOT_IN_SESSION) {
	printf("You must do \"beginrepair\" first\n");
	return;
    }

    if (ConflictObj->dirconf) {
	printf("\"replaceinc\" can only be used to repair file conflicts\n");
	return;
    }
    printf("\"replaceinc\" will terminate the current repair session\n");

    if (largc == 1) {
	printf("Pathname of object to replace %s ", ConflictObj->rodir);
	Parser_getstr("with?", "", mergefile, MAXPATHLEN);
    }
    else if (largc == 2)
	strncpy(mergefile, largv[1], MAXPATHLEN);
    else {
	printf("replaceinc <mergedfile>\n");
	return;
    }

    if ((rc = stat(mergefile, &sbuf)) != 0) {
	fprintf(stderr, "Couldn't find %s: %s\n", mergefile, strerror(errno));
	return;
    }
    if (!(sbuf.st_mode & S_IFREG)) {
	fprintf(stderr, "File %s is not a regular file (and hence cannot be used for repair)\n", mergefile);
	return;
    }
    if (!repair_getfid(mergefile, &fixfid, fixrealm, &fixvv, msgbuf, sizeof(msgbuf)) &&
        (fixvv.StoreId.HostId != (unsigned)-1))
	sprintf(fixpath, "@%x.%x.%x@%s", fixfid.Volume, fixfid.Vnode, fixfid.Unique, fixrealm);
    else strcpy(fixpath, mergefile);

    printf("Replace %s\n   with %s?", ConflictObj->rodir, mergefile);
    if (!Parser_getbool("", 1)) {
	printf("Operation aborted.\n");
	return;
    }

    if ((rc = EndRepair(ConflictObj, 0, msgbuf, sizeof(msgbuf))) < 0) {
	fprintf(stderr, "%s\nError ending repair session.\n", msgbuf);
	exit(EXIT_FAILURE);
    }

    if ((dorep(ConflictObj, fixpath, NULL, 0) < 0) && (errno != ETOOMANYREFS)){
	fprintf(stderr, "Error repairing conflict: %s\n", strerror(errno));
    }
    else {
	fprintf(stderr, "Repair successful.\n");
    }

    exit(EXIT_FAILURE);
}
