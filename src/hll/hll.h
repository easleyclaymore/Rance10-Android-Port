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

#ifndef SYSTEM4_HLL_H
#define SYSTEM4_HLL_H

/*
 * 用于实现各个库的 DSL。
 */

#include "vm.h"
#include "system4.h"

void static_library_replace(struct static_library *lib, const char *name, void *fun);

#define HLL_WARN_UNIMPLEMENTED(rval, rtype, libname, fname, ...)	\
	static rtype libname ## _ ## fname(__VA_ARGS__) {		\
		WARNING("Unimplemented HLL function: " #libname "." #fname); \
		return rval;						\
	}

#define HLL_QUIET_UNIMPLEMENTED(rval, rtype, libname, fname, ...)	\
	static rtype libname ## _ ## fname(__VA_ARGS__) {		\
		return rval;						\
	}

#define HLL_EXPORT(fname, funptr) { .name = #fname, .fun = funptr, .nr_args = -1, .ret_type = -1 }
/* 同名重载用这个标注参数个数；链接时优先选 arity 相等的那一项。 */
#define HLL_EXPORT_N(fname, funptr, n) { .name = #fname, .fun = funptr, .nr_args = (n), .ret_type = -1 }
/* [D284] 同名 + 同 arity、只有数值类型不同的重载（Math.Min/Max 各 2/3/4 参
 * 都有 int 与 float 两版）必须再靠返回类型区分，否则浮点版会被绑到整数
 * 实现上，把小数截断成 0。用法：HLL_EXPORT_RT(Min, Math_Min, AIN_INT)。 */
#define HLL_EXPORT_RT(fname, funptr, rt) { .name = #fname, .fun = funptr, .nr_args = -1, .ret_type = (rt) }
#define HLL_EXPORT_NRT(fname, funptr, n, rt) { .name = #fname, .fun = funptr, .nr_args = (n), .ret_type = (rt) }
#define HLL_TODO_EXPORT(fname, funptr) { .name = #fname, .fun = NULL, .nr_args = -1, .ret_type = -1 }

#define HLL_LIBRARY(lname, ...)				\
	struct static_library lib_ ## lname = {		\
		.name = #lname,				\
		.functions = {				\
			__VA_ARGS__,			\
			{ .name = NULL, .fun = NULL }	\
		}					\
	}

#endif /* SYSTEM4_HLL_H */
