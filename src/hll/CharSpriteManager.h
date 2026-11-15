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

#ifndef SYSTEM4_HLL_CHARSPRITE_MANAGER_H
#define SYSTEM4_HLL_CHARSPRITE_MANAGER_H

#include <stdbool.h>

struct page;
struct string;

int CharSpriteManager_CreateHandle(void);
bool CharSpriteManager_ReleaseHandle(int handle);
void CharSpriteManager_Clear(void);
void CharSpriteManager_Rebuild(void);
bool CharSpriteManager_Save(struct page **iarray);
bool CharSpriteManager_Load(struct page **iarray);
void CharSprite_Release(int nHandle);
void CharSprite_SetChar(int handle, struct string **s, struct page **property);
void CharSprite_SetPos(int handle, int x, int y);
void CharSprite_SetZ(int handle, int z);
void CharSprite_SetAlphaRate(int handle, int rate);
void CharSprite_SetShow(int handle, bool show);
void CharSprite_GetChar(int handle, struct string **ch);
int CharSprite_GetAlphaRate(int handle);

#endif /* SYSTEM4_HLL_CHARSPRITE_MANAGER_H */
