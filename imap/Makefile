# Program:	IMAP Toolkit Makefile
#
# Author:	Mark Crispin
#		Networks and Distributed Computing
#		Computing & Communications
#		University of Washington
#		Administration Building, AG-44
#		Seattle, WA  98195
#		Internet: MRC@CAC.Washington.EDU
#
# Date:		7 December 1989
# Last Edited:	30 December 1997
#
# Copyright 1997 by the University of Washington
#
#  Permission to use, copy, modify, and distribute this software and its
# documentation for any purpose and without fee is hereby granted, provided
# that the above copyright notice appears in all copies and that both the
# above copyright notice and this permission notice appear in supporting
# documentation, and that the name of the University of Washington not be
# used in advertising or publicity pertaining to distribution of the software
# without specific, written prior permission.  This software is made
# available "as is", and
# THE UNIVERSITY OF WASHINGTON DISCLAIMS ALL WARRANTIES, EXPRESS OR IMPLIED,
# WITH REGARD TO THIS SOFTWARE, INCLUDING WITHOUT LIMITATION ALL IMPLIED
# WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE, AND IN
# NO EVENT SHALL THE UNIVERSITY OF WASHINGTON BE LIABLE FOR ANY SPECIAL,
# INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
# LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, TORT
# (INCLUDING NEGLIGENCE) OR STRICT LIABILITY, ARISING OUT OF OR IN CONNECTION
# WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.


CAT=cat
CC=cc
CD=cd
LN=ln -s
MAKE=make
MKDIR=mkdir
RM=rm -rf
SH=sh
SYSTEM=unix
TOOLS=tools


# Make the IMAP Toolkit

all:	OSTYPE rebuild


#  The following ports are defined.  These refer to the *standard* compiler
# on the given system.  This means, for example, that the sol port is for SUN's
# compiler and not for a non-standard compiler such as gcc.
#  If you are using gcc and it is not the standard compiler on your system, try
# using an ANSI port that is close to what you have.  For example, if your
# system is SVR4ish, try a32 or lnx; if it's more BSDish, try nxt, mct, or bsi.
#
# a32	AIX 3.2 for RS/6000
# a41	AIX 4.1 for RS/6000
# aix	AIX/370
# ami	AmigaDOS
# am2	AmigaDOS with a 68020+
# ama	AmigaDOS using AS225R2
# amn	AmigaDOS with a 680x0 using "new" socket library
# aos	AOS for RT
# art	AIX 2.2.1 for RT
# asv	Altos SVR4
# aux	A/UX
# bsd	generic BSD
# bsf	FreeBSD
# bsi	BSD/i386
# cvx	Convex
# d-g	Data General DG/UX
# d41	Data General DG/UX 4.11
# d54	Data General DG/UX 5.4
# dpx	Bull DPX/2 B.O.S.
# drs	ICL DRS/NX
# dyn	Dynix
# epx	EP/IX
# gas	GCC Altos SVR4
# gh9   GCC HP-UX 9.x
# ghp	GCC HP-UX 10.x
# gso	GCC Solaris
# gsu	GCC SUN-OS
# gul	GCC RISC Ultrix (DEC-5000)
# hpp	HP-UX 9.x
# hpx	HP-UX 10.x
# hxd	HP-UX 10.x with DCE security
# isc	Interactive Systems
# lnx	Linux
# lyn	LynxOS
# mct	MachTen
# mnt	Atari ST Mint (not MacMint)
# neb	NetBSD/FreeBSD
# nxt	NEXTSTEP
# osf	OSF/1
# os4	OSF/1 (Digital UNIX) 4
# ptx	PTX
# pyr	Pyramid
# qnx	QNX 4
# s40	SUN-OS 4.0
# sc5	SCO Open Server 5.0.x
# sco	Santa Cruz Operation
# shp	HP-UX with Trusted Computer Base
# sgi	Silicon Graphics IRIX
# slx	Linux with shadow password security
# snx	Siemens Nixdorf SININX or Reliant UNIX
# sol	Solaris (won't work unless "ucbcc" works -- use gso instead)
# sos	OSF/1 with SecureWare
# ssn	SUN-OS with shadow password security
# sun	SUN-OS 4.1 or better
# sv2	SVR2 on AT&T PC-7300 (incomplete port)
# sv4	generic SVR4
# ult	RISC Ultrix (DEC-5000)
# uw2	UnixWare SVR4.2
# vul	VAX Ultrix
# vu2	VAX Ultrix 2.3 (e.g. for VAXstation-2000 or similar old version)

OSTYPE:
	@echo 'Not processed yet.  In a first-time build, you must specify'
	@echo 'the system type so that the sources are properly processed.'
	@false


# Note on SCO you may have to set LN to "ln".

a32 a41 aix bsf bsi d-g d41 d54 drs epx gas gh9 ghp gso gsu gul hpp hpx hxd lnx lyn mct mnt neb nxt osf os4 ptx qnx sc5 sco sgi shp slx snx sol sos uw2:
	$(MAKE) build OS=$@ PROCESS=an

# If you use sv4, you may find that it works to move it to use the an process.
# If so, you probably will want to delete the "-Dconst=" from the sv4 CFLAGS in
# the c-client Makefile.

aos art asv aux bsd cvx dpx dyn isc pyr s40 ssn sun sv4 ult vul vu2:
	$(MAKE) build OS=$@ PROCESS=ua

# SVR2 doesn't have symbolic links (at least my SVR2 system doesn't)

sv2:
	$(MAKE) build OS=$@ PROCESS=ua LN=ln


# Pine port names, not distinguished in c-client

bs2:
	$(MAKE) build OS=bsi PROCESS=an

pt1:
	$(MAKE) build OS=ptx PROCESS=an

# Amiga

ami am2 ama amn:
	$(MAKE) build OS=$@ PROCESS=an LN=cp SYSTEM=amiga

# Courtesy entry for NT

nt:
	nmake /nologo -f makefile.nt

# Courtesy entry for WCE

wce:
	nmake /nologo -f makefile.wce

build: clean process rebuild

process:
	@echo Processing sources for $(OS)...
	$(TOOLS)/$(PROCESS) "$(LN)" src/c-client c-client
	$(TOOLS)/$(PROCESS) "$(LN)" src/ansilib c-client
	$(TOOLS)/$(PROCESS) "$(LN)" src/charset c-client
	$(LN) `pwd`/src/kerberos/* c-client
	$(TOOLS)/$(PROCESS) "$(LN)" src/osdep/$(SYSTEM) c-client
	echo $(OS) > OSTYPE

rebuild:
	@echo Building processed sources for `cat OSTYPE`...
	$(CD) c-client;$(MAKE) `cat ../OSTYPE` CC=$(CC)

clean:
	@echo 'Removing old processed sources and binaries...'
	$(RM) OSTYPE c-client
	$(CD) tools;$(MAKE) clean


# A monument to a hack of long ago and far away...
love:
	@echo 'not war?'
