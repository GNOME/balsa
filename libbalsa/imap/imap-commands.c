#include "imap-handle.h"
#include "imap-commands.h"
#include "imap_private.h"

/* RFC2060 */
/* 6.1 Client Commands - Any State */


/* 6.1.1 CAPABILITY Command */
/* imap_check_capability: make sure we can log in to this server. */
static gboolean
imap_check_capability(ImapMboxHandle* handle)
{
  if (imap_cmd_exec(handle, "CAPABILITY") != 0)
    return FALSE;

  if (!(imap_mbox_handle_can_do(handle, IMCAP_IMAP4) ||
        imap_mbox_handle_can_do(handle, IMCAP_IMAP4REV1))) {
    g_warning("IMAP4rev1 required but not provided.\n");
    return FALSE;
  }  
  return TRUE;
}

int
imap_mbox_handle_can_do(ImapMboxHandle* handle, ImapCapability cap)
{
  /* perhaps it already has capabilities? */
  if(!handle->has_capabilities)
    imap_check_capability(handle);

  if(cap>=0 && cap<IMCAP_MAX)
    return handle->capabilities[cap];
  else return 0;
}



/* 6.1.2 NOOP Command */
ImapResult
imap_mbox_handle_noop(ImapMboxHandle *handle)
{
  return imap_cmd_exec(handle, "NOOP") == IMR_OK 
    ? IMAP_SUCCESS : IMAP_PROTOCOL_ERROR;
}


/* 6.1.3 LOGOUT Command */
/* unref handle to logout */


/* 6.2 Client Commands - Non-Authenticated State */


/* 6.2.1 AUTHENTICATE Command */
/* for CRAM methode implemented in auth-cram.c */


/* 6.2.2 LOGIN Command */
/* implemented in imap-auth.c */


/* 6.3 Client Commands - Authenticated State */


/* 6.3.1 SELECT Command */
ImapResult
imap_mbox_select(ImapMboxHandle* handle, const char* mbox)
{
  gchar* cmd;
  ImapResponse rc;
  if (handle->state == IMHS_SELECTED && strcmp(handle->mbox, mbox) == 0)
      return IMAP_SUCCESS;

  cmd = g_strdup_printf("SELECT %s", mbox);
  rc = imap_cmd_exec(handle, cmd);
  g_free(cmd);
  if(rc == IMR_OK) {
    g_free(handle->mbox);
    handle->mbox = g_strdup(mbox);
    handle->state = IMHS_SELECTED;
    return IMAP_SUCCESS;
  } else return IMAP_SELECT_FAILED;
}



/* 6.3.2 EXAMINE Command */
ImapResult
imap_mbox_examine(ImapMboxHandle* handle, const char* mbox)
{
  gchar* cmd = g_strdup_printf("EXAMINE %s", mbox);
  ImapResponse rc = imap_cmd_exec(handle, cmd);
  g_free(cmd);
  if(rc == IMR_OK) {
    g_free(handle->mbox);
    handle->mbox = g_strdup(mbox);
    handle->state = IMHS_SELECTED;
    return IMAP_SUCCESS;
  } else return IMAP_SELECT_FAILED;
}


/* 6.3.3 CREATE Command */
ImapResult
imap_mbox_create(ImapMboxHandle* handle, const char* new_mbox)
{
  gchar* cmd = g_strdup_printf("CREATE %s", new_mbox);
  ImapResponse rc = imap_cmd_exec(handle, cmd);
  g_free(cmd);
  if(rc == IMR_OK)
    return IMAP_SUCCESS;
  else
    return IMAP_SELECT_FAILED;
}


/* 6.3.4 DELETE Command */
ImapResult
imap_mbox_delete(ImapMboxHandle* handle, const char* mbox)
{
  gchar* cmd = g_strdup_printf("DELETE %s", mbox);
  ImapResponse rc = imap_cmd_exec(handle, cmd);
  g_free(cmd);
  if(rc == IMR_OK)
    return IMAP_SUCCESS;
  else
    return IMAP_SELECT_FAILED;
}


/* 6.3.5 RENAME Command */
ImapResult
imap_mbox_rename(ImapMboxHandle* handle,
		 const char* old_mbox,
		 const char* new_mbox)
{
  gchar* cmd = g_strdup_printf("RENAME %s %s", old_mbox, new_mbox);
  ImapResponse rc = imap_cmd_exec(handle, cmd);
  g_free(cmd);
  if(rc == IMR_OK)
    return IMAP_SUCCESS;
  else
    return IMAP_SELECT_FAILED;
}


/* 6.3.6 SUBSCRIBE Command */
/* 6.3.7 UNSUBSCRIBE Command */
ImapResult
imap_mbox_subscribe(ImapMboxHandle* handle,
		    const char* mbox, gboolean subscribe)
{
  gchar* cmd = g_strdup_printf("%s %s",
			       subscribe ? "SUBSCRIBE" : "UNSUBSCRIBE",
			       mbox);
  ImapResponse rc = imap_cmd_exec(handle, cmd);
  g_free(cmd);
  if(rc == IMR_OK)
    return IMAP_SUCCESS;
  else
    return IMAP_SELECT_FAILED;
}


/* 6.3.8 LIST Command */
ImapResult
imap_mbox_list(ImapMboxHandle *handle, const char*ref, const char *mbox)
{
  gchar * cmd;
  ImapResult rc;
  int len;
  char *delim = "/";

  len = strlen(ref);
  /* FIXME: should use imap server folder separator */ 
  if (ref[len-1]==delim[0])
      delim = "";
  cmd = g_strdup_printf("LIST \"%s%s\" \"%s\"", ref, delim, mbox);
  rc = imap_cmd_exec(handle, cmd);  
  g_free(cmd);
  return rc == IMR_OK ? IMAP_SUCCESS : IMAP_PROTOCOL_ERROR;
}


