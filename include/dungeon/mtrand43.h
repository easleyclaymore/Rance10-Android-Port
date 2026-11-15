/* 版权所有 (C) 2021 kichikuou <KichikuouChrome@gmail.com>
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

#ifndef SYSTEM4_MTRAND43_H
#define SYSTEM4_MTRAND43_H

#include <stddef.h>
#include <stdint.h>

#define MTRAND43_STATE_SIZE 4

// Mersenne Twister engine with N=4, M=3
struct mtrand43 {
	uint32_t st[MTRAND43_STATE_SIZE];
	int i;
};

void mtrand43_init(struct mtrand43 *mt, uint32_t seed);
uint32_t mtrand43_genrand(struct mtrand43 *mt);
float mtrand43_genfloat(struct mtrand43 *mt);

#endif /* SYSTEM4_MTRAND43_H */
