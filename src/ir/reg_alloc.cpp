#include "ir/reg_alloc.hpp"
#include <algorithm>
#include <set>

const char* RegAlloc::REG_NAMES[] = {"t0", "t1", "t2", "t3", "t4", "t5", "t6"};

std::vector<RegAlloc::LiveInterval> RegAlloc::computeLiveIntervals(const IRFunction& func) {
    std::unordered_map<int, LiveInterval> intervals;

    for (size_t i = 0; i < func.instructions.size(); i++) {
        const auto& instr = func.instructions[i];

        auto id = [](int idx) -> int { return idx; };

        auto track = [&](const IROperand& op) {
            if (op.kind != IROperand::VREG || op.value < 0) return;
            int v = op.value;
            if (intervals.find(v) == intervals.end()) {
                intervals[v] = {(int)v, (int)i, (int)i};
            } else {
                intervals[v].end = std::max(intervals[v].end, (int)i);
            }
        };

        track(instr.dest);
        track(instr.src1);
        track(instr.src2);
    }

    std::vector<LiveInterval> result;
    for (auto& [vreg, iv] : intervals) {
        result.push_back(iv);
    }
    return result;
}

std::unordered_map<int, RegAlloc::Allocation> RegAlloc::allocate(const IRFunction& func) {
    auto intervals = computeLiveIntervals(func);

    // 按 start 排序
    std::sort(intervals.begin(), intervals.end(),
              [](const LiveInterval& a, const LiveInterval& b) { return a.start < b.start; });

    std::unordered_map<int, Allocation> result;

    // active[i] = vreg 当前占用物理寄存器 i
    std::vector<int> activeReg(NUM_REGS, -1); // -1 = 空闲
    // 每个 vreg 的活跃区间，用于溢出决策
    std::unordered_map<int, int> activeEnd; // vreg → end position

    int spillSlotOffset = -20; // 溢出槽偏移起点

    for (const auto& interval : intervals) {
        // 1. 过期处理: 移除已结束的 active 区间
        for (int r = 0; r < NUM_REGS; r++) {
            int v = activeReg[r];
            if (v >= 0 && activeEnd[v] < interval.start) {
                activeReg[r] = -1;
                activeEnd.erase(v);
            }
        }

        // 2. 查找空闲寄存器
        int freeReg = -1;
        for (int r = 0; r < NUM_REGS; r++) {
            if (activeReg[r] == -1) {
                freeReg = r;
                break;
            }
        }

        if (freeReg >= 0) {
            // 有空闲寄存器
            Allocation alloc;
            alloc.kind = Allocation::REG;
            alloc.physReg = freeReg;
            alloc.stackSlot = 0;
            result[interval.vreg] = alloc;
            activeReg[freeReg] = interval.vreg;
            activeEnd[interval.vreg] = interval.end;
        } else {
            // 无空闲寄存器 → 溢出最远的
            int spillReg = 0;
            int farthestEnd = activeEnd[activeReg[0]];
            for (int r = 1; r < NUM_REGS; r++) {
                int v = activeReg[r];
                if (activeEnd[v] > farthestEnd) {
                    farthestEnd = activeEnd[v];
                    spillReg = r;
                }
            }

            int spilledVReg = activeReg[spillReg];

            if (activeEnd[spilledVReg] > interval.end) {
                // 当前活跃的最远 > 新 interval → 溢出老的, 当前获得寄存器
                Allocation spillAlloc;
                spillAlloc.kind = Allocation::SPILL;
                spillAlloc.physReg = -1;
                spillAlloc.stackSlot = spillSlotOffset;
                spillSlotOffset -= 4;
                result[spilledVReg] = spillAlloc;

                activeEnd.erase(spilledVReg);

                Allocation alloc;
                alloc.kind = Allocation::REG;
                alloc.physReg = spillReg;
                alloc.stackSlot = 0;
                result[interval.vreg] = alloc;
                activeReg[spillReg] = interval.vreg;
                activeEnd[interval.vreg] = interval.end;
            } else {
                // 当前 interval 被溢出
                Allocation spillAlloc;
                spillAlloc.kind = Allocation::SPILL;
                spillAlloc.physReg = -1;
                spillAlloc.stackSlot = spillSlotOffset;
                spillSlotOffset -= 4;
                result[interval.vreg] = spillAlloc;
            }
        }
    }

    return result;
}
