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

#include "system4/string.h"
#include "hll.h"

HLL_WARN_UNIMPLEMENTED( , void, Confirm2, Init, void *imainsystem);
HLL_WARN_UNIMPLEMENTED(1, int,  Confirm2, ExistKeyFile, struct string *filename, int keynum);
//int Confirm2_CheckProtectFile(struct string *filename, int keynum);
//int Confirm2_CreateKeyFile(struct string *filename, int keynum);


static void Confirm2_ExistKeyFile2(possibly_unused struct string *pIFileName, possibly_unused int nKeyNum, struct string **pIResult)
{
	// XXX：这是《Yokubori Saboten》的魔法值
	//      需要确认对其他游戏是否成立
	*pIResult = make_string("moke-", 5);
}

HLL_LIBRARY(Confirm2,
	    HLL_EXPORT(Init, Confirm2_Init),
	    HLL_EXPORT(ExistKeyFile, Confirm2_ExistKeyFile),
	    //HLL_EXPORT(CheckProtectFile, Confirm2_CheckProtectFile),
	    //HLL_EXPORT(CreateKeyFile, Confirm2_CreateKeyFile),
	    HLL_EXPORT(ExistKeyFile2, Confirm2_ExistKeyFile2));
