#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#include <fcntl.h>
#include <sys/stat.h>
#include <mqueue.h>

#include <glib.h>

#include "msg_queue.h"
#include "log.h"

//FIXME: should we consider multi-seat ? multi-display ?
#define DIME_SERVER_MQ_NAME_TMPL "/dime-server-%s"
#define DIME_CONNECTION_MQ_NAME_TMPL "/dime-connect-%s-%d"

#ifdef G_LOG_DOMAIN
#undef G_LOG_DOMAIN
#endif
#define G_LOG_DOMAIN "mq"

#define MSG_BUF_SIZE 1024 /* this is big enough to hold any message */
#define MSG_MAX_NR 10

#define CLIENT_MAGIC 0xfadeceed

struct _DimeServer {
    DIME_UUID initial; /* increment */
    DIME_UUID last;
    mqd_t mq;
    char mq_name[NAME_MAX];
    char *msgbuf;
    long msgsize;

    GIOChannel *ch;
    GHashTable *connections;
    GHashTable *token_map; /* <token, id> */
};

/* connection state */
enum {
    CONN_INITIALIZED,
    CONN_HANDSHAKE,
    CONN_ESTABLISHED,
};

typedef struct _DimeConnection {
    int id;

    int8_t state;

    char mq_name[NAME_MAX];
    mqd_t mq_srv; /* send request */

    char mq_msg_name[NAME_MAX];
    mqd_t mq_msg; /* receive messages */

    char *msgbuf;
    long msgsize;

    GIOChannel *ch;
} DimeConnection;

//FIXME: clients <-> mq map
struct _DimeClient {
    uint32_t magic;
    DIME_UUID token; /* if token is 0, means token is not requested from server, 
                        the reasons vary, e.g server is down */

    DimeConnection* conn; /* shared connection between all clients from one context */
};

static const char* g_msgname[MSG_MAX] = {
    /*MSG_INVALID,*/        "<INVAL>",
    /*MSG_ENABLE,*/         "ENABLE",
    /*MSG_FOUCS_IN,*/       "FOCUS_IN",
    /*MSG_FOUCS_OUT,*/      "FOCUS_OUT",
    /*MSG_ADD_IC,*/         "ADD_IC",
    /*MSG_DEL_IC,*/         "DEL_IC",
    /*MSG_CURSOR,*/         "CURSOR",
    /*MSG_INPUT,*/          "INPUT",
    /*MSG_INPUT_FEEDBACK,*/ "INPUT_FEEDBACK",

    /*MSG_COMMIT,*/         "COMMIT",
    /*MSG_PREEDIT,*/        "PREEDIT",
    /*MSG_PREEDIT_CLEAR,*/  "PREEDIT_CLEAR",
    /*MSG_FORWARD,*/        "FORWARD",

    /*MSG_ACQUIRE_TOKEN,*/  "ACK_TOKEN",
    /*MSG_RELEASE_TOKEN,*/  "REL_TOKEN",
    /*MSG_CONNECT,*/        "CONNECT",
};

static size_t g_msgsz[MSG_MAX] = {
    /*MSG_INVALID,*/        0,
    /*MSG_ENABLE,*/         sizeof(DimeMessageEnable),
    /*MSG_FOUCS_IN,*/       sizeof(DimeMessageFocus),
    /*MSG_FOUCS_OUT,*/      sizeof(DimeMessageFocus),
    /*MSG_ADD_IC,*/         sizeof(DimeMessageIc),
    /*MSG_DEL_IC,*/         sizeof(DimeMessageIc),
    /*MSG_CURSOR,*/         sizeof(DimeMessageCursor),
    /*MSG_INPUT,*/          sizeof(DimeMessageInput),
    /*MSG_INPUT_FEEDBACK,*/ sizeof(DimeMessageInputFeedback),

    /*MSG_COMMIT,*/         sizeof(DimeMessageCommit),
    /*MSG_PREEDIT,*/        sizeof(DimeMessagePreedit),
    /*MSG_PREEDIT_CLEAR,*/  sizeof(DimeMessagePreeditClear),
    /*MSG_FORWARD,*/        sizeof(DimeMessageForward),

    /*MSG_ACQUIRE_TOKEN,*/  sizeof(DimeMessageToken),
    /*MSG_RELEASE_TOKEN,*/  sizeof(DimeMessageToken),
    /*MSG_CONNECT,*/        sizeof(DimeMessageConnect),
};

/* in theory, we can only have one connection per process. */ 
static DimeConnection *_conn = NULL;

