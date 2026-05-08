#include "codegen/riscv.hpp"
#include "ir/ir_builder.hpp"
#include <iostream>
#include <sstream>

// ============================
// IR → 汇编 代码生成
// ============================

std::string RISCVCodeGenerator::generate(CompilationUnit& unit,
                                          const std::unordered_map<std::string, FunctionInfo>& funcTable) {
    // Step 1: AST → IR
    IRBuilder irBuilder(optimizationsEnabled);
    auto irFunctions = irBuilder.build(unit);

    // Step 2: IR → RISC-V 汇编
    functions = funcTable;
    output.clear();
    labelCounter = 0;

    emit(".text");
    emit(".global main");

    for (const auto& irFunc : irFunctions) {
        generateFunctionFromIR(irFunc);
    }

    return output;
}

void RISCVCodeGenerator::generateFunctionFromIR(const IRFunction& irFunc) {
    vregSlots.clear();
    irLabels.clear();
    vregSlotOffset = -20;
    paramIdx = 0;

    // 运行寄存器分配
    RegAlloc ra;
    regAlloc = ra.allocate(irFunc);

    // 为溢出 vreg 分配栈槽
    for (auto& [vreg, alloc] : regAlloc) {
        if (alloc.kind == RegAlloc::Allocation::SPILL) {
            vregSlots[vreg] = vregSlotOffset;
            vregSlotOffset -= 4;
        }
    }

    // 分配 IR 标签 → 汇编标签
    for (const auto& instr : irFunc.instructions) {
        if (instr.op == IROp::LABEL) {
            int id = instr.src1.value;
            if (irLabels.find(id) == irLabels.end()) {
                irLabels[id] = newLabel("L");
            }
        }
        if (instr.op == IROp::BR) {
            int l1 = instr.src1.value;
            int l2 = instr.src2.value;
            if (irLabels.find(l1) == irLabels.end()) irLabels[l1] = newLabel("L");
            if (irLabels.find(l2) == irLabels.end()) irLabels[l2] = newLabel("L");
        }
    }

    int totalFrame = (-vregSlotOffset + 16 + 15) & ~15;
    generatePrologue(irFunc.name, totalFrame);

    for (int i = 0; i < irFunc.paramCount && i < 8; i++) {
        int paramOffset = -20 - i * 4;
        emit("sw a" + std::to_string(i) + ", " + std::to_string(paramOffset) + "(fp)");
    }

    for (const auto& instr : irFunc.instructions) {
        emitIRInstruction(instr);
    }

    regAlloc.clear();
}

// 获取 vreg 所在的寄存器名 (若在寄存器中) 或加载到 t0 返回 "t0"
std::string RISCVCodeGenerator::getSrcReg(int vreg) {
    auto it = regAlloc.find(vreg);
    if (it != regAlloc.end() && it->second.kind == RegAlloc::Allocation::REG) {
        return RegAlloc::REG_NAMES[it->second.physReg];
    }
    // spilled 或未分配: 加载到 t0
    auto slot = vregSlots.find(vreg);
    if (slot != vregSlots.end()) {
        emit("lw t0, " + std::to_string(slot->second) + "(fp)");
    }
    return "t0";
}

// 将结果写回 (若在寄存器中，值已在那里; 若 spilled，存到栈)
void RISCVCodeGenerator::storeResult(int vreg, const std::string& reg) {
    auto it = regAlloc.find(vreg);
    if (it != regAlloc.end() && it->second.kind == RegAlloc::Allocation::REG) {
        if (RegAlloc::REG_NAMES[it->second.physReg] != reg) {
            emit("mv " + std::string(RegAlloc::REG_NAMES[it->second.physReg]) + ", " + reg);
        }
        return;
    }
    auto slot = vregSlots.find(vreg);
    if (slot != vregSlots.end()) {
        emit("sw " + reg + ", " + std::to_string(slot->second) + "(fp)");
    }
}

