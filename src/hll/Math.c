/* 版权所有 (C) 2019 Nunuhara Cabbage <nunuhara@haniwa.technology>
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

#include <stdlib.h>
#include <math.h>
#include <time.h>
#include <cglm/cglm.h>

#ifndef M_PI
#define M_PI (3.14159265358979323846)
#endif

#include "system4/mt19937int.h"

#include "hll.h"
#include "vm/page.h"

static struct mt19937 mt;
static bool mt_initialized;

static uint32_t math_genrand(void)
{
	if (!mt_initialized) {
		mt19937_init(&mt, 4357);
		mt_initialized = true;
	}
	return mt19937_genrand(&mt);
}

struct shuffle_table {
	int id;
	struct shuffle_table *next;
	int *elems;
	int size;
	int i;
};
static struct shuffle_table *shuffle_tables;

static struct shuffle_table *find_shuffle_table(int id)
{
	for (struct shuffle_table *tbl = shuffle_tables; tbl; tbl = tbl->next) {
		if (tbl->id == id)
			return tbl;
	}
	return NULL;
}

static inline float deg2rad(float deg)
{
	return deg * (M_PI / 180.0);
}

static float Math_Cos(float x)
{
	return cosf(deg2rad(x));
}

static float Math_Sin(float x)
{
	return sinf(deg2rad(x));
}

static float Math_Tan(float x)
{
	return tanf(deg2rad(x));
}

static void Math_SetSeed(int seed)
{
	mt19937_init(&mt, (uint32_t)seed | 1);
	mt19937_genrand(&mt);  // the original engine discards the first value
	mt_initialized = true;
}

static void Math_SetSeedByCurrentTime(void)
{
	Math_SetSeed(time(NULL));
}

/* [D284] 同名同 arity、只有数值类型不同的重载，C 侧按「返回类型」分开实现。
 * 名字（Min/Max/Abs/Clamp）必须与 AIN 库表逐字相同，靠 HLL_EXPORT_NRT 的
 * (arity, ret_type) 让链接器唯一选中 —— 不许再造 `MinF` 这种 AIN 里不存在的名字。 */
static int Math_Min(int a, int b)
{
	return a < b ? a : b;
}

static float Math_MinF(float a, float b)
{
	return a < b ? a : b;
}

static int Math_Max(int a, int b)
{
	return a > b ? a : b;
}

/* [D284] AIN 的 `Math.Min`/`Math.Max` 各有 6 个重载：2/3/4 参 × int/float。
 * 导出表原来只有 2 参的 `Min` 和名字根本对不上的 `MinF`，于是：
 *   ① float 版被绑到 int 实现（arity 相同，只按名字+arity 分不开）
 *      → `rangeF(0,1,0.75)` 把 0.75 截断成 0 → `Parts@SetAlpha` 推不动 alpha；
 *   ② 3/4 参版没有对应导出，直接落空桩返回 0。
 * 这里把 6 个重载全部补齐，并用 HLL_EXPORT_NRT 标注 (arity, 返回类型)，
 * 让链接器能唯一选中。 */
static int Math_Min3(int a, int b, int c)
{
	int m = a < b ? a : b;
	return m < c ? m : c;
}

static float Math_MinF3(float a, float b, float c)
{
	float m = a < b ? a : b;
	return m < c ? m : c;
}

static int Math_Min4(int a, int b, int c, int d)
{
	int m = Math_Min3(a, b, c);
	return m < d ? m : d;
}

static float Math_MinF4(float a, float b, float c, float d)
{
	float m = Math_MinF3(a, b, c);
	return m < d ? m : d;
}

static int Math_Max3(int a, int b, int c)
{
	int m = a > b ? a : b;
	return m > c ? m : c;
}

static float Math_MaxF3(float a, float b, float c)
{
	float m = a > b ? a : b;
	return m > c ? m : c;
}

static int Math_Max4(int a, int b, int c, int d)
{
	int m = Math_Max3(a, b, c);
	return m > d ? m : d;
}

static float Math_MaxF4(float a, float b, float c, float d)
{
	float m = Math_MaxF3(a, b, c);
	return m > d ? m : d;
}

static float Math_MaxF(float a, float b)
{
	return a > b ? a : b;
}

static void Math_Swap(int *a, int *b)
{
	int tmp = *a;
	*a = *b;
	*b = tmp;
}

static void Math_SwapF(float *a, float *b)
{
	float tmp = *a;
	*a = *b;
	*b = tmp;
}

