/* 版权所有 (C) 2022 kichikuou <KichikuouChrome@gmail.com>
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

HLL_QUIET_UNIMPLEMENTED( , void, Confirm3, Init, void *iMainsystem);
HLL_QUIET_UNIMPLEMENTED(0, int, Confirm3, Initialize, void);
HLL_QUIET_UNIMPLEMENTED(0, int, Confirm3, GetHDDVolumeNumber, void);
HLL_QUIET_UNIMPLEMENTED(5, int, Confirm3, GetDrive, struct string *filename);
HLL_QUIET_UNIMPLEMENTED(0, int, Confirm3, GetRandom, int seed);
HLL_QUIET_UNIMPLEMENTED(true, bool, Confirm3, File_Open, struct string *filename);
HLL_QUIET_UNIMPLEMENTED(true, bool, Confirm3, File_Create, struct string *filename);
HLL_QUIET_UNIMPLEMENTED(true, bool, Confirm3, File_SetPos, int pos);

static bool Confirm3_File_ReadByte(int *data)
{
	*data = 0;
	return true;
}

static bool Confirm3_File_ReadInt(int *data)
{
	*data = 0;
	return true;
}

HLL_QUIET_UNIMPLEMENTED(true, bool, Confirm3, File_WriteInt, int data);
HLL_QUIET_UNIMPLEMENTED(true, bool, Confirm3, File_Close, void);

HLL_LIBRARY(Confirm3,
	    HLL_EXPORT(Init, Confirm3_Init),
	    HLL_EXPORT(GetHDDVolumeNumber, Confirm3_GetHDDVolumeNumber),
	    HLL_EXPORT(Initialize, Confirm3_Initialize),
	    HLL_EXPORT(GetDrive, Confirm3_GetDrive),
	    HLL_EXPORT(GetRandom, Confirm3_GetRandom),
	    HLL_EXPORT(File_Open, Confirm3_File_Open),
	    HLL_EXPORT(File_Create, Confirm3_File_Create),
	    HLL_EXPORT(File_SetPos, Confirm3_File_SetPos),
	    HLL_EXPORT(File_ReadByte, Confirm3_File_ReadByte),
	    HLL_EXPORT(File_ReadInt, Confirm3_File_ReadInt),
	    HLL_EXPORT(File_WriteInt, Confirm3_File_WriteInt),
	    HLL_EXPORT(File_Close, Confirm3_File_Close)
	    );
