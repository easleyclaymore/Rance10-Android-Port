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

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <SDL3/SDL.h>

#include "system4.h"
#include "system4/ain.h"
#include "system4/file.h"
#include "system4/little_endian.h"
#include "system4/string.h"
#include "system4/utfsjis.h"

#include "vm.h"
#include "xsystem4.h"

#include "hll.h"

static bool enabled = false;
static int state = 0;

static char *save_path;
static uint8_t *flags;
static int nr_flags;

static void msgskip_save(void)
{
	FILE *f = file_open_utf8(save_path, "wb");
	if (!f)
		ERROR("fopen: '%s': %s", display_utf0(save_path), strerror(errno));

	uint8_t header[4];
	LittleEndian_putDW(header, 0, nr_flags);
	fwrite(header, sizeof(header), 1, f);
	fwrite(flags, (nr_flags + 7) / 8, 1, f);
	fclose(f);
}

#ifdef __ANDROID__
static int msgskip_event_handler(possibly_unused void *user_data, SDL_Event *e) {
	switch (e->type) {
	case SDL_APP_WILLENTERBACKGROUND:
		msgskip_save();
		break;
	}
	return 0;
}
#endif

static int MsgSkip_Init(struct string *name)
{
	if (flags)
		return 1;

	nr_flags = ain->nr_messages;
	flags = xcalloc((nr_flags + 7) / 8, 1);

	// 多数游戏会在 `name` 前面带上 system.GetSaveFolderName()，但有些
	// 游戏传的是固定字符串 "SaveData\\MsgSkip.asd"。原样交给
	// savedir_path() 会把 "SaveData/" 重复一次，所以去掉它。
	if (!strncmp(name->text, "SaveData\\", 9)) {
		save_path = savedir_path(name->text + 9);
	} else {
		save_path = savedir_path(name->text);
	}

	size_t data_size;
	uint8_t *data = file_read(save_path, &data_size);
	if (data) {
		if (data_size == (size_t)ain->nr_messages) {
			// 从旧格式迁移。
			for (int i = 0; i < data_size; i++) {
				if (data[i])
					flags[i >> 3] |= 0x80 >> (i & 7);
			}
		} else {
			int n = LittleEndian_getDW(data, 0);
			if ((n + 7) / 8 != data_size - 4) {
				WARNING("Corrupted MsgSkip file '%s'", display_utf0(save_path));
			} else if (n != nr_flags) {
				WARNING("Incorrect size for MsgSkip file '%s'", display_utf0(save_path));
				if (n > nr_flags)
					n = nr_flags;
				memcpy(flags, data + 4, (n + 7) / 8);
			} else {
				memcpy(flags, data + 4, (nr_flags + 7) / 8);
			}
		}
		free(data);
	}
#ifdef __ANDROID__
	SDL_AddEventWatch(msgskip_event_handler, NULL);
#endif
	atexit(msgskip_save);
	return 1;
}

static void MsgSkip_SetFlag(int msgnum)
{
	if (msgnum < 0 || msgnum >= nr_flags)
		return;
	flags[msgnum >> 3] |= 0x80 >> (msgnum & 7);
}

static int MsgSkip_GetFlag(int msgnum)
{
	if (msgnum < 0 || msgnum >= nr_flags)
		return 0;
	return !!(flags[msgnum >> 3] & 0x80 >> (msgnum & 7));
}

static void MsgSkip_SetEnable(int enable)
{
	enabled = !!enable;
}

static int MsgSkip_GetEnable(void)
{
	return enabled;
}

static void MsgSkip_SetState(int _state)
{
	state = _state;
}

/* [D497] SetAdvFlag/GetAdvFlag：以（ADV 函数名, 版本, 步数）为键的
 * "跳过标记"表。调用点证据（0x990a4-0x990b6）：
 *   GetAdvFlag(GetADVFunctionName(), GetADVFunctionVersion(),
 *              GetADVFunctionStep())
 * —— 三个实参合成一个键。AIN 声明 (string, int, int)；此前按别名接到
 * MsgSkip_Set/GetFlag(msgnum) 上，string 指针被当成位图下标用，
 * 一旦被调就是 OOB 写（arity 审计 D497 揪出）。运行期 0 次命中，
 * 键控表是按钉死语义的最小实现。 */
#define ADV_FLAG_MAX 256
static struct adv_flag {
	char *key;      /* "name\x1fver\x1fstep"；NULL = 空槽 */
	int val;
} adv_flags[ADV_FLAG_MAX];
static unsigned adv_flag_cursor;

static char *adv_flag_key(struct string *name, int ver, int step)
{
	char buf[512];
	int n = snprintf(buf, sizeof(buf), "%s\x1f%d\x1f%d",
			 name && name->text ? name->text : "", ver, step);
	if (n <= 0 || n >= (int)sizeof(buf))
		return NULL;
	return strdup(buf);
}

