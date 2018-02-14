#include <glib.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <locale.h>
#include <mqueue.h>

#include "msg_queue.h"
#include "log.h"

#include "py.h"

#ifdef G_LOG_DOMAIN
#undef G_LOG_DOMAIN
#endif
#define G_LOG_DOMAIN "dime"

char py[] = "zhongguo";
char *p1 = py;
char *p2 = py;
static DimeClient* c1 = NULL, *c2 = NULL;

gboolean on_idle(gpointer data)
{
    DimeClient* c = (DimeClient*)data;
    if (!dime_mq_client_is_valid(c)) 
        return TRUE;

    dime_mq_client_enable(c);

    if (c == c1) {
        dime_mq_client_key(c, *p1++, 0);
        if (*p1 == 0) {
            dime_mq_client_key_async(c, '\n', 0); // assume commit
            return FALSE;
        }
    } else {
        dime_mq_client_key(c, *p2++, 0);
        if (*p2 == 0) {
            dime_mq_client_key_async(c, '\n', 0); // assume commit
            return FALSE;
        }
    }

    /*dime_mq_release_token(c);*/
    return TRUE;
}


static gboolean on_preedit(DimeClient* c, DimeMessage* msg)
{
    dime_debug("%p: %s", c, msg->preedit.text);
    return 0;
}

static gboolean on_commit(DimeClient* c, DimeMessage* msg)
{
    dime_debug("%p: %s", c, msg->commit.text);
    return 0;
}

// server
static gboolean on_input(DimeServer* s, DimeMessageInput* msg)
{
    //TODO: IM engine 
    int key = msg->key;
    if (key == '\n') {
        dime_mq_server_send(s, msg->token, 0, MSG_COMMIT, EIM.StringGet, strlen(EIM.StringGet) + 1);
    } else {
        PY_DoInput(key);
        dime_mq_server_send(s, msg->token, 0, MSG_PREEDIT, EIM.CodeInput, strlen(EIM.CodeInput) + 1);
    }

    return FALSE;
}

int main(int argc, char *argv[])
{
    setlocale(LC_ALL, "");

    GMainLoop *l = g_main_loop_new(NULL, TRUE);

    pid_t pid = fork();
    if (pid == 0) {
        DimeMessageCallbacks cbs = {
            .on_commit = on_commit,
            .on_preedit = on_preedit
        };

        {
            c1 = dime_mq_acquire_token();
            dime_mq_client_set_receive_callbacks(c1, cbs);
            g_timeout_add(0, on_idle, c1);
        }

        {
            c2 = dime_mq_acquire_token();
            dime_mq_client_set_receive_callbacks(c2, cbs);
            g_timeout_add(0, on_idle, c2);
        }

    } else if (pid > 0) {
        DimeServer* s = dime_mq_server_new();
        PY_Init(0);

        DimeServerCallbacks cbs = {
            .on_input = on_input
        };
        dime_mq_server_set_callbacks(s, cbs);

        //TODO: need to handle this crash and restore situation for client
        /*dime_mq_server_close(s);*/
        /*s = dime_mq_server_new();*/

    } else {
        return -1;
    }

    g_main_loop_run(l);
    return 0;
}
