#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
using namespace std;

struct Clause { vector<int> literals; };
int n, l;
vector<Clause> clauses;

bool parseCNF(const string& filename) {
    ifstream file(filename);
    if (!file) return false;
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
    return true;
}

bool checkAssignment(const vector<int>& assign) {
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
    return true;
}

int main(int argc, char* argv[]) {
    if (argc != 3) {
        cerr << "Usage: " << argv[0] << " <cnf-file> <output-file>\n";
        return 1;
    }
    if (!parseCNF(argv[1])) { cerr << "Error parsing CNF.\n"; return 1; }
    ifstream out(argv[2]);
    if (!out) { cerr << "Cannot open output file.\n"; return 1; }

    auto trim = [](string& s) {
        s.erase(0, s.find_first_not_of(" \t\n\r"));
        s.erase(s.find_last_not_of(" \t\n\r") + 1);
    };
    string firstLine;
    getline(out, firstLine);
    trim(firstLine);

    if (firstLine == "UNSAT") {
        cout << "UNSAT (cannot verify unsatisfiability)\n";
        return 0;
    } else if (firstLine == "SAT") {
        vector<int> assign(n + 1, -1);
        string line;
        bool hasAssign = false;
        while (getline(out, line)) {
            trim(line);
            if (line.empty()) continue;
            stringstream ss(line);
            string token;
            while (ss >> token) {
                size_t eq = token.find('=');
                if (eq == string::npos) continue;
                string varStr = token.substr(0, eq);
                string valStr = token.substr(eq + 1);
                if (varStr.empty() || valStr.empty()) continue;
                if (varStr[0] == 'x' || varStr[0] == 'X') {
                    int var = stoi(varStr.substr(1));
                    int val = stoi(valStr);
                    if (var >= 1 && var <= n) {
                        assign[var] = val;
                        hasAssign = true;
                    }
                }
            }
        }
        if (!hasAssign) {
            cout << "INVALID: No assignment found.\n";
            return 0;
        }
        for (int i = 1; i <= n; ++i) {
            if (assign[i] == -1) {
                cout << "INVALID: Variable x" << i << " not assigned.\n";
                return 0;
            }
        }
        if (checkAssignment(assign))
            cout << "VALID\n";
        else
            cout << "INVALID: Assignment does not satisfy all clauses.\n";
        return 0;
    } else {
        cout << "INVALID: Unknown first line.\n";
        return 1;
    }
}