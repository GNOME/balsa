/*
 * Program:	BSI check password
 *
 * Author:	Mark Crispin
 *		Networks and Distributed Computing
 *		Computing & Communications
 *		University of Washington
 *		Administration Building, AG-44
 *		Seattle, WA  98195
 *		Internet: MRC@CAC.Washington.EDU
 *
 * Date:	1 August 1988
 * Last Edited:	27 January 1998
 *
 * Copyright 1998 by the University of Washington
 *
 *  Permission to use, copy, modify, and distribute this software and its
 * documentation for any purpose and without fee is hereby granted, provided
 * that the above copyright notice appears in all copies and that both the
 * above copyright notice and this permission notice appear in supporting
 * documentation, and that the name of the University of Washington not be
 * used in advertising or publicity pertaining to distribution of the software
 * without specific, written prior permission.  This software is made available
 * "as is", and
 * THE UNIVERSITY OF WASHINGTON DISCLAIMS ALL WARRANTIES, EXPRESS OR IMPLIED,
 * WITH REGARD TO THIS SOFTWARE, INCLUDING WITHOUT LIMITATION ALL IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE, AND IN
 * NO EVENT SHALL THE UNIVERSITY OF WASHINGTON BE LIABLE FOR ANY SPECIAL,
 * INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, TORT
 * (INCLUDING NEGLIGENCE) OR STRICT LIABILITY, ARISING OUT OF OR IN CONNECTION
 * WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 */

/* Check password
 * Accepts: login passwd struct
 *	    password string
 *	    argument count
 *	    argument vector
 * Returns: passwd struct if password validated, NIL otherwise
 */

#include <login_cap.h>

struct passwd *checkpw (struct passwd *pw,char *pass,int argc,char *argv[])
{
  char *s,tmp[MAILTMPLEN];
  login_cap_t *lc;
				/* make service name */
  sprintf (tmp,"auth-%s",(char *) mail_parameters (NIL,GET_SERVICENAME,NIL));
				/* validate password */
  return ((lc = login_getclass (pw->pw_class)) &&
	  (s = login_getstyle (lc,NIL,tmp)) &&
	  (auth_response (pw->pw_name,lc->lc_class,s,"response",NIL,"",pass)
	   > 0)) ? pw : NIL;
}
