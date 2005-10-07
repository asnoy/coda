/* BLURB lgpl

                           Coda File System
                              Release 5

          Copyright (c) 1987-1999 Carnegie Mellon University
                  Additional copyrights listed below

This  code  is  distributed "AS IS" without warranty of any kind under
the  terms of the  GNU  Library General Public Licence  Version 2,  as
shown in the file LICENSE. The technical and financial contributors to
Coda are listed in the file CREDITS.

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

#include <linux/types.h>
#include <linux/errqueue.h>

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/time.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <assert.h>
#include "rpc2.private.h"
#include <rpc2/se.h>
#include "cbuf.h"
#include "trace.h"

#ifndef MSG_CONFIRM
#define MSG_CONFIRM 0
#endif

/* The MSG_CONFIRM flag significantly reduces arp traffic with linux-2.4 and
 * higher. However... a precompiled binary that uses this flag fails to send
 * any packets when run on linux-2.2 kernels. So we initially set msg_confirm
 * to match the flag, but if we get an EINVAL error back from sendto we clear
 * it. This way we lose the first packet after startup, but retransmission
 * should fix that automatically. --JH
 */
static int msg_confirm = MSG_CONFIRM;

static long DefaultRetryCount = 6;
static struct timeval DefaultRetryInterval = {60, 0};

/* Hooks for failure emulation package (libfail)

   Libfail will set these to its predicate routines when initialized.
   If libfail is not linked in, they remain NULL, and nothing happens.
   See documentation for libfail for details.
 */

int (*Fail_SendPredicate)() = NULL,
    (*Fail_RecvPredicate)() = NULL;

static long FailPacket(int (*predicate)(), RPC2_PacketBuffer *pb,
			   struct RPC2_addrinfo *addr, int sock)
{
    long drop;
    unsigned char ip1, ip2, ip3, ip4;
    unsigned char color;
    struct sockaddr_in *sin;
    unsigned char *inaddr;

    if (!predicate)
	return 0;

#warning "fail filters can only handle ipv4 addresses"
    if (addr->ai_family != PF_INET)
	return 0;

    sin = (struct sockaddr_in *)addr->ai_addr;
    inaddr = (unsigned char *)&sin->sin_addr;

    ip1 = inaddr[0]; ip2 = inaddr[1]; ip3 = inaddr[2]; ip4 = inaddr[3]; 

    ntohPktColor(pb);
    color = GetPktColor(pb);
    htonPktColor(pb);

    drop = ((*predicate)(ip1, ip2, ip3, ip4, color, pb, sin, sock) == 0);
    return drop;
}

