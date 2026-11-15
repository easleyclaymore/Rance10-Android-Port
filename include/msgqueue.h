/*
 * 版权所有 (C) 2021 kichikuou <KichikuouChrome@gmail.com>
 *
 * 本程序是自由软件：你可以依据自由基金会发布的
 * GNU 通用公共许可证（第 2 版，或你选择的任何更高版本）的条款，
 * 重新分发和/或修改本程序。
 *
 * 分发本程序是希望它有用，但不作任何担保，甚至不担保适销性或
 * 适用于特定用途。详见 GNU 通用公共许可证。
 *
 * 你应当已经随本程序收到一份 GNU 通用公共许可证的副本；
 * 如果没有，请写信给：Free Software Foundation, Inc.,
 * 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA。
 */

#ifndef __MSGQUEUE_H__
#define __MSGQUEUE_H__

#include <stdbool.h>
#include <SDL_mutex.h>

struct msgq_elem;

struct msgq {
	SDL_mutex *mutex;
	SDL_cond *cond_nonempty;
	struct msgq_elem *head;
	struct msgq_elem *last;
};

static inline bool msgq_isempty(struct msgq *q) {
	return !q->head;
}

struct msgq *msgq_new(void);
void msgq_free(struct msgq *q);
void msgq_enqueue(struct msgq *q, void *msg);
void *msgq_dequeue(struct msgq *q);
void *msgq_dequeue_timeout(struct msgq *q, uint32_t timeout_ms);

#endif // __MSGQUEUE_H__