static int _receive_message(mqd_t mq, char* buf, size_t sz, DimeMessage* msg)
{
    dime_debug("receive(%d)", mq);
    ssize_t nread = mq_receive(mq, buf, sz, NULL);
    if (nread > 0) {
        memcpy(msg, buf, g_msgsz[((DimeMessage*)buf)->type]);
        return 0;

    } else {
        dime_warn("mq_receive failed: %s", strerror(errno));
        return -1;
    }
}

static int _receive_message_sync(mqd_t mq, char* buf, size_t sz, DimeMessage* msg)
{
    dime_debug("receive_sync(%d)", mq);
    for (;;) {
        ssize_t nread = mq_receive(mq, buf, sz, NULL);
        if (nread > 0) {
            memcpy(msg, buf, g_msgsz[((DimeMessage*)buf)->type]);
            return 0;

        } else {
            if (errno != EAGAIN) {
                dime_warn("mq_receive failed: %s", strerror(errno));
                return -1;
            }
        }
    }
}

static int _send_message(mqd_t mq, DimeMessage* msg)
{
    dime_debug("send(%d, %s)", mq, g_msgname[msg->type]);
    return mq_send(mq, (char*)msg, g_msgsz[msg->type], 0);
}

static gboolean client_dispatch_callback(GIOChannel *ch, GIOCondition condition, gpointer data)
{
    if (condition != G_IO_IN)
        return FALSE;


    DimeMessage msg;
    if (_receive_message(_conn->mq_msg, _conn->msgbuf, _conn->msgsize, &msg) >= 0) {
        dime_debug("get %s", g_msgname[msg.type]);

        switch(msg.type) {
            case MSG_CONNECT:
                if (_conn->state == CONN_HANDSHAKE) {
                    g_assert(_conn->id == msg.connect.id);
                    _conn->state = CONN_ESTABLISHED;
                }
                break;

            case MSG_ACQUIRE_TOKEN: {
                DimeClient* c = (DimeClient*)msg.token.outband;
                g_assert(c->magic == CLIENT_MAGIC);
                /*c->conn = _conn;*/
                c->token = msg.token.token;
                dime_info("acquired token %d", c->token);
                break;
            }
            default: break;
        }
    }

    return TRUE;
}

static int dime_mq_build_connect(DimeConnection* c)
{
    DimeMessageConnect msg_conn;
    if (c->state == CONN_ESTABLISHED) return 0;

    if (c->state == CONN_INITIALIZED) {
        msg_conn.type = MSG_CONNECT;
        msg_conn.id  = c->id;
        _send_message(c->mq_srv, (DimeMessage*)&msg_conn);

        c->state = CONN_HANDSHAKE;
    }

    int ret;
    // should receive a pong reply
    if ((ret = _receive_message(c->mq_msg, c->msgbuf, c->msgsize, (DimeMessage*)&msg_conn)) < 0) {
        // means not connected, server may not start.
        // TODO: pull up server right now and wait for async reply
        return 0;
    }

    g_assert (msg_conn.id == c->id);
    c->state = CONN_ESTABLISHED;
    return 0;
}

//TODO: should allow dangling clients and reconnect when server gets back online.
int dime_mq_connect()
{
    if (_conn) {
        dime_warn("connection already exists!");
        return 0;
    }

    DimeConnection* c = (DimeConnection*)calloc(1, sizeof(DimeConnection));
    c->id = getpid();
    c->state = CONN_INITIALIZED;

    char *disp = getenv("DISPLAY");
    snprintf(c->mq_name, NAME_MAX, DIME_SERVER_MQ_NAME_TMPL, disp);
    dime_info("connect to %s", c->mq_name);

    struct mq_attr attr = {
        .mq_curmsgs = 0,
        .mq_flags = 0,
        .mq_msgsize = MSG_BUF_SIZE,
        .mq_maxmsg = MSG_MAX_NR,
    };
    c->mq_srv = mq_open(c->mq_name, O_CREAT|O_WRONLY|O_NONBLOCK, 0664, &attr);
    if (c->mq_srv < 0) {
        dime_warn("connect failed: %s", strerror(errno));
        goto _error;
    }

    mq_getattr(c->mq_srv, &attr);
    g_assert(attr.mq_msgsize == MSG_BUF_SIZE);
    dime_debug("attr.mq_maxmsg = %d", attr.mq_maxmsg);
    g_assert(attr.mq_maxmsg == MSG_MAX_NR);
    c->msgsize = attr.mq_msgsize;
    c->msgbuf = (char*)malloc(c->msgsize);

    attr.mq_flags = 0;
    attr.mq_curmsgs = 0;

    snprintf(c->mq_msg_name, NAME_MAX, DIME_CONNECTION_MQ_NAME_TMPL, disp, c->id);
    dime_debug("build connection [%s]", c->mq_msg_name);
    c->mq_msg = mq_open(c->mq_msg_name, O_CREAT|O_RDONLY|O_NONBLOCK, 0664, &attr);
    if (c->mq_msg < 0) {
        dime_warn("mq_open failed: %s", strerror(errno));
        goto _error;
    }

    c->ch = g_io_channel_unix_new(c->mq_msg);
    g_io_channel_set_encoding(c->ch, NULL, NULL);
    g_io_channel_set_buffered(c->ch, FALSE);
    g_io_add_watch(c->ch, G_IO_IN, client_dispatch_callback, c);

    _conn = c;
    dime_mq_build_connect(c);
    dime_debug("new connection");
    return 0;

_error:
    free(c->msgbuf);
    if (c->ch) g_io_channel_unref(c->ch);
    free(c);
    return -1;
}