/* Check if there is an outstanding ICMP errors on the socket */
static int coda_recverr(int s)
{
    struct sockaddr_storage sa;
    struct msghdr msg = { 0, };
    ssize_t ret;
    struct RPC2_addrinfo *peer;

    msg.msg_name	= &sa;
    msg.msg_namelen	= sizeof(sa);

    ret = recvmsg(s, &msg, MSG_ERRQUEUE);
    if (ret < 0 || !(msg.msg_flags & MSG_ERRQUEUE))
	return -1;

    if (msg.msg_namelen) {
	peer = RPC2_allocaddrinfo((struct sockaddr *)&sa, msg.msg_namelen,
				  SOCK_DGRAM, IPPROTO_UDP);

	rpc2_HostUnreach(peer);

	if (RPC2_DebugLevel) {
	    char buf[RPC2_ADDRSTRLEN] = "unknown host";
	    RPC2_formataddrinfo(peer, buf, sizeof(buf));
	    fprintf(rpc2_logfile, "Got MSG_ERRQUEUE from %s\n", buf);
	}
	RPC2_freeaddrinfo(peer);
	return 0;
    }

#if 0
    char control[4096];
    msg.msg_control	= control;
    msg.msg_controllen	= sizeof(control);

    struct cmsghdr *cmsg;
    struct sock_extended_err *ee = NULL;
    struct sockaddr *offender;
    socklen_t olen;

    for (cmsg = CMSG_FIRSTHDR(&msg); cmsg; cmsg = CMSG_NXTHDR(&msg, cmsg))
    {
	if ((cmsg->cmsg_level==SOL_IP && cmsg->cmsg_type==IP_RECVERR) ||
	    (cmsg->cmsg_level==IPPROTO_IPV6 && cmsg->cmsg_type==IPV6_RECVERR))
	{
	    ee = (struct sock_extended_err *)CMSG_DATA(cmsg);
	    break;
	}
    }
    if (ee) {
	say(-1, RPC2_DebugLevel, "ERRQUEUE MSG errno %d origin %d type %d code %d info %d\n",
	    ee->ee_errno, ee->ee_origin, ee->ee_type, ee->ee_code, ee->ee_info);
	
	offender = SO_EE_OFFENDER(ee);
	switch (offender->sa_family) {
	case PF_UNSPEC: olen = 0; break;
	case PF_INET:	olen = sizeof(struct sockaddr_in); break;
	case PF_INET6:	olen = sizeof(struct sockaddr_in6); break;
	}

	peer = RPC2_allocaddrinfo(offender, olen, SOCK_DGRAM, IPPROTO_UDP);
	RPC2_formataddrinfo(peer, buf, sizeof(buf));
	RPC2_freeaddrinfo(peer);
	say(-1, RPC2_DebugLevel, "ERRQUEUE MSG offender %s\n", buf);
    }
#endif

    return -1;
}


int rpc2_XmitPacket(IN RPC2_PacketBuffer *whichPB, IN struct RPC2_addrinfo *addr, int confirm)
{
    int whichSocket, n, flags = 0;

    say(0, RPC2_DebugLevel, "rpc2_XmitPacket()\n");

#ifdef RPC2DEBUG
    if (RPC2_DebugLevel > 9)
	{
	fprintf(rpc2_logfile, "\t");
	rpc2_printaddrinfo(addr, rpc2_logfile);
	fprintf(rpc2_logfile, "\n");
	rpc2_PrintPacketHeader(whichPB, rpc2_logfile);
	}
#endif

    assert(whichPB->Prefix.MagicNumber == OBJ_PACKETBUFFER);

    TR_XMIT();

    /* Only Internet for now; no name->number translation attempted */

    if (ntohl(whichPB->Header.Flags) & RPC2_MULTICAST)
    {
	rpc2_MSent.Total++;
	rpc2_MSent.Bytes += whichPB->Prefix.LengthOfPacket;
    }
    else
    {
	rpc2_Sent.Total++;
	rpc2_Sent.Bytes += whichPB->Prefix.LengthOfPacket;
    }

    whichSocket = rpc2_v6RequestSocket;

    if (whichSocket == -1 ||
	(rpc2_v4RequestSocket != -1 && addr->ai_family == PF_INET))
	whichSocket = rpc2_v4RequestSocket;

    if (whichSocket == -1)
	return -1; // RPC2_NOCONNECTION

    if (FailPacket(Fail_SendPredicate, whichPB, addr, whichSocket))
	return 0;

retry:
    if (confirm)
	flags = msg_confirm;

    n = sendto(whichSocket, &whichPB->Header, whichPB->Prefix.LengthOfPacket,
	       flags, addr->ai_addr, addr->ai_addrlen);

    if (n == -1) {
	switch (errno) {
	/* send buffer full? just drop the packet */
	case EAGAIN:
	case ENOBUFS:
	case ENOMEM:
	    return 0;

	/* interrupted before sending data, retry */
	case EINTR:
	    goto retry;

	/* locally generated error? maybe the interface is down */
	case ENETDOWN:
	case ENETUNREACH:
	case EHOSTDOWN:
	case EHOSTUNREACH:
	    return -1;

	/* the kernel didn't like the MSG_CONFIRM flag? */
	case EINVAL:
	    if (msg_confirm) {
		msg_confirm = 0;
		goto retry;
	    }
	    break;

	/* or maybe we have to clear a queued ICMP error first */
	case ECONNREFUSED:
	    if (coda_recverr(whichSocket) == 0)
		goto retry;
	    break;
	}
    }

    if (RPC2_Perror && n != whichPB->Prefix.LengthOfPacket)
    {
	char msg[100];
	sprintf(msg, "Xmit_Packet socket %d", whichSocket);
	perror(msg);
    }
    return 0;
}

