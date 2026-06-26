#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <thread>
#include <atomic>
#include <cmath>
#include <algorithm>

using namespace std;

struct Clause {
    vector<int> literals;
};

int n, l;
vector<Clause> clauses;
atomic<bool> found(false);
vector<int> solution;          // 1‑based，solution[i] 为变量 xi 的值

void checkAssignment(int assignment) {
    if (found.load()) return;  // 已有解，提前返回

    // 检查所有子句
    for (const auto& cls : clauses) {
        bool clauseSat = false;
        for (int lit : cls.literals) {
            int var = abs(lit) - 1;          // 转为 0‑based 索引
            bool val = (assignment >> var) & 1;
            if ((lit > 0 && val) || (lit < 0 && !val)) {
                clauseSat = true;
                break;
            }
        }
        if (!clauseSat) return;              // 该赋值不满足
    }

    // 满足所有子句，尝试记录解
    bool expected = false;
    if (found.compare_exchange_strong(expected, true)) {
        solution.assign(n + 1, 0);
        for (int i = 0; i < n; ++i)
            solution[i + 1] = (assignment >> i) & 1;
    }
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        cerr << "Usage: " << argv[0] << " <filename.cnf>\n";
        return 1;
    }

    ifstream file(argv[1]);
    if (!file) {
        cerr << "Cannot open file: " << argv[1] << '\n';
        return 1;
    }

    string line;
    while (getline(file, line)) {
        if (line.empty() || line[0] == 'c') continue;
        if (line[0] == 'p') {
            string p, cnf;
            stringstream ss(line);
            ss >> p >> cnf >> n >> l;
            continue;
        }
        // 子句行
        stringstream ss(line);
        int lit;
        Clause cls;
        while (ss >> lit && lit != 0)
            cls.literals.push_back(lit);
        if (!cls.literals.empty())
            clauses.push_back(cls);
    }

    int total = 1 << n;
    vector<thread> threads;
    threads.reserve(total);
    for (int i = 0; i < total; ++i)
        threads.emplace_back(checkAssignment, i);

    for (auto& t : threads)
        t.join();

    if (found.load()) {
        cout << "SAT\n";
        for (int i = 1; i <= n; ++i)
            cout << "x" << i << "=" << solution[i] << (i == n ? "\n" : " ");
    } else {
        cout << "UNSAT\n";
    }
    return 0;
}