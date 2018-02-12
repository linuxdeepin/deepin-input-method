#include <glib.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <mqueue.h>

#include "msg_queue.h"
#include "log.h"

char py[] = "zhongguo";
char *p = py;
gboolean on_idle(gpointer data)
{
    DimeClient* c = (DimeClient*)data;
    if (!dime_mq_client_valid(c)) 
        return TRUE;

    dime_mq_client_enable(c);

    if (dime_mq_client_key(c, *p++, 0) < 0) {
        return FALSE;
    }

    if (*p == 0) {
        dime_mq_client_key_async(c, '\n', 0); // assume commit
        return FALSE;
    }
    /*dime_mq_release_token(c);*/
    return TRUE;
}


static gboolean on_preedit(DimeClient* c, DimeMessage* msg)
{
    dime_debug("%s", msg->preedit.text);
    return 0;
}

static gboolean on_commit(DimeClient* c, DimeMessage* msg)
{
    dime_debug("%s", msg->commit.text);
    return 0;
}

int main(int argc, char *argv[])
{
    GMainLoop *l = g_main_loop_new(NULL, TRUE);

    pid_t pid = fork();
    if (pid == 0) {
        DimeClient* c = dime_mq_acquire_token();
        DimeMessageCallbacks cbs = {
            .on_commit = on_commit,
            .on_preedit = on_preedit
        };
        dime_mq_client_set_receive_callbacks(c, cbs);
        usleep(100);

        g_timeout_add(1, on_idle, c);

    } else if (pid > 0) {
        DimeServer* s = dime_mq_server_new();

        //TODO: need to handle this crash and restore situation for client
        /*dime_mq_server_close(s);*/
        /*s = dime_mq_server_new();*/

    } else {
        return -1;
    }

    g_main_loop_run(l);
    return 0;
}