static void MsgSkip_SetAdvFlag(struct string *name, int ver, int step)
{
	char *k = adv_flag_key(name, ver, step);
	if (!k)
		return;
	for (int i = 0; i < ADV_FLAG_MAX; i++) {
		if (adv_flags[i].key && !strcmp(adv_flags[i].key, k)) {
			adv_flags[i].val = 1;
			free(k);
			return;
		}
	}
	struct adv_flag *e = &adv_flags[adv_flag_cursor++ % ADV_FLAG_MAX];
	free(e->key);
	e->key = k;
	e->val = 1;
}

static int MsgSkip_GetAdvFlag(struct string *name, int ver, int step)
{
	char *k = adv_flag_key(name, ver, step);
	if (!k)
		return 0;
	int r = 0;
	for (int i = 0; i < ADV_FLAG_MAX; i++) {
		if (adv_flags[i].key && !strcmp(adv_flags[i].key, k)) {
			r = adv_flags[i].val;
			break;
		}
	}
	free(k);
	return r;
}

static int MsgSkip_GetState(void)
{
	return state;
}

HLL_WARN_UNIMPLEMENTED( , void, MsgSkip, UseFlag, int use);
HLL_WARN_UNIMPLEMENTED(0, int,  MsgSkip, GetNumofMsg, void);
HLL_WARN_UNIMPLEMENTED(0, int,  MsgSkip, GetNumofFlag, void);

static void MsgSkip_PreLink(void)
{
	int libno = ain_get_library(ain, "MsgSkip");
	assert(libno >= 0);
	if (ain_get_library_function(ain, libno, "Init") < 0) {
		struct string *name = cstr_to_string("MsgSkip.asd");
		MsgSkip_Init(name);
		free_string(name);
	}
}

HLL_LIBRARY(MsgSkip,
	    HLL_EXPORT(_PreLink, MsgSkip_PreLink),
	    HLL_EXPORT(Init, MsgSkip_Init),
	    HLL_EXPORT(UseFlag, MsgSkip_UseFlag),
	    HLL_EXPORT(SetEnable, MsgSkip_SetEnable),
	    HLL_EXPORT(SetState, MsgSkip_SetState),
	    HLL_EXPORT(SetFlag, MsgSkip_SetFlag),
	    HLL_EXPORT(GetEnable, MsgSkip_GetEnable),
	    HLL_EXPORT(GetState, MsgSkip_GetState),
	    HLL_EXPORT(GetFlag, MsgSkip_GetFlag),
	    HLL_EXPORT(GetNumofMsg, MsgSkip_GetNumofMsg),
	    HLL_EXPORT(GetNumofFlag, MsgSkip_GetNumofFlag),
	    /* [D497] SetAdvFlag/GetAdvFlag：键控表（见 adv_flag 注释）——
	     * 此前按别名接 Set/GetFlag(msgnum)，string 指针当位图下标，
	     * arity 审计揪出的 OOB 写级错形。 */
	    HLL_EXPORT(SetAdvFlag, MsgSkip_SetAdvFlag),
	    HLL_EXPORT(GetAdvFlag, MsgSkip_GetAdvFlag));

HLL_QUIET_UNIMPLEMENTED(, void, vmMsgSkip, Init, void *imainsystem);

static int vmMsgSkip_EnumHandle(void)
{
	return flags ? 1 : 0;
}

static int vmMsgSkip_Open(struct string *fname, possibly_unused int option)
{
	return MsgSkip_Init(fname);
}

static void vmMsgSkip_Close(possibly_unused int handle)
{
	msgskip_save();
	free(flags);
	flags = NULL;
}

static int vmMsgSkip_Query(possibly_unused int handle, int msgnum)
{
	int r = MsgSkip_GetFlag(msgnum);
	// vmMsgSkip 没有显式的设置函数。
	MsgSkip_SetFlag(msgnum);
	return state && r;
}

HLL_LIBRARY(vmMsgSkip,
	    HLL_EXPORT(Init, vmMsgSkip_Init),
	    HLL_EXPORT(EnumHandle, vmMsgSkip_EnumHandle),
	    HLL_EXPORT(Open, vmMsgSkip_Open),
	    HLL_EXPORT(Close, vmMsgSkip_Close),
	    HLL_EXPORT(Query, vmMsgSkip_Query),
	    HLL_EXPORT(SetEnable, MsgSkip_SetEnable),
	    HLL_EXPORT(SetState, MsgSkip_SetState),
	    HLL_EXPORT(GetEnable, MsgSkip_GetEnable),
	    HLL_EXPORT(GetState, MsgSkip_GetState)
	    );
