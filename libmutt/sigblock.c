/*
 * Copyright (C) 1996-8 Michael R. Elkins <me@cs.hmc.edu>
 * 
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 * 
 *     This program is distributed in the hope that it will be useful,
 *     but WITHOUT ANY WARRANTY; without even the implied warranty of
 *     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *     GNU General Public License for more details.
 * 
 *     You should have received a copy of the GNU General Public License
 *     along with this program; if not, write to the Free Software
 *     Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */ 

#include "mutt.h"
#include "mutt_curses.h"

#include <signal.h>
#include <string.h>
#include <sys/wait.h>

extern sigset_t Sigset;

/* signals which are important to block while doing critical ops */
void mutt_block_signals (void)
{
  if (!option (OPTSIGNALSBLOCKED))
  {
    sigemptyset (&Sigset);
    sigaddset (&Sigset, SIGINT);
    sigaddset (&Sigset, SIGWINCH);
    sigaddset (&Sigset, SIGHUP);
    sigaddset (&Sigset, SIGTERM);
    sigaddset (&Sigset, SIGTSTP);
    sigprocmask (SIG_BLOCK, &Sigset, 0);
    set_option (OPTSIGNALSBLOCKED);
  }
}

/* restore the previous signal mask */
void mutt_unblock_signals (void)
{
  if (option (OPTSIGNALSBLOCKED))
  {
    sigprocmask (SIG_UNBLOCK, &Sigset, 0);
    unset_option (OPTSIGNALSBLOCKED);
  }
}
