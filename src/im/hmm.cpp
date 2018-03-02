#include "hmm.h"
#include <iostream>
#include <cmath>
#include <unordered_map>
#include <string>
#include <vector>
#include <cassert>

using namespace std;

namespace dime
{

template<class T>
ostream& operator<<(ostream& os, const vector<T>& v)
{
    os << "[";
    for (auto i: v) {
        os << i << " ";
    }
    return os << "]";
}

template<class K, class V>
ostream& operator<<(ostream& os, const unordered_map<K, V>& m)
{
    os << "{";
    for (auto p: m) {
        os << "{" << p.first << ", " << p.second << "}" << " ";
    }
    return os << "}";
}

static vector<string> _hmm_get_zi(HMM& hmm, const string& py)
{
    vector<string> res;
    for (const auto& p: hmm.emission) {
        if (p.second.find(py) != p.second.end()) {
            res.push_back(p.first);
        }
    }

    return res;
}

// HMM: initial probabilities, transfer matrix, emission matrix,
// output probabilities
vector<string> viterbi(const vector<string>& obs, HMM& hmm)
{
    auto st = _hmm_get_zi(hmm, obs[0]);
    int n_states = st.size();
    int n_seq = obs.size();

    if (n_states == 0 || n_seq == 0) return {};


    auto res = vector<string>(n_seq);
    auto v = unordered_map<int, unordered_map<int, double>>();
    auto parents = unordered_map<int, unordered_map<int, int>>();

    for (auto i = 0; i < n_states; i++) {
        if (hmm.emission[st[i]].count(obs[0])) {
            v[0][i] =  hmm.pi[st[i]] + hmm.emission[st[i]][obs[0]];
        } else {
            v[0][i] = -1000.0;
        }
    }

    cout << "0: " << st << endl;

    for (auto i = 1; i < n_seq; i++) {
        auto st_next = _hmm_get_zi(hmm, obs[i]); 
        auto n_states_next = st_next.size();
        for (auto j = 0; j < n_states_next; j++) {

            double max = -1000.0;
            int parent = 0;
            for (auto l = 0; l < n_states; l++) {
                assert(hmm.emission[st_next[j]].count(obs[i]));
                if (hmm.a.count(st[l]) == 0 || hmm.a[st[l]].count(st_next[j]) == 0) {
                    continue;
                }

                //assert(v[i-1][l] != 0.0 && v[i-1][l] > -1000.0);
                auto m = v[i-1][l] + hmm.a[st[l]][st_next[j]] + hmm.emission[st_next[j]][obs[i]];
                if (m > max) {
                    max = m;
                    parent = l;
                }
                //cout << m << endl;
            }

            v[i][j] = max;
            parents[i][j] = parent;
        }

        st = st_next;
        n_states = n_states_next;
    }

    double max = -1000.0;
    int k = 0;
    for (auto l = 0; l < n_states; l++) {
        auto m = v[n_seq-1][l];
        if (m > max) {
            max = m;
            k = l;
        }
    }

    cout << "k = " << k << ", p: " << max << endl;

    res[n_seq-1] = st[k];
    for (auto t = n_seq-2; t >= 0; t--) {
        k = parents[t+1][k];
        res[t] = _hmm_get_zi(hmm, obs[t])[k];
    }


    cout << res << endl;
    return res;
}

int test_viterbi()
{
    HMM hmm;

    hmm.states = {"H", "F"};

    hmm.pi = {
        {"H", 0.6},
        {"F", 0.4}
    };
    hmm.a = {
        {"H", {{"H", 0.7}, {"F", 0.3}}},
        {"F", {{"H", 0.4}, {"F", 0.6}}},
    };

    hmm.emission = {
        {"H", {{"normal", 0.5}, {"cold", 0.4}, {"dizzy", 0.1}}},
        {"F", {{"normal", 0.1}, {"cold", 0.3}, {"dizzy", 0.6}}},
    };

    vector<string> obs {"normal", "cold", "dizzy"};
    viterbi(obs, hmm); // H, H, F

    obs = {"normal", "normal", "cold", "cold", "dizzy", "cold", "dizzy", "dizzy", "dizzy", "normal"};
    viterbi(obs, hmm); // H H H H F F F F F H

    return 0;
}

}
