/* system —— 兰斯10 启动用实现。
 *
 * 存档/读档已接上 src/savedata.c 的 gsave 实现（D127）；ResumeSave/Load 仍是桩。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL3/SDL.h>

#include "system4/string.h"
#include "system4/utfsjis.h"

#include "hll.h"
#include "vm.h"
#include "vm/page.h"
#include "savedata.h"
#include "xsystem4.h"

/* ---------------- 存档 / 读档（空桩） ---------------- */

/* [D496] `bool system.ResumeSave(string key, string file, ref int n)` /
 * `void system.ResumeLoad(string key, string file)`。
 *
 * 这是**睡眠存档**（VM 全量快照：堆 + 调用栈 + 栈），不是 GroupSave 那种
 * 按全局组的选择性存档 —— 底层走 resume.c 的 `vm_save_image`/`vm_load_image`
 * （文件是 00.asd 这类自动挂起档，json/rsave 双格式）。
 * 上游在 syscall SYS_RESUME_SAVE 里接的是同一对函数：ref int 收
 * `vm_save_image` 的返回值，bool 返回成功。D127 当年只接了 GroupSave
 * 一族，这对漏了 —— 实测 268 次 `not implemented (…, 00.asd)`。
 *
 * 注意 ResumeLoad 的语义是**整个 VM 状态原地替换**（调用栈都会换掉），
 * 所以它只可能出现在游戏启动时"从上次挂起继续"那条路上 —— 那时当前
 * 的调用栈本来就该被丢弃，与上游 syscall 行为一致。 */
static bool System_ResumeSave(struct string *key, struct string *file, int *n)
{
	if (!key || !file)
		return false;
	int r = vm_save_image(key->text, file->text);
	if (n)
		*n = r;
	return r != 0;
}

static void System_ResumeLoad(struct string *key, struct string *file)
{
	if (!key || !file)
		return;
	vm_load_image(key->text, file->text);
}

static bool System_ResumeWriteComment(struct string *a, struct string *b, struct page **c)
{
	return false;
}

static bool System_ResumeReadComment(struct string *a, struct string *b, struct page **c)
{
	return false;
}

/* ---------------- 存档 / 读档 ----------------
 *
 * [D127] 这四个原本是 `return false` 的空桩，导致：
 *   - 每次改配置都弹 `[Error] 存档文件「 AFConfig.asd 」保存失败`
 *     （`gamesave::detail::配置文件存档` → `system.GroupSave` → false → `system.Error`）；
 *   - 游戏永远存不了档，`ExistSaveFile` 恒 false，读档路径全部走不通。
 *
 * `src/savedata.c` 里早就有一套完整的 gsave 读写（`save_globals`/`load_globals`，
 * 内部会调 `savedir_path()` 解析存档目录），签名与本库一一对应，直接接上即可。
 */

static bool System_GroupSave(struct string *key, struct string *file, struct string *group, int *n)
{
	if (!key || !file)
		return false;
	return save_globals(key->text, file->text, group ? group->text : NULL, n) != 0;
}

static bool System_GroupLoad(struct string *key, struct string *file, struct string *group, int *n)
{
	if (!key || !file)
		return false;
	return load_globals(key->text, file->text, group ? group->text : NULL, n) != 0;
}

static bool System_ExistSaveFile(struct string *name)
{
	if (!name)
		return false;
	char *path = savedir_path(name->text);
	FILE *fp = fopen(path, "rb");
	bool exists = fp != NULL;
	if (fp)
		fclose(fp);
	free(path);
	/* [D517i] **配置文件不在存档目录**。`3DConfig.vsf` 这类文件由 `VSFile`
	 * 库经 `gamedir_path()` 写在**游戏目录**；而
	 * `config::detail::CAS3DSetting@Load`(2745) 的第一步是
	 * `if (!ExistReignSaveFile()) return 1;` → `ExistReignSaveFile`(2749)
	 * = `system.ExistSaveFile(CAS3DSetting@GetSaveFileName())`。
	 * 只查存档目录就恒 false，Load 直接早退：d690 实证「进 3D 页后
	 * VSFile 零调用」，于是设置**能存进去、读不回来**（d686/d688 里
	 * `GetRenderMode` 恒返回默认值 2，界面永远停在「着色存在」）。
	 * 存档目录优先、游戏目录兜底；`XSYS4_EXIST_SAVE_GAMEDIR=0` 回退。 */
	if (!exists) {
		static int gamedir_fallback = -1;
		if (gamedir_fallback < 0) {
			const char *e = getenv("XSYS4_EXIST_SAVE_GAMEDIR");
			gamedir_fallback = !(e && *e == '0');
		}
		if (gamedir_fallback) {
			char *g = gamedir_path_icase(name->text);
			if (g) {
				FILE *gf = fopen(g, "rb");
				exists = gf != NULL;
				if (gf)
					fclose(gf);
				free(g);
			}
		}
	}
	return exists;
}

static bool System_DeleteSaveFile(struct string *name)
{
	if (!name)
		return false;
	char *path = savedir_path(name->text);
	bool ok = remove(path) == 0;
	free(path);
	return ok;
}

static bool System_CopySaveFile(struct string *src, struct string *dst)
{
	if (!src || !dst)
		return false;
	char *sp = savedir_path(src->text), *dp = savedir_path(dst->text);
	FILE *in = fopen(sp, "rb");
	if (!in) {
		free(sp); free(dp);
		return false;
	}
	FILE *out = fopen(dp, "wb");
	if (!out) {
		fclose(in);
		free(sp); free(dp);
		return false;
	}
	char buf[8192];
	size_t n;
	bool ok = true;
	while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
		if (fwrite(buf, 1, n, out) != n) {
			ok = false;
			break;
		}
	}
	fclose(in);
	if (fclose(out) != 0)
		ok = false;
	free(sp);
	free(dp);
	return ok;
}

