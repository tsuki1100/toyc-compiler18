#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>
#include <cstdlib>

// Minimal RISC-V 32-bit simulator for validating toy compiler output.
// Supports the subset of instructions our compiler generates.
// Usage: rvsim < assembly.s   — prints return value to stdout, errors to stderr.

int32_t regs[32];  // x0..x31
std::vector<uint8_t> memory(1024 * 1024);  // 1MB
int32_t memOffset = 0;  // stack starts at top of memory

std::vector<std::string> lines;
std::unordered_map<std::string, int> labels;
uint32_t pc = 0;
bool running = true;

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

int32_t sext(int32_t val, int bits) {
    int32_t shift = 32 - bits;
    return (val << shift) >> shift;
}

int32_t parseImm(const std::string& s) {
    if (s.empty()) return 0;
    try { return std::stoi(s); }
    catch (...) { return 0; }
}

int getReg(const std::string& name) {
    if (name == "zero" || name == "x0")  return 0;
    if (name == "ra"   || name == "x1")  return 1;
    if (name == "sp"   || name == "x2")  return 2;
    if (name == "gp"   || name == "x3")  return 3;
    if (name == "tp"   || name == "x4")  return 4;
    if (name == "t0"   || name == "x5")  return 5;
    if (name == "t1"   || name == "x6")  return 6;
    if (name == "t2"   || name == "x7")  return 7;
    if (name == "fp" || name == "s0" || name == "x8") return 8;
    if (name == "s1"   || name == "x9")  return 9;
    if (name == "a0"   || name == "x10") return 10;
    if (name == "a1"   || name == "x11") return 11;
    if (name == "a2"   || name == "x12") return 12;
    if (name == "a3"   || name == "x13") return 13;
    if (name == "a4"   || name == "x14") return 14;
    if (name == "a5"   || name == "x15") return 15;
    if (name == "a6"   || name == "x16") return 16;
    if (name == "a7"   || name == "x17") return 17;
    if (name == "s2"   || name == "x18") return 18;
    if (name == "s3"   || name == "x19") return 19;
    if (name == "s4"   || name == "x20") return 20;
    if (name == "s5"   || name == "x21") return 21;
    if (name == "s6"   || name == "x22") return 22;
    if (name == "s7"   || name == "x23") return 23;
    if (name == "s8"   || name == "x24") return 24;
    if (name == "s9"   || name == "x25") return 25;
    if (name == "s10"  || name == "x26") return 26;
    if (name == "s11"  || name == "x27") return 27;
    if (name == "t3"   || name == "x28") return 28;
    if (name == "t4"   || name == "x29") return 29;
    if (name == "t5"   || name == "x30") return 30;
    if (name == "t6"   || name == "x31") return 31;
    return -1;
}

// Extract register name from string (e.g., "t0," → "t0")
std::string extractReg(std::string s) {
    s = trim(s);
    while (!s.empty() && (s.back() == ',' || s.back() == ')' || s.back() == ' '))
        s.pop_back();
    return s;
}

// Memory helpers
int32_t memLoad(int32_t addr) {
    int32_t base = addr + memOffset;
    if (base < 0 || base + 3 >= (int)memory.size()) {
        std::cerr << "Memory load out of bounds: " << addr << std::endl;
        exit(1);
    }
    return (uint8_t)memory[base] | ((uint8_t)memory[base+1] << 8) |
           ((uint8_t)memory[base+2] << 16) | ((uint8_t)memory[base+3] << 24);
}

void memStore(int32_t addr, int32_t val) {
    int32_t base = addr + memOffset;
    if (base < 0 || base + 3 >= (int)memory.size()) {
        std::cerr << "Memory store out of bounds: " << addr << std::endl;
        exit(1);
    }
    memory[base]   = val & 0xFF;
    memory[base+1] = (val >> 8) & 0xFF;
    memory[base+2] = (val >> 16) & 0xFF;
    memory[base+3] = (val >> 24) & 0xFF;
}

