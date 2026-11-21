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
/* [D459] 下载器的"没有下载任务"状态。
 *
 * 本引擎是离线单机，`Get`/`Post` 已经是 `HLL_WARN_UNIMPLEMENTED(false, …)`
 * —— 即**启动下载就失败**。所以同族的查询接口必须和这个前提一致：
 * 永远没有在跑的任务、永远没有可读数据。否则调用方会以为下载进行中，
 * 在一个永远不会有数据的 socket 上死等。
 *
 * 语义对照（args/ret 取自 `--dump-hll`）：
 *   IsRun        0 / bool   → false
 *   Stop         0 / void   → 无任务可停
 *   GetReadSize  0 / int    → 0
 *   ReadString   1 / bool   （`ref string` 出参）→ false，不动出参
 *
 * `UTF8ToSJIS` / `SJISToUTF8` / `EncodeString` / `DecodeString` **故意不补**：
 * 它们唯一的实参在字节码里是 `PUSHLOCALPAGE 1; REF`，
 * 从字节码分不清是 `struct string*` 还是 `struct string**`，
 * 猜错就是往一个字符串对象里写指针，必崩。等有真调用点跑出来再定。 */
static bool HTTPDownloader_IsRun(void)
{
	return false;
}

static void HTTPDownloader_Stop(void)
{
}

static int HTTPDownloader_GetReadSize(void)
{
	return 0;
}

static bool HTTPDownloader_ReadString(struct string **pIString)
{
	(void)pIString;
	return false;
}

HLL_LIBRARY(HTTPDownloader,
	    HLL_EXPORT(Get, HTTPDownloader_Get),
	    HLL_EXPORT(Post, HTTPDownloader_Post),
	    HLL_EXPORT(IsRun, HTTPDownloader_IsRun),
	    HLL_EXPORT(Stop, HTTPDownloader_Stop),
	    HLL_EXPORT(GetReadSize, HTTPDownloader_GetReadSize),
	    HLL_EXPORT(ReadString, HTTPDownloader_ReadString),
	    HLL_TODO_EXPORT(ReadStringUTF8ToSJIS, HTTPDownloader_ReadStringUTF8ToSJIS)
	    );