void RISCVCodeGenerator::emitIRInstruction(const IRInstr& instr) {
    switch (instr.op) {

    case IROp::LI: {
        auto it = regAlloc.find(instr.dest.value);
        if (it != regAlloc.end() && it->second.kind == RegAlloc::Allocation::REG) {
            emit("li " + std::string(RegAlloc::REG_NAMES[it->second.physReg]) +
                 ", " + std::to_string(instr.src1.value));
        } else {
            emit("li t0, " + std::to_string(instr.src1.value));
            storeResult(instr.dest.value, "t0");
        }
        break;
    }

    case IROp::MOVE: {
        std::string s = getSrcReg(instr.src1.value);
        storeResult(instr.dest.value, s);
        break;
    }

    case IROp::ADD: case IROp::SUB: case IROp::MUL:
    case IROp::DIV: case IROp::MOD:
    case IROp::AND: case IROp::OR:
    case IROp::LT:  case IROp::LE:  case IROp::GT:
    case IROp::GE:  case IROp::EQ:  case IROp::NE: {
        std::string rs1 = getSrcReg(instr.src1.value);
        std::string rs2 = getSrcReg(instr.src2.value);

        // 确定目标寄存器
        std::string rd = "t0";
        auto it = regAlloc.find(instr.dest.value);
        bool destInReg = (it != regAlloc.end() && it->second.kind == RegAlloc::Allocation::REG);
        if (destInReg) {
            rd = RegAlloc::REG_NAMES[it->second.physReg];
            // 如果 rs1 或 rs2 恰好是 t0，需要复制到目标寄存器后再操作
            if (rs2 == rd) {
                emit("mv t1, " + rs2);
                rs2 = "t1";
            }
            if (rs1 == "t0" && rd != "t0") {
                emit("mv " + rd + ", t0");
                rs1 = rd;
            }
        }

        switch (instr.op) {
            case IROp::ADD: emit("add " + rd + ", " + rs1 + ", " + rs2); break;
            case IROp::SUB: emit("sub " + rd + ", " + rs1 + ", " + rs2); break;
            case IROp::MUL: emit("mul " + rd + ", " + rs1 + ", " + rs2); break;
            case IROp::DIV: emit("div " + rd + ", " + rs1 + ", " + rs2); break;
            case IROp::MOD: emit("rem " + rd + ", " + rs1 + ", " + rs2); break;
            case IROp::AND: emit("and " + rd + ", " + rs1 + ", " + rs2); break;
            case IROp::OR:  emit("or "  + rd + ", " + rs1 + ", " + rs2); break;
            case IROp::LT:  emit("slt " + rd + ", " + rs1 + ", " + rs2); break;
            case IROp::LE:
                emit("slt t1, " + rs2 + ", " + rs1);
                emit("xori " + rd + ", t1, 1");
                break;
            case IROp::GT:  emit("slt " + rd + ", " + rs2 + ", " + rs1); break;
            case IROp::GE:
                emit("slt t1, " + rs1 + ", " + rs2);
                emit("xori " + rd + ", t1, 1");
                break;
            case IROp::EQ:
                emit("sub " + rd + ", " + rs1 + ", " + rs2);
                emit("seqz " + rd + ", " + rd);
                break;
            case IROp::NE:
                emit("sub " + rd + ", " + rs1 + ", " + rs2);
                emit("snez " + rd + ", " + rd);
                break;
            default: break;
        }
        if (!destInReg) {
            storeResult(instr.dest.value, rd);
        }
        break;
    }

    case IROp::NEG: {
        std::string s = getSrcReg(instr.src1.value);
        emit("neg t0, " + s);
        storeResult(instr.dest.value, "t0");
        break;
    }

    case IROp::NOT: {
        std::string s = getSrcReg(instr.src1.value);
        emit("seqz t0, " + s);
        storeResult(instr.dest.value, "t0");
        break;
    }

    case IROp::LOAD: {
        auto it = regAlloc.find(instr.dest.value);
        std::string destReg = "t0";
        if (it != regAlloc.end() && it->second.kind == RegAlloc::Allocation::REG) {
            destReg = RegAlloc::REG_NAMES[it->second.physReg];
        }
        if (instr.src1.kind == IROperand::IMM) {
            emit("lw " + destReg + ", " + std::to_string(instr.src1.value) + "(fp)");
        } else {
            emit("auipc " + destReg + ", %pcrel_hi(" + instr.src1.name + ")");
            emit("lw " + destReg + ", %pcrel_lo(" + instr.src1.name + ")(" + destReg + ")");
        }
        if (destReg == "t0") {
            storeResult(instr.dest.value, "t0");
        }
        break;
    }

    case IROp::STORE: {
        std::string s = getSrcReg(instr.src1.value);
        if (instr.src2.kind == IROperand::IMM) {
            emit("sw " + s + ", " + std::to_string(instr.src2.value) + "(fp)");
        } else {
            emit("auipc t1, %pcrel_hi(" + instr.src2.name + ")");
            emit("sw " + s + ", %pcrel_lo(" + instr.src2.name + ")(t1)");
        }
        break;
    }

    case IROp::LABEL:
        emitLabel(getAsmLabel(instr.src1.value));
        break;

    case IROp::JUMP:
        emit("j " + getAsmLabel(instr.src1.value));
        break;

    case IROp::BR: {
        std::string cond = getSrcReg(instr.src1.value);
        emit("beqz " + cond + ", " + getAsmLabel(instr.src2.value));
        break;
    }

    case IROp::PARAM: {
        std::string s = getSrcReg(instr.src1.value);
        if (paramIdx < 8) {
            emit("mv a" + std::to_string(paramIdx) + ", " + s);
        } else {
            emit("addi sp, sp, -4");
            emit("sw " + s + ", 0(sp)");
        }
        paramIdx++;
        break;
    }

    case IROp::CALL:
        emit("call " + instr.src1.name);
        if (instr.dest.kind == IROperand::VREG) {
            storeResult(instr.dest.value, "a0");
        }
        {
            int extraParams = instr.src2.value - 8;
            if (extraParams > 0) {
                emit("addi sp, sp, " + std::to_string(extraParams * 4));
            }
        }
        paramIdx = 0;
        break;

    case IROp::RET:
        if (instr.src1.kind == IROperand::VREG && instr.src1.value >= 0) {
            std::string s = getSrcReg(instr.src1.value);
            emit("mv a0, " + s);
        }
        generateEpilogue();
        break;
    }
}

