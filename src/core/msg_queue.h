#ifndef _DIME_MSG_QUEUE_H
#define _DIME_MSG_QUEUE_H 

#include <stdint.h>
#include <glib.h>

typedef enum {
    MSG_INVALID,

    MSG_ENABLE,
    MSG_FOCUS_IN,
    MSG_FOCUS_OUT,
    MSG_ADD_IC,
    MSG_DEL_IC,
    MSG_CURSOR,
    MSG_INPUT,
    MSG_INPUT_FEEDBACK,

    MSG_COMMIT,
    MSG_PREEDIT,
    MSG_PREEDIT_CLEAR,
    MSG_FORWARD,

    MSG_ACQUIRE_TOKEN,
    MSG_RELEASE_TOKEN,
    MSG_CONNECT,

    MSG_MAX
} DIME_MSG_TYPE;

typedef struct {
    int8_t type;
    int8_t flags;

    uint32_t token; /* mark client */
    int8_t val;
} DimeMessageEnable;

typedef struct {
    int8_t type;
    int8_t flags;

    uint32_t token; /* mark client */
} DimeMessageFocus;

typedef struct {
    int8_t type;
    int8_t flags;

    uint32_t token; /* mark client */
} DimeMessageIc;

typedef struct {
    int8_t type;
    int8_t flags;

    uint32_t token; /* mark client */
    int32_t key;
    uint32_t time;
} DimeMessageInput;

typedef struct {
    int8_t type;
    int8_t flags;

    uint32_t token; /* mark client */
    uint32_t time; /* timestamp feedback sent */
    int result;
} DimeMessageInputFeedback;

typedef struct {
    int8_t type;
    int8_t flags;

    uint32_t token; /* mark client */
    short x, y, w, h;
} DimeMessageCursor;

// below are sent by server
typedef struct {
    int8_t type;
    int8_t flags;

    uint32_t id;
    uint32_t token; /* mark client */
    char* text; /* filled by server, and it only keeps valid before next 
                   message gets recieved. no need to be freed */
    int text_len;
} DimeMessageCommit;

typedef struct {
    int8_t type;
    int8_t flags;

    uint32_t token; /* mark client */
    char* text; /* see Commit.text */
    int text_len;
} DimeMessagePreedit;

typedef struct {
    int8_t type;
    int8_t flags;
    uint32_t token; /* mark client */
} DimeMessagePreeditClear;

typedef struct {
    int8_t type;
    int8_t flags;
    uint32_t token; /* mark client */
    int32_t key;
} DimeMessageForward;

typedef struct {
    int8_t type;
    int8_t flags;

    int id; /* opaque id (actually it's pid_t now) */
} DimeMessageConnect;

typedef struct {
    int8_t type;
    int8_t flags;
    int id; /* opaque id (actually it's pid_t now) */
    uint32_t token;
    uintptr_t outband; /* internal use, provided by client */
} DimeMessageToken;

/* flags for DimeMessage */
#define DIME_MSG_FLAG_SYNC   0x01

typedef union {
    struct {
        int8_t type;
        int8_t flags;
    };

    DimeMessageEnable enable;
    DimeMessageFocus focus;
    DimeMessageIc ic;
    DimeMessageInput input;
    DimeMessageInputFeedback input_feedback;
    DimeMessageCursor cursor;
    DimeMessageCommit commit;
    DimeMessagePreedit preedit;
    DimeMessagePreeditClear preedit_clear;
    DimeMessageForward forward;

    DimeMessageConnect connect;
    DimeMessageToken token;
} DimeMessage;


typedef struct _DimeServer DimeServer;
typedef struct _DimeClient DimeClient;

// client api

typedef gboolean (*DimeMessageCallback)(DimeClient*, DimeMessage*);

typedef struct _DimeMessageCallbacks DimeMessageCallbacks;
struct _DimeMessageCallbacks {
    DimeMessageCallback on_commit;
    DimeMessageCallback on_preedit;
    DimeMessageCallback on_preedit_clear;
    DimeMessageCallback on_forward;
    DimeMessageCallback on_enable;
};

/* one connection per process ? 
 * called implicitly when necessary
 **/
int dime_mq_connect();
int dime_mq_disconnect();
/* one connection can spawn multiple clients, each with one unique token as UUID */
DimeClient* dime_mq_acquire_token();
int dime_mq_release_token(DimeClient*);
int dime_mq_client_is_valid(DimeClient*);
int dime_mq_client_is_enabled(DimeClient*);
int dime_mq_client_is_focused(DimeClient*);

int dime_mq_client_enable(DimeClient*);
int dime_mq_client_focus(DimeClient*, gboolean val);
int dime_mq_client_key(DimeClient*, int key, uint32_t time); //sync
int dime_mq_client_key_async(DimeClient*, int key, uint32_t time);
int dime_mq_client_send(DimeClient*, int8_t flag, int8_t type, ...);
int dime_mq_client_set_receive_callbacks(DimeClient*, DimeMessageCallbacks cbs);

// server api
typedef gboolean (*DimeServerCallback)(DimeServer*, DimeMessage*);

typedef struct _DimeServerCallbacks DimeServerCallbacks;
struct _DimeServerCallbacks {
    DimeServerCallback on_input;
    DimeServerCallback on_enable;
    DimeServerCallback on_focus;
    DimeServerCallback on_cursor;
};
DimeServer* dime_mq_server_new();
void dime_mq_server_close(DimeServer* s);
int dime_mq_server_set_callbacks(DimeServer*, DimeServerCallbacks cbs);
int dime_mq_server_send(DimeServer*, int token, int8_t flag, int8_t type, ...);

#endif /* ifndef _DIME_MSG_QUEUE_H */
