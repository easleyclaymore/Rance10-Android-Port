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

#include "system4.h"
#include "system4/string.h"
#include "system4/utfsjis.h"

#include "hll.h"
#include "xsystem4.h"

bool is_opened = false;

static bool CrayfishLogViewer_OpenViewer(void)
{
	is_opened = true;
	return true;
}

static bool CrayfishLogViewer_CloseViewer(void)
{
	is_opened = false;
	return true;
}

static bool CrayfishLogViewer_IsOpenedViewer(void)
{
	return is_opened;
}

HLL_WARN_UNIMPLEMENTED( , void, CrayfishLogViewer, SetWindowTitleName,
			possibly_unused struct string *window_title_name);

static bool CrayfishLogViewer_AddText(struct string *text)
{
	log_message("Crayfish", "%s", display_sjis0(text->text));
	return true;
}

HLL_QUIET_UNIMPLEMENTED(true, bool, CrayfishLogViewer, ClearText);
static struct string *log_save_folder = NULL;

static void CrayfishLogViewer_SetSaveFolderName(struct string *save_folder_name)
{
	if (log_save_folder)
		free_string(log_save_folder);
	log_save_folder = save_folder_name ? string_dup(save_folder_name) : NULL;
}

HLL_LIBRARY(CrayfishLogViewer,
	    HLL_EXPORT(OpenViewer, CrayfishLogViewer_OpenViewer),
	    HLL_EXPORT(CloseViewer, CrayfishLogViewer_CloseViewer),
	    HLL_EXPORT(IsOpenedViewer, CrayfishLogViewer_IsOpenedViewer),
	    HLL_EXPORT(SetWindowTitleName, CrayfishLogViewer_SetWindowTitleName),
	    HLL_EXPORT(AddText, CrayfishLogViewer_AddText),
	    HLL_EXPORT(ClearText, CrayfishLogViewer_ClearText),
	    HLL_EXPORT(SetSaveFolderName, CrayfishLogViewer_SetSaveFolderName));