std::string RISCVCodeGenerator::getAsmLabel(int irLabelId) {
    return irLabels[irLabelId];
}

// ============================
// 旧的 AST Visitor 方法 (保留)
// ============================

bool RISCVCodeGenerator::optimizeConstantFolding(BinaryExpression& node) {
    if (!optimizationsEnabled) return false;

    // 完全常量折叠：两个操作数都是常量
    if (isConstantExpression(node.left.get()) && isConstantExpression(node.right.get())) {
        int leftVal = evaluateConstantExpression(node.left.get());
        int rightVal = evaluateConstantExpression(node.right.get());
        int result = 0;

        switch (node.op) {
            case BinaryExpression::ADD: result = leftVal + rightVal; break;
            case BinaryExpression::SUB: result = leftVal - rightVal; break;
            case BinaryExpression::MUL: result = leftVal * rightVal; break;
            case BinaryExpression::DIV:
                if (rightVal != 0) result = leftVal / rightVal;
                else return false;
                break;
            case BinaryExpression::MOD:
                if (rightVal != 0) result = leftVal % rightVal;
                else return false;
                break;
            default: return false;
        }

        emit("li t0, " + std::to_string(result));
        emit("addi sp, sp, -4");
        emit("sw t0, 0(sp)");
        return true;
    }

    // 代数简化：右操作数为常量
    if (isConstantExpression(node.right.get())) {
        int rightVal = evaluateConstantExpression(node.right.get());

        if (node.op == BinaryExpression::ADD && rightVal == 0) {
            node.left->accept(*this);
            return true;
        }
        if (node.op == BinaryExpression::SUB && rightVal == 0) {
            node.left->accept(*this);
            return true;
        }
        if (node.op == BinaryExpression::MUL && rightVal == 1) {
            node.left->accept(*this);
            return true;
        }
        if (node.op == BinaryExpression::MUL && rightVal == 0) {
            emit("li t0, 0");
            emit("addi sp, sp, -4");
            emit("sw t0, 0(sp)");
            return true;
        }
        if (node.op == BinaryExpression::DIV && rightVal == 1) {
            node.left->accept(*this);
            return true;
        }
    }

    // 代数简化：左操作数为常量
    if (isConstantExpression(node.left.get())) {
        int leftVal = evaluateConstantExpression(node.left.get());

        if (node.op == BinaryExpression::ADD && leftVal == 0) {
            node.right->accept(*this);
            return true;
        }
        if (node.op == BinaryExpression::MUL && leftVal == 0) {
            emit("li t0, 0");
            emit("addi sp, sp, -4");
            emit("sw t0, 0(sp)");
            return true;
        }
        if (node.op == BinaryExpression::MUL && leftVal == 1) {
            node.right->accept(*this);
            return true;
        }
    }

    return false;
}

