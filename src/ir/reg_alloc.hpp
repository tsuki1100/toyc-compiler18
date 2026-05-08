#pragma once
#include "ir/ir.hpp"
#include <unordered_map>
#include <vector>

class RegAlloc {
public:
    struct Allocation {
        enum Kind { REG, SPILL };
        Kind kind;
        int physReg;   // 物理寄存器编号 (0=t0, 1=t1, ...)
        int stackSlot; // 溢出槽偏移 (仅 SPILL 有效)
    };

    // 分配寄存器，返回 vreg → allocation 映射
    std::unordered_map<int, Allocation> allocate(const IRFunction& func);

    // 可分配寄存器数量
    static constexpr int NUM_REGS = 7;
    static const char* REG_NAMES[];

private:
    // 活跃区间
    struct LiveInterval {
        int vreg;
        int start;   // 第一条定义或使用的指令索引
        int end;     // 最后一条使用的指令索引
    };

    std::vector<LiveInterval> computeLiveIntervals(const IRFunction& func);
};
