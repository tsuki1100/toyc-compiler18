#include "ir/ir_builder.hpp"
#include <stdexcept>

// 辅助：返回最后计算出的 vreg
static thread_local int resultVReg;

int IRBuilder::emit(IROp op, int destVReg, IROperand src1, IROperand src2) {
    if (!currentFunc) return -1;

    IRInstr instr;
    instr.op = op;

    if (destVReg >= 0) {
        instr.dest = IROperand::vreg(destVReg);
    } else {
        instr.dest = IROperand::vreg(currentFunc->newVReg());
    }
    instr.src1 = src1;
    instr.src2 = src2;

    currentFunc->instructions.push_back(instr);
    return instr.dest.value;
}

// === 表达式 ===

void IRBuilder::visit(BinaryExpression& node) {
    node.left->accept(*this);
    int leftVReg = resultVReg;
    node.right->accept(*this);
    int rightVReg = resultVReg;

    IROp op;
    switch (node.op) {
        case BinaryExpression::ADD: op = IROp::ADD; break;
        case BinaryExpression::SUB: op = IROp::SUB; break;
        case BinaryExpression::MUL: op = IROp::MUL; break;
        case BinaryExpression::DIV: op = IROp::DIV; break;
        case BinaryExpression::MOD: op = IROp::MOD; break;
        case BinaryExpression::LT:  op = IROp::LT;  break;
        case BinaryExpression::LE:  op = IROp::LE;  break;
        case BinaryExpression::GT:  op = IROp::GT;  break;
        case BinaryExpression::GE:  op = IROp::GE;  break;
        case BinaryExpression::EQ:  op = IROp::EQ;  break;
        case BinaryExpression::NE:  op = IROp::NE;  break;
        case BinaryExpression::AND: op = IROp::AND; break;
        case BinaryExpression::OR:  op = IROp::OR;  break;
        default: throw std::runtime_error("Unknown binary operator");
    }

    resultVReg = emit(op, -1, IROperand::vreg(leftVReg), IROperand::vreg(rightVReg));
}

void IRBuilder::visit(UnaryExpression& node) {
    node.operand->accept(*this);
    int opVReg = resultVReg;

    IROp op;
    switch (node.op) {
        case UnaryExpression::MINUS: op = IROp::NEG; break;
        case UnaryExpression::NOT:   op = IROp::NOT; break;
        case UnaryExpression::PLUS:  resultVReg = opVReg; return; // 无操作
        default: throw std::runtime_error("Unknown unary operator");
    }
    resultVReg = emit(op, -1, IROperand::vreg(opVReg));
}

void IRBuilder::visit(NumberLiteral& node) {
    resultVReg = emit(IROp::LI, -1, IROperand::imm(node.value));
}

void IRBuilder::visit(Identifier& node) {
    auto it = localVars.find(node.name);
    if (it != localVars.end()) {
        // 局部变量: 加载
        resultVReg = emit(IROp::LOAD, -1, IROperand::imm(it->second));
    } else {
        // 全局变量: 用特殊标记
        resultVReg = emit(IROp::LOAD, -1, IROperand::func(node.name));
    }
}

void IRBuilder::visit(FunctionCall& node) {
    int argCount = node.arguments.size();

    // 压入参数
    for (int i = 0; i < argCount; i++) {
        node.arguments[i]->accept(*this);
        emit(IROp::PARAM, -1, IROperand::vreg(resultVReg));
    }

    // 调用
    int retVReg = emit(IROp::CALL, -1, IROperand::func(node.functionName),
                       IROperand::imm(argCount));
    resultVReg = retVReg;
}

// === 语句 ===

void IRBuilder::visit(AssignmentStatement& node) {
    node.value->accept(*this);
    int valVReg = resultVReg;

    auto it = localVars.find(node.variable);
    if (it != localVars.end()) {
        emit(IROp::STORE, -1, IROperand::vreg(valVReg), IROperand::imm(it->second));
    } else {
        emit(IROp::STORE, -1, IROperand::vreg(valVReg), IROperand::func(node.variable));
    }
}

