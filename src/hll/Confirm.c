/* 版权所有 (C) 2021 kichikuou <KichikuouChrome@gmail.com>
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

#include "hll.h"

HLL_WARN_UNIMPLEMENTED( , void, Confirm, Init, void *imainsystem);
HLL_WARN_UNIMPLEMENTED(1, int,  Confirm, ExistKeyFile, struct string *filename, int keynum);
//int Confirm_CheckProtectFile(struct string *filename, int keynum);
//int Confirm_CreateKeyFile(struct string *filename, int keynum);

HLL_LIBRARY(Confirm,
			HLL_EXPORT(Init, Confirm_Init),
			HLL_EXPORT(ExistKeyFile, Confirm_ExistKeyFile)
			//HLL_EXPORT(CheckProtectFile, Confirm_CheckProtectFile),
			//HLL_EXPORT(CreateKeyFile, Confirm_CreateKeyFile)
			);
