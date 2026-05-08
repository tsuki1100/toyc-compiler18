#pragma once
#include <string>
#include <vector>

// IR 操作数
struct IROperand {
    enum Kind { VREG, IMM, LABEL, FUNC };
    Kind kind;
    int value;     // vreg号 / 立即数值 / 标签id
    std::string name; // 用于 FUNC (函数名)

    IROperand() : kind(IMM), value(0) {}
    static IROperand vreg(int id) { IROperand o; o.kind = VREG; o.value = id; return o; }
    static IROperand imm(int v)   { IROperand o; o.kind = IMM; o.value = v; return o; }
    static IROperand label(int id){ IROperand o; o.kind = LABEL; o.value = id; return o; }
    static IROperand func(const std::string& n) { IROperand o; o.kind = FUNC; o.value = 0; o.name = n; return o; }

    bool operator==(const IROperand& other) const {
        return kind == other.kind && value == other.value && name == other.name;
    }
};

// IR 指令
enum class IROp {
    // 二元算术 (dest = src1 op src2)
    ADD, SUB, MUL, DIV, MOD,
    // 比较 (dest = src1 op src2)
    LT, LE, GT, GE, EQ, NE,
    // 逻辑
    AND, OR,
    // 一元
    NEG, NOT,
    // 立即数加载
    LI,
    // 数据移动
    MOVE,
    // 内存
    LOAD,   // dest = [base + offset]
    STORE,  // [base + offset] = src
    // 控制流
    LABEL,  // label:
    JUMP,   // goto label
    BR,     // if src != 0 goto label1 else label2
    // 函数
    PARAM,  // push param(src1)
    CALL,   // dest = call func(dest.name), 参数数量=src1.value
    RET,    // return (src1 or void)
};

struct IRInstr {
    IROp op;
    IROperand dest;
    IROperand src1;
    IROperand src2;
};

// IR 函数
struct IRFunction {
    std::string name;
    int paramCount;
    bool isVoid;
    std::vector<IRInstr> instructions;

    int newVReg() { return nextVReg++; }
    int newLabel() { return nextLabel++; }

private:
    int nextVReg = 0;
    int nextLabel = 1;
};