/* Reads the next packet from whichSocket into whichBuff, sets its
   LengthOfPacket field, fills in whichHost and whichPort, and
   returns 0; Returns -3 iff a too-long packet arrived.  Returns -1 on
   any other system call error.

   Note that whichBuff should at least be able to accomodate 1 byte
   more than the longest receivable packet.  Only Internet packets are
   dealt with currently.  */
long rpc2_RecvPacket(IN long whichSocket, OUT RPC2_PacketBuffer *whichBuff) 
{
    long rc, len;
    size_t fromlen;
    struct sockaddr_storage sa;

    say(0, RPC2_DebugLevel, "rpc2_RecvPacket()\n");
    assert(whichBuff->Prefix.MagicNumber == OBJ_PACKETBUFFER);

    len = whichBuff->Prefix.BufferSize - (long)(&whichBuff->Header) + (long)(whichBuff);
    assert(len > 0);
    
    /* WARNING: only Internet works; no warnings */
    fromlen = sizeof(sa);
    rc = recvfrom(whichSocket, &whichBuff->Header, len, 0,
		  (struct sockaddr *)&sa, &fromlen);

    if (rc == -1) {
	/* maybe corrupt udp checksum or we got an ICMP error */
	if (errno != EAGAIN && coda_recverr(whichSocket) != 0)
	    say(0, RPC2_DebugLevel, "Error in recvf from: errno = %d\n", errno);
	return(-1);
    }

    whichBuff->Prefix.PeerAddr =
	RPC2_allocaddrinfo((struct sockaddr *)&sa, fromlen,
			   SOCK_DGRAM, IPPROTO_UDP);

    TR_RECV();

    if (FailPacket(Fail_RecvPredicate, whichBuff, whichBuff->Prefix.PeerAddr,
		   whichSocket))
    {
	    errno = 0;
	    return (-1);
    }

    whichBuff->Prefix.LengthOfPacket = rc;

    if (rc == len) {
	    if (ntohl(whichBuff->Header.Flags) & RPC2_MULTICAST)
		    rpc2_MRecvd.Giant++;
	    else
		    rpc2_Recvd.Giant++;
	    return(-3);
    }

    /* Try to get an accurate arrival time estimate for this packet */
    /* This ioctl might be used on linux systems only, but you never know */
#if 0 // defined(SIOCGSTAMP)
/* Very nice for accurate network RTT estimates, but we don't measure the time
 * it takes for the server to wake up and send back the response. i.e. The
 * client will end up assuming the server is faster than it really is so I've
 * disabled this code -JH */
    rc = ioctl(whichSocket, SIOCGSTAMP, &whichBuff->Prefix.RecvStamp);
    if (rc < 0)
#endif
    {
	FT_GetTimeOfDay(&whichBuff->Prefix.RecvStamp, (struct timezone *)0);
    }

    return(0);
}


