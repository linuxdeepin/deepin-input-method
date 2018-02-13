#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <unistd.h>
#include <string.h>
#include <stdarg.h>
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

#define MSG_BUF_SIZE 4096 /* this is big enough to hold any message */
#define MSG_MAX_NR 6

#define CLIENT_MAGIC 0xfadeceed

typedef struct _DimeClientState {
    uint32_t token; /* 0 if no focused client */
    int enabled: 1;
} DimeClientState;

struct _DimeServer {
    uint32_t initial; /* increment */
    uint32_t last;
    mqd_t mq;
    char mq_name[NAME_MAX];
    char *msgbuf;
    long msgsize;

    GIOChannel *ch;
    GHashTable *connections;
    GHashTable *token_map; /* <token, id> */

    DimeServerCallbacks* callbacks;

    DimeClientState active; /* current focus client */
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

    GSList *clients; /* optimize DS later */
} DimeConnection;

//FIXME: clients <-> mq map
struct _DimeClient {
    uint32_t magic;
    uint32_t token; /* if token is 0, means token is not requested from server, 
                        the reasons vary, e.g server is down */
    int enabled: 1;
    int focused: 1;

    DimeMessageCallbacks* callbacks;

    DimeConnection* conn; /* shared connection between all clients from one context */
};

static const char* g_msgname[MSG_MAX] = {
    /*MSG_INVALID,*/        "<INVALID>",
    /*MSG_ENABLE,*/         "ENABLE",
    /*MSG_FOCUS_IN,*/       "FOCUS_IN",
    /*MSG_FOCUS_OUT,*/      "FOCUS_OUT",
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
    /*MSG_FOCUS_IN,*/       sizeof(DimeMessageFocus),
    /*MSG_FOCUS_OUT,*/      sizeof(DimeMessageFocus),
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
    ssize_t nread = mq_receive(mq, buf, sz, NULL);
    if (nread > 0) {
        memcpy(msg, buf, g_msgsz[((DimeMessage*)buf)->type]);
        if (msg->type == MSG_COMMIT) {
            ((DimeMessageCommit*)msg)->text = (buf + g_msgsz[MSG_COMMIT]);

        } else if (msg->type == MSG_PREEDIT) {
            ((DimeMessagePreedit*)msg)->text = (buf + g_msgsz[MSG_PREEDIT]);

        }
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
            if (msg->type == MSG_COMMIT) {
                ((DimeMessageCommit*)msg)->text = (buf + g_msgsz[MSG_COMMIT]);

            } else if (msg->type == MSG_PREEDIT) {
                ((DimeMessagePreedit*)msg)->text = (buf + g_msgsz[MSG_COMMIT]);

            }
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
    if (mq_send(mq, (char*)msg, g_msgsz[msg->type], 0) < 0) {
        dime_warn("mq_send failed: %s", strerror(errno));
        return -1;
    }

    return 0;
}

static int _send_message_sync(mqd_t mq, DimeMessage* msg, ...)
{
    // assume outband data has been packed along with msg pointer
    int outband_sz = 0;
    if (msg->type == MSG_PREEDIT || msg->type == MSG_COMMIT) {
        va_list(ap);
        va_start(ap, msg);
        outband_sz = va_arg(ap, int);
        va_end(ap);
    }
    for (;;) {
        if (mq_send(mq, (char*)msg, g_msgsz[msg->type] + outband_sz, 0) < 0) {
            if (errno == EAGAIN) {
                usleep(100);
                continue;
            }

            dime_warn("mq_send failed: %s", strerror(errno));
            return -1;
        } 

        break;
    }

    return 0;
}

static DimeClient* _find_client(DimeConnection* conn, uint32_t token)
{
    for (GSList *l = _conn->clients; l; l = l->next) {
        if (((DimeClient*)l->data)->token == token) {
            return (DimeClient*)l->data;
        }
    }

    dime_warn("can not find client %u", token);
    return NULL;
}

#define _handle_client_message(token, msg, cb) do {     \
    DimeClient* c = _find_client(_conn, token);         \
    if (c && c->callbacks && c->callbacks->cb)          \
        c->callbacks->cb(c, msg);       \
} while (0)

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
                c->token = msg.token.token;
                dime_info("acquired token %d", c->token);
                break;
            }

            case MSG_COMMIT: 
                _handle_client_message(msg.commit.token, &msg, on_commit);
                break;
            case MSG_ENABLE: 
                _handle_client_message(msg.enable.token, &msg, on_enable);
                break;
            case MSG_PREEDIT:
                _handle_client_message(msg.preedit.token, &msg, on_preedit);
                break;
            case MSG_PREEDIT_CLEAR:
                _handle_client_message(msg.preedit_clear.token, &msg, on_preedit_clear);
                break;
            case MSG_FORWARD: 
                _handle_client_message(msg.forward.token, &msg, on_forward);
                break;

            case MSG_INPUT_FEEDBACK:
                break;

            default: 
                g_assert_not_reached();
                break;
        }
    }

    return TRUE;
}