//void Math_SetRandMode(int mode);

static int Math_Rand(void)
{
	return math_genrand() & 0x7fffffff;
}

static float Math_RandF(void)
{
	return (float)(math_genrand() * (1.0 / 4294967296.0));
}

/* AIN Math f[20]–f[24]：MT 系列随机。它们在库表里是**独立导出**，
 * 与 `SetSeed`/`Rand` 不同名，以前一个都没注册 —— 参数校验/伤害浮动
 * 一旦调到就是 VM_ERROR。本实现与 `Rand` 共用同一个 mt19937 状态
 * （原引擎也是同一发生器，只是入口名不同）。 */
static void Math_MTSetSeed(int seed)
{
	Math_SetSeed(seed);
}

static void Math_MTSetSeedByCurrentTime(void)
{
	Math_SetSeedByCurrentTime();
}

static int Math_MTRand(void)
{
	return math_genrand() & 0x7fffffff;
}

static float Math_MTRandF(void)
{
	return (float)(math_genrand() * (1.0 / 4294967296.0));
}

/* 名里的 `Include1` = 区间**包含 1.0**：整型取值在 [0, 2^32-1] 上闭区间
 * 归一化，于是 4294967295 → 1.0。 */
static float Math_MTRandFInclude1(void)
{
	return (float)(math_genrand() * (1.0 / 4294967295.0));
}

static void shuffle_array(int *a, int len)
{
	for (int i = len - 1; i > 0; i--) {
		int j = Math_RandF() * i;
		int tmp = a[j];
		a[j] = a[i];
		a[i] = tmp;
	}
}

static void Math_RandTableInit(int num, int size)
{
	struct shuffle_table *tbl = find_shuffle_table(num);
	if (tbl) {
		free(tbl->elems);
	} else {
		tbl = xmalloc(sizeof(struct shuffle_table));
		tbl->id = num;
		tbl->next = shuffle_tables;
		shuffle_tables = tbl;
	}
	tbl->elems = xmalloc(size * sizeof(int));
	tbl->size = size;
	tbl->i = 0;
	for (int i = 0; i < size; i++)
		tbl->elems[i] = i;
	shuffle_array(tbl->elems, size);
}

static int Math_RandTable(int num)
{
	struct shuffle_table *tbl = find_shuffle_table(num);
	if (!tbl) {
		WARNING("Invalid rand table id %d", num);
		return 0;
	}
	if (tbl->i >= tbl->size) {
		// reshuffle
		shuffle_array(tbl->elems, tbl->size);
		tbl->i = 0;
	}
	return tbl->elems[tbl->i++];
}

//void Math_RandTable2Init(int num, struct page *array);
//int Math_RandTable2(int num);

static int Math_Ceil(float f)
{
	return (int)ceilf(f);
}

/* AIN Math f[10] `int Floor(float)`、f[11] `int Round(float)`。
 * 之前两条都没导出，启动期两条 Unimplemented 告警。
 * Round 用 `floorf(x + 0.5f)`（AIN 侧只用来把浮点血量/伤害取整，
 * 与 C `roundf` 只在 .5 的银行家舍入上不同，而原引擎是四舍五入）。 */
static int Math_Floor(float f)
{
	return (int)floorf(f);
}

static int Math_Round(float f)
{
	return (int)floorf(f + 0.5f);
}

static bool Math_BezierCurve(struct page **x_array, struct page **y_array, int num, float t, int *result_x, int *result_y)
{
	vec2 *coeffs = xmalloc(num * sizeof(vec2));
	for (int i = 0; i < num; i++) {
		coeffs[i][0] = (*x_array)->values[i].i;
		coeffs[i][1] = (*y_array)->values[i].i;
	}

	// De Casteljau 算法。
	for (; num > 1; num--) {
		for (int i = 0; i < num - 1; i++)
			glm_vec2_lerp(coeffs[i], coeffs[i + 1], t, coeffs[i]);
	}
	*result_x = coeffs[0][0];
	*result_y = coeffs[0][1];

	free(coeffs);
	return true;
}

/* AIN Math f[37]/f[38]：`Clamp` 的 int 与 float 两个重载。
 * 以前只实现整数形式，float 调用被绑到 `int Clamp(int,int,int)` ——
 * 参数被当成整数读、返回值被截断，与 D284 的 Min/Max 是同一个坑。 */
