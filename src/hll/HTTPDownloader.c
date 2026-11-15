/* 版权所有 (C) 2025 kichikuou <KichikuouChrome@gmail.com>
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

HLL_WARN_UNIMPLEMENTED(false, bool, HTTPDownloader, Get, struct string *pIURL);
HLL_WARN_UNIMPLEMENTED(false, bool, HTTPDownloader, Post, struct string *pIURL, struct string *pIParam);
//bool HTTPDownloader_IsRun(void);
//void HTTPDownloader_Stop(void);
//int HTTPDownloader_GetReadSize(void);
//bool HTTPDownloader_ReadString(struct string **pIString);
//bool HTTPDownloader_ReadStringUTF8ToSJIS(struct string **pIString);

HLL_LIBRARY(HTTPDownloader,
	    HLL_EXPORT(Get, HTTPDownloader_Get),
	    HLL_EXPORT(Post, HTTPDownloader_Post),
	    HLL_TODO_EXPORT(IsRun, HTTPDownloader_IsRun),
	    HLL_TODO_EXPORT(Stop, HTTPDownloader_Stop),
	    HLL_TODO_EXPORT(GetReadSize, HTTPDownloader_GetReadSize),
	    HLL_TODO_EXPORT(ReadString, HTTPDownloader_ReadString),
	    HLL_TODO_EXPORT(ReadStringUTF8ToSJIS, HTTPDownloader_ReadStringUTF8ToSJIS)
	    );