bool RISCVCodeGenerator::isConstantExpression(Expression* expr) {
    if (!expr) return false;
    
    // 检查是否是数字字面量
    if (dynamic_cast<NumberLiteral*>(expr)) {
        return true;
    }
    
    // 检查是否是已知常量变量
    if (auto ident = dynamic_cast<Identifier*>(expr)) {
        return constantValues.find(ident->name) != constantValues.end();
    }
    
    return false;
}

int RISCVCodeGenerator::evaluateConstantExpression(Expression* expr) {
    if (auto numLit = dynamic_cast<NumberLiteral*>(expr)) {
        return numLit->value;
    }
    
    if (auto ident = dynamic_cast<Identifier*>(expr)) {
        auto it = constantValues.find(ident->name);
        if (it != constantValues.end()) {
            return it->second;
        }
    }
    
    return 0;
}

void RISCVCodeGenerator::visit(BinaryExpression& node) {
    // 尝试优化，成功则跳过常规代码生成
    if (optimizeConstantFolding(node)) {
        return;
    }

    // 常规代码生成逻辑
    node.left->accept(*this);
    node.right->accept(*this);
    
    emit("lw t1, 0(sp)");    // 加载右操作数到t1
    emit("addi sp, sp, 4");
    emit("lw t0, 0(sp)");    // 加载左操作数到t0
    
    switch (node.op) {
        case BinaryExpression::ADD:
            emit("add t0, t0, t1");
            break;
        case BinaryExpression::SUB:
            emit("sub t0, t0, t1");
            break;
        case BinaryExpression::MUL:
            emit("mul t0, t0, t1");
            break;
        case BinaryExpression::DIV:
            // 处理除法，确保结果符合C语言标准
            emit("div t0, t0, t1");
            break;
        case BinaryExpression::MOD:
            emit("rem t0, t0, t1");
            break;
        case BinaryExpression::LT:
            emit("slt t0, t0, t1");
            break;
        case BinaryExpression::LE:
            emit("slt t2, t1, t0");
            emit("xori t0, t2, 1");
            break;
        case BinaryExpression::GT:
            emit("slt t0, t1, t0");
            break;
        case BinaryExpression::GE:
            emit("slt t2, t0, t1");
            emit("xori t0, t2, 1");
            break;
        case BinaryExpression::EQ:
            emit("sub t0, t0, t1");
            emit("seqz t0, t0");
            break;
        case BinaryExpression::NE:
            emit("sub t0, t0, t1");
            emit("snez t0, t0");
            break;
        case BinaryExpression::AND:
            emit("and t0, t0, t1");
            break;
        case BinaryExpression::OR:
            emit("or t0, t0, t1");
            break;
    }
    
    emit("sw t0, 0(sp)");
}

void RISCVCodeGenerator::emit(const std::string& instruction) {
    output += instruction + "\n";
}

void RISCVCodeGenerator::emitLabel(const std::string& label) {
    output += label + ":\n";
}

std::string RISCVCodeGenerator::newLabel(const std::string& prefix) {
    return prefix + std::to_string(labelCounter++);
}

void RISCVCodeGenerator::generatePrologue(const std::string& funcName, int frameSize) {
    emitLabel(funcName);
    emit("addi sp, sp, -16");
    emit("sw ra, 12(sp)");
    emit("sw fp, 8(sp)");
    emit("addi fp, sp, 16");
    if (frameSize > 16) {
        emit("addi sp, sp, -" + std::to_string(frameSize - 16));
    }
}