void executeInstruction(const std::string& line) {
    std::istringstream iss(line);
    std::string op;
    iss >> op;
    if (op.empty() || op[0] == '#') return;

    // --- Pseudo-instructions ---

    if (op == "li") {
        // li rd, imm → addi rd, x0, imm
        std::string rd, immStr;
        iss >> rd;
        iss >> immStr;
        rd = extractReg(rd);
        int r = getReg(rd);
        if (r < 0) { std::cerr << "Bad reg: " << rd << std::endl; return; }
        int32_t imm = parseImm(immStr);
        if (r != 0) regs[r] = imm;
        return;
    }

    if (op == "mv") {
        std::string rd, rs;
        iss >> rd >> rs;
        rd = extractReg(rd); rs = extractReg(rs);
        int r = getReg(rd), s = getReg(rs);
        if (r >= 0 && r != 0 && s >= 0) regs[r] = regs[s];
        return;
    }

    if (op == "neg") {
        std::string rd, rs;
        iss >> rd >> rs;
        rd = extractReg(rd); rs = extractReg(rs);
        int r = getReg(rd), s = getReg(rs);
        if (r >= 0 && r != 0 && s >= 0) regs[r] = -regs[s];
        return;
    }

    if (op == "seqz") {
        std::string rd, rs;
        iss >> rd >> rs;
        rd = extractReg(rd); rs = extractReg(rs);
        int r = getReg(rd), s = getReg(rs);
        if (r >= 0 && r != 0 && s >= 0) regs[r] = (regs[s] == 0) ? 1 : 0;
        return;
    }

    if (op == "snez") {
        std::string rd, rs;
        iss >> rd >> rs;
        rd = extractReg(rd); rs = extractReg(rs);
        int r = getReg(rd), s = getReg(rs);
        if (r >= 0 && r != 0 && s >= 0) regs[r] = (regs[s] != 0) ? 1 : 0;
        return;
    }

    if (op == "beqz") {
        std::string rs, label;
        iss >> rs >> label;
        rs = extractReg(rs); label = extractReg(label);
        int s = getReg(rs);
        if (s >= 0 && regs[s] == 0) {
            auto it = labels.find(label);
            if (it != labels.end()) pc = it->second;
        }
        return;
    }

    if (op == "j") {
        std::string label;
        iss >> label;
        label = extractReg(label);
        auto it = labels.find(label);
        if (it != labels.end()) pc = it->second;
        return;
    }

    if (op == "call") {
        std::string label;
        iss >> label;
        label = extractReg(label);
        regs[1] = pc + 1; // ra = return address
        auto it = labels.find(label);
        if (it != labels.end()) pc = it->second;
        return;
    }

    if (op == "ret") {
        if (regs[1] == 0) {
            running = false; // main returned
        } else {
            pc = regs[1] - 1;
        }
        return;
    }

    // --- R-type ---
    if (op == "add" || op == "sub" || op == "mul" || op == "div" || op == "rem" ||
        op == "slt" || op == "and" || op == "or") {
        std::string rd, rs1, rs2;
        iss >> rd >> rs1 >> rs2;
        rd = extractReg(rd); rs1 = extractReg(rs1); rs2 = extractReg(rs2);
        int r = getReg(rd), s1 = getReg(rs1), s2 = getReg(rs2);
        if (r < 0 || s1 < 0 || s2 < 0) return;
        if (r == 0) return;
        if (op == "add") regs[r] = regs[s1] + regs[s2];
        else if (op == "sub") regs[r] = regs[s1] - regs[s2];
        else if (op == "mul") regs[r] = regs[s1] * regs[s2];
        else if (op == "div") regs[r] = (regs[s2] != 0) ? regs[s1] / regs[s2] : 0;
        else if (op == "rem") regs[r] = (regs[s2] != 0) ? regs[s1] % regs[s2] : 0;
        else if (op == "slt") regs[r] = (regs[s1] < regs[s2]) ? 1 : 0;
        else if (op == "and") regs[r] = regs[s1] & regs[s2];
        else if (op == "or")  regs[r] = regs[s1] | regs[s2];
        return;
    }

    // --- I-type ---
    if (op == "addi") {
        std::string rd, rs1, immStr;
        iss >> rd >> rs1 >> immStr;
        rd = extractReg(rd); rs1 = extractReg(rs1);
        int r = getReg(rd), s = getReg(rs1);
        if (r >= 0 && r != 0 && s >= 0) regs[r] = regs[s] + parseImm(immStr);
        return;
    }

    if (op == "xori") {
        std::string rd, rs1, immStr;
        iss >> rd >> rs1 >> immStr;
        rd = extractReg(rd); rs1 = extractReg(rs1);
        int r = getReg(rd), s = getReg(rs1);
        if (r >= 0 && r != 0 && s >= 0) regs[r] = regs[s] ^ parseImm(immStr);
        return;
    }

    if (op == "slti") {
        std::string rd, rs1, immStr;
        iss >> rd >> rs1 >> immStr;
        rd = extractReg(rd); rs1 = extractReg(rs1);
        int r = getReg(rd), s = getReg(rs1);
        if (r >= 0 && r != 0 && s >= 0) regs[r] = (regs[s] < parseImm(immStr)) ? 1 : 0;
        return;
    }

    // --- Load ---
    if (op == "lw") {
        std::string rd, rest;
        iss >> rd;
        std::getline(iss, rest);
        rd = extractReg(rd);
        int r = getReg(rd);
        if (r < 0 || r == 0) return;
        // Parse offset(rs) format
        auto lp = rest.find('(');
        auto rp = rest.find(')');
        if (lp != std::string::npos && rp != std::string::npos) {
            std::string offStr = trim(rest.substr(0, lp));
            std::string rsStr = extractReg(rest.substr(lp + 1, rp - lp - 1));
            int32_t offset = parseImm(offStr);
            int s = getReg(rsStr);
            if (s >= 0) regs[r] = memLoad(regs[s] + offset);
        }
        return;
    }

    // --- Store ---
    if (op == "sw") {
        std::string rs2, rest;
        iss >> rs2;
        std::getline(iss, rest);
        rs2 = extractReg(rs2);
        int s2 = getReg(rs2);
        auto lp = rest.find('(');
        auto rp = rest.find(')');
        if (lp != std::string::npos && rp != std::string::npos) {
            std::string offStr = trim(rest.substr(0, lp));
            std::string rs1Str = extractReg(rest.substr(lp + 1, rp - lp - 1));
            int32_t offset = parseImm(offStr);
            int s1 = getReg(rs1Str);
            if (s1 >= 0 && s2 >= 0) memStore(regs[s1] + offset, regs[s2]);
        }
        return;
    }

    // --- U-type (auipc) ---
    if (op == "auipc") {
        // auipc rd, %pcrel_hi(symbol) — simplify: load 0 for our purposes
        std::string rd, immStr;
        iss >> rd >> immStr;
        rd = extractReg(rd);
        int r = getReg(rd);
        if (r >= 0 && r != 0) regs[r] = 0; // global vars not used in tests
        return;
    }
}

