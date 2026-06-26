#!/bin/bash
if [ $# -ne 1 ]; then
    echo "Usage: $0 <cnf-file>"
    exit 1
fi
CNF="$1"
if [ ! -f "$CNF" ]; then
    echo "File not found: $CNF"
    exit 1
fi

# 确保可执行文件存在
for exe in determ method1 method2 verify; do
    if [ ! -x "./$exe" ]; then
        echo "Error: $exe not found or not executable. Run 'make' first."
        exit 1
    fi
done

TMP_DET=$(mktemp)
TMP_M1=$(mktemp)
TMP_M2=$(mktemp)

run_and_time() {
    local prog=$1
    local outfile=$2
    local cnf=$3
    /usr/bin/time -f "%e" -o time.tmp ./$prog "$cnf" > "$outfile"
    local elapsed=$(cat time.tmp)
    rm -f time.tmp
    echo "$elapsed"
}

echo "=========================================="
echo "Testing CNF: $CNF"
echo "=========================================="

echo -n "Running determ... "
TIME_DET=$(run_and_time determ "$TMP_DET" "$CNF")
echo "done (${TIME_DET}s)"
echo -n "Verifying determ output... "
VER_DET=$(./verify "$CNF" "$TMP_DET" | tail -n1)
echo "$VER_DET"

echo -n "Running method1... "
TIME_M1=$(run_and_time method1 "$TMP_M1" "$CNF")
echo "done (${TIME_M1}s)"
echo -n "Verifying method1 output... "
VER_M1=$(./verify "$CNF" "$TMP_M1" | tail -n1)
echo "$VER_M1"

echo -n "Running method2... "
TIME_M2=$(run_and_time method2 "$TMP_M2" "$CNF")
echo "done (${TIME_M2}s)"
echo -n "Verifying method2 output... "
VER_M2=$(./verify "$CNF" "$TMP_M2" | tail -n1)
echo "$VER_M2"

echo "=========================================="
echo "Summary:"
printf "%-10s %10s %10s\n" "Method" "Time(s)" "Verification"
printf "%-10s %10s %10s\n" "determ" "$TIME_DET" "$VER_DET"
printf "%-10s %10s %10s\n" "method1" "$TIME_M1" "$VER_M1"
printf "%-10s %10s %10s\n" "method2" "$TIME_M2" "$VER_M2"

rm -f "$TMP_DET" "$TMP_M1" "$TMP_M2"