/* ---------------- 简单接口 ---------------- */

static void System_Peek(void)
{
}

static void System_Exit(int code)
{
	vm_exit(code);
}

static void System_Reset(void)
{
	vm_reset();
}

static bool System_IsDebugMode(void)
{
	return false;
}

static void System_Sleep(int time)
{
	vm_sleep(time);
}

static struct string *System_Output(struct string *s)
{
	if (s)
		printf("%s\n", s->text);
	return string_dup(s ? s : &EMPTY_STRING);
}

static struct string *System_MsgBox(struct string *s)
{
	if (s)
		printf("[MsgBox] %s\n", s->text);
	/* [D498] 菜单「关闭游戏」等确认走真对话框（原先只 printf，AIN 拿到
	 * 返回值就继续——在真实显示器上用户根本看不到提示）。按钮 id 与
	 * vm.c 的 SYS_MSGBOX_OK_CANCEL 同口径：OK=1 / Cancel=0。 */
	char *utf = s ? sjis2utf(s->text, s->size) : NULL;
	SDL_ShowSimpleMessageBox(0, "xsystem4", utf ? utf : "", NULL);
	free(utf);
	return string_dup(s ? s : &EMPTY_STRING);
}

static const SDL_MessageBoxButtonData system_ok_cancel_buttons[] = {
	{ SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, "OK" },
	{ SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "Cancel" },
};

static int System_MsgBoxOkCancel(struct string *s)
{
	if (s)
		printf("[MsgBoxOkCancel] %s\n", s->text);
	/* [D498] 原来恒返回 1(OK)——菜单「关闭游戏」点一下就真退了。
	 * 现在弹真对话框。无 WM 的 Xvfb 里 SDL 弹窗会失败 → result 保持
	 * 0(Cancel)，跑测安全；真实显示器上用户可以选。 */
	int result = 0;
	char *utf = s ? sjis2utf(s->text, s->size) : NULL;
	const SDL_MessageBoxData mbox = {
		SDL_MESSAGEBOX_INFORMATION,
		NULL,
		"xsystem4",
		utf ? utf : "",
		SDL_arraysize(system_ok_cancel_buttons),
		system_ok_cancel_buttons,
		NULL
	};
	/* SDL3 返回 true = 成功（vm.c 的 SYS_MSGBOX_OK_CANCEL 一直把成功当
	 * 失败打了 WARNING，顺手一起修）。 */
	if (SDL_ShowMessageBox(&mbox, &result)) {
		// result: 1=OK, 0=Cancel（AIN 口径与之一致）
	} else {
		WARNING("MsgBoxOkCancel: SDL dialog failed (%s)", SDL_GetError());
		result = 0;
	}
	free(utf);
	return result;
}

static struct string *System_Error(struct string *s)
{
	if (s) {
		fprintf(stderr, "[Error] %s\n", s->text);
		/* 打印调用栈，定位是哪个 ain 函数在报错 */
		extern void vm_dump_call_stack(int max);
		vm_dump_call_stack(12);
	}
	return string_dup(s ? s : &EMPTY_STRING);
}

static void System_OpenWeb(struct string *url)
{
	if (url)
		SDL_OpenURL(url->text);
}

static struct string *System_GetSaveFolderName(void)
{
	return make_string("SaveData", 8);
}

static struct string *System_GetGameName(void)
{
	return make_string("Rance10", 7);
}

static int System_GetTime(void)
{
	return vm_time();
}

static struct string *System_GetFuncStackName(int n)
{
	return string_dup(&EMPTY_STRING);
}

static bool System_ExistFunc(struct string *name)
{
	return false;
}

HLL_LIBRARY(system,
	HLL_EXPORT(ResumeSave, System_ResumeSave),
	HLL_EXPORT(ResumeLoad, System_ResumeLoad),
	HLL_EXPORT(Peek, System_Peek),
	HLL_EXPORT(Exit, System_Exit),
	HLL_EXPORT(Reset, System_Reset),
	HLL_EXPORT(IsDebugMode, System_IsDebugMode),
	HLL_EXPORT(ResumeWriteComment, System_ResumeWriteComment),
	HLL_EXPORT(ResumeReadComment, System_ResumeReadComment),
	HLL_EXPORT(GroupSave, System_GroupSave),
	HLL_EXPORT(GroupLoad, System_GroupLoad),
	HLL_EXPORT(ExistSaveFile, System_ExistSaveFile),
	HLL_EXPORT(DeleteSaveFile, System_DeleteSaveFile),
	HLL_EXPORT(CopySaveFile, System_CopySaveFile),
	HLL_EXPORT(Sleep, System_Sleep),
	HLL_EXPORT(Output, System_Output),
	HLL_EXPORT(MsgBox, System_MsgBox),
	HLL_EXPORT(MsgBoxOkCancel, System_MsgBoxOkCancel),
	HLL_EXPORT(Error, System_Error),
	HLL_EXPORT(OpenWeb, System_OpenWeb),
	HLL_EXPORT(GetSaveFolderName, System_GetSaveFolderName),
	HLL_EXPORT(GetGameName, System_GetGameName),
	HLL_EXPORT(GetTime, System_GetTime),
	HLL_EXPORT(GetFuncStackName, System_GetFuncStackName),
	HLL_EXPORT(ExistFunc, System_ExistFunc));