#undef _handle_client_message

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
    _conn->clients = g_slist_prepend(_conn->clients, c);

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

int dime_mq_client_is_valid(DimeClient* c)
{
    return c->token != 0 && c->conn && c->conn->state == CONN_ESTABLISHED;
}

int dime_mq_client_is_enabled(DimeClient* c)
{
    return c->enabled;
}

int dime_mq_client_is_focused(DimeClient* c)
{
    return c->focused;
}

int dime_mq_client_enable(DimeClient* c)
{
    if (c->enabled) return 0;
    return dime_mq_client_send(c, 0, MSG_ENABLE, 1);
}

int dime_mq_client_focus(DimeClient* c, gboolean val)
{
    if (val == c->focused) return 0;
    return dime_mq_client_send(c, 0, val ? MSG_FOCUS_IN: MSG_FOCUS_OUT);
}

int dime_mq_client_key(DimeClient* c, int key, uint32_t time)
{
    return dime_mq_client_send(c, DIME_MSG_FLAG_SYNC, MSG_INPUT, key, time);
}

int dime_mq_client_key_async(DimeClient* c, int key, uint32_t time)
{
    return dime_mq_client_send(c, 0, MSG_INPUT, key, time);
}

int dime_mq_client_send(DimeClient* c, int8_t flag, int8_t type, ...)
{
    if (c->conn->state != CONN_ESTABLISHED || c->token == 0) {
        dime_debug("no connection or invalid client, drop request");
        return -1;
    }

    dime_debug("client(%d) send(%d, %s)%s", c->token, type, g_msgname[type],
            (flag & DIME_MSG_FLAG_SYNC ? " SYNC":""));

    va_list ap;

    DimeMessage msg = {.type = type, .flags = flag };

    va_start(ap, type);
    switch(type) {
        case MSG_ENABLE:
            msg.enable.val = va_arg(ap, int);
            msg.enable.token = c->token;
            c->enabled = msg.enable.val;
            break;

        case MSG_FOCUS_IN:
            c->focused = FALSE;
            msg.focus.token = c->token;
            break;

        case MSG_FOCUS_OUT:
            msg.focus.token = c->token;
            c->focused = TRUE;
            break;

        case MSG_ADD_IC:
        case MSG_DEL_IC:
        case MSG_CURSOR:
            break;

        case MSG_INPUT:
            msg.input.token = c->token;
            msg.input.key = va_arg(ap, int32_t);
            msg.input.time = va_arg(ap, uint32_t);
            break;

        default: g_assert_not_reached(); break;
    }
    va_end(ap);

    g_assert (msg.type > MSG_INVALID && msg.type <= MSG_MAX);
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

int dime_mq_client_set_receive_callbacks(DimeClient* c, DimeMessageCallbacks cbs)
{
    if (!c->callbacks) {
        c->callbacks = (DimeMessageCallbacks*)calloc(1, sizeof(DimeMessageCallbacks));
    }
    memcpy(c->callbacks, &cbs, sizeof cbs);
    return 0;
}

/*----------------------------------------------------------------------*/


static int handle_connect(DimeServer* s, DimeMessage* msg)
{
    DimeMessageConnect* msg_conn = &msg->connect;

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

static int handle_token(DimeServer* s, DimeMessage* msg)
{
    DimeMessageToken* msg_token = &msg->token;

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

static int handle_input(DimeServer* s, DimeMessage* msg)
{
    DimeMessageInput* msg_input = &msg->input;

    dime_debug("input %c", msg_input->key);

    DimeMessageInputFeedback resp;
    resp.type = MSG_INPUT_FEEDBACK;
    resp.time = msg_input->time;//FIXME:??
    resp.token = msg_input->token;
    resp.result = 1;

    int id = (long)g_hash_table_lookup(s->token_map, GINT_TO_POINTER(msg_input->token));
    mqd_t mq = (mqd_t)(long)g_hash_table_lookup(s->connections, GINT_TO_POINTER(id));
    g_assert(mq != 0);
    _send_message(mq, (DimeMessage*)&resp);

    if (s->active.token != msg_input->token) {
        // we only send feedback but do not process it
        return 0;
    }

    if (s->callbacks && s->callbacks->on_input) {
        return s->callbacks->on_input(s, msg);
    }

    return 0;
}

static int handle_focus(DimeServer* s, DimeMessage* msg)
{
    DimeMessageFocus* msg_focus = &msg->focus;

    dime_debug("client(%d) ", msg_focus->token, g_msgname[msg_focus->type]);

    int id = (long)g_hash_table_lookup(s->token_map, GINT_TO_POINTER(msg_focus->token));
    g_return_val_if_fail(id > 0, 0);

    if (msg_focus->type == MSG_FOCUS_IN) {
        //ignore previous focused
        s->active.token = msg_focus->token;
        s->active.enabled = 0; //FIXME: by default, should be able to change by user
    } else {
        s->active.token = 0;
    }

    if (s->callbacks && s->callbacks->on_focus) {
        return s->callbacks->on_focus(s, msg);
    }
    return 0;
}

static int handle_enable(DimeServer* s, DimeMessage* msg)
{
    DimeMessageEnable* msg_enable = &msg->enable;

    dime_debug("client(%d) enable %d", msg_enable->token, msg_enable->val);

    int id = (long)g_hash_table_lookup(s->token_map, GINT_TO_POINTER(msg_enable->token));
    g_return_val_if_fail(id > 0, 0);

    if (s->active.token == 0) {
        s->active.token = msg_enable->token;
    }

    if (s->active.token == msg_enable->token) {
        s->active.enabled = msg_enable->val;
    }

    if (s->callbacks && s->callbacks->on_enable) {
        return s->callbacks->on_enable(s, msg);
    }
    return 0;
}

static int handle_unimplemented(DimeServer* s, DimeMessage* msg)
{
    dime_warn("handle for %s not implemented", g_msgname[msg->type]);
    return 0;
}

static int handle_invalid(DimeServer* s, DimeMessage* msg)
{
    dime_warn("invalid handle for %s", g_msgname[msg->type]);
    return 0;
}

static DimeServerCallback g_dispatch_table[MSG_MAX] = {
    /*MSG_INVALID,*/        handle_invalid,
    /*MSG_ENABLE,*/         handle_enable,
    /*MSG_FOCUS_IN,*/       handle_focus,
    /*MSG_FOCUS_OUT,*/      handle_focus,
    /*MSG_ADD_IC,*/         handle_unimplemented,
    /*MSG_DEL_IC,*/         handle_unimplemented,
    /*MSG_CURSOR,*/         handle_unimplemented,
    /*MSG_INPUT,*/          handle_input,
    /*MSG_INPUT_FEEDBACK,*/ handle_invalid,

    /*MSG_COMMIT,*/         handle_invalid,
    /*MSG_PREEDIT,*/        handle_invalid,
    /*MSG_PREEDIT_CLEAR,*/  handle_invalid,
    /*MSG_FORWARD,*/        handle_invalid,

    /*MSG_ACQUIRE_TOKEN,*/  handle_token,
    /*MSG_RELEASE_TOKEN,*/  handle_token,
    /*MSG_CONNECT,*/        handle_connect,
};

static gboolean dispatch(DimeServer* s, DimeMessage* msg)
{
    return g_dispatch_table[msg->type](s, msg) == 0;
}

static gboolean server_callback(GIOChannel *ch, GIOCondition condition, gpointer data)
{
    if (condition != G_IO_IN) {
        dime_warn("condition = %d", condition);
        return FALSE;
    }

    DimeServer* srv = (DimeServer*)data;

    ssize_t nread = mq_receive(srv->mq, srv->msgbuf, srv->msgsize, NULL);
    if (nread > 0) {
        DimeMessage* m = (DimeMessage*)srv->msgbuf;
        dime_debug("handle %s", g_msgname[m->type]);
        dispatch(srv, m);
    } else {
        dime_debug("mq(%d), errno: %d, %s", srv->mq, errno, strerror(errno));
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

int dime_mq_server_set_callbacks(DimeServer* s, DimeServerCallbacks cbs)
{
    if (!s->callbacks) {
        s->callbacks = (DimeServerCallbacks*)calloc(1, sizeof(DimeServerCallbacks));
    }
    memcpy(s->callbacks, &cbs, sizeof cbs);
    return 0;
}

int dime_mq_server_send(DimeServer* s, int token, int8_t flag, int8_t type, ...)
{
    va_list(ap);
    va_start(ap, type);

    int id = (long)g_hash_table_lookup(s->token_map, GINT_TO_POINTER(token));
    mqd_t mq_cli = (mqd_t)(long)g_hash_table_lookup(s->connections, GINT_TO_POINTER(id));
    g_assert(mq_cli != 0);

    switch (type) {
        case MSG_COMMIT: {
            DimeMessageCommit* commit = (DimeMessageCommit*)s->msgbuf;
            commit->type = MSG_COMMIT;
            commit->flags = 0;
            commit->token = token;
            char *data = va_arg(ap, char*);
            strcpy(s->msgbuf + g_msgsz[MSG_COMMIT], data);
            commit->text_len = va_arg(ap, int);
            _send_message_sync(mq_cli, (DimeMessage*)commit, commit->text_len);
            break;
        }

        case MSG_PREEDIT: {
            DimeMessagePreedit* preedit = (DimeMessagePreedit*)s->msgbuf;
            preedit->type = MSG_PREEDIT;
            preedit->flags = 0;
            preedit->token = token;
            char *data = va_arg(ap, char*);
            strcpy(s->msgbuf + g_msgsz[MSG_PREEDIT], data);
            preedit->text_len = va_arg(ap, int);
            _send_message_sync(mq_cli, (DimeMessage*)preedit, preedit->text_len);
            break;
        }

        default: break;
    }

    va_end(ap);
    return 0;
}