void RISCVCodeGenerator::generateEpilogue() {
    emit("addi sp, fp, -16");
    emit("lw ra, 12(sp)");
    emit("lw fp, 8(sp)");
    emit("addi sp, sp, 16");
    emit("ret");
}

bool RISCVCodeGenerator::evaluateCondition(Expression* expr) {
    if (!expr) return false;
    if (auto numLit = dynamic_cast<NumberLiteral*>(expr)) {
        return numLit->value != 0;
    }
    return false;
}

int RISCVCodeGenerator::calculateTotalLocalVariables(Statement* stmt) {
    if (!stmt) return 0;
    
    int count = 0;
    
    // 如果是变量声明
    if (dynamic_cast<VariableDeclaration*>(stmt)) {
        count = 1;
    }
    // 如果是代码块
    else if (auto block = dynamic_cast<Block*>(stmt)) {
        for (const auto& blockStmt : block->statements) {
            count += calculateTotalLocalVariables(blockStmt.get());
        }
    }
    // 如果是if语句
    else if (auto ifStmt = dynamic_cast<IfStatement*>(stmt)) {
        count += calculateTotalLocalVariables(ifStmt->thenStatement.get());
        if (ifStmt->elseStatement) {
            count += calculateTotalLocalVariables(ifStmt->elseStatement.get());
        }
    }
    // 如果是while语句
    else if (auto whileStmt = dynamic_cast<WhileStatement*>(stmt)) {
        count += calculateTotalLocalVariables(whileStmt->body.get());
    }
    
    return count;
}

// 动态计算栈空间需求
int RISCVCodeGenerator::calculateDynamicStackSpace(int paramCount, int localVarCount) {
    // 基础栈空间：保存的寄存器 + 参数存储 + 局部变量
    int baseSpace = 16;  // ra(12) + fp(4)
    
    // 参数存储空间
    int regParamSpace = std::min(paramCount, 8) * 4;  // 前8个参数存储
    int stackParamSpace = std::max(0, paramCount - 8) * 4;  // 栈参数存储
    
    // 局部变量空间
    int localVarSpace = localVarCount * 4;
    
    // 动态表达式计算空间
    int expressionSpace = 320;  // 基础表达式空间
    
    // 根据参数数量调整表达式空间
    if (paramCount > 16) {
        expressionSpace += (paramCount - 16) * 4;  // 每个额外参数增加4字节
    }
    if (paramCount > 32) {
        expressionSpace += (paramCount - 32) * 2;  // 更多参数时额外增加2字节
    }
    
    // 根据局部变量数量调整表达式空间
    if (localVarCount > 20) {
        expressionSpace += (localVarCount - 20) * 2;  // 每个额外变量增加2字节
    }
    if (localVarCount > 50) {
        expressionSpace += (localVarCount - 50) * 1;  // 更多变量时额外增加1字节
    }
    
    // 确保最小空间
    expressionSpace = std::max(expressionSpace, 512);
    
    // 计算总空间
    int totalSpace = baseSpace + regParamSpace + stackParamSpace + localVarSpace + expressionSpace;
    
    // 确保16字节对齐
    return (totalSpace + 15) & ~15;
}

// Visitor 方法实现
void RISCVCodeGenerator::visit(UnaryExpression& node) {
    node.operand->accept(*this);
    emit("lw t0, 0(sp)");
    
    switch (node.op) {
        case UnaryExpression::PLUS:
            // 正号不需要操作
            break;
        case UnaryExpression::MINUS:
            emit("neg t0, t0");
            break;
        case UnaryExpression::NOT:
            emit("seqz t0, t0");
            break;
    }
    
    emit("sw t0, 0(sp)");
}

void RISCVCodeGenerator::visit(NumberLiteral& node) {
    emit("li t0, " + std::to_string(node.value));
    emit("addi sp, sp, -4");
    emit("sw t0, 0(sp)");
}

