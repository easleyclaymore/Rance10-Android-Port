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

#ifndef SYSTEM4_ID_POOL_H
#define SYSTEM4_ID_POOL_H

// 基于指针的句柄，用一个合理的起始值。
#define ID_POOL_HANDLE_BASE 0x100000

struct id_pool {
	void **slots;
	int nr_slots;
	int base;
};

void id_pool_init(struct id_pool *pool, int base);
void id_pool_delete(struct id_pool *pool);
void *id_pool_release(struct id_pool *pool, int id);
int id_pool_count(struct id_pool *pool);
int id_pool_get_unused(struct id_pool *pool);
int id_pool_get_first(struct id_pool *pool);
int id_pool_get_next(struct id_pool *pool, int id);
void *id_pool_get(struct id_pool *pool, int id);
void *id_pool_set(struct id_pool *pool, int id, void *data);

#endif /* SYSTEM4_ID_POOL_H */
