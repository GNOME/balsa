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

static sigset_t Sigset;
static int IsEndwin = 0;

/* Attempt to catch "ordinary" signals and shut down gracefully. */
RETSIGTYPE mutt_exit_handler (int sig)
{
  curs_set (1);
  endwin (); /* just to be safe */
#if SYS_SIGLIST_DECLARED
  printf("Caught %s...  Exiting.\n", sys_siglist[sig]);
#else
#if (__sun__ && __svr4__)
  printf("Caught %s...  Exiting.\n", _sys_siglist[sig]);
#else
  printf("Caught signal %d...  Exiting.\n", sig);
#endif
#endif
  exit (0);
}

RETSIGTYPE sighandler (int sig)
{
  switch (sig)
  {
    case SIGTSTP: /* user requested a suspend */
      if(!option(OPTSUSPEND))
        break;
      IsEndwin = isendwin ();
      curs_set (1);
      if (!IsEndwin)
	endwin ();
      kill (0, SIGSTOP);

    case SIGCONT:
      if (!IsEndwin)
	refresh ();
      mutt_curs_set (-1);
      break;

#if defined (USE_SLANG_CURSES) || defined (HAVE_RESIZETERM)
    case SIGWINCH:
      Signals |= S_SIGWINCH;
      break;
#endif
    case SIGINT:
      Signals |= S_INTERRUPT;
      break;

    default:
      break;
  }
}

#ifdef USE_SLANG_CURSES
int mutt_intr_hook (void)
{
  return (-1);
}
#endif /* USE_SLANG_CURSES */

void mutt_signal_init (void)
{
  struct sigaction act;

  memset (&act, 0, sizeof (struct sigaction));

  act.sa_handler = SIG_IGN;
  sigaction (SIGPIPE, &act, 0);

  act.sa_handler = mutt_exit_handler;
  sigaction (SIGTERM, &act, 0);
  sigaction (SIGHUP, &act, 0);

  act.sa_handler = sighandler;
#ifdef SA_INTERRUPT
  /* POSIX.1 uses SA_RESTART, but SunOS 4.x uses this instead */
  act.sa_flags = SA_INTERRUPT;
#endif
  sigaction (SIGCONT, &act, 0);
  sigaction (SIGINT, &act, 0);

  /* SIGTSTP is the one signal in which we want to restart a system call if it
   * was interrupted in progress.  This is especially important if we are in
   * the middle of a system() call, like if the user is editing a message.
   * Otherwise, the system() will exit when SIGCONT is received and Mutt will
   * resume even though the subprocess may not be finished.
   */
#ifdef SA_RESTART
  act.sa_flags = SA_RESTART;
#else
  act.sa_flags = 0;
#endif
  sigaction (SIGTSTP, &act, 0);

#if defined (USE_SLANG_CURSES) || defined (HAVE_RESIZETERM)
  sigaction (SIGWINCH, &act, 0);
#endif

#ifdef USE_SLANG_CURSES
  /* This bit of code is required because of the implementation of
   * SLcurses_wgetch().  If a signal is received (like SIGWINCH) when we
   * are in blocking mode, SLsys_getkey() will not return an error unless
   * a handler function is defined and it returns -1.  This is needed so
   * that if the user resizes the screen while at a prompt, it will just
   * abort and go back to the main-menu.
   */
  SLang_getkey_intr_hook = mutt_intr_hook;
#endif
}

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

void mutt_block_signals_system (void)
{
  struct sigaction sa;

  if (! option (OPTSIGNALSBLOCKED))
  {
    sa.sa_handler = SIG_IGN;
    sa.sa_flags = 0;
    sigaction (SIGINT, &sa, NULL);
    sigaction (SIGQUIT, &sa, NULL);
    sigemptyset (&Sigset);
    sigaddset (&Sigset, SIGCHLD);
    sigprocmask (SIG_BLOCK, &Sigset, 0);
    set_option (OPTSIGNALSBLOCKED);
  }
}

void mutt_unblock_signals_system (int catch)
{
  struct sigaction sa;

  if (option (OPTSIGNALSBLOCKED))
  {
    sa.sa_flags = 0;
    sa.sa_handler = mutt_exit_handler;
    sigaction (SIGQUIT, &sa, NULL);
    if (catch)
      sa.sa_handler = sighandler;
    sigaction (SIGINT, &sa, NULL);
    sigprocmask (SIG_UNBLOCK, &Sigset, NULL);
    unset_option (OPTSIGNALSBLOCKED);
  }
}
