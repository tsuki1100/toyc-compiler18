#!/bin/bash

COMPILER=${1:-"./build/toyc"}
TEST_DIR="test_samples"
TEMP_DIR="/tmp/toyc_test_$$"

# Auto-detect platform (add .exe on Windows)
if [[ "$(uname -s)" == *"MINGW"* ]] || [[ "$(uname -s)" == *"MSYS"* ]]; then
    COMPILER="${COMPILER}.exe"
fi

# RVSIM is in the same directory as the compiler
RVSIM="$(dirname "$COMPILER")/rvsim"
if [[ "$COMPILER" == *.exe ]]; then
    RVSIM="${RVSIM}.exe"
fi

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

if [ ! -x "$COMPILER" ]; then
    echo -e "${RED}Error: Compiler not found or not executable: $COMPILER${NC}"
    exit 1
fi

mkdir -p "$TEMP_DIR"

echo -e "${BLUE}ToyC Compiler Test Suite${NC}"
echo "=================================================="
echo "Compiler: $COMPILER"
echo "Simulator: $RVSIM"
echo "Test directory: $TEST_DIR"
echo ""

total_tests=0
passed_tests=0

# Parse expected value from test file
get_expected() {
    local test_file=$1
    sed -n 's/.*\/\/[[:space:]]*expect:[[:space:]]*\(-\?[0-9]\+\).*/\1/p' "$test_file" 2>/dev/null || echo ""
}

run_test() {
    local test_file=$1
    local test_name=$(basename "$test_file" .tc)
    local use_opt=$2

    echo -n "Testing $test_name"
    if [ "$use_opt" = "true" ]; then
        echo -n " (optimized)"
    fi
    echo -n "... "

    total_tests=$((total_tests + 1))

    local output_file="$TEMP_DIR/$test_name.s"
    local opt_flag=""
    if [ "$use_opt" = "true" ]; then
        opt_flag="-opt"
        output_file="$TEMP_DIR/${test_name}_opt.s"
    fi

    # Step 1: Compile
    if ! "$COMPILER" $opt_flag < "$test_file" > "$output_file" 2>"$TEMP_DIR/$test_name.err"; then
        echo -e "${RED}FAIL${NC} (compilation error)"
        if [ -f "$TEMP_DIR/$test_name.err" ] && [ -s "$TEMP_DIR/$test_name.err" ]; then
            echo "  Error output:"
            sed 's/^/    /' "$TEMP_DIR/$test_name.err"
        fi
        return
    fi

    if [ ! -f "$output_file" ] || [ ! -s "$output_file" ]; then
        echo -e "${RED}FAIL${NC} (empty output)"
        return
    fi

    # Step 2: Check if runtime verification is available
    local expected=$(get_expected "$test_file")
    local line_count=$(wc -l < "$output_file")

    if [ -x "$RVSIM" ] && [ -n "$expected" ]; then
        # Run the generated assembly through the simulator
        local actual=$("$RVSIM" < "$output_file" 2>/dev/null)
        if [ "$actual" = "$expected" ]; then
            echo -e "${GREEN}PASS${NC} (${line_count} asm lines, result: $actual)"
            passed_tests=$((passed_tests + 1))
        else
            echo -e "${RED}FAIL${NC} (expected $expected, got $actual, ${line_count} asm lines)"
        fi
    elif [ -x "$RVSIM" ] && [ -z "$expected" ]; then
        # No expected value annotation; just check compilation
        echo -e "${GREEN}PASS${NC} (${line_count} asm lines, no runtime check)"
        passed_tests=$((passed_tests + 1))
    else
        # No simulator available
        echo -e "${GREEN}PASS${NC} (${line_count} asm lines, compilation only)"
        passed_tests=$((passed_tests + 1))
    fi
}

# Run normal mode tests
echo "Running compilation + verification tests (normal mode):"
for test_file in "$TEST_DIR"/*.tc; do
    if [ -f "$test_file" ]; then
        run_test "$test_file" false
    fi
done

# Run optimized mode tests
echo ""
echo "Running compilation + verification tests (optimized mode):"
for test_file in "$TEST_DIR"/*.tc; do
    if [ -f "$test_file" ]; then
        run_test "$test_file" true
    fi
done

# Test error handling
echo ""
echo "Testing error handling:"
cat > "$TEMP_DIR/syntax_error.tc" << 'EOF'
int main() {
    int x = ;
    return x;
}
EOF

echo -n "Testing syntax error detection... "
if "$COMPILER" < "$TEMP_DIR/syntax_error.tc" > /dev/null 2>&1; then
    echo -e "${RED}FAIL${NC} (should have failed)"
else
    echo -e "${GREEN}PASS${NC}"
    passed_tests=$((passed_tests + 1))
fi
total_tests=$((total_tests + 1))

echo ""
echo "=================================================="
echo -e "Tests completed: ${GREEN}$passed_tests${NC}/${total_tests} passed"

if [ $passed_tests -eq $total_tests ]; then
    echo -e "${GREEN}All tests passed!${NC}"
    rm -rf "$TEMP_DIR"
    exit 0
else
    echo -e "${RED}$((total_tests - passed_tests)) tests failed${NC}"
    echo "Generated files are in: $TEMP_DIR"
    exit 1
fi
