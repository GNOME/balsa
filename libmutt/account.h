/*
 * Copyright (C) 2000 Brendan Cully <brendan@kublai.com>
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
 *     Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111, USA.
 */ 

/* remote host account manipulation (POP/IMAP) */

#ifndef _MUTT_ACCOUNT_H_
#define _MUTT_ACCOUNT_H_ 1

#include "url.h"

/* account types */
enum
{
  M_ACCT_TYPE_NONE = 0,
  M_ACCT_TYPE_IMAP,
  M_ACCT_TYPE_POP
};

/* account flags */
#define M_ACCT_PORT (1<<0)
#define M_ACCT_USER (1<<1)
#define M_ACCT_PASS (1<<2)
#define M_ACCT_SSL  (1<<3)

typedef struct
{
  char user[64];
  char pass[64];
  char host[128];
  unsigned short port;
  unsigned char type;
  unsigned char flags;
} ACCOUNT;

int mutt_account_match (const ACCOUNT* a1, const ACCOUNT* m2);
int mutt_account_fromurl (ACCOUNT* account, ciss_url_t* url);
void mutt_account_tourl (ACCOUNT* account, ciss_url_t* url);
int mutt_account_getuser (ACCOUNT* account);
int mutt_account_getpass (ACCOUNT* account);

#endif /* _MUTT_ACCOUNT_H_ */
