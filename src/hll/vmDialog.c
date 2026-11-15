/* 版权所有 (C) 2024 kichikuou <KichikuouChrome@gmail.com>
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

#include <SDL.h>

#include "system4/string.h"
#include "system4/utfsjis.h"

#include "hll.h"

HLL_QUIET_UNIMPLEMENTED( , void, vmDialog, Init, void *imainsystem);

static int vmDialog_MsgBox(struct string *t_string, struct string *string, int type, possibly_unused int default_button)
{
	uint32_t flags = (type == 4) ? SDL_MESSAGEBOX_ERROR : 0;
	char *title = sjis2utf(t_string->text, 0);
	char *message = sjis2utf(string->text, 0);
	SDL_ShowSimpleMessageBox(flags, title, message, NULL);
	free(message);
	free(title);
	return 1;
}

//int vmDialog_InputNum(struct string *pITString, int nMin, int nMax, int *pnNum);
//int vmDialog_InputString(struct string *pITString, int nLength, struct string **pIString);
//int vmDialog_SelectNum(struct string *pITString, int nMin, int nMax, int *pnNum);
//int vmDialog_SelectString(struct string *pITString, struct page **pIVMArray, int nNum, struct string **pIString);
//int vmDialog_InputOpenFile(struct string *pITString, struct string **pIPString, struct string **pIString, struct string *pIDString, struct string *pIFString);
//int vmDialog_InputSaveFile(struct string *pITString, struct string **pIPString, struct string **pIString, struct string *pIDString, struct string *pIFString);

HLL_LIBRARY(vmDialog,
	    HLL_EXPORT(Init, vmDialog_Init),
	    HLL_EXPORT(MsgBox, vmDialog_MsgBox),
	    HLL_TODO_EXPORT(InputNum, vmDialog_InputNum),
	    HLL_TODO_EXPORT(InputString, vmDialog_InputString),
	    HLL_TODO_EXPORT(SelectNum, vmDialog_SelectNum),
	    HLL_TODO_EXPORT(SelectString, vmDialog_SelectString),
	    HLL_TODO_EXPORT(InputOpenFile, vmDialog_InputOpenFile),
	    HLL_TODO_EXPORT(InputSaveFile, vmDialog_InputSaveFile)
	    );