/*
  Initializes default retry intervals given the number of
  retries desired and the keepalive interval.
  This implementation has:

  Note: Beta(0) is a special case of keepalive.
  
  (1)	Beta[i+1] = 2*Beta[i] for i >= 1
  (2)	Beta[0] = Beta[1] + Beta[2] ... + Beta[Retry_N+1]

  Time constants less than LOWERLIMIT are set to LOWERLIMIT.
  There is a limit on retry intervals and timeouts of just over an
  hour, since we do these computations in microseconds.
  Returns 0 on success, -1 on bogus parameters.
*/
long rpc2_InitRetry(IN long HowManyRetries, IN struct timeval *Beta0)
		/*  HowManyRetries" should be less than 30; -1 for default */
	        /*  Beta0: NULL for default */
{
    long betax, timeused, beta0;	/* entirely in microseconds */
    long i;

    if (HowManyRetries >= 30) return(-1);	/* else overflow with 32-bit integers */
    if (HowManyRetries < 0) HowManyRetries = DefaultRetryCount;	/* it's ok, call by value */
    if (Beta0 == NULL) Beta0 = &DefaultRetryInterval; /* ditto */

    assert(Retry_Beta == NULL);

    Retry_N = HowManyRetries;
    Retry_Beta = (struct timeval *)malloc(sizeof(struct timeval)*(2+HowManyRetries));
    memset(Retry_Beta, 0, sizeof(struct timeval)*(2+HowManyRetries));
    Retry_Beta[0] = *Beta0;	/* structure assignment */

    /* this prevents long stalls whenever the server sends RPC2_BUSY */
    Retry_Beta[0].tv_sec /= 3;
    Retry_Beta[0].tv_usec /= 3;
    
    /* Twice Beta0 is how long responses are saved */
    SaveResponse.tv_usec = (2*Beta0->tv_usec) % 1000000;
    SaveResponse.tv_sec = (2*Beta0->tv_usec) / 1000000;
    SaveResponse.tv_sec += 2*Beta0->tv_sec;
    
    /* compute Retry_Beta[1] .. Retry_Beta[N] */
    betax = (1000000 * Beta0->tv_sec + Beta0->tv_usec)/((1 << (Retry_N+1)) - 1);
    beta0 = (1000000 * Beta0->tv_sec + Beta0->tv_usec);
    timeused = 0;
    for (i = 1; i < Retry_N+2 && beta0 > timeused; i++)
	{
	if (betax < LOWERLIMIT)	/* NOTE: we don't bother with (beta0 - timeused < LOWERLIMIT) */
	    {
	    Retry_Beta[i].tv_sec = 0;
	    Retry_Beta[i].tv_usec = LOWERLIMIT;
	    timeused += LOWERLIMIT;
	    }
	else
	    {
	    if (beta0 - timeused > betax)
		{
		Retry_Beta[i].tv_sec = betax/1000000;
		Retry_Beta[i].tv_usec = betax % 1000000;
		timeused += betax;
		}
	    else
		{
		Retry_Beta[i].tv_sec = (beta0 - timeused)/1000000;
		Retry_Beta[i].tv_usec = (beta0 - timeused)%1000000;
		timeused = beta0;
		}
	    }
	betax = betax << 1;
	}
    return(0);
    }


long rpc2_SetRetry(IN Conn)
    struct CEntry *Conn;

    /*
      Resets the retry intervals for the given connection
      based on the number of retries and the keepalive
      interval (which don't change at the moment), and 
      the LowerLimit for the connection (which does change
      based on the RTT).  The comment for rpc2_InitRetry
      applies here.
    */
    {
    long betax, timeused, beta0;	/* entirely in microseconds */
    long i;

    assert(Conn);

    /* zero everything but the keep alive interval */
    memset(&Conn->Retry_Beta[1], 0, sizeof(struct timeval)*(1+Conn->Retry_N));
    
    /* recompute Retry_Beta[1] .. Retry_Beta[N] */
    /* betax is the shortest interval */
    betax = (1000000*Conn->Retry_Beta[0].tv_sec + Conn->Retry_Beta[0].tv_usec)/((1 << (Conn->Retry_N+1)) - 1);
    beta0 = (1000000*Conn->Retry_Beta[0].tv_sec + Conn->Retry_Beta[0].tv_usec);
    timeused = 0;
    for (i = 1; i < Conn->Retry_N+2 && beta0 > timeused; i++)
	{
	if (betax < Conn->LowerLimit)	/* NOTE: we don't bother with (beta0 - timeused < LOWERLIMIT) */
	    {
	    Conn->Retry_Beta[i].tv_sec = Conn->LowerLimit / 1000000;
	    Conn->Retry_Beta[i].tv_usec = Conn->LowerLimit % 1000000;
	    timeused += Conn->LowerLimit;
	    }
	else
	    {
	    if (beta0 - timeused > betax)
		{
		Conn->Retry_Beta[i].tv_sec = betax/1000000;
		Conn->Retry_Beta[i].tv_usec = betax % 1000000;
		timeused += betax;
		}
	    else
		{
		Conn->Retry_Beta[i].tv_sec = (beta0 - timeused)/1000000;
		Conn->Retry_Beta[i].tv_usec = (beta0 - timeused)%1000000;
		timeused = beta0;
		}
	    }
	betax = betax << 1;
	}
    return(0);
    }