static int Math_Clamp(int v, int lo, int hi)
{
	if (v < lo)
		return lo;
	if (v > hi)
		return hi;
	return v;
}

static float Math_ClampF(float v, float lo, float hi)
{
	if (v < lo)
		return lo;
	if (v > hi)
		return hi;
	return v;
}

/* AIN Math f[7]/f[8]：`Abs` 的 int 与 float 重载。同理，旧表只有 `Abs -> abs`。 */
static float Math_AbsF(float v)
{
	return fabsf(v);
}

HLL_LIBRARY(Math,
	    HLL_EXPORT(Cos, Math_Cos),
	    HLL_EXPORT(Sin, Math_Sin),
	    HLL_EXPORT(Tan, Math_Tan),
	    HLL_EXPORT(Sqrt, sqrtf),
	    HLL_EXPORT(Asin, asinf),
	    HLL_EXPORT(Acos, acosf),
	    HLL_EXPORT(Atan, atanf),
	    HLL_EXPORT(Atan2, atan2f),
	    HLL_EXPORT_NRT(Abs, abs, 1, AIN_INT),
	    HLL_EXPORT_NRT(Abs, Math_AbsF, 1, AIN_FLOAT),
	    HLL_EXPORT(Pow, powf),
	    HLL_EXPORT(SetSeed, Math_SetSeed),
	    HLL_EXPORT(SetSeedByCurrentTime, Math_SetSeedByCurrentTime),
	    //HLL_EXPORT(SetRandMode, Math_SetRandMode),
	    HLL_EXPORT(Rand, Math_Rand),
	    HLL_EXPORT(RandF, Math_RandF),
	    HLL_EXPORT(RandTableInit, Math_RandTableInit),
	    HLL_EXPORT(RandTable, Math_RandTable),
	    HLL_EXPORT(MTSetSeed, Math_MTSetSeed),
	    HLL_EXPORT(MTSetSeedByCurrentTime, Math_MTSetSeedByCurrentTime),
	    HLL_EXPORT(MTRand, Math_MTRand),
	    HLL_EXPORT(MTRandF, Math_MTRandF),
	    HLL_EXPORT(MTRandFInclude1, Math_MTRandFInclude1),
	    //HLL_EXPORT(RandTable2Init, Math_RandTable2Init),
	    //HLL_EXPORT(RandTable2, Math_RandTable2),
	    /* [D284] 6 个重载全部标注 (arity, 返回类型)。
	     * 名字必须都是 `Min`/`Max`（AIN 库里就是这么叫的），
	     * 靠 arity + ret_type 区分 —— `MinF` 这种名字 AIN 中不存在。 */
	    HLL_EXPORT_NRT(Min, Math_Min, 2, AIN_INT),
	    HLL_EXPORT_NRT(Min, Math_MinF, 2, AIN_FLOAT),
	    HLL_EXPORT_NRT(Min, Math_Min3, 3, AIN_INT),
	    HLL_EXPORT_NRT(Min, Math_MinF3, 3, AIN_FLOAT),
	    HLL_EXPORT_NRT(Min, Math_Min4, 4, AIN_INT),
	    HLL_EXPORT_NRT(Min, Math_MinF4, 4, AIN_FLOAT),
	    HLL_EXPORT_NRT(Max, Math_Max, 2, AIN_INT),
	    HLL_EXPORT_NRT(Max, Math_MaxF, 2, AIN_FLOAT),
	    HLL_EXPORT_NRT(Max, Math_Max3, 3, AIN_INT),
	    HLL_EXPORT_NRT(Max, Math_MaxF3, 3, AIN_FLOAT),
	    HLL_EXPORT_NRT(Max, Math_Max4, 4, AIN_INT),
	    HLL_EXPORT_NRT(Max, Math_MaxF4, 4, AIN_FLOAT),
	    HLL_EXPORT(Swap, Math_Swap),
	    HLL_EXPORT(SwapF, Math_SwapF),
	    HLL_EXPORT(Log, logf),
	    HLL_EXPORT(Log10, log10f),
	    HLL_EXPORT(Ceil, Math_Ceil),
	    HLL_EXPORT(Floor, Math_Floor),
	    HLL_EXPORT(Round, Math_Round),
	    HLL_EXPORT(BezierCurve, Math_BezierCurve),
	    HLL_EXPORT_NRT(Clamp, Math_Clamp, 3, AIN_INT),
	    HLL_EXPORT_NRT(Clamp, Math_ClampF, 3, AIN_FLOAT));