int dime_mq_disconnect()
{
    if (!_conn) {
        return 0;
    }

    dime_debug("disconnection %s", _conn->mq_name);
    free(_conn->msgbuf);
    free(_conn);
    _conn = NULL;
    return 0;
}

DimeClient* dime_mq_acquire_token()
{
    dime_mq_connect();

    DimeClient* c = (DimeClient*)calloc(1, sizeof(DimeClient));
    c->magic = CLIENT_MAGIC;
    c->conn = _conn;

    DimeMessageToken msg_token;
    msg_token.type = MSG_ACQUIRE_TOKEN;
    msg_token.id = _conn->id;
    msg_token.outband = (uintptr_t)c;
    _send_message(_conn->mq_srv, (DimeMessage*)&msg_token);

    dime_debug("");
    return c;
}

int dime_mq_release_token(DimeClient* c)
{
    if (c->conn->state != CONN_ESTABLISHED || c->token == 0)
        return -1;

    DimeMessageToken msg_token;
    msg_token.type = MSG_RELEASE_TOKEN;
    msg_token.token = c->token;
    msg_token.id = c->conn->id;
    _send_message(c->conn->mq_srv, (DimeMessage*)&msg_token);

    dime_info("release token %d", c->token);
    free(c);

    return 0;
}

int dime_mq_client_send(DimeClient* c, int8_t flag, int8_t type, ...)
{
    if (c->conn->state != CONN_ESTABLISHED || c->token == 0)
        return -1;

    va_list ap;

    DimeMessage msg = {.type = type };

    va_start(ap, type);
    switch(type) {
        case MSG_ENABLE:
        case MSG_FOUCS_IN:
        case MSG_FOUCS_OUT:
        case MSG_ADD_IC:
        case MSG_DEL_IC:
        case MSG_CURSOR:
            break;

        case MSG_INPUT:
            msg.input.flags = flag;
            msg.input.token = c->token;
            msg.input.key = va_arg(ap, int32_t);
            msg.input.time = va_arg(ap, uint32_t);
        default: break;
    }

    _send_message(c->conn->mq_srv, &msg);
    if (flag & DIME_MSG_FLAG_SYNC) {
        if (_receive_message_sync(c->conn->mq_msg, c->conn->msgbuf, c->conn->msgsize, &msg) < 0) {
            goto _error;
        }
    }
    return 0;

_error:
    return -1;
}

/*----------------------------------------------------------------------*/


static int handle_connect(DimeServer* s, DimeMessageConnect* msg_conn)
{
    g_return_val_if_fail(msg_conn && msg_conn->type == MSG_CONNECT, -1);

    char conn_name[NAME_MAX];
    char *disp = getenv("DISPLAY");
    snprintf(conn_name, NAME_MAX, DIME_CONNECTION_MQ_NAME_TMPL, disp, msg_conn->id);
    struct mq_attr attr = {
        .mq_curmsgs = 0,
        .mq_flags = 0,
        .mq_msgsize = MSG_BUF_SIZE,
        .mq_maxmsg = MSG_MAX_NR,
    };
    mqd_t mq = mq_open(conn_name, O_CREAT|O_WRONLY|O_NONBLOCK, 0664, &attr);
    if (mq < 0) {
        dime_warn("mq_open(%s) failed: %s", conn_name, strerror(errno));
        return -1;
    }

    g_hash_table_insert(s->connections, GINT_TO_POINTER(msg_conn->id), GINT_TO_POINTER(mq));

    DimeMessageConnect resp;
    resp.type = MSG_CONNECT;
    resp.id  = msg_conn->id;
    _send_message(mq, (DimeMessage*)&resp);
    return 0;
}