int RPC2_SetTimeout(RPC2_Handle whichConn, struct timeval timeout)
{
    struct CEntry *Conn = rpc2_GetConn(whichConn);
    if (!Conn) return RPC2_NOCONNECTION;
    Conn->Retry_Beta[0] = timeout;
    return rpc2_SetRetry(Conn);
}

/* HACK. if bandwidth is low, increase retry intervals appropriately */
void rpc2_ResetLowerLimit(IN Conn, IN Packet)
    struct CEntry *Conn;
    RPC2_PacketBuffer *Packet;
{
    unsigned long delta, bits;

    Conn->reqsize = Packet->Prefix.LengthOfPacket;

    /* take response into account.  At least a packet header, probably more */
    bits = (Conn->reqsize + 2*sizeof(struct RPC2_PacketHeader)) * 8;
    delta = bits * 1000 / rpc2_Bandwidth;
    delta *= 1000;  /* was in msec to avoid overflow */

    say(4, RPC2_DebugLevel,
	"ResetLowerLimit: conn %#x, lower limit %ld usec, delta %ld usec\n",
	 Conn->UniqueCID, Conn->LowerLimit, delta);

    Conn->LowerLimit += delta;
    rpc2_SetRetry(Conn);
}


long rpc2_CancelRetry(IN Conn, IN Sle)
    struct CEntry *Conn;
    struct SL_Entry *Sle;	
    /* 
     * see if we've heard anything from a side effect
     * since we've been asleep. If so, pretend we got
     * a keepalive at that time, and activate a SLE 
     * with a timeout of beta_0 after that time.
     */
    {
    struct timeval now, lastword, timeout;
    struct timeval *retry;

    say(0, RPC2_DebugLevel, "rpc2_CancelRetry()\n");

    retry = Conn->Retry_Beta;

    if ((Conn->SEProcs != NULL) && 
	(Conn->SEProcs->SE_GetSideEffectTime != NULL) &&
	(Conn->SEProcs->SE_GetSideEffectTime(Conn->UniqueCID, &lastword) == RPC2_SUCCESS) &&
	TIMERISSET(&lastword)) {  /* don't bother unless we've actually heard */
	FT_GetTimeOfDay(&now, (struct timezone *)0);
	SUBTIME(&now, &lastword);
	say(9, RPC2_DebugLevel,
	    "Heard from side effect on %#x %ld.%06ld ago, retry interval was %ld.%06ld\n",
	     Conn->UniqueCID, now.tv_sec, now.tv_usec, 
	     retry[Sle->RetryIndex].tv_sec, retry[Sle->RetryIndex].tv_usec);
	if (CMPTIME(&now, &retry[Sle->RetryIndex], <)) {
	    timeout = retry[0];
	    SUBTIME(&timeout, &now);
	    say(/*9*/4, RPC2_DebugLevel,
		"Supressing retry %ld at %ld on %#x, new timeout = %ld.%06ld\n",
		 Sle->RetryIndex, rpc2_time(), Conn->UniqueCID,
	         timeout.tv_sec, timeout.tv_usec);

	    rpc2_Sent.Cancelled++;
	    Sle->RetryIndex = 0;
	    rpc2_ActivateSle(Sle, &timeout);
	    return(1);
	}
    }
    return(0);
  }


