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

#ifndef ANTEATERADVENGINE_H
#define ANTEATERADVENGINE_H

#include <stdbool.h>

struct string;
struct page;

void ADVLogList_Clear(void);
void ADVLogList_AddText(struct string **text);
void ADVLogList_AddNewLine(void);
void ADVLogList_AddNewPage(void);
void ADVLogList_AddVoice(int nVoiceNumber);
void ADVLogList_SetEnable(bool bEnable);
bool ADVLogList_IsEnable(void);
int ADVLogList_GetNumofADVLog(void);
int ADVLogList_GetNumofADVLogText(int nADVLog);
void ADVLogList_GetADVLogText(int nADVLog, int nText, struct string **text);
int ADVLogList_GetNumofADVLogVoice(int nADVLog);
int ADVLogList_GetADVLogVoice(int nADVLog, int Index);
int ADVLogList_GetADVLogVoiceLast(int log_no); // for StoatSpriteEngine
bool ADVLogList_Save(struct page **iarray);
bool ADVLogList_Load(struct page **iarray);
bool ADVSceneKeeper_AddADVScene(struct page **page);
int ADVSceneKeeper_GetNumofADVScene(void);
bool ADVSceneKeeper_GetADVScene(int nIndex, struct page **page);
void ADVSceneKeeper_Clear(void);
bool ADVSceneKeeper_Save(struct page **iarray);
bool ADVSceneKeeper_Load(struct page **iarray);

#endif /* ANTEATERADVENGINE_H */