void RISCVCodeGenerator::visit(Identifier& node) {
    // 查找变量在栈中的位置
    auto it = localVariables.find(node.name);
    if (it != localVariables.end()) {
        // 使用相对于 fp 的偏移量访问局部变量
        emit("lw t0, " + std::to_string(it->second) + "(fp)");
        emit("addi sp, sp, -4");
        emit("sw t0, 0(sp)");
    } else {
        // 全局变量或未定义变量 - 使用PC相对寻址（更符合GCC标准）
        emit("auipc t0, %pcrel_hi(" + node.name + ")");
        emit("lw t0, %pcrel_lo(" + node.name + ")(t0)");
        emit("addi sp, sp, -4");
        emit("sw t0, 0(sp)");
    }
}

void RISCVCodeGenerator::visit(FunctionCall& node) {
    // 生成函数调用 - 遵循 RISC-V 调用约定
    // 参数通过 a0-a7 寄存器传递，多余的通过栈传递
    
    int argCount = node.arguments.size();
    int stackArgs = 0;
    
    // 处理参数
    for (int i = 0; i < argCount; i++) {
        node.arguments[i]->accept(*this);
        emit("lw t0, 0(sp)");
        emit("addi sp, sp, 4");
        
        if (i < 8) {
            // 前8个参数使用寄存器 a0-a7
            emit("mv a" + std::to_string(i) + ", t0");
        } else {
            // 额外的参数通过栈传递
            emit("addi sp, sp, -4");
            emit("sw t0, 0(sp)");
            stackArgs++;
        }
    }
    
    // 调用函数
    emit("call " + node.functionName);
    
    // 清理栈参数
    if (stackArgs > 0) {
        emit("addi sp, sp, " + std::to_string(stackArgs * 4));
    }
    
    // 保存返回值
    emit("addi sp, sp, -4");
    emit("sw a0, 0(sp)");
}

void RISCVCodeGenerator::visit(AssignmentStatement& node) {
    node.value->accept(*this);
    emit("lw t0, 0(sp)");
    emit("addi sp, sp, 4");
    
    auto it = localVariables.find(node.variable);
    if (it != localVariables.end()) {
        // 局部变量赋值
        emit("sw t0, " + std::to_string(it->second) + "(fp)");
    } else {
        // 全局变量赋值 - 使用PC相对寻址（更符合GCC标准）
        emit("auipc t1, %pcrel_hi(" + node.variable + ")");
        emit("sw t0, %pcrel_lo(" + node.variable + ")(t1)");
    }
}

void RISCVCodeGenerator::visit(VariableDeclaration& node) {
    if (node.initializer) {
        node.initializer->accept(*this);
        emit("lw t0, 0(sp)");
        emit("addi sp, sp, 4");
    } else {
        emit("li t0, 0");
    }
    
    stackOffset -= 4;
    localVariables[node.name] = stackOffset;
    emit("sw t0, " + std::to_string(stackOffset) + "(fp)");
}

void RISCVCodeGenerator::visit(Block& node) {
    // 保存当前作用域
    scopeStack.push(localVariables);
    
    // 处理块中的语句
    for (const auto& stmt : node.statements) {
        stmt->accept(*this);
    }
    
    // 恢复外层作用域
    localVariables = scopeStack.top();
    scopeStack.pop();
}

void RISCVCodeGenerator::visit(IfStatement& node) {
    // 死代码消除：常量条件
    if (optimizationsEnabled) {
        if (auto* numLit = dynamic_cast<NumberLiteral*>(node.condition.get())) {
            if (numLit->value != 0) {
                node.thenStatement->accept(*this);
                return;
            } else if (node.elseStatement) {
                node.elseStatement->accept(*this);
                return;
            } else {
                return; // if (0) {} → 什么都不生成
            }
        }
    }

    std::string elseLabel = newLabel("else");
    std::string endLabel = newLabel("endif");

    node.condition->accept(*this);
    emit("lw t0, 0(sp)");
    emit("addi sp, sp, 4");
    emit("beqz t0, " + elseLabel);

    node.thenStatement->accept(*this);
    emit("j " + endLabel);

    emitLabel(elseLabel);
    if (node.elseStatement) {
        node.elseStatement->accept(*this);
    }

    emitLabel(endLabel);
}

