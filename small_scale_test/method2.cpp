#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <future>
#include <atomic>
#include <memory>
#include <cmath>
#include <algorithm>

using namespace std;

struct Clause {
    vector<int> literals;
};

struct Instr {
    enum Type { GUESS, ASSERT, STOP } type;
    int var;                   // 用于 GUESS
    vector<int> clause;        // 用于 ASSERT

    // 默认构造函数，避免未初始化警告
    Instr() : type(STOP), var(0) {}
};

int n, l;
vector<Clause> clauses;
vector<Instr> program;
vector<int> solution;          // 1‑based

struct State {
    vector<int> assign;        // 1‑based，-1 表示未赋值
    int pc;                    // 程序计数器
    State(int n) : assign(n + 1, -1), pc(0) {}
};

bool execute(State state, shared_ptr<atomic<bool>> done) {
    if (done->load()) return false;

    if (state.pc >= (int)program.size()) {
        // 所有指令执行完毕，成功
        bool expected = false;
        if (done->compare_exchange_strong(expected, true))
            solution = state.assign;
        return true;
    }

    const Instr& instr = program[state.pc];

    if (instr.type == Instr::GUESS) {
        int var = instr.var;
        State s0 = state;
        s0.assign[var] = 0;
        s0.pc++;
        State s1 = state;
        s1.assign[var] = 1;
        s1.pc++;

        auto fut0 = async(launch::async, execute, move(s0), done);
        auto fut1 = async(launch::async, execute, move(s1), done);

        bool res0 = fut0.get();
        bool res1 = fut1.get();
        return res0 || res1;
    }
    else if (instr.type == Instr::ASSERT) {
        const auto& cls = instr.clause;
        bool sat = false;
        for (int lit : cls) {
            int var = abs(lit);
            int val = state.assign[var];
            if (val == -1) return false;   // 理论上不会发生
            if ((lit > 0 && val == 1) || (lit < 0 && val == 0)) {
                sat = true;
                break;
            }
        }
        if (!sat) return false;
        state.pc++;
        return execute(state, done);
    }
    else { // STOP
        bool expected = false;
        if (done->compare_exchange_strong(expected, true))
            solution = state.assign;
        return true;
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
        stringstream ss(line);
        int lit;
        Clause cls;
        while (ss >> lit && lit != 0)
            cls.literals.push_back(lit);
        if (!cls.literals.empty())
            clauses.push_back(cls);
    }

    // 生成非确定性程序
    for (int i = 1; i <= n; ++i) {
        Instr instr;
        instr.type = Instr::GUESS;
        instr.var = i;
        program.push_back(instr);
    }
    for (const auto& c : clauses) {
        Instr instr;
        instr.type = Instr::ASSERT;
        instr.clause = c.literals;
        program.push_back(instr);
    }
    Instr stop;
    stop.type = Instr::STOP;
    program.push_back(stop);

    State init(n);
    auto done = make_shared<atomic<bool>>(false);
    execute(init, done);   // 返回值无需使用，忽略

    if (done->load()) {
        cout << "SAT\n";
        for (int i = 1; i <= n; ++i)
            cout << "x" << i << "=" << solution[i] << (i == n ? "\n" : " ");
    } else {
        cout << "UNSAT\n";
    }
    return 0;
}