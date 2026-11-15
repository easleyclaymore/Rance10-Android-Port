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
 * DSL for implementing libraries.
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

#define HLL_EXPORT(fname, funptr) { .name = #fname, .fun = funptr, .nr_args = -1 }
/* 同名重载用这个标注参数个数；链接时优先选 arity 相等的那一项。 */
#define HLL_EXPORT_N(fname, funptr, n) { .name = #fname, .fun = funptr, .nr_args = (n) }
#define HLL_TODO_EXPORT(fname, funptr) { .name = #fname, .fun = NULL, .nr_args = -1 }

#define HLL_LIBRARY(lname, ...)				\
	struct static_library lib_ ## lname = {		\
		.name = #lname,				\
		.functions = {				\
			__VA_ARGS__,			\
			{ .name = NULL, .fun = NULL }	\
		}					\
	}

#endif /* SYSTEM4_HLL_H */
