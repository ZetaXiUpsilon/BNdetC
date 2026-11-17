"""Exercise the published CLI with independent expected outcomes."""
import argparse
import csv
import itertools
import os
from pathlib import Path
import random
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]


class Suite:
    def __init__(self, binaries, work):
        self.binaries = binaries
        self.work = work
        self.count = 0

    def run(self, tool, *args, expected=0):
        suffix = ".exe" if os.name == "nt" else ""
        result = subprocess.run(
            [str(self.binaries / (tool + suffix)), *map(str, args)],
            cwd=self.work, text=True, capture_output=True, timeout=20,
        )
        assert result.returncode == expected, (
            tool, args, expected, result.returncode, result.stdout, result.stderr
        )
        self.count += 1
        return result.stdout

    def file(self, name, content):
        path = self.work / name
        path.write_text(content, encoding="utf-8")
        return path

    def metrics(self):
        with (self.work / "stats.csv").open(newline="") as stream:
            return {row["metric"]: row["value"] for row in csv.DictReader(stream)}

    def trace(self):
        with (self.work / "trace.csv").open(newline="") as stream:
            return list(csv.DictReader(stream))

    def result(self):
        lines = (self.work / "result.txt").read_text(encoding="ascii").splitlines()
        return {tuple(line.split()[:2]): line.split()[2]
                for line in lines if line.startswith(("REG ", "MEM "))}

    def image(self, code, metadata="INPUT 0\nPRIVATE 0\n"):
        return self.file("hand.ndp", "BNDETC-IMAGE 0.1\nISA 0.1\n" + metadata + "CODE\n" + code)

    def status(self, image, expected, *options):
        exit_code = 2 if expected == "UNKNOWN" else 1 if expected == "ERROR" else 0
        self.run("ndexec", image, *options, expected=exit_code)
        data = (self.work / "result.txt").read_text(encoding="ascii")
        assert f"STATUS {expected}\n" in data, data
        assert self.metrics()["status"] == expected
        return data

    def cnf(self, variables, clauses, layout=None, *, slots=4):
        content = f"p cnf {variables} {len(clauses)}\n"
        if layout is None:
            layout = "".join(" ".join(map(str, clause + [0])) + "\n" for clause in clauses)
        cnf = self.file("case.cnf", content + layout)
        sat = any(
            all(any(values[abs(lit) - 1] == (lit > 0) for lit in clause)
                for clause in clauses)
            for values in itertools.product((False, True), repeat=variables)
        )
        label = "SAT" if sat else "UNSAT"
        assert self.run("satref", cnf, "--max-vars", 12).splitlines()[0] == label
        self.run("cnf2isa", cnf)
        for backend in ("functional", "timed"):
            self.status("program.ndp", "ACCEPT" if sat else "EXHAUSTED",
                        "--backend", backend, "--slots", slots)
            assert f"VERIFIED {label}" in self.run(
                "satref", cnf, "--check", "result.txt", "program.map", "--max-vars", 12
            )
            stats = self.metrics()
            assert int(stats["peak_states"]) <= 64
            assert int(stats["peak_slots"]) <= slots
            payload = 34 + 2 * variables
            assert int(stats["copy_read_bytes"]) >= int(stats["copies"]) * payload
            assert int(stats["copy_write_bytes"]) >= int(stats["copies"]) * payload
            assert bool(stats["reclaimed_cycle"]) == (backend == "timed")

    def core(self):
        for tool in ("cnf2isa", "ndexec", "satref"):
            assert "0.1.0" in self.run(tool, "--version")
        self.cnf(0, [])
        self.cnf(0, [[]])
        self.cnf(3, [[1, 2, 3], [-1, -3]], "1 2\nc continued\n3 0 -1 -3 0\n")
        self.cnf(3, [], slots=1)
        self.cnf(2, [[1, 2], [1, -2], [-1, 2], [-1, -2]], slots=1)
        self.cnf(12, [[-1, 1]])
        rng = random.Random(20261112)
        for _ in range(24):
            n = rng.randint(1, 6)
            clauses = [
                [rng.choice((-1, 1)) * rng.randint(1, n)
                 for _ in range(rng.randint(1, 4))]
                for _ in range(rng.randint(2, 18))
            ]
            self.cnf(n, clauses)
        self.parser_errors()
        self.instructions()
        self.lifecycle()

    def parser_errors(self):
        image_before = (self.work / "program.ndp").read_bytes()
        mapping_before = (self.work / "program.map").read_bytes()
        for data in ("p cnf 1 2\n1 0\n", "p cnf 1 1\n1\n",
                     "p cnf 1 0\n0\n", "p cnf 1 1\n-2 0\n"):
            path = self.file("invalid.cnf", data)
            self.run("cnf2isa", path, expected=1)
            self.run("satref", path, expected=1)
            assert (self.work / "program.ndp").read_bytes() == image_before
            assert (self.work / "program.map").read_bytes() == mapping_before
        for code in ("MOVI R0, 32768\nSTOP\n", "GUESS S0\nSTOP\n", "JMP R12\n",
                     "JMP 65535\n", "JMP missing\n", "x: STOP\nx: STOP\n", ""):
            self.status(self.image(code), "ERROR")
        self.status(self.image("MOVI R0, 0\nSTORE [R0], R0\nSTOP\n",
                               "INPUT 1 7\nPRIVATE 0\n"), "ERROR")
        self.status(self.image("LOAD R0, [R0]\nSTOP\n"), "ERROR")
        self.status(self.image("ADD R0, R0, R0\n"), "ERROR")
        self.status(self.image("MOVI R0, 0xFFFF\nJMP R0\n"), "ERROR")
        self.status(self.image("STOP\n", "INPUT 1\nPRIVATE 0\n"), "ERROR")
        self.status(self.image("STOP\n", "INPUT 0\nPRIVATE 1\nEXPORT MEM 0 2\n"), "ERROR")
        self.run("ndexec", "hand.ndp", "--result", "./hand.ndp", expected=1)
        assert not list(self.work.glob("*.tmp.*"))

    def instructions(self):
        code = """
MOVI R0, 32767
INC R0
MOVI R1, -1
LT R2, R0, R1
ASSERT R2
NOT R3, R1
OR R4, R0, R1
AND R4, R4, R0
SUB R0, R0, R1
MOV R5, R0
DEC R5
MOVI R10, ret
JMP func
ret:
STOP
func:
MOVI R6, 2
loop:
DEC R6
JZ R6, done
JMP loop
done:
JMP R10
"""
        image = self.image(code, "INPUT 0\nPRIVATE 0\n" +
                           "".join(f"EXPORT REG R{i}\n" for i in range(7)))
        for backend in ("functional", "timed"):
            self.status(image, "ACCEPT", "--backend", backend, "--dump-code", "words.txt")
            assert self.result() == {
                ("REG", "R0"): "0x8001", ("REG", "R1"): "0xFFFF",
                ("REG", "R2"): "0x0001", ("REG", "R3"): "0x0000",
                ("REG", "R4"): "0x8000", ("REG", "R5"): "0x8000",
                ("REG", "R6"): "0x0000",
            }
            assert "9C100000" in (self.work / "words.txt").read_text(encoding="ascii")
            assert "80A10000" in (self.work / "words.txt").read_text(encoding="ascii")
            self.status(ROOT / "examples/subset_sum.ndp", "ACCEPT", "--backend", backend)
            assert self.result() == {
                ("REG", "R2"): "0x0008", ("MEM", "3"): "0x0001",
                ("MEM", "4"): "0x0001", ("MEM", "5"): "0x0000",
            }

        isolated = self.image("""
GUESS R0
JZ R0, parent
MOVI R4, 3
delay: DEC R4
JZ R4, child
JMP delay
child:
LOAD R2, [R1]
EQ R3, R2, R1
ASSERT R3
STORE [R1], R0
GUESS R0
ASSERT R0
STOP
parent:
MOVI R3, 0x1234
STORE [R1], R3
ASSERT R0
STOP
""", "INPUT 0\nPRIVATE 1\nEXPORT MEM 0 1\n")
        for backend in ("functional", "timed"):
            self.status(isolated, "ACCEPT", "--backend", backend)
            assert self.result() == {("MEM", "0"): "0x0001"}

    def lifecycle(self):
        image = self.image("GUESS R0\nSTOP\n", "INPUT 0\nPRIVATE 3\nEXPORT REG R0\n")
        for backend in ("functional", "timed"):
            result = self.status(image, "UNKNOWN", "--backend", backend, "--slots", 1, "--states", 1)
            assert "REASON STATE_CAPACITY" in result
            stats = self.metrics()
            assert stats["completed_GUESS"] == stats["copies"] == "0"
            assert stats["peak_states"] == "1"

        result = self.status(image, "UNKNOWN", "--backend", "timed",
                             "--cycle-limit", 8, "--trace", "trace.csv")
        assert "REASON MODEL_CYCLES" in result
        stats = self.metrics()
        assert stats["completed_GUESS"] == stats["copies"] == "0"
        assert 0 < int(stats["copy_write_bytes"]) < 40
        assert int(stats["terminal_cycle"]) < int(stats["quiescent_cycle"]) < int(stats["reclaimed_cycle"])
        trace = self.trace()
        assert "copy_cancel" in {row["kind"] for row in trace}
        assert "publish_child" not in {row["kind"] for row in trace}
        assert trace[-1]["occupied"] == "0"

        pending_store = self.image("MOVI R0, 0\nMOVI R1, 7\nSTORE [R0], R1\nSTOP\n",
                                   "INPUT 0\nPRIVATE 1\n")
        self.status(pending_store, "UNKNOWN", "--backend", "timed",
                    "--cycle-limit", 7, "--trace", "trace.csv")
        stats = self.metrics()
        response = next(row for row in self.trace() if row["kind"] == "drained_memory")
        assert int(response["cycle"]) > int(stats["terminal_cycle"])
        assert stats["completed_STORE"] == "0" and stats["private_writes"] == "1"
        assert int(stats["reclaimed_cycle"]) > int(response["cycle"])

        loop = self.image("again: JMP again\n")
        assert "REASON WALL_TIME" in self.status(loop, "UNKNOWN", "--timeout-ms", 20)
        assert "REASON MODEL_CYCLES" in self.status(
            loop, "UNKNOWN", "--backend", "timed", "--cycle-limit", 25,
            "--trace", "trace.csv", "--trace-limit", 4
        )
        assert len(self.trace()) == 4 and self.metrics()["trace_truncated"] == "1"
        self.status(self.image("STOP\n"), "ACCEPT", "--backend", "timed", "--cycle-limit", 2)
        self.status(self.image("ASSERT R0\n"), "EXHAUSTED", "--backend", "timed", "--cycle-limit", 3)

        tie = self.image("""
GUESS R0
LOAD R1, [R2]
JZ R0, extra
JMP finish
extra: MOVI R4, 0
MOVI R4, 0
finish: STOP
""", "INPUT 0\nPRIVATE 1\nEXPORT REG R0\n")
        self.status(tie, "ACCEPT", "--backend", "timed", "--trace", "trace.csv")
        successes = [row for row in self.trace() if row["kind"] == "STOP"]
        assert len(successes) == 2 and len({row["cycle"] for row in successes}) == 1, successes
        assert self.result()[("REG", "R0")] == "0x0000"
        assert int(self.metrics()["private_wait_cycles"]) > 0

        simultaneous_error = self.image("""
MOVI R5, 9
GUESS R0
LOAD R1, [R2]
JZ R0, extra
JMP bad
extra: MOVI R4, 0
MOVI R4, 0
STOP
bad: LOAD R3, [R5]
""", "INPUT 0\nPRIVATE 1\n")
        assert "REASON DATA_BOUNDS" in self.status(
            simultaneous_error, "ERROR", "--backend", "timed", "--trace", "trace.csv"
        )
        assert self.metrics()["first_success_cycle"] == self.metrics()["terminal_cycle"]

        simultaneous_capacity = self.image("""
GUESS R0
JZ R0, extra
GUESS R1
STOP
extra: MOVI R4, 0
STOP
""", "INPUT 0\nPRIVATE 0\nEXPORT REG R0\n")
        self.status(simultaneous_capacity, "ACCEPT", "--backend", "timed",
                    "--slots", 2, "--states", 2, "--trace", "trace.csv")
        trace = self.trace()
        assert next(row["cycle"] for row in trace if row["kind"] == "capacity") == (
            self.metrics()["winner_selected_cycle"]
        )
        assert self.result()[("REG", "R0")] == "0x0000"

        earlier = self.image("GUESS R0\nJZ R0, slow\nSTOP\nslow: MOVI R4, 0\nINC R4\nSTOP\n",
                             "INPUT 0\nPRIVATE 0\nEXPORT REG R0\n")
        self.status(earlier, "ACCEPT", "--backend", "timed")
        assert self.result()[("REG", "R0")] == "0x0001"

        # Exercise the upper address boundary without truncating I + M or N.
        address = self.image("MOVI R0, 0xFFFF\nMOVI R1, 37\nSTORE [R0], R1\n"
                             "LOAD R2, [R0]\nEQ R3, R1, R2\nASSERT R3\nSTOP\n",
                             "INPUT 0\nPRIVATE 65536\nEXPORT MEM 65535 1\n")
        self.status(address, "ACCEPT")
        assert self.result()[("MEM", "65535")] == "0x0025"
        last = self.image("JMP last\n" + "INC R0\n" * 65534 + "last: STOP\n")
        self.status(last, "ACCEPT", "--dump-code", "words.txt")
        words = (self.work / "words.txt").read_text(encoding="ascii").splitlines()
        assert len(words) == 65536 and words[0] == "0000 8000FFFF"
        last = self.image("JMP last\n" + "INC R0\n" * 65534 + "last: INC R0\n")
        assert "REASON PC_BOUNDS" in self.status(last, "ERROR")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary_directory", type=Path)
    args = parser.parse_args()
    build = ROOT / "build"
    build.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="acceptance-", dir=build) as directory:
        suite = Suite(args.binary_directory.resolve(), Path(directory))
        suite.core()
        print(f"PASS: {suite.count} CLI checks, including 30 CNFs on both backends")


if __name__ == "__main__":
    main()