void IRBuilder::visit(VariableDeclaration& node) {
    if (node.initializer) {
        node.initializer->accept(*this);
    } else {
        resultVReg = emit(IROp::LI, -1, IROperand::imm(0));
    }

    stackOffset -= 4;
    localVars[node.name] = stackOffset;
    emit(IROp::STORE, -1, IROperand::vreg(resultVReg), IROperand::imm(stackOffset));
}

void IRBuilder::visit(Block& node) {
    scopeStack.push(localVars);
    for (const auto& stmt : node.statements) {
        stmt->accept(*this);
    }
    localVars = scopeStack.top();
    scopeStack.pop();
}

void IRBuilder::visit(IfStatement& node) {
    int labelElse  = currentFunc->newLabel();
    int labelEnd   = currentFunc->newLabel();

    node.condition->accept(*this);
    // BR cond, L_then_fallthrough, L_else
    // 这里用: 如果 cond==0 跳到 else, 否则继续(then)
    emit(IROp::BR, -1, IROperand::vreg(resultVReg),
         IROperand::label(labelElse));

    node.thenStatement->accept(*this);
    emit(IROp::JUMP, -1, IROperand::label(labelEnd));

    emit(IROp::LABEL, -1, IROperand::label(labelElse));
    if (node.elseStatement) {
        node.elseStatement->accept(*this);
    }
    emit(IROp::LABEL, -1, IROperand::label(labelEnd));
}

void IRBuilder::visit(WhileStatement& node) {
    int labelLoop = currentFunc->newLabel();
    int labelBody = currentFunc->newLabel();
    int labelEnd  = currentFunc->newLabel();

    breakLabels.push(labelEnd);
    continueLabels.push(labelLoop);

    emit(IROp::LABEL, -1, IROperand::label(labelLoop));

    node.condition->accept(*this);
    // BR cond, body, end
    emit(IROp::BR, -1, IROperand::vreg(resultVReg),
         IROperand::label(labelEnd));

    emit(IROp::LABEL, -1, IROperand::label(labelBody));
    node.body->accept(*this);
    emit(IROp::JUMP, -1, IROperand::label(labelLoop));

    emit(IROp::LABEL, -1, IROperand::label(labelEnd));

    breakLabels.pop();
    continueLabels.pop();
}

void IRBuilder::visit(BreakStatement&) {
    if (!breakLabels.empty()) {
        emit(IROp::JUMP, -1, IROperand::label(breakLabels.top()));
    }
}

void IRBuilder::visit(ContinueStatement&) {
    if (!continueLabels.empty()) {
        emit(IROp::JUMP, -1, IROperand::label(continueLabels.top()));
    }
}

void IRBuilder::visit(ReturnStatement& node) {
    if (node.value) {
        node.value->accept(*this);
        emit(IROp::RET, -1, IROperand::vreg(resultVReg));
    } else {
        emit(IROp::RET);
    }
}

void IRBuilder::visit(ExpressionStatement& node) {
    node.expression->accept(*this);
    // 结果被丢弃
}

void IRBuilder::visit(FunctionDefinition& node) {
    currentFunc = new IRFunction();
    currentFunc->name = node.name;
    currentFunc->paramCount = node.parameters.size();
    currentFunc->isVoid = (node.returnType == Expression::VOID);
    currentFuncName = node.name;

    localVars.clear();
    stackOffset = -20;
    scopeStack = std::stack<std::unordered_map<std::string, int>>();

    // 处理参数 → 分配栈偏移
    for (size_t i = 0; i < node.parameters.size(); i++) {
        stackOffset -= 4;
        localVars[node.parameters[i].name] = stackOffset;
        // 参数通过 a0-a7 传入，此处暂不生成 STORE，交给 codegen 处理
    }

    node.body->accept(*this);

    // void 函数末尾隐式返回
    if (node.returnType == Expression::VOID) {
        emit(IROp::RET);
    }

    functions.push_back(*currentFunc);
    delete currentFunc;
    currentFunc = nullptr;
}

void IRBuilder::visit(CompilationUnit& node) {
    for (const auto& func : node.functions) {
        func->accept(*this);
    }
}

std::vector<IRFunction> IRBuilder::build(CompilationUnit& unit) {
    functions.clear();
    unit.accept(*this);
    return functions;
}
