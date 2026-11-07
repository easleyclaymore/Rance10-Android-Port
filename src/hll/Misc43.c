/* InstallInfo / Sys43VM / TextFile / TextSurfaceManager
 * System 4.3 companion libraries for Rance X bring-up.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "system4/string.h"

#include "hll.h"
#include "vm.h"
#include "vm/page.h"

/* ---------------- InstallInfo ---------------- */

static bool InstallInfo_IsInstalledGame(struct string *name)
{
	return true;
}

static int InstallInfo_GetInstallMediaType(void)
{
	return 0; /* 0 = installed (non-disc) */
}

HLL_LIBRARY(InstallInfo,
	HLL_EXPORT(IsInstalledGame, InstallInfo_IsInstalledGame),
	HLL_EXPORT(GetInstallMediaType, InstallInfo_GetInstallMediaType));

/* ---------------- Sys43VM ---------------- */

static bool Sys43VM_GetFunctionNameList(struct page **out)
{
	if (out)
		*out = NULL;
	return false;
}

static bool Sys43VM_BeginProfiler(void)
{
	return true;
}

static bool Sys43VM_EndProfiler(void)
{
	return true;
}

static bool Sys43VM_WriteGroupSaveComment(struct string *a, struct string *b, struct string *c)
{
	return false;
}

static bool Sys43VM_ReadGroupSaveComment(struct string *a, struct string *b, struct string **out)
{
	if (out)
		*out = string_dup(&EMPTY_STRING);
	return false;
}

static int Sys43VM_GetActualNumofPage(void)
{
	return 0;
}

static int Sys43VM_GetActualMemorySize(void)
{
	return 0;
}

HLL_LIBRARY(Sys43VM,
	HLL_EXPORT(GetFunctionNameList, Sys43VM_GetFunctionNameList),
	HLL_EXPORT(BeginProfiler, Sys43VM_BeginProfiler),
	HLL_EXPORT(EndProfiler, Sys43VM_EndProfiler),
	HLL_EXPORT(WriteGroupSaveComment, Sys43VM_WriteGroupSaveComment),
	HLL_EXPORT(ReadGroupSaveComment, Sys43VM_ReadGroupSaveComment),
	HLL_EXPORT(GetActualNumofPage, Sys43VM_GetActualNumofPage),
	HLL_EXPORT(GetActualMemorySize, Sys43VM_GetActualMemorySize));

/* ---------------- TextFile ---------------- */

static FILE *tf_fp = NULL;

static bool TextFile_Create(struct string *path)
{
	if (tf_fp) {
		fclose(tf_fp);
		tf_fp = NULL;
	}
	if (!path)
		return false;
	tf_fp = fopen(path->text, "w");
	return tf_fp != NULL;
}

static bool TextFile_Close(void)
{
	if (!tf_fp)
		return false;
	fclose(tf_fp);
	tf_fp = NULL;
	return true;
}

static bool TextFile_Write(struct string *s)
{
	if (!tf_fp || !s)
		return false;
	return fputs(s->text, tf_fp) >= 0;
}

static bool TextFile_ReadAll(struct string *path, struct string **out)
{
	FILE *fp = path ? fopen(path->text, "rb") : NULL;
	if (!fp) {
		if (out)
			*out = string_dup(&EMPTY_STRING);
		return false;
	}
	fseek(fp, 0, SEEK_END);
	long n = ftell(fp);
	fseek(fp, 0, SEEK_SET);
	if (n < 0)
		n = 0;
	char *buf = malloc((size_t)n + 1);
	size_t rd = fread(buf, 1, (size_t)n, fp);
	buf[rd] = '\0';
	fclose(fp);
	if (out)
		*out = make_string(buf, rd);
	free(buf);
	return true;
}

HLL_LIBRARY(TextFile,
	HLL_EXPORT(Create, TextFile_Create),
	HLL_EXPORT(Close, TextFile_Close),
	HLL_EXPORT(Write, TextFile_Write),
	HLL_EXPORT(ReadAll, TextFile_ReadAll));

/* ---------------- TextSurfaceManager ---------------- */

static bool TextSurfaceManager_GetFontWidth(struct string *font, int *out, int a, int b,
					    int c, int d, int e, float f1, float f2,
					    int g, int h, int i)
{
	if (out)
		*out = 0;
	return false; /* font metrics unavailable without a real text engine */
}

HLL_LIBRARY(TextSurfaceManager,
	HLL_EXPORT(GetFontWidth, TextSurfaceManager_GetFontWidth));