static int handle_token(DimeServer* s, DimeMessageToken* msg_token)
{
    if (msg_token->type == MSG_ACQUIRE_TOKEN) {
        DimeMessageToken resp = *msg_token;
        resp.token = s->last++; //FIXME: terrible, what if overflow and circle back

        dime_debug("acquire token: conn %d", msg_token->id);
        mqd_t mq = (mqd_t)(long)g_hash_table_lookup(s->connections, GINT_TO_POINTER(msg_token->id));
        g_assert(mq != 0);

        _send_message(mq, (DimeMessage*)&resp);

        g_hash_table_insert(s->token_map, GINT_TO_POINTER(resp.token), GINT_TO_POINTER(resp.id));

    } else {
        if (g_hash_table_remove(s->token_map, GINT_TO_POINTER(msg_token->token))) {
            dime_debug("release token %d for conn %d", msg_token->token, msg_token->id);
            //FIXME: send response ?
        }
    }
    return 0;
}

static int handle_input(DimeServer* s, DimeMessageInput* msg_input)
{
    dime_debug("input %c", msg_input->key);

    DimeMessageInputFeedback resp;
    resp.type = MSG_INPUT_FEEDBACK;
    resp.token = msg_input->token;
    resp.result = 1;

    int id = (long)g_hash_table_lookup(s->token_map, GINT_TO_POINTER(msg_input->token));
    mqd_t mq = (mqd_t)(long)g_hash_table_lookup(s->connections, GINT_TO_POINTER(id));
    g_assert(mq != 0);
    _send_message(mq, (DimeMessage*)&resp);

    return 0;
}

static gboolean dispatch(DimeServer* s, DimeMessage* msg)
{
    switch (msg->type) {
        case MSG_CONNECT:       return !handle_connect(s, &msg->connect);
        case MSG_ACQUIRE_TOKEN:
        case MSG_RELEASE_TOKEN: return !handle_token(s, &msg->token);
        case MSG_INPUT:         return !handle_input(s, &msg->input);
        default: break;
    }
    return FALSE;
}

static gboolean server_callback(GIOChannel *ch, GIOCondition condition, gpointer data)
{
    if (condition != G_IO_IN)
        return FALSE;

    DimeServer* srv = (DimeServer*)data;

    ssize_t nread = mq_receive(srv->mq, srv->msgbuf, srv->msgsize, NULL);
    if (nread > 0) {
        DimeMessage* m = (DimeMessage*)srv->msgbuf;
        dispatch(srv, m);
    } else {
        dime_warn("%s", strerror(errno));
    }

    return TRUE;
}

DimeServer* dime_mq_server_new()
{
    DimeServer* s = (DimeServer*)calloc(1, sizeof(DimeServer));
    //NOTE: I will use a random start number later
    s->initial = s->last = 100;
    s->connections = g_hash_table_new(g_direct_hash, g_direct_equal);
    s->token_map = g_hash_table_new(g_direct_hash, g_direct_equal);

    char *disp = getenv("DISPLAY");
    snprintf(s->mq_name, NAME_MAX, DIME_SERVER_MQ_NAME_TMPL, disp);
    dime_debug("start server [%s]", s->mq_name);

    struct mq_attr attr = {
        .mq_curmsgs = 0,
        .mq_flags = 0,
        .mq_msgsize = MSG_BUF_SIZE,
        .mq_maxmsg = MSG_MAX_NR,
    };
    s->mq = mq_open(s->mq_name, O_CREAT|O_RDONLY|O_NONBLOCK, 0664, &attr);
    if (s->mq < 0) {
        dime_warn("mq_open failed: %s", strerror(errno));
        return NULL;
    }

    mq_getattr(s->mq, &attr);
    s->msgsize = attr.mq_msgsize;
    s->msgbuf = (char*)malloc(s->msgsize);

    s->ch = g_io_channel_unix_new(s->mq);
    g_io_channel_set_encoding(s->ch, NULL, NULL);
    g_io_channel_set_buffered(s->ch, FALSE);
    g_io_add_watch(s->ch, G_IO_IN, server_callback, s);

    return s;
}

void dime_mq_server_close(DimeServer* s)
{
    //TODO: disconnect all clients or not ?
    mq_close(s->mq);
    mq_unlink(s->mq_name);
    g_io_channel_unref(s->ch);
    free(s->msgbuf);
    free(s);
}

