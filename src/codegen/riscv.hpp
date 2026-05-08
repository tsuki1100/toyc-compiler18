#pragma once
#include "ast/ast.hpp"
#include "common/types.hpp"
#include "ir/ir.hpp"
#include "ir/reg_alloc.hpp"
#include <string>
#include <unordered_map>
#include <vector>
#include <stack>

class RISCVCodeGenerator : public Visitor {
private:
    std::string output;
    std::unordered_map<std::string, int> localVariables;
    std::unordered_map<std::string, FunctionInfo> functions;
    int stackOffset;
    int labelCounter;
    std::string currentFunction;

    // 优化相关
    bool optimizationsEnabled;
    std::unordered_map<std::string, int> constantValues;

    // 标签栈管理
    std::stack<std::string> breakLabels;
    std::stack<std::string> continueLabels;

    // 作用域管理
    std::stack<std::unordered_map<std::string, int>> scopeStack;

    // 寄存器分配结果
    std::unordered_map<int, RegAlloc::Allocation> regAlloc;

    // IR 代码生成
    std::unordered_map<int, int> vregSlots;    // vreg → 栈偏移 (spilled only)
    std::unordered_map<int, std::string> irLabels; // IR label id → asm label
    int vregSlotOffset = 0;
    int paramIdx = 0;

    void generateFunctionFromIR(const IRFunction& irFunc);
    void emitIRInstruction(const IRInstr& instr);

    // 寄存器感知的加载/存储
    std::string getSrcReg(int vreg);
    void storeResult(int vreg, const std::string& reg);
    std::string getAsmLabel(int irLabelId);

public:
    RISCVCodeGenerator() : stackOffset(0), labelCounter(0), optimizationsEnabled(false) {}

    std::string generate(CompilationUnit& unit, const std::unordered_map<std::string, FunctionInfo>& funcTable);

    void enableOptimizations() { optimizationsEnabled = true; }

    // Visitor接口 (保留用于兼容)
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
    void emit(const std::string& instruction);
    void emitLabel(const std::string& label);
    std::string newLabel(const std::string& prefix = "L");
    void generatePrologue(const std::string& funcName, int localSize);
    void generateEpilogue();

    bool optimizeConstantFolding(BinaryExpression& node);
    bool evaluateCondition(Expression* expr);
    bool isConstantExpression(Expression* expr);
    int evaluateConstantExpression(Expression* expr);

    int calculateTotalLocalVariables(Statement* stmt);
    int calculateDynamicStackSpace(int paramCount, int localVarCount);
};