/* FileDialog —— 兰斯10 启动用的空桩实现。
 * 在接上真正的 UI 之前返回空串（表示对话框被取消）。
 */
#include <string.h>

#include "hll.h"
#include "vm.h"
#include "system4/string.h"

#define FD_STUB(name, ...)                                            \
	static struct string *FileDialog_##name(__VA_ARGS__)          \
	{                                                             \
		return string_dup(&EMPTY_STRING);                     \
	}

FD_STUB(OpenLoadDialog, struct string *a, struct string *b, struct string *c,
	struct string *d, int e)
FD_STUB(OpenSaveDialog, struct string *a, struct string *b, struct string *c,
	struct string *d, int e)
FD_STUB(OpenSelectFolderDialog, struct string *a, struct string *b, int c)

HLL_LIBRARY(FileDialog,
	HLL_EXPORT(OpenLoadDialog, FileDialog_OpenLoadDialog),
	HLL_EXPORT(OpenSaveDialog, FileDialog_OpenSaveDialog),
	HLL_EXPORT(OpenSelectFolderDialog, FileDialog_OpenSelectFolderDialog));