long rpc2_SendReliably(IN Conn, IN Sle, IN Packet, IN TimeOut)
    struct CEntry *Conn;
    struct SL_Entry *Sle;	
    RPC2_PacketBuffer *Packet;
    struct timeval *TimeOut;
    {
    struct SL_Entry *tlp;
    long hopeleft, finalrc = RPC2_SUCCESS;
    struct timeval *tout;
    struct timeval *ThisRetryBeta;

    say(0, RPC2_DebugLevel, "rpc2_SendReliably()\n");

    TR_SENDRELIABLY();

    if (TimeOut != NULL)
	{/* create a time bomb */
	tlp = rpc2_AllocSle(OTHER, NULL);
	rpc2_ActivateSle(tlp, TimeOut);
	}
    else tlp = NULL;

    ThisRetryBeta = Conn->Retry_Beta;
    if (TestRole(Conn, CLIENT))   /* stamp the outgoing packet */
	Packet->Header.TimeStamp = htonl(rpc2_MakeTimeStamp());
	    
    /* Do an initial send of the packet */
    say(9, RPC2_DebugLevel, "Sending try at %ld on %#x (timeout %ld.%06ld)\n",
			     rpc2_time(), Conn->UniqueCID,
			     ThisRetryBeta[1].tv_sec, ThisRetryBeta[1].tv_usec);

    if (rpc2_XmitPacket(Packet, Conn->HostInfo->Addr, 0) == -1) {
	rpc2_DeactivateSle(Sle, TIMEOUT);
	goto exit;
    }

    if (rpc2_Bandwidth) rpc2_ResetLowerLimit(Conn, Packet);

    /* Initialize the SL Entry */
    /* NOTE: we don't register for RetryBeta[0] here which is the 
       keepalive */ 
    Sle->RetryIndex = 1;
    rpc2_ActivateSle(Sle, &ThisRetryBeta[1]);

    do
	{
	hopeleft = 0;
	LWP_WaitProcess((char *)Sle);  /* SocketListener will awaken me */

	if (tlp && tlp->ReturnCode == TIMEOUT)
	    {
	    /* Overall timeout expired: clean up state and quit */
	    rpc2_IncrementSeqNumber(Conn);
	    SetState(Conn, C_THINK);
	    finalrc = RPC2_TIMEOUT;
	    break;  /* while */
	    }

	switch(Sle->ReturnCode)
	    {
	    case NAKED:
	    case ARRIVED:
		break;		/* switch */
	    
	    case KEPTALIVE:
		hopeleft = 1;
		Sle->RetryIndex = 0;
		rpc2_ActivateSle(Sle, &ThisRetryBeta[0]);
		break;	/* switch */
		
	    case RPC2_ABANDONED:
		Sle->ReturnCode = TIMEOUT;
		hopeleft = 0;
		break;

	    case TIMEOUT:
		if ((hopeleft = rpc2_CancelRetry(Conn, Sle)))
		    break;      /* switch; we heard from side effect recently */
		if (Sle->RetryIndex > Conn->Retry_N)
		    break;	/* switch; note hopeleft must be 0 */
		/* else retry with the next Beta value  for timeout */
		Sle->RetryIndex += 1;
		tout = &ThisRetryBeta[Sle->RetryIndex];

		if (tout->tv_sec  <= 0 && tout->tv_usec  <= 0)
		    break;	/* switch; LowerLimit must have shortened later retries to 0 */
		else hopeleft = 1;
		rpc2_ActivateSle(Sle, tout);
		say(9, RPC2_DebugLevel,
		    "Sending retry %ld at %ld on %#x (timeout %ld.%06ld)\n",
		     Sle->RetryIndex, rpc2_time(), Conn->UniqueCID,
		     tout->tv_sec, tout->tv_usec);
		Packet->Header.Flags = htonl((ntohl(Packet->Header.Flags) | RPC2_RETRY));
		if (TestRole(Conn, CLIENT))   /* restamp retries if client */
		    Packet->Header.TimeStamp = htonl(rpc2_MakeTimeStamp());
		rpc2_Sent.Retries += 1;
		if (rpc2_XmitPacket(Packet, Conn->HostInfo->Addr, 0) == -1) {
		    rpc2_DeactivateSle(Sle, TIMEOUT);
		    hopeleft = 0;
		}
		break;	/* switch */
		
	    default: assert(FALSE);
	    }
	}
    while (hopeleft);

exit:
    if (tlp)
    	{
	rpc2_DeactivateSle(tlp, 0);  	/* delete  time bomb */
	rpc2_FreeSle(&tlp);
	}

    return(finalrc);
    }


