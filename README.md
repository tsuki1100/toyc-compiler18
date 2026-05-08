# ToyC Compiler

A simple C-like language compiler that generates RISC-V assembly code.

## Features

- **Lexical Analysis**: Flex-based tokenizer
- **Syntax Analysis**: Bison-based parser with AST generation
- **Semantic Analysis**: Type checking and scope management
- **Code Generation**: RISC-V assembly output
- **Optimizations**: Constant folding and dead code elimination

## Prerequisites

- CMake (>= 3.15)
- Flex (>= 2.6.4)
- Bison (>= 3.8.2)
- C++ compiler with C++20 support

### Ubuntu/Debian
```bash
sudo apt-get update
sudo apt-get install cmake flex bison build-essential
```

### macOS
```bash
brew install cmake flex bison
```

### Windows (MSYS2)
```bash
pacman -S mingw-w64-x86_64-cmake mingw-w64-x86_64-flex mingw-w64-x86_64-bison mingw-w64-x86_64-gcc
```

## Building

```bash
# Clone the repository
git clone <your-repo-url>
cd toyc-compiler

# Build using the provided script
./build.sh

# Or manually
mkdir build
cd build
cmake ..
make -j$(nproc)
```

## Usage

### Basic compilation
```bash
# Compile from stdin
echo "int main() { return 42; }" | ./build/compiler

# Compile a file
./build/compiler < test_samples/hello.tc

# With optimizations
./build/compiler -opt < test_samples/factorial.tc
```

### Running tests
```bash
./run_tests.sh build/compiler
```

## Language Features

### Supported constructs:
- Variable declarations: `int x = 10;`
- Arithmetic operations: `+`, `-`, `*`, `/`, `%`
- Comparison operators: `==`, `!=`, `<`, `<=`, `>`, `>=`
- Logical operators: `&&`, `||`, `!`
- Control flow: `if-else`, `while`, `break`, `continue`
- Function definitions and calls
- Comments: `//` and `/* */`

### Example program:
```c
int main() {
    int x = 10;
    int y = 20;
    
    if (x < y) {
        return x + y;
    } else {
        return x - y;
    }
}
```

## Project Structure

```
src/
├── ast/           # Abstract Syntax Tree
├── codegen/       # RISC-V code generation
├── common/        # Shared types and utilities
├── semantic/      # Semantic analysis
├── utils/         # Utility functions
├── lexer.l        # Flex lexer specification
├── parser.y       # Bison parser specification
└── main.cpp       # Main program

test_samples/      # Test programs
build.sh          # Build script
run_tests.sh      # Test runner
```

## Contributing

1. Fork the repository
2. Create a feature branch
3. Make your changes
4. Add tests if applicable
5. Submit a pull request

## License

[Add your license here] 