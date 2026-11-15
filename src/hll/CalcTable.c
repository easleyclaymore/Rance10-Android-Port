/* 版权所有 (C) 2025 kichikuou <KichikuouChrome@gmail.com>
 *
 * 本程序是自由软件：你可以依据自由软件基金会发布的 GNU 通用公共许可证
 * （第 2 版，或你选择的任何更高版本）的条款，重新分发和/或修改本程序。
 *
 * 分发本程序是希望它有用，但不作任何担保，甚至不担保适销性或
 * 适用于特定用途。详见 GNU 通用公共许可证。
 *
 * 你应当已经随本程序收到一份 GNU 通用公共许可证的副本；
 * 如果没有，见 <http://gnu.org/licenses/>。
 */

#include <string.h>
#include "hll.h"

#define REG_COUNT 32

static int64_t regs[REG_COUNT];

#define REG_CHECK(reg) ((reg) >= 0 && (reg) < REG_COUNT)

static int CalcTable_Get(int reg)
{
	if (reg < 0 || reg >= REG_COUNT)
		return 0;
	if (regs[reg] > INT32_MAX)
		return INT32_MAX;
	if (regs[reg] < INT32_MIN)
		return INT32_MIN;
	return (int)regs[reg];
}

static void CalcTable_Init(bool clear)
{
	if (clear) {
		memset(regs, 0, sizeof(regs));
	}
}

static bool CalcTable_Set(int regR, int num)
{
	if (!REG_CHECK(regR))
		return false;
	regs[regR] = num;
	return true;
}

static bool CalcTable_Copy(int regR, int regA)
{
	if (!REG_CHECK(regR) || !REG_CHECK(regA))
		return false;
	regs[regR] = regs[regA];
	return true;
}

static bool CalcTable_Add(int regR, int regA, int regB)
{
	if (!REG_CHECK(regR) || !REG_CHECK(regA) || !REG_CHECK(regB))
		return false;
	regs[regR] = regs[regA] + regs[regB];
	return true;
}

static bool CalcTable_Sub(int regR, int regA, int regB)
{
	if (!REG_CHECK(regR) || !REG_CHECK(regA) || !REG_CHECK(regB))
		return false;
	regs[regR] = regs[regA] - regs[regB];
	return true;
}

static bool CalcTable_Mul(int regR, int regA, int regB)
{
	if (!REG_CHECK(regR) || !REG_CHECK(regA) || !REG_CHECK(regB))
		return false;
	regs[regR] = regs[regA] * regs[regB];
	return true;
}

static bool CalcTable_Div(int regR, int regA, int regB)
{
	if (!REG_CHECK(regR) || !REG_CHECK(regA) || !REG_CHECK(regB))
		return false;
	regs[regR] = regs[regA] / regs[regB];  // NOTE: No check for division by zero
	return true;
}

static bool CalcTable_And(int regR, int regA, int regB)
{
	if (!REG_CHECK(regR) || !REG_CHECK(regA) || !REG_CHECK(regB))
		return false;
	regs[regR] = regs[regA] & regs[regB];
	return true;
}

static bool CalcTable_Or(int regR, int regA, int regB)
{
	if (!REG_CHECK(regR) || !REG_CHECK(regA) || !REG_CHECK(regB))
		return false;
	regs[regR] = regs[regA] | regs[regB];
	return true;
}

static bool CalcTable_Xor(int regR, int regA, int regB)
{
	if (!REG_CHECK(regR) || !REG_CHECK(regA) || !REG_CHECK(regB))
		return false;
	regs[regR] = regs[regA] ^ regs[regB];
	return true;
}

static bool CalcTable_ShiftL(int regR, int regA, int regB)
{
	if (!REG_CHECK(regR) || !REG_CHECK(regA) || !REG_CHECK(regB))
		return false;
	regs[regR] = regs[regA] << regs[regB];
	return true;
}

static bool CalcTable_ShiftR(int regR, int regA, int regB)
{
	if (!REG_CHECK(regR) || !REG_CHECK(regA) || !REG_CHECK(regB))
		return false;
	regs[regR] = regs[regA] >> regs[regB];
	return true;
}

static bool CalcTable_Percent(int regR, int regA, int regB, bool rounding)
{
	if (!REG_CHECK(regR) || !REG_CHECK(regA) || !REG_CHECK(regB))
		return false;
	int64_t v = regs[regA] * regs[regB];
	int64_t q = v / 100;
	int64_t r = v % 100;
	regs[regR] = q + (rounding && r != 0 ? 1 : 0);
	return true;
}

static void CalcTable_Run(int loop)
{
	if (loop != 1) {
		VM_ERROR("Unsupported loop count: %d", loop);
	}
	// 在这个实现里这个函数是空操作。
}

HLL_LIBRARY(CalcTable,
	    HLL_EXPORT(Get, CalcTable_Get),
	    HLL_EXPORT(Init, CalcTable_Init),
	    HLL_EXPORT(Set, CalcTable_Set),
	    HLL_EXPORT(Copy, CalcTable_Copy),
	    HLL_EXPORT(Add, CalcTable_Add),
	    HLL_EXPORT(Sub, CalcTable_Sub),
	    HLL_EXPORT(Mul, CalcTable_Mul),
	    HLL_EXPORT(Div, CalcTable_Div),
	    HLL_EXPORT(And, CalcTable_And),
	    HLL_EXPORT(Or, CalcTable_Or),
	    HLL_EXPORT(Xor, CalcTable_Xor),
	    HLL_EXPORT(ShiftL, CalcTable_ShiftL),
	    HLL_EXPORT(ShiftR, CalcTable_ShiftR),
	    HLL_EXPORT(Percent, CalcTable_Percent),
	    HLL_EXPORT(Run, CalcTable_Run)
	    );
