#ifndef __MBOX_HANDLE_H__
#define __MBOX_HANDLE_H__

#include <glib.h>

#include "imap.h"

typedef enum {
  IM_EVENT_EXISTS,
  IM_EVENT_EXPUNGE
} ImapMboxEventType;

/* connection states, as defined in rfc-2060, 3 */
typedef enum {
  IMHS_DISCONNECTED,
  IMHS_CONNECTED, /* non authenticated */
  IMHS_AUTHENTICATED,
  IMHS_SELECTED
} ImapMboxHandleState;

/* imap server responses, as defined in rfc-2060 */
typedef enum {
  IMR_SEVERED,  /* OK, this is a connection problem */
  IMR_UNTAGGED, /* this is seen only in low-level: "*" */
  IMR_RESPOND,  /* response is expected: "+" */
  IMR_PROTOCOL, /* unexpected protocol error */
  IMR_OK,
  IMR_NO,
  IMR_BAD
} ImapResponse;

typedef char ImapCmdTag[7]; /* Imap command tag */

/* Capabilities recognized by the library. Add new ones alphabetically. */
typedef enum
{
  IMCAP_IMAP4 = 0,
  IMCAP_IMAP4REV1,
  IMCAP_STATUS,
  IMCAP_ACL,			/* RFC 2086: IMAP4 ACL extension */
  IMCAP_NAMESPACE,              /* RFC 2342: IMAP4 Namespace */
  IMCAP_ACRAM_MD5,		/* RFC 2195: CRAM-MD5 authentication */
  IMCAP_AGSSAPI,		/* RFC 1731: GSSAPI authentication */
  IMCAP_AUTH_ANON, 	        /*           AUTH=ANONYMOUS */
  IMCAP_STARTTLS,		/* RFC 2595: STARTTLS */
  IMCAP_LOGINDISABLED,		/*           LOGINDISABLED */
  IMCAP_THREAD_ORDEREDSUBJECT,
  IMCAP_THREAD_REFERENCES,
  IMCAP_MAX
} ImapCapability;

typedef enum
{
    IMLIST_NOINFERIORS = 1<<0,
    IMLIST_NOSELECT    = 1<<1,
    IMLIST_MARKED      = 1<<2,
    IMLIST_UNMARKED    = 1<<3
} ImapListFlags;

typedef struct _ImapMboxHandleClass ImapMboxHandleClass;

typedef void (*LBIResponseCallback)(ImapMboxHandle *h, const char* response,
                                    void *data);

typedef struct {
  const gchar* response;
  LBIResponseCallback cb;
  void *cb_data;
} LBIResponseHandler;

ImapMboxHandle *imap_mbox_handle_new(void);

typedef void (*ImapMboxNotifyCb)(ImapMboxHandle*handle, ImapMboxEventType ev, 
                                 int seqno, void* data);

typedef void (*ImapInfoCb)(const char *buffer, void *arg);
typedef void (*ImapMonitorCb)(const char *buffer, int length, int direction,
                              void *arg);

typedef void (*ImapFlagsCb)(unsigned seqno, void *arg);

void imap_handle_set_monitorcb(ImapMboxHandle* h, ImapMonitorCb cb, void*);
void imap_handle_set_infocb(ImapMboxHandle* h, ImapInfoCb cb, void*);
void imap_handle_set_alertcb(ImapMboxHandle* h, ImapInfoCb cb, void*);
void imap_handle_set_flagscb(ImapMboxHandle* h, ImapFlagsCb cb, void*);

ImapResult imap_mbox_handle_connect(ImapMboxHandle* r,
                                    const char *hst, int prt, 
                                    const char* user, const char* passwd, 
                                    const char *mbox);

typedef void(*ImapListCallback)(const char* mbox, gboolean *flags, void*);

#include "imap-commands.h"
#include "imap-fetch.h"

/* int below is a boolean */
int      imap_mbox_handle_can_do(ImapMboxHandle* handle, ImapCapability cap);
unsigned imap_mbox_handle_get_exists(ImapMboxHandle* handle);
GNode *imap_mbox_handle_get_thread_root(ImapMboxHandle* handle);

void imap_mbox_handle_connect_notify(ImapMboxHandle* handle,
                                     ImapMboxNotifyCb cb,
                                     void *data);

void imap_mbox_handle_destroy(ImapMboxHandle* handle);

ImapMessage* imap_mbox_handle_get_msg(ImapMboxHandle* handle, unsigned no);
unsigned imap_mbox_handle_first_unseen(ImapMboxHandle* handle);

/* possibly private functions... */
ImapMboxHandleState imap_mbox_handle_get_state(ImapMboxHandle *h);

ImapResponse imap_cmd_exec(ImapMboxHandle* handle, const char* cmd);
char* imap_mbox_gets(ImapMboxHandle *h, char* buf, size_t sz);

/* even more private functions */
void imap_mbox_handle_set_state(ImapMboxHandle *h,
                                ImapMboxHandleState newstate);

int imap_cmd_start(ImapMboxHandle* handle, const char* cmd, char* tag);
ImapResponse imap_cmd_step(ImapMboxHandle* handle, const char* tag);
int imap_handle_write(ImapMboxHandle *conn, const char *buf, size_t len);
#endif