void RISCVCodeGenerator::visit(WhileStatement& node) {
    // 死代码消除：while (0) { ... }
    if (optimizationsEnabled) {
        if (auto* numLit = dynamic_cast<NumberLiteral*>(node.condition.get())) {
            if (numLit->value == 0) {
                return; // while (0) 整个循环体被消除
            }
        }
    }

    std::string loopLabel = newLabel("loop");
    std::string endLabel = newLabel("endloop");

    breakLabels.push(endLabel);
    continueLabels.push(loopLabel);

    emitLabel(loopLabel);

    node.condition->accept(*this);
    emit("lw t0, 0(sp)");
    emit("addi sp, sp, 4");
    emit("beqz t0, " + endLabel);

    node.body->accept(*this);
    emit("j " + loopLabel);

    emitLabel(endLabel);

    breakLabels.pop();
    continueLabels.pop();
}

void RISCVCodeGenerator::visit(BreakStatement& node) {
    (void)node; // 避免未使用参数警告
    // 简单的 break 实现，跳转到循环结束标签
    if (!breakLabels.empty()) {
        emit("j " + breakLabels.top());
    } else {
        // 如果没有 break 标签，生成一个错误
        emit("# ERROR: break statement outside of loop");
    }
}

void RISCVCodeGenerator::visit(ContinueStatement& node) {
    (void)node; // 避免未使用参数警告
    // 简单的 continue 实现，跳转到循环开始标签
    if (!continueLabels.empty()) {
        emit("j " + continueLabels.top());
    } else {
        // 如果没有 continue 标签，生成一个错误
        emit("# ERROR: continue statement outside of loop");
    }
}

void RISCVCodeGenerator::visit(ReturnStatement& node) {
    if (node.value) {
        // 计算返回值
        node.value->accept(*this);
        emit("lw a0, 0(sp)");
        emit("addi sp, sp, 4");
    } else {
        // void函数不需要设置返回值
        // 不设置a0寄存器
    }
    
    // 生成函数尾声
    generateEpilogue();
}

void RISCVCodeGenerator::visit(ExpressionStatement& node) {
    node.expression->accept(*this);
    emit("addi sp, sp, 4"); // 弹出表达式结果
}

void RISCVCodeGenerator::visit(FunctionDefinition& node) {
    currentFunction = node.name;
    localVariables.clear();
    stackOffset = -20; // 从 -20 开始，为 ra(12), fp(8) 预留空间
    
    // 计算所有作用域中的局部变量数量
    int totalLocalVarCount = calculateTotalLocalVariables(node.body.get());
    
    // 使用动态栈空间计算
    int localSize = calculateDynamicStackSpace(node.parameters.size(), totalLocalVarCount);
    // 确保 16 字节对齐
    localSize = (localSize + 15) & ~15;
    
    generatePrologue(node.name, localSize);
    
    // 处理函数参数 - 将寄存器参数和栈参数存储到栈中
    for (size_t i = 0; i < node.parameters.size(); i++) {
        std::string paramName = node.parameters[i].name;
        stackOffset -= 4;
        localVariables[paramName] = stackOffset;
        
        if (i < 8) {
            // 前8个参数通过寄存器 a0-a7 传递
            emit("sw a" + std::to_string(i) + ", " + std::to_string(stackOffset) + "(fp)");
        } else {
            // 额外的参数通过栈传递
            // 栈参数在当前栈帧的顶部，相对于fp的位置
            // 栈参数在fp+16, fp+20, fp+24, ... (相对于当前fp)
            int stackParamOffset = 16 + (i - 8) * 4;
            emit("lw t0, " + std::to_string(stackParamOffset) + "(fp)");
            emit("sw t0, " + std::to_string(stackOffset) + "(fp)");
        }
    }
    
    // 正常的函数体处理
    node.body->accept(*this);
    
    // 如果函数没有显式的return语句，添加隐式返回
    // 对于void函数，不需要设置返回值
    if (node.returnType == Expression::VOID) {
        generateEpilogue();
    }
}

void RISCVCodeGenerator::visit(CompilationUnit& node) {
    for (const auto& func : node.functions) {
        func->accept(*this);
    }
}