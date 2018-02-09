#ifndef _DIME_MSG_QUEUE_H
#define _DIME_MSG_QUEUE_H 

#include <stdint.h>

typedef enum {
    MSG_INVALID,

    MSG_ENABLE,
    MSG_FOUCS_IN,
    MSG_FOUCS_OUT,
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
    int8_t focused;
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
    char text[1024];
} DimeMessageCommit;

typedef struct {
    int8_t type;
    int8_t flags;

    uint32_t token; /* mark client */
    char text[1024];
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
    int id;
    uint32_t token;
} DimeMessageToken;

/* flags for DimeMessage */
#define DIME_MSG_FLAG_SYNC   0x01

typedef union {
    int8_t type;
    int8_t flags;

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

/* token is globally unique for every single IC, which may comes from qt5 context,
 * gtk3 context etc. mq server maintains a bidirection map between connection and token
 **/

typedef uint32_t DIME_UUID;

typedef struct _DimeServer DimeServer;
typedef struct _DimeClient DimeClient;

// client api

typedef gboolean (*DimeMessageCallback)(DimeServer* s, DimeMessage* msg);


/* one connection per process ? 
 * called implicitly when necessary
 **/
int dime_mq_connect();
int dime_mq_disconnect();
/* one connection can spawn multiple clients, each with one unique token as UUID */
DimeClient* dime_mq_acquire_token();
int dime_mq_release_token(DimeClient*);

int dime_mq_client_send(DimeClient*, int8_t flag, int8_t type, ...);
int dime_mq_client_set_receive_callback(DimeClient*, DimeMessageCallback cb);

// server api
DimeServer* dime_mq_server_new();
void dime_mq_server_close(DimeServer* s);

#endif /* ifndef _DIME_MSG_QUEUE_H */
