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

#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

int _mutt_system (const char *cmd, int flags)
{
  int rc = -1;
  struct sigaction act;
  struct sigaction oldtstp;
  struct sigaction oldcont;
  sigset_t set;
  pid_t thepid;

  if (!cmd || !*cmd)
    return (0);

  /* must ignore SIGINT and SIGQUIT */

  mutt_block_signals_system ();

  /* also don't want to be stopped right now */
  if (flags & M_DETACH_PROCESS)
  {
    sigemptyset (&set);
    sigaddset (&set, SIGTSTP);
    sigprocmask (SIG_BLOCK, &set, NULL);
  }
  else
  {
    act.sa_handler = SIG_DFL;
    /* we want to restart the waitpid() below */
#ifdef SA_RESTART
    act.sa_flags = SA_RESTART;
#endif
    sigemptyset (&act.sa_mask);
    sigaction (SIGTSTP, &act, &oldtstp);
    sigaction (SIGCONT, &act, &oldcont);
  }

  if ((thepid = fork ()) == 0)
  {
    act.sa_flags = 0;

    if (flags & M_DETACH_PROCESS)
    {
      int fd;

      /* give up controlling terminal */
      setsid ();

      switch (fork ())
      {
	case 0:
#if defined(OPEN_MAX)
	  for (fd = 0; fd < OPEN_MAX; fd++)
	    close (fd);
#elif defined(_POSIX_OPEN_MAX)
	  for (fd = 0; fd < _POSIX_OPEN_MAX; fd++)
	    close (fd);
#else
	  close (0);
	  close (1);
	  close (2);
#endif
	  chdir ("/");
	  act.sa_handler = SIG_DFL;
	  sigaction (SIGCHLD, &act, NULL);
	  break;

	case -1:
	  _exit (127);

	default:
	  _exit (0);
      }
    }

    /* reset signals for the child; not really needed, but... */
    mutt_unblock_signals_system (0);
    act.sa_handler = SIG_DFL;
    act.sa_flags = 0;
    sigemptyset (&act.sa_mask);
    sigaction (SIGTERM, &act, NULL);
    sigaction (SIGTSTP, &act, NULL);
    sigaction (SIGCONT, &act, NULL);

    execl (EXECSHELL, "sh", "-c", cmd, NULL);
    _exit (127); /* execl error */
  }
  else if (thepid != -1)
  {
    /* wait for the (first) child process to finish */
    waitpid (thepid, &rc, 0);
  }

  sigaction (SIGCONT, &oldcont, NULL);
  sigaction (SIGTSTP, &oldtstp, NULL);

  /* reset SIGINT, SIGQUIT and SIGCHLD */
  mutt_unblock_signals_system (1);
  if (flags & M_DETACH_PROCESS)
    sigprocmask (SIG_UNBLOCK, &set, NULL);

  rc = (thepid != -1) ? (WIFEXITED (rc) ? WEXITSTATUS (rc) : -1) : -1;

  return (rc);
}
