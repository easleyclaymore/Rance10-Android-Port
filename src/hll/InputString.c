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

#include <SDL3/SDL.h>

#include "system4.h"
#include "system4/ain.h"
#include "system4/utfsjis.h"
#include "system4/string.h"

#include "vm.h"
#include "gfx/types.h"
#include "gfx/gfx.h"
#include "hll.h"
#include "input.h"

static int font_size;
static int font_weight;
static Point pos;
static bool has_editing_text;
static bool get_result_string_clears;

static struct string *result;

static void InputString_ClearResultString(void)
{
	if (result)
		free_string(result);
	result = string_ref(&EMPTY_STRING);
}

static struct string *InputString_GetResultString(void)
{
	struct string *s = string_ref(result);
	if (get_result_string_clears)
		InputString_ClearResultString();
	return s;
}

static void InputString_SetResultString(struct string *s)
{
	if (result)
		free_string(result);
	result = string_ref(s);
}

static void InputString_SetFont(int nSize, possibly_unused struct string *pIName, int nWeight)
{
	font_size = nSize;
	font_weight = nWeight;
}

static void InputString_SetPos(int nX, int nY)
{
	pos.x = nX;
	pos.y = nY;
}

static void InputString_Begin(void)
{
	InputString_ClearResultString();
}

static void InputString_End(void)
{
	InputString_ClearResultString();
}

static void handle_input(const char *text)
{
	char *u = utf2sjis(text, 0);
	string_append_cstr(&result, u, strlen(u));
	free(u);
	has_editing_text = false;
}

// TODO：这个函数得在给定坐标/字号/字重下把文字画出来。
//       需要一种「总是画在场景最上层」的纹理创建接口。
//       理想情况下不依赖 SACT。
static void handle_editing(const char *text, int start, int length)
{
	has_editing_text = *text != '\0';
}

static void handle_key(int code)
{
	if (code == VK_BACK && !has_editing_text && result && result->size > 0)
		string_pop_back(&result);
}

static void InputString_OpenIME(void)
{
	register_input_handler(handle_input);
	register_editing_handler(handle_editing);
	register_key_handler(handle_key);
	has_editing_text = false;
	input_start_text_input();
}

static void InputString_CloseIME(void)
{
	input_stop_text_input();
	has_editing_text = false;
	clear_input_handler();
	clear_editing_handler();
	clear_key_handler();
}

static bool InputString_Inputs(void)
{
	return has_editing_text;
}

static bool InputString_Converts(void)
{
	return has_editing_text;
}

static void InputString_PreLink(void)
{
	get_result_string_clears = true;
	int libno = ain_get_library(ain, "InputString");
	if (libno < 0)
		return;
	if (ain_get_library_function(ain, libno, "SetResultString") >= 0 ||
	    ain_get_library_function(ain, libno, "Inputs") >= 0) {
		get_result_string_clears = false;
	}
}

HLL_LIBRARY(InputString,
	    HLL_EXPORT(_PreLink, InputString_PreLink),
	    HLL_EXPORT(SetFont, InputString_SetFont),
	    HLL_EXPORT(SetPos, InputString_SetPos),
	    HLL_EXPORT(Begin, InputString_Begin),
	    HLL_EXPORT(End, InputString_End),
	    HLL_EXPORT(OpenIME, InputString_OpenIME),
	    HLL_EXPORT(CloseIME, InputString_CloseIME),
	    HLL_EXPORT(GetResultString, InputString_GetResultString),
	    HLL_EXPORT(ClearResultString, InputString_ClearResultString),
	    HLL_EXPORT(SetResultString, InputString_SetResultString),
	    HLL_EXPORT(Inputs, InputString_Inputs),
	    HLL_EXPORT(Converts, InputString_Converts));
