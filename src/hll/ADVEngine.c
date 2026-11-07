/* ADVEngine - instrumented stub for Rance X bring-up.
 *
 * Semantics are undocumented (function-definition files, step/if execution,
 * delegate object IDs). Every entry point logs its arguments so the first run
 * tells us the real call sequence, then behaviour can be filled in.
 */
#include <stdio.h>
#include <string.h>

#include "system4/string.h"

#include "hll.h"
#include "vm.h"
#include "vm/page.h"

#define LOG_CALL(...) WARNING(__VA_ARGS__)

static void ADVEngine_GetFunctionList(struct string *name, struct page **out)
{
	LOG_CALL("ADVEngine.GetFunctionList(%s)", name ? name->text : "");
}

static bool ADVEngine_Load(struct string *name)
{
	LOG_CALL("ADVEngine.Load(%s)", name ? name->text : "");
	return false;
}

static bool ADVEngine_LoadByFile(struct string *path, struct string *name)
{
	LOG_CALL("ADVEngine.LoadByFile(%s, %s)", path ? path->text : "", name ? name->text : "");
	return false;
}

static void ADVEngine_Release(void)
{
	LOG_CALL("ADVEngine.Release()");
}

static bool ADVEngine_IsExistLowerIf(void)
{
	LOG_CALL("ADVEngine.IsExistLowerIf()");
	return false;
}

static int ADVEngine_NumofStep(void)
{
	LOG_CALL("ADVEngine.NumofStep()");
	return 0;
}

static struct string *ADVEngine_GetStepText(int step)
{
	LOG_CALL("ADVEngine.GetStepText(%d)", step);
	return string_dup(&EMPTY_STRING);
}

static void ADVEngine_OpenFunctionFile(int id)
{
	LOG_CALL("ADVEngine.OpenFunctionFile(%d)", id);
}

static int ADVEngine_GetCodeType(int id)
{
	LOG_CALL("ADVEngine.GetCodeType(%d)", id);
	return 0;
}

static struct string *ADVEngine_GetMessage(int id)
{
	LOG_CALL("ADVEngine.GetMessage(%d)", id);
	return string_dup(&EMPTY_STRING);
}

static bool ADVEngine_BindFunction(int id)
{
	LOG_CALL("ADVEngine.BindFunction(%d)", id);
	return false;
}

static struct string *ADVEngine_GetFuncName(int id)
{
	LOG_CALL("ADVEngine.GetFuncName(%d)", id);
	return string_dup(&EMPTY_STRING);
}

static int ADVEngine_CreateDelegateObjectID(int id)
{
	LOG_CALL("ADVEngine.CreateDelegateObjectID(%d)", id);
	return -1;
}

static bool ADVEngine_GetArgumentValue_int(int *out, int a, int b)
{
	LOG_CALL("ADVEngine.GetArgumentValue_int(?, %d, %d)", a, b);
	if (out)
		*out = 0;
	return false;
}

static bool ADVEngine_GetArgumentValue_float(float *out, int a, int b)
{
	LOG_CALL("ADVEngine.GetArgumentValue_float(?, %d, %d)", a, b);
	if (out)
		*out = 0.0f;
	return false;
}

static bool ADVEngine_GetArgumentValue_bool(int *out, int a, int b)
{
	LOG_CALL("ADVEngine.GetArgumentValue_bool(?, %d, %d)", a, b);
	if (out)
		*out = 0;
	return false;
}

static bool ADVEngine_GetArgumentValue_string(struct string **out, int a, int b)
{
	LOG_CALL("ADVEngine.GetArgumentValue_string(?, %d, %d)", a, b);
	if (out)
		*out = string_dup(&EMPTY_STRING);
	return false;
}

static struct string *ADVEngine_GetIfFuncName(int id)
{
	LOG_CALL("ADVEngine.GetIfFuncName(%d)", id);
	return string_dup(&EMPTY_STRING);
}

static struct string *ADVEngine_GetIfFuncArg(int id)
{
	LOG_CALL("ADVEngine.GetIfFuncArg(%d)", id);
	return string_dup(&EMPTY_STRING);
}

static int ADVEngine_GetIfValue(int id)
{
	LOG_CALL("ADVEngine.GetIfValue(%d)", id);
	return 0;
}

static bool ADVEngine_IsLowerIf(int id)
{
	LOG_CALL("ADVEngine.IsLowerIf(%d)", id);
	return false;
}

static struct string *ADVEngine_GetAttribute(struct string *key)
{
	LOG_CALL("ADVEngine.GetAttribute(%s)", key ? key->text : "");
	return string_dup(&EMPTY_STRING);
}

HLL_LIBRARY(ADVEngine,
	HLL_EXPORT(GetFunctionList, ADVEngine_GetFunctionList),
	HLL_EXPORT(Load, ADVEngine_Load),
	HLL_EXPORT(LoadByFile, ADVEngine_LoadByFile),
	HLL_EXPORT(Release, ADVEngine_Release),
	HLL_EXPORT(IsExistLowerIf, ADVEngine_IsExistLowerIf),
	HLL_EXPORT(NumofStep, ADVEngine_NumofStep),
	HLL_EXPORT(GetStepText, ADVEngine_GetStepText),
	HLL_EXPORT(OpenFunctionFile, ADVEngine_OpenFunctionFile),
	HLL_EXPORT(GetCodeType, ADVEngine_GetCodeType),
	HLL_EXPORT(GetMessage, ADVEngine_GetMessage),
	HLL_EXPORT(BindFunction, ADVEngine_BindFunction),
	HLL_EXPORT(GetFuncName, ADVEngine_GetFuncName),
	HLL_EXPORT(CreateDelegateObjectID, ADVEngine_CreateDelegateObjectID),
	HLL_EXPORT(GetArgumentValue_int, ADVEngine_GetArgumentValue_int),
	HLL_EXPORT(GetArgumentValue_float, ADVEngine_GetArgumentValue_float),
	HLL_EXPORT(GetArgumentValue_bool, ADVEngine_GetArgumentValue_bool),
	HLL_EXPORT(GetArgumentValue_string, ADVEngine_GetArgumentValue_string),
	HLL_EXPORT(GetIfFuncName, ADVEngine_GetIfFuncName),
	HLL_EXPORT(GetIfFuncArg, ADVEngine_GetIfFuncArg),
	HLL_EXPORT(GetIfValue, ADVEngine_GetIfValue),
	HLL_EXPORT(IsLowerIf, ADVEngine_IsLowerIf),
	HLL_EXPORT(GetAttribute, ADVEngine_GetAttribute));
