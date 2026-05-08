#include "codegen/riscv.hpp"
#include <iostream>
#include <sstream>

// 在RISCVCodeGenerator类中添加这些方法

void RISCVCodeGenerator::optimizeConstantFolding(BinaryExpression& node) {
    if (!optimizationsEnabled) return;
    
    // 检查是否可以进行常量折叠
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
                else return; // 避免除零
                break;
            case BinaryExpression::MOD: 
                if (rightVal != 0) result = leftVal % rightVal; 
                else return;
                break;
            default: return; // 其他操作不优化
        }
        
        // 直接加载常量结果
        emit("li t0, " + std::to_string(result));
        emit("addi sp, sp, -4");
        emit("sw t0, 0(sp)");
        return;
    }
    
    // 如果不能完全折叠，检查是否有简单优化
    if (isConstantExpression(node.right.get())) {
        int rightVal = evaluateConstantExpression(node.right.get());
        
        // 优化加0、乘1等
        if (node.op == BinaryExpression::ADD && rightVal == 0) {
            node.left->accept(*this);
            return;
        }
        if (node.op == BinaryExpression::MUL && rightVal == 1) {
            node.left->accept(*this);
            return;
        }
        if (node.op == BinaryExpression::MUL && rightVal == 0) {
            emit("li t0, 0");
            emit("addi sp, sp, -4");
            emit("sw t0, 0(sp)");
            return;
        }
    }
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
    // 尝试优化
    if (optimizationsEnabled) {
        optimizeConstantFolding(node);
        return;
    }
    
    // 原有的代码生成逻辑
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

// 添加所有缺失的方法实现
std::string RISCVCodeGenerator::generate(CompilationUnit& unit, const std::unordered_map<std::string, FunctionInfo>& funcTable) {
    output.clear();
    functions = funcTable;
    stackOffset = 0;
    labelCounter = 0;

    

    
    // 生成数据段
    emit(".data");
    emit(".text");
    emit(".global main");
    
    // 访问编译单元
    unit.accept(*this);
    
    return output;
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

void RISCVCodeGenerator::generatePrologue(const std::string& funcName, int localSize) {
    emitLabel(funcName);
    // 保存返回地址和帧指针
    emit("addi sp, sp, -16");  // 分配16字节对齐的栈空间
    emit("sw ra, 12(sp)");     // 保存返回地址
    emit("sw fp, 8(sp)");      // 保存帧指针
    emit("addi fp, sp, 16");   // 设置新的帧指针
    
    // 为局部变量分配额外空间
    if (localSize > 16) {
        emit("addi sp, sp, -" + std::to_string(localSize - 16));
    }
}

void RISCVCodeGenerator::generateEpilogue() {
    // 恢复帧指针和返回地址
    // 首先恢复到保存 ra 和 fp 时的 sp 位置
    emit("addi sp, fp, -16");  // sp = fp - 16，回到保存 ra 和 fp 的位置
    emit("lw ra, 12(sp)");     // 恢复返回地址
    emit("lw fp, 8(sp)");      // 恢复帧指针
    emit("addi sp, sp, 16");   // 恢复栈指针
    emit("ret");
}

void RISCVCodeGenerator::optimizeDeadCodeElimination() {
    // 简单的死代码消除实现
    for (const auto& code : deadCode) {
        (void)code; // 避免未使用变量警告
        // 这里可以实现更复杂的死代码消除逻辑
    }
}

// 计算所有作用域中的局部变量数量
int RISCVCodeGenerator::calculateTotalLocalVariables(Statement* stmt) {
    if (!stmt) return 0;
    
    int count = 0;
    
    // 如果是变量声明
    if (auto varDecl = dynamic_cast<VariableDeclaration*>(stmt)) {
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
    std::string elseLabel = newLabel("else");
    std::string endLabel = newLabel("endif");
    
    // 计算条件表达式
    node.condition->accept(*this);
    emit("lw t0, 0(sp)");
    emit("addi sp, sp, 4");
    
    // 条件跳转 - 使用更高效的指令序列
    emit("beqz t0, " + elseLabel);
    
    // then 分支
    node.thenStatement->accept(*this);
    emit("j " + endLabel);
    
    // else 分支
    emitLabel(elseLabel);
    if (node.elseStatement) {
        node.elseStatement->accept(*this);
    }
    
    emitLabel(endLabel);
}

void RISCVCodeGenerator::visit(WhileStatement& node) {
    std::string loopLabel = newLabel("loop");
    std::string endLabel = newLabel("endloop");
    
    // 压入标签栈，供 break 和 continue 使用
    breakLabels.push(endLabel);
    continueLabels.push(loopLabel);
    
    // 循环开始标签
    emitLabel(loopLabel);
    
    // 计算循环条件
    node.condition->accept(*this);
    emit("lw t0, 0(sp)");
    emit("addi sp, sp, 4");
    
    // 条件为假时跳出循环
    emit("beqz t0, " + endLabel);
    
    // 循环体
    node.body->accept(*this);
    
    // 跳回循环开始
    emit("j " + loopLabel);
    
    // 循环结束标签
    emitLabel(endLabel);
    
    // 弹出标签栈
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
    
    // 计算总栈空间：基础帧(16) + 寄存器参数存储 + 栈参数存储 + 局部变量(4*count) + 表达式计算临时空间
    int regParamSize = std::min((int)node.parameters.size(), 8) * 4;  // 前8个参数存储空间
    int stackParamSize = std::max(0, (int)node.parameters.size() - 8) * 4;  // 栈参数存储空间
    int localSize = 16 + regParamSize + stackParamSize + totalLocalVarCount * 4 + 512;  // 增加512字节用于表达式计算
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