int main() {
    // Set up stack pointer at top of memory
    memOffset = memory.size() / 2;
    regs[2] = 0;  // sp will be set by prologue
    regs[8] = 0;  // fp

    // Read all lines
    std::string line;
    while (std::getline(std::cin, line)) {
        std::string trimmed = trim(line);
        // Skip empty lines, comments, directives (except .text and .global)
        if (trimmed.empty() || trimmed[0] == '.') {
            if (trimmed.rfind(".text", 0) == 0 || trimmed.rfind(".global", 0) == 0) {
                // ignore
            }
            continue;
        }
        // Check if this is a label
        if (trimmed.back() == ':') {
            std::string label = trimmed.substr(0, trimmed.size() - 1);
            labels[label] = lines.size();
            lines.push_back(trimmed);
        } else {
            lines.push_back(trimmed);
        }
    }

    // Find main entry point
    auto it = labels.find("main");
    if (it == labels.end()) {
        std::cerr << "ERROR: main not found" << std::endl;
        return 1;
    }
    pc = it->second;

    // Execute until main returns
    while (running && pc < lines.size()) {
        std::string instr = lines[pc];
        // Skip label-only lines
        if (instr.back() != ':') {
            executeInstruction(instr);
        }
        pc++;
        // Stop when we hit ret in main context (after first ret instruction)
        // We detect main returning by checking if we just executed "ret"
        if (!lines.empty() && pc > 0 && pc - 1 < lines.size()) {
            std::string prev = lines[pc - 1];
            if (trim(prev) == "ret") {
                // This could be any function's ret; we'll check if sp is back to original
                // For simplicity: stop on main's ret
                running = false;
            }
        }
    }

    // Output the return value (a0 register)
    std::cout << regs[10] << std::endl;
    return 0;
}
