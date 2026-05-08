#pragma once
#include "ast/ast.hpp"
#include "ir/ir.hpp"
#include <string>
#include <vector>
#include <unordered_map>
#include <stack>

class IRBuilder : public Visitor {
public:
    IRBuilder(bool enableOpt = false) : optimize(enableOpt) {}

    std::vector<IRFunction> build(CompilationUnit& unit);

    // Visitor 接口
    void visit(BinaryExpression& node) override;
    void visit(UnaryExpression& node) override;
    void visit(NumberLiteral& node) override;
    void visit(Identifier& node) override;
    void visit(FunctionCall& node) override;
    void visit(AssignmentStatement& node) override;
    void visit(VariableDeclaration& node) override;
    void visit(Block& node) override;
    void visit(IfStatement& node) override;
    void visit(WhileStatement& node) override;
    void visit(BreakStatement& node) override;
    void visit(ContinueStatement& node) override;
    void visit(ReturnStatement& node) override;
    void visit(ExpressionStatement& node) override;
    void visit(FunctionDefinition& node) override;
    void visit(CompilationUnit& node) override;

private:
    // 写入一条 IR 指令，返回 dest vreg (新分配或传入的)
    int emit(IROp op, int destVReg = -1, IROperand src1 = IROperand(), IROperand src2 = IROperand());

    // 当前 IR 函数
    IRFunction* currentFunc = nullptr;
    std::vector<IRFunction> functions;
    bool optimize;

    // 局部变量 → 栈偏移映射 (延续之前的简单方案)
    std::unordered_map<std::string, int> localVars;
    int stackOffset = 0;
    std::stack<std::unordered_map<std::string, int>> scopeStack;

    // 控制流标签栈
    std::stack<int> breakLabels;
    std::stack<int> continueLabels;
    std::string currentFuncName;

    // 符号类型表 (从语义分析)
    std::unordered_map<std::string, bool> funcIsVoid; // name → isVoid
};
