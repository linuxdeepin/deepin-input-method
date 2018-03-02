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

#include <iostream>
#include <unordered_map>
#include <string>
#include <vector>

#include <QtSql>

#include "msg_queue.h"
#include "log.h"

#include "py.h"
#include "hmm.h"
using namespace std;
using namespace dime;

#ifdef G_LOG_DOMAIN
#undef G_LOG_DOMAIN
#endif
#define G_LOG_DOMAIN "dime"

char py[] = "zhongguo";
char *p1 = py;
char *p2 = py;
static DimeClient* c1 = NULL, *c2 = NULL;
static DimeServer* s = NULL;

inline static int id(DimeClient* c)
{
    return g_int_hash(c) % 17;
}

gboolean on_idle(gpointer data)
{
    DimeClient* c = (DimeClient*)data;
    if (!dime_mq_client_is_valid(c)) {
        g_timeout_add(10, on_idle, data);
        return FALSE;
    }

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
    dime_debug("C%d: %s", id(c), msg->preedit.text);
    return 0;
}

static gboolean on_commit(DimeClient* c, DimeMessage* msg)
{
    dime_debug("C%d: %s", id(c), msg->commit.text);
    return 0;
}

// server
static int on_input(DimeServer* s, DimeMessage* msg)
{
    //TODO: IM engine 
    int key = msg->input.key;
    if (key == '\n') {
        dime_mq_server_send(s, msg->input.token, 0, MSG_COMMIT, EIM.StringGet, strlen(EIM.StringGet) + 1);
    } else {
        PY_DoInput(key);
        dime_mq_server_send(s, msg->input.token, 0, MSG_PREEDIT, EIM.CodeInput, strlen(EIM.CodeInput) + 1);
    }

    return 0;
}

static HMM load_hmm(const char* filepath)
{
    HMM hmm;

    QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE");
    db.setDatabaseName(filepath);
    if (!db.open()) {
        qDebug() << db.lastError();
        return hmm;
    }

    QSqlQuery qry("select character,probability from starting");
    while (qry.next()) {
        auto ch = qry.value(0).toString().toStdString();
        auto prob = qry.value(1).toDouble();

        hmm.pi[ch] = (prob);
    }

    qry = QSqlQuery("select previous,behind,probability from transition");
    //qry = QSqlQuery("select transition.previous, transition.behind, emission.pinyin, transition.probability + emission.probability as prob from transition inner join emission on transition.behind = emission.character");
    while (qry.next()) {
        auto s1 = qry.value(0).toString().toStdString();
        auto s2 = qry.value(1).toString().toStdString();
        auto prob = qry.value(2).toDouble();

        hmm.a[s1][s2] = (prob);
    }

    //qry = QSqlQuery("select character,pinyin,probability from emission");
    qry = QSqlQuery("select emission.character, emission.pinyin, emission.probability + starting.probability from emission join starting on emission.character = starting.character");
    while (qry.next()) {
        auto s = qry.value(0).toString().toStdString();
        auto py = qry.value(1).toString().toStdString();
        auto prob = qry.value(2).toDouble();

        hmm.emission[s][py] = (prob);
    }

    return hmm;
}

template<class T>
ostream& operator<<(ostream& os, const vector<T>& v)
{
    os << "[";
    for (auto i: v) {
        os << i << " ";
    }
    return os << "]";
}

static vector<string> simple_pinyin_split(const string& py)
{
    vector<string> res;
    string::size_type s = 0, p = 0;
    while ((p = py.find_first_of('\'', s)) != string::npos) {
        res.push_back(py.substr(s, p-s));
        s = p+1;
    }

    if (s < py.size()) {
        res.push_back(py.substr(s));
    }
    cout << res << endl;
    return res;
}


int main(int argc, char *argv[])
{
    setlocale(LC_ALL, "");

    gchar *cmd = g_path_get_basename(argv[0]);

    GMainLoop *l = g_main_loop_new(NULL, TRUE);

    if (!g_str_equal(cmd, "dinput")) {
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

    } else {
        QCoreApplication app(argc, argv);
        auto now = QTime::currentTime();

        auto hmm = load_hmm("/tmp/hmm.sqlite");
        if (hmm.pi.size() == 0) return -1;
        dime::viterbi(simple_pinyin_split("tian'qi"), hmm);
        dime::viterbi(simple_pinyin_split("duan'yu"), hmm);
        dime::viterbi(simple_pinyin_split("gong'ju"), hmm);
        dime::viterbi(simple_pinyin_split("tian'long'ba'bu"), hmm);
        dime::viterbi(simple_pinyin_split("qiao'feng'he'duan'yu'shi'hao'xiong'di"), hmm);
        dime::viterbi(simple_pinyin_split("hao'xiong'di"), hmm);
        dime::viterbi(simple_pinyin_split("yi'jie'shu'sheng"), hmm);
        dime::viterbi(simple_pinyin_split("yi'dong'bu'ru'yi'jing"), hmm);
        auto d = now.msecsTo(QTime::currentTime());
        qDebug() << "cost: " << d;
        return 0;


        s = dime_mq_server_new();
        PY_Init(0);

        DimeServerCallbacks cbs = {
            .on_input = on_input
        };
        dime_mq_server_set_callbacks(s, cbs);

    }

    g_main_loop_run(l);

    if (g_str_equal(cmd, "dinput")) {
        dime_mq_server_close(s);

    } else {
        dime_mq_release_token(c1);
        dime_mq_release_token(c2);
    }
    return 0;
}
