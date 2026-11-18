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

#include <stdlib.h>
#include "msgqueue.h"

struct msgq_elem {
	void *msg;
	struct msgq_elem *next;
};

struct msgq *msgq_new(void) {
	struct msgq *q = calloc(1, sizeof(struct msgq));
	q->mutex = SDL_CreateMutex();
	q->cond_nonempty = SDL_CreateCondition();
	return q;
}

void msgq_free(struct msgq *q) {
	SDL_DestroyMutex(q->mutex);
	SDL_DestroyCondition(q->cond_nonempty);
	free(q);
}

void msgq_enqueue(struct msgq *q, void *msg) {
	struct msgq_elem *e = malloc(sizeof(struct msgq_elem));
	e->msg = msg;
	e->next = NULL;

	SDL_LockMutex(q->mutex);
	if (!q->head) {
		q->head = q->last = e;
	} else {
		q->last->next = e;
		q->last = e;
	}
	SDL_UnlockMutex(q->mutex);
	SDL_SignalCondition(q->cond_nonempty);
}

void *msgq_dequeue(struct msgq *q) {
	SDL_LockMutex(q->mutex);
	while (!q->head)
		SDL_WaitCondition(q->cond_nonempty, q->mutex);

	struct msgq_elem *e = q->head;
	q->head = e->next;
	if (!e->next)
		q->last = NULL;

	SDL_UnlockMutex(q->mutex);

	void *msg = e->msg;
	free(e);
	return msg;
}

void *msgq_dequeue_timeout(struct msgq *q, uint32_t timeout_ms) {
	SDL_LockMutex(q->mutex);

	while (!q->head && SDL_WaitConditionTimeout(q->cond_nonempty, q->mutex, (Sint32)timeout_ms))
		;

	if (!q->head) {  // timed out
		SDL_UnlockMutex(q->mutex);
		return NULL;
	}

	struct msgq_elem *e = q->head;
	q->head = e->next;
	if (!e->next)
		q->last = NULL;

	SDL_UnlockMutex(q->mutex);

	void *msg = e->msg;
	free(e);
	return msg;
}