/* 6.3.9 LSUB Command */
ImapResult
imap_mbox_lsub(ImapMboxHandle *handle, const char*ref, const char *mbox)
{
  gchar * cmd;
  ImapResult rc;
  int len;
  char *delim = "/";

  len = strlen(ref);
  /* FIXME: should use imap server folder separator */ 
  if (ref[len-1]==delim[0])
      delim = "";
  cmd = g_strdup_printf("LSUB \"%s%s\" \"%s\"", ref, delim, mbox);
  rc = imap_cmd_exec(handle, cmd);  
  g_free(cmd);
  return rc == IMR_OK ? IMAP_SUCCESS : IMAP_PROTOCOL_ERROR;
}


/* 6.3.10 STATUS Command */
/* FIXME: implement */


/* 6.3.11 APPEND Command */
ImapResult
imap_mbox_append(ImapMboxHandle *handle, ImapMsgFlags flags,
		 size_t len, const char *msgtext)
{
  GString *cmd;
  int i;
  int flags_todo=0;
  char *append_cmd;
  ImapCmdTag tag;
  ImapResponse rc;

  g_return_val_if_fail(handle, IMR_BAD);
  if (handle->state == IMHS_DISCONNECTED)
    return IMAP_PROTOCOL_ERROR;

  cmd = g_string_new("APPEND ");
  g_string_append(cmd, handle->mbox);
  for (i=0; i < IMSGF_LAST; i++)
      if (flags[i]==TRUE) {
	  flags_todo++;
      }
  if (flags_todo) {
      g_string_append(cmd, " (");
      for (i=0; i < IMSGF_LAST; i++)
	  if (flags[i])
	      g_string_append_printf(cmd, "\\%s%s", msg_flags[i],
				     --flags_todo ? " " : "");
      g_string_append(cmd, ")");
  }
  g_string_append_printf(cmd, " {%d}", len);
  append_cmd = g_string_free(cmd, FALSE);

  /* create sequence for command */
  rc = imap_cmd_start(handle, append_cmd, tag);
  if (rc<0) /* irrecoverable connection error. */
    return IMAP_PROTOCOL_ERROR;

  sio_flush(handle->sio);
  do {
    rc = imap_cmd_step (handle, tag);
  } while (rc == IMR_UNTAGGED);

  if (rc == IMR_RESPOND) {
    char line[2048];
    sio_gets(handle->sio, line, sizeof(line));
    sio_write(handle->sio, msgtext, len);
    sio_write(handle->sio, "\n", 1);
    sio_flush(handle->sio);
    do {
	rc = imap_cmd_step (handle, tag);
    } while (rc == IMR_UNTAGGED);
  }

  if (rc != IMR_OK)
    g_warning("cmd '%s' failed.\n", cmd, IMAP_SUCCESS);
  g_free(append_cmd);
  return IMAP_SUCCESS;
}


/* 6.4 Client Commands - Selected State */


/* 6.4.1 CHECK Command */
/* FIXME: implement */

/* 6.4.2 CLOSE Command */
/* FIXME: implement */

/* 6.4.3 EXPUNGE Command */
/* FIXME: implement */

/* 6.4.4 SEARCH Command */
ImapResult
imap_mbox_search(ImapMboxHandle *h, const char* query)
{
  ImapResult res;
  gchar* cmd = g_strdup_printf("SEARCH %s", query);
  res = imap_cmd_exec(h, cmd) == IMR_OK 
    ? IMAP_SUCCESS : IMAP_PROTOCOL_ERROR;
  g_free(cmd);
  return res;
}

ImapResult
imap_mbox_uid_search(ImapMboxHandle *h, const char* query)
{
  ImapResult res;
  gchar* cmd = g_strdup_printf("UID SEARCH %s", query);
  res = imap_cmd_exec(h, cmd) == IMR_OK 
    ? IMAP_SUCCESS : IMAP_PROTOCOL_ERROR;
  g_free(cmd);
  return res;
}


/* 6.4.5 FETCH Command */
/* implemented in imap-fetch.c */


/* 6.4.6 STORE Command */
ImapResult
imap_mbox_store_flag(ImapMboxHandle *h, int seq, ImapMsgFlag flg, 
                     gboolean state)
{
  ImapResult res;
  gchar* cmd = g_strdup_printf("STORE %d %cFLAGS (\\%s)", seq,
                               state ? '+' : '-', msg_flags[flg]);
  res = imap_cmd_exec(h, cmd) == IMR_OK 
    ? IMAP_SUCCESS : IMAP_PROTOCOL_ERROR;
  g_free(cmd);
  return res;
}


/* 6.4.7 COPY Command */
/* FIXME: implement */

/* 6.4.8 UID Command */
/* FIXME: implement */
/* implemented as alternatives of the commands */


/* 6.5 Client Commands - Experimental/Expansion */
/* 6.5.1 X<atom> Command */
ImapResult
imap_mbox_scan(ImapMboxHandle *handle, const char*what, const char*str)
{
  gchar * cmd = g_strdup_printf("SCAN \"%s\" \"*\" \"%s\"",what, str);
  ImapResult rc = 
    imap_cmd_exec(handle, cmd) == IMR_OK 
    ? IMAP_SUCCESS : IMAP_PROTOCOL_ERROR;  
  g_free(cmd);
  return rc;
}

