/* libimap library.
 * Copyright (C) 2003-2016 Pawel Salek.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option) 
 * any later version.
 *  
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of 
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the  
 * GNU General Public License for more details.
 *  
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */
/*
 * STARTTLS Command
 * see RFC2595, A. Appendix -- Compliance Checklist

   Rules (for client)                                    Section
   -----                                                 -------
   Mandatory-to-implement Cipher Suite                      2.1   OK
   SHOULD have mode where encryption required               2.2   OK
   client MUST check server identity                        2.4   
   client MUST use hostname used to open connection         2.4   OK
   client MUST NOT use hostname from insecure remote lookup 2.4   OK
   client SHOULD support subjectAltName of dNSName type     2.4   OK
   client SHOULD ask for confirmation or terminate on fail  2.4   OK
   MUST check result of STARTTLS for acceptable privacy     2.5   OK
   client MUST NOT issue commands after STARTTLS
      until server response and negotiation done        3.1,4,5.1 OK
   client MUST discard cached information             3.1,4,5.1,9 OK
   client SHOULD re-issue CAPABILITY/CAPA command       3.1,4     OK
   IMAP client MUST NOT issue LOGIN if LOGINDISABLED        3.2   OK

   client SHOULD warn when session privacy not active and/or
     refuse to proceed without acceptable security level    9
   SHOULD be configurable to refuse weak mechanisms or
     cipher suites                                          9
 */

#include "config.h"
#include "siobuf-nc.h"
#include "imap_private.h"

ImapResponse
imap_handle_starttls(ImapMboxHandle *handle, GError **error)
{
	ImapResponse rc;

	IMAP_REQUIRED_STATE1(handle, IMHS_CONNECTED, IMR_BAD);
	if (!imap_mbox_handle_can_do(handle, IMCAP_STARTTLS))
		return IMR_NO;

	rc = imap_cmd_exec(handle, "StartTLS");
	if (rc != IMR_OK) {
		return rc;
	}
	if (net_client_start_tls(NET_CLIENT(handle->sio), error)) {
		handle->has_capabilities = 0;
		return IMR_OK;
	} else {
		/* ssl is owned now by sio, no need to free it SSL_free(ssl); */
		imap_handle_disconnect(handle);
		return IMR_NO;
	}
}
