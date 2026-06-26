#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
using namespace std;

struct Clause { vector<int> literals; };
int n, l;
vector<Clause> clauses;
vector<int> solution; // 1‑based

bool checkPartial(const vector<int>& assign) {
    for (const auto& cls : clauses) {
        bool allAssigned = true;
        bool sat = false;
        for (int lit : cls.literals) {
            int var = abs(lit);
            if (assign[var] == -1) { allAssigned = false; break; }
            if ((lit > 0 && assign[var] == 1) || (lit < 0 && assign[var] == 0)) {
                sat = true;
                break;
            }
        }
        if (allAssigned && !sat) return false;
    }
    return true;
}

bool backtrack(vector<int>& assign, int idx) {
    if (idx > n) {
        for (const auto& cls : clauses) {
            bool sat = false;
            for (int lit : cls.literals) {
                int var = abs(lit);
                if ((lit > 0 && assign[var] == 1) || (lit < 0 && assign[var] == 0)) {
                    sat = true;
                    break;
                }
            }
            if (!sat) return false;
        }
        solution = assign;
        return true;
    }
    for (int val : {0, 1}) {
        assign[idx] = val;
        if (checkPartial(assign)) {
            if (backtrack(assign, idx + 1)) return true;
        }
        assign[idx] = -1;
    }
    return false;
}

int main(int argc, char* argv[]) {
    if (argc < 2) { cerr << "Usage: " << argv[0] << " <filename.cnf>\n"; return 1; }
    ifstream file(argv[1]);
    if (!file) { cerr << "Cannot open file.\n"; return 1; }
    string line;
    while (getline(file, line)) {
        if (line.empty() || line[0] == 'c') continue;
        if (line[0] == 'p') {
            string p, cnf;
            stringstream ss(line);
            ss >> p >> cnf >> n >> l;
            continue;
        }
        stringstream ss(line);
        int lit;
        Clause cls;
        while (ss >> lit && lit != 0) cls.literals.push_back(lit);
        if (!cls.literals.empty()) clauses.push_back(cls);
    }
    vector<int> assign(n + 1, -1);
    bool sat = backtrack(assign, 1);
    if (sat) {
        cout << "SAT\n";
        for (int i = 1; i <= n; ++i)
            cout << "x" << i << "=" << solution[i] << (i == n ? "\n" : " ");
    } else {
        cout << "UNSAT\n";
    }
    return 0;
}