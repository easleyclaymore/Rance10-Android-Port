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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "system4/string.h"
#include "system4/utfsjis.h"

#include "hll.h"

HLL_WARN_UNIMPLEMENTED(false, bool, HTTPDownloader, Get, struct string *pIURL);
HLL_WARN_UNIMPLEMENTED(false, bool, HTTPDownloader, Post, struct string *pIURL, struct string *pIParam);
/* [D459] 下载器的"没有下载任务"状态。
 *
 * 本引擎是离线单机，`Get`/`Post` 已经是 `HLL_WARN_UNIMPLEMENTED(false, …)`
 * —— 即**启动下载就失败**。所以同族的查询接口必须和这个前提一致：
 * 永远没有在跑的任务、永远没有可读数据。否则调用方会以为下载进行中，
 * 在一个永远不会有数据的 socket 上死等。 */
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

/* ==========================================================================
 * [D494] 编码四条：`bool XXX(ref string str)` —— 原地转换，返回成功与否。
 *
 * 签名判据（两层，均为离线证据）：
 *  1. `--dump-hll` 第 6 列（D493 新增参数类型表）：四个函数都是
 *     `nr_args=1, ret=bool(47), arg0=ref string(20)`。
 *  2. 调用点形状与 `PCInfo.GetOSInfo`（运行期已验证的 `struct string **`）
 *     完全一致：`PUSHLOCALPAGE n / REF` + CALLHLL + NOT/IFNZ。
 *     当年 D459 卡在"分不清一级指针还是二级指针"——现在声明表里有类型码，
 *     不用猜了。ffi 侧（src/ffi.c）：ref string 编成 `&heap_ptrs[i]`，
 *     调用后写回 heap 槽，旧串引用计数归调用方管，所以替换前要 free_string。
 *
 * 语义判据（AIN 侧配对管线，反汇编实证）：
 *  - 发送：`CASGameNetService@BeginSendCommand(6860)` 把参数逐段
 *    `TransDelimiter` 拼接 → `SJISToUTF8` → `EncodeString` → 存参数数组 → Post；
 *  - 接收：`EndSendCommand(6863)` → `ReadString` → `DecodeString` → `UTF8ToSJIS`。
 *  即发送前把 SJIS 文本转 UTF-8 再做传输编码，接收端逆序还原。
 *  - SJIS/UTF8 转换用 libsys4 的 `sjis2utf`/`utf2sjis`（subprojects/libsys4/
 *    utfsjis.c）—— 与引擎文字显示同一张 s2u 码表，不引 iconv/gconv 依赖。
 *    语义是**尽力而为**：映射不到的字节替换 '?'，不报错，所以恒返回 true。
 *    ⚠ 与 iconv 语义的差异：非法序列不返回 false 而是静默替换；
 *    处理 NUL 结尾的 C 串（AIN 字符串按 SJIS 文本约定，嵌 \0 不在此列）。
 */

bool HTTPDownloader_SJISToUTF8(struct string **str)
{
	char *u = sjis2utf((*str)->text, (*str)->size);
	free_string(*str);
	*str = make_string(u, strlen(u));
	free(u);
	return true;
}

bool HTTPDownloader_UTF8ToSJIS(struct string **str)
{
	char *s = utf2sjis((*str)->text, (*str)->size);
	free_string(*str);
	*str = make_string(s, strlen(s));
	free(s);
	return true;
}

/* RFC 3986 unreserved：字母数字与 `-._~` 不编码，其余按字节 %XX。
 * 按**字节**编码（不是按字符）：UTF-8 多字节序列逐字节转义后天然安全。
 *
 * Encode/Decode 选 **URL 百分号编码**：与"转成 UTF-8 再编码"的管线自洽，
 * 也是 HTTP 参数编码的事实标准。
 * ⚠ 该管线在本引擎里跑不到（Get/Post 硬失败），调用点是**静态上界**
 * （reach_hll d=4），编码格式的最终确认要等真有网络调用时。 */
static bool httpd_percent_encode(struct string **str)
{
	static const char hex[] = "0123456789ABCDEF";
	struct string *s = *str;
	struct string *out = make_string("", 0);
	for (size_t i = 0; i < s->size; i++) {
		unsigned char c = (unsigned char)s->text[i];
		if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
		    (c >= '0' && c <= '9') || c == '-' || c == '.' ||
		    c == '_' || c == '~') {
			string_append_cstr(&out, (const char *)&c, 1);
		} else {
			char esc[2] = { hex[c >> 4], hex[c & 0xF] };
			string_append_cstr(&out, "%", 1);
			string_append_cstr(&out, esc, 2);
		}
	}
	free_string(*str);
	*str = out;
	return true;
}

/* 百分号解码。`%` 后不足两位十六进制属坏序列：返回 false 不动出参。 */
static bool httpd_percent_decode(struct string **str)
{
	struct string *s = *str;
	struct string *out = make_string("", 0);
	for (size_t i = 0; i < s->size; i++) {
		unsigned char c = (unsigned char)s->text[i];
		if (c != '%') {
			string_append_cstr(&out, (const char *)&c, 1);
			continue;
		}
		if (i + 2 >= s->size) {
			WARNING("DecodeString: truncated %% escape at byte %zu", i);
			free_string(out);
			return false;
		}
		char pair[3] = { s->text[i + 1], s->text[i + 2], 0 };
		char *end;
		long v = strtol(pair, &end, 16);
		if (end != pair + 2 || v < 0) {
			WARNING("DecodeString: bad %% escape \"%s\" at byte %zu", pair, i);
			free_string(out);
			return false;
		}
		i += 2;
		char b = (char)v;
		string_append_cstr(&out, &b, 1);
	}
	free_string(*str);
	*str = out;
	return true;
}

bool HTTPDownloader_EncodeString(struct string **str)
{
	return httpd_percent_encode(str);
}

bool HTTPDownloader_DecodeString(struct string **str)
{
	return httpd_percent_decode(str);
}

HLL_LIBRARY(HTTPDownloader,
	    HLL_EXPORT(Get, HTTPDownloader_Get),
	    HLL_EXPORT(Post, HTTPDownloader_Post),
	    HLL_EXPORT(IsRun, HTTPDownloader_IsRun),
	    HLL_EXPORT(Stop, HTTPDownloader_Stop),
	    HLL_EXPORT(GetReadSize, HTTPDownloader_GetReadSize),
	    HLL_EXPORT(ReadString, HTTPDownloader_ReadString),
	    HLL_EXPORT(SJISToUTF8, HTTPDownloader_SJISToUTF8),
	    HLL_EXPORT(UTF8ToSJIS, HTTPDownloader_UTF8ToSJIS),
	    HLL_EXPORT(EncodeString, HTTPDownloader_EncodeString),
	    HLL_EXPORT(DecodeString, HTTPDownloader_DecodeString),
	    HLL_TODO_EXPORT(ReadStringUTF8ToSJIS, HTTPDownloader_ReadStringUTF8ToSJIS)
	    );
