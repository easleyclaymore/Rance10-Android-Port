/* Clipboard —— 兰斯10 启动用的最小实现。
 * SetText / GetText，背后是一个内存缓冲。
 */
#include <string.h>

#include "hll.h"
#include "vm.h"
#include "system4/string.h"

static struct string *clip_text = NULL;

static void Clipboard_SetText(struct string *text)
{
	if (clip_text) {
		free_string(clip_text);
		clip_text = NULL;
	}
	if (text)
		clip_text = string_dup(text);
}

static struct string *Clipboard_GetText(void)
{
	if (clip_text)
		return string_dup(clip_text);
	return string_dup(&EMPTY_STRING);
}

HLL_LIBRARY(Clipboard,
	HLL_EXPORT(SetText, Clipboard_SetText),
	HLL_EXPORT(GetText, Clipboard_GetText));