/* For converting packet headers to/from network order */
void rpc2_htonp(RPC2_PacketBuffer *p)
{
	p->Header.ProtoVersion = htonl(p->Header.ProtoVersion);
	p->Header.RemoteHandle = htonl(p->Header.RemoteHandle);
	p->Header.LocalHandle = htonl(p->Header.LocalHandle);
	p->Header.Flags = htonl(p->Header.Flags);
	p->Header.BodyLength = htonl(p->Header.BodyLength);
	p->Header.SeqNumber = htonl(p->Header.SeqNumber);
	p->Header.Opcode = htonl(p->Header.Opcode);
	p->Header.SEFlags = htonl(p->Header.SEFlags);
	p->Header.SEDataOffset = htonl(p->Header.SEDataOffset);
	p->Header.SubsysId = htonl(p->Header.SubsysId);
	p->Header.ReturnCode = htonl(p->Header.ReturnCode);
	p->Header.Lamport = htonl(p->Header.Lamport);
	p->Header.Uniquefier = htonl(p->Header.Uniquefier);
	p->Header.TimeStamp = htonl(p->Header.TimeStamp);
	p->Header.BindTime = htonl(p->Header.BindTime);
}

void rpc2_ntohp(RPC2_PacketBuffer *p)
{
	p->Header.ProtoVersion = ntohl(p->Header.ProtoVersion);
	p->Header.RemoteHandle = ntohl(p->Header.RemoteHandle);
	p->Header.LocalHandle = ntohl(p->Header.LocalHandle);
	p->Header.Flags = ntohl(p->Header.Flags);
	p->Header.BodyLength = ntohl(p->Header.BodyLength);
	p->Header.SeqNumber = ntohl(p->Header.SeqNumber);
	p->Header.Opcode = ntohl(p->Header.Opcode);
	p->Header.SEFlags = ntohl(p->Header.SEFlags);
	p->Header.SEDataOffset = ntohl(p->Header.SEDataOffset);
	p->Header.SubsysId = ntohl(p->Header.SubsysId);
	p->Header.ReturnCode = ntohl(p->Header.ReturnCode);
	p->Header.Lamport = ntohl(p->Header.Lamport);
	p->Header.Uniquefier = ntohl(p->Header.Uniquefier);
	p->Header.TimeStamp = ntohl(p->Header.TimeStamp);
	p->Header.BindTime = ntohl(p->Header.BindTime);
}

void rpc2_InitPacket(RPC2_PacketBuffer *pb, struct CEntry *ce, long bodylen)
{
	assert(pb);

	memset(&pb->Header, 0, sizeof(struct RPC2_PacketHeader));
	pb->Header.ProtoVersion = RPC2_PROTOVERSION;
	pb->Header.Lamport	= RPC2_LamportTime();
	pb->Header.BodyLength   = bodylen;
	pb->Prefix.LengthOfPacket = sizeof(struct RPC2_PacketHeader) + bodylen;
	//pb->Prefix.PeerAddr   = NULL;
	memset(&pb->Prefix.RecvStamp, 0, sizeof(struct timeval));
	if (ce)	{
		pb->Header.RemoteHandle = ce->PeerHandle;
		pb->Header.LocalHandle  = ce->UniqueCID;
		pb->Header.SubsysId  = ce->SubsysId;
		pb->Header.Uniquefier = ce->PeerUnique;
		SetPktColor(pb, ce->Color);
    	}
}
