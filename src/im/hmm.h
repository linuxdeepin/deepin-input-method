#ifndef _DIME_HMM_H
#define _DIME_HMM_H 

#include <unordered_map>
#include <string>
#include <vector>


namespace dime
{
    using namespace std;
    using Table = unordered_map<string, double>;
    using Matrix = unordered_map<string, Table>;

    struct HMM {
        vector<string> states;
        Table pi; // initial states' prob
        Matrix a; // transfer matrix
        Matrix emission; // emission matrix
    };

    vector<int> viterbi(const vector<string>& obs, HMM& hmm);
}

#endif /* ifndef _DIME_HMM_H */
