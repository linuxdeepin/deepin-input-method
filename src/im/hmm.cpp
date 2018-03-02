#include "hmm.h"
#include <iostream>
#include <cmath>
#include <unordered_map>
#include <string>
#include <vector>

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

#define LE(p) ((p))

// HMM: initial probabilities, transfer matrix, emission matrix,
// output probabilities
vector<int> viterbi(const vector<string>& obs, HMM& hmm)
{
    hmm.states.clear();
    for (const auto& py: obs) {
        for (const auto& p: hmm.emission) {
            if (p.second.find(py) != p.second.end()) {
                hmm.states.push_back(p.first);
            }
        }
    }
    cout << "states: " << hmm.states << endl;

    int n_states = hmm.states.size();
    int n_seq = obs.size();

    auto& st = hmm.states;

    if (n_states == 0 || n_seq == 0) return {};

    auto res = vector<int>(n_seq);
    auto v = vector<vector<double>>(n_seq, vector<double>(n_states));
    auto parents = vector<vector<int>>(n_seq, vector<int>(n_states));

    for (auto i = 0; i < n_states; i++) {
        if (hmm.emission[st[i]].count(obs[0])) {
            v[0][i] =  LE(hmm.pi[st[i]]) * LE(hmm.emission[st[i]][obs[0]]);
        }
    }

    for (auto i = 1; i < n_seq; i++) {
        for (auto j = 0; j < n_states; j++) {

            double max = 0.0;
            int parent = 0;
            for (auto l = 0; l < n_states; l++) {
                if (hmm.a[st[l]][st[j]] == 0.0 || hmm.emission[st[j]][obs[i]] == 0.0) {
                    continue;
                }

                auto m = v[i-1][l] * LE(hmm.a[st[l]][st[j]]) * LE(hmm.emission[st[j]][obs[i]]);
                if (m > max) {
                    max = m;
                    parent = l;
                }
            }

            v[i][j] = max;
            parents[i][j] = parent;
        }
    }

    double max = 0.0;
    int k = 0;
    for (auto l = 0; l < n_states; l++) {
        auto m = v[n_seq-1][l];
        if (m > max) {
            max = m;
            k = l;
        }
    }

    cout << "k = " << k << ", p: " << max << endl;
    //cout << v << endl;

    res[n_seq-1] = k;
    for (auto t = n_seq-2; t >= 0; t--) {
        res[t] = parents[t+1][res[t+1]];
    }


    for (auto i: res) {
        cout << st[i] << " ";
    }
    cout << endl;
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
