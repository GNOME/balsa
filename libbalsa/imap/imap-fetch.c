#include "imap-handle.h"
#include "imap-fetch.h"
#include "imap_private.h"

ImapResponse
imap_mbox_handle_fetch(ImapMboxHandle* handle, const gchar *seq, 
                       const gchar* headers[])
{
  char* cmd;
  int i;
  GString* hdr = g_string_new(headers[0]);
  ImapResponse rc;
  
  for(i=1; headers[i]; i++) {
    g_string_append_c(hdr, ' ');
    g_string_append(hdr, headers[i]);
  }
  cmd = g_strdup_printf("FETCH %s (FLAGS BODY[HEADER.FIELDS (%s)])",
                        seq, hdr->str);
  g_string_free(hdr, TRUE);
  rc = imap_cmd_exec(handle, cmd);
  g_free(cmd);
  return rc;
}

ImapResponse
imap_mbox_handle_fetch_env(ImapMboxHandle* handle, const gchar *seq)
{
  char* cmd;
  ImapResponse rc;
  
  cmd = g_strdup_printf("FETCH %s (ENVELOPE FLAGS UID)", seq);
  rc = imap_cmd_exec(handle, cmd);
  g_free(cmd);
  return rc;
}

ImapResponse
imap_mbox_handle_fetch_body(ImapMboxHandle* handle, const gchar *seq)
{
  char* cmd;
  ImapResponse rc;
  
  cmd = g_strdup_printf("FETCH %s (FLAGS BODY.PEEK[])", seq);
  rc = imap_cmd_exec(handle, cmd);
  g_free(cmd);
  return rc;
}


