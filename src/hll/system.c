/* system - implementation for Rance X bring-up.
 *
 * Save/load functions are stubs (return false) for now; the rest are real.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL.h>

#include "system4/string.h"

#include "hll.h"
#include "vm.h"
#include "vm/page.h"

/* ---------------- save / load (stubs) ---------------- */

static bool System_ResumeSave(struct string *a, struct string *b, int *c)
{
	WARNING("system.ResumeSave: not implemented (%s, %s)", a ? a->text : "", b ? b->text : "");
	return false;
}

static void System_ResumeLoad(struct string *a, struct string *b)
{
	WARNING("system.ResumeLoad: not implemented (%s, %s)", a ? a->text : "", b ? b->text : "");
}

static bool System_ResumeWriteComment(struct string *a, struct string *b, struct page **c)
{
	return false;
}

static bool System_ResumeReadComment(struct string *a, struct string *b, struct page **c)
{
	return false;
}

static bool System_GroupSave(struct string *a, struct string *b, struct string *c, int *d)
{
	return false;
}

static bool System_GroupLoad(struct string *a, struct string *b, struct string *c, int *d)
{
	return false;
}

static bool System_ExistSaveFile(struct string *name)
{
	return false;
}

static bool System_DeleteSaveFile(struct string *name)
{
	return false;
}

static bool System_CopySaveFile(struct string *src, struct string *dst)
{
	return false;
}

/* ---------------- simple ---------------- */

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
	return string_dup(s ? s : &EMPTY_STRING);
}

static int System_MsgBoxOkCancel(struct string *s)
{
	if (s)
		printf("[MsgBoxOkCancel] %s\n", s->text);
	return 1; /* 1 = OK */
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
