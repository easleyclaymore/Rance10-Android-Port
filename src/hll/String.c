/* String / Int / Float —— 兰斯10 用的 System 4.3 字符串与数字辅助函数。
 */
#include <ctype.h>
#include <regex.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "system4/string.h"

#include "xsystem4.h"
#include "hll.h"
#include "vm.h"
#include "vm/heap.h"
#include "vm/page.h"

/* ---------------- 整数 / 浮点 ---------------- */

static struct string *Int_String(int *v)
{
	char b[32];
	snprintf(b, sizeof(b), "%d", v ? *v : 0);
	return make_string(b, strlen(b));
}

HLL_LIBRARY(Int, HLL_EXPORT(String, Int_String));

static struct string *Float_String(float *v, int precision)
{
	char b[64];
	snprintf(b, sizeof(b), "%.*f", precision > 0 ? precision : 6, v ? *v : 0.0f);
	return make_string(b, strlen(b));
}

HLL_LIBRARY(Float, HLL_EXPORT(String, Float_String));

/* ---------------- 字符串 ---------------- */

#define SV(x) ((x) ? (x)->text : "")

static void replace_str(struct string **s, const char *buf, size_t len)
{
	struct string *ns = make_string(buf, len);
	if (*s)
		free_string(*s);
	*s = ns;
}

static struct string *String_PadLeftEx(struct string **s, int width, int ch);
static struct string *String_PadRightEx(struct string **s, int width, int ch);


static int String_Int(struct string **s)
{
	/* [D151] XSYS4_STR_INT_DBG=1：把 Int() 真正读到的文本打出来。
	 * `Int(GetPart(PartsName,3)) - 1` 当数组下标用，读空串会得 0 → 下标 -1
	 * → `m_btn[-1]` 空引用。只有看到文本才能分清"GetPart 取错了"和
	 * "取对了但字符串已经死了"。 */
	if (getenv("XSYS4_STR_INT_DBG")) {
		/* 不设总量配额：Startup 期间 Int() 会被调用几十次，配额一定在
		 * 走到编队界面之前就烧光。只按内容过滤（空串 / "Tab" 开头）。 */
		const char *t = (s && *s && (*s)->text) ? (*s)->text : "";
		if (!*t || !strncmp(t, "Tab", 3))
			NOTICE("[strint] text=\"%s\"", *t ? display_sjis0(t) : "");
	}
	return s && *s ? atoi((*s)->text) : 0;
}

static float String_Float(struct string **s)
{
	return s && *s ? (float)atof((*s)->text) : 0.0f;
}

static int String_Length(struct string **s)
{
	if (!s || !*s)
		return 0;
	/* 数字符个数（UTF-8 感知） */
	const unsigned char *p = (const unsigned char *)(*s)->text;
	int n = 0;
	while (*p) {
		if ((*p & 0xC0) != 0x80)
			n++;
		p++;
	}
	return n;
}

static int String_LengthByte(struct string **s)
{
	return (s && *s) ? (int)strlen((*s)->text) : 0;
}

static bool String_Empty(struct string **s)
{
	return !s || !*s || (*s)->text[0] == '\0';
}

static void String_PushBack(struct string **s, int ch)
{
	if (!s || !*s)
		return;
	size_t len = strlen((*s)->text);
	char *buf = malloc(len + 5);
	memcpy(buf, (*s)->text, len);
	int n = 0;
	if (ch < 0x80) {
		buf[len + n++] = (char)ch;
	} else if (ch < 0x800) {
		buf[len + n++] = (char)(0xC0 | (ch >> 6));
		buf[len + n++] = (char)(0x80 | (ch & 0x3F));
	} else if (ch < 0x10000) {
		buf[len + n++] = (char)(0xE0 | (ch >> 12));
		buf[len + n++] = (char)(0x80 | ((ch >> 6) & 0x3F));
		buf[len + n++] = (char)(0x80 | (ch & 0x3F));
	} else {
		buf[len + n++] = (char)(0xF0 | (ch >> 18));
		buf[len + n++] = (char)(0x80 | ((ch >> 12) & 0x3F));
		buf[len + n++] = (char)(0x80 | ((ch >> 6) & 0x3F));
		buf[len + n++] = (char)(0x80 | (ch & 0x3F));
	}
	buf[len + n] = '\0';
	replace_str(s, buf, len + n);
	free(buf);
}

static void String_PopBack(struct string **s)
{
	if (!s || !*s)
		return;
	char *t = (*s)->text;
	size_t len = strlen(t);
	if (len == 0)
		return;
	size_t i = len - 1;
	while (i > 0 && ((unsigned char)t[i] & 0xC0) == 0x80)
		i--;
	char *nb = malloc(i + 1);
	memcpy(nb, t, i);
	nb[i] = '\0';
	replace_str(s, nb, i);
	free(nb);
}

static void String_Erase(struct string **s, int pos, int count)
{
	if (!s || !*s || count <= 0)
		return;
	char *t = (*s)->text;
	int len = (int)strlen(t);
	if (pos < 0 || pos >= len)
		return;
	if (pos + count > len)
		count = len - pos;
	char *nb = malloc((size_t)(len - count + 1));
	memcpy(nb, t, (size_t)pos);
	memcpy(nb + pos, t + pos + count, (size_t)(len - pos - count + 1));
	replace_str(s, nb, len - count);
	free(nb);
}

static void String_Insert(struct string **s, int pos, struct string *ins)
{
	if (!s || !*s || !ins)
		return;
	char *t = (*s)->text;
	int len = (int)strlen(t);
	int ilen = (int)strlen(ins->text);
	if (pos < 0)
		pos = 0;
	if (pos > len)
		pos = len;
	char *buf = malloc((size_t)(len + ilen + 1));
	memcpy(buf, t, (size_t)pos);
	memcpy(buf + pos, ins->text, (size_t)ilen);
	memcpy(buf + pos + ilen, t + pos, (size_t)(len - pos + 1));
	replace_str(s, buf, len + ilen);
	free(buf);
}

static int String_Find(struct string **s, struct string *needle)
{
	if (!s || !*s || !needle)
		return -1;
	char *p = strstr((*s)->text, needle->text);
	return p ? (int)(p - (*s)->text) : -1;
}

static int String_FindLast(struct string **s, struct string *needle)
{
	if (!s || !*s || !needle)
		return -1;
	char *t = (*s)->text, *n = needle->text;
	int last = -1;
	char *p = t;
	while ((p = strstr(p, n)) != NULL) {
		last = (int)(p - t);
		p++;
	}
	return last;
}

static bool String_Contains(struct string **s, struct string *needle)
{
	return String_Find(s, needle) >= 0;
}

static bool String_StartsWith(struct string **s, struct string *prefix)
{
	if (!s || !*s || !prefix)
		return false;
	return strncmp((*s)->text, prefix->text, strlen(prefix->text)) == 0;
}

static bool String_EndsWith(struct string **s, struct string *suffix)
{
	if (!s || !*s || !suffix)
		return false;
	size_t l = strlen((*s)->text), r = strlen(suffix->text);
	if (r > l)
		return false;
	return strcmp((*s)->text + l - r, suffix->text) == 0;
}

/* [D423] `Search/SearchAll/Match2` 的 `re` 采取「只读不写」的保守实现。
 *
 * 签名里 `struct page **re` 是结果输出数组，但条目格式（起点数组 / (start,end)
 * 对数组 / 含不含捕获组）没有取证。三种都写错都会让调用方读出垃圾下标。
 *
 * 反过来看：`re` 若是调用方预建的空数组，**不写**就是一个安全的空结果 ——
 * 调用方遍历 0 项，什么也不做。而 bool 返回值有明确语义（有没有匹配），
 * 这一半是确定可用的。
 *
 * 所以：bool 按真语义算，`re` 一律不写。比恒 false 的桩更接近原意，
 * 又不会因为猜错格式而崩。等拿到一次真实调用样本再把 `re` 补上。
 * 日志里 `String.Search` 1254 次/轮、`SearchAll` 与 `Match2` 各 627 次。 */
static bool String_Search(struct string **s, struct page **re, struct string *pat)
{
	(void)re;
	if (!s || !*s || !pat || pat->text[0] == '\0')
		return false;
	regex_t rx;
	if (regcomp(&rx, pat->text, REG_EXTENDED) != 0)
		return false;
	bool ok = regexec(&rx, (*s)->text, 1, NULL, 0) == 0;
	regfree(&rx);
	return ok;
}

static bool String_SearchAll(struct string **s, struct page **re, struct string *pat)
{
	/* 与 Search 同：`re` 不写，bool = 至少有一处匹配。 */
	return String_Search(s, re, pat);
}

static bool String_Match2(struct string **s, struct page **re, struct string *pat)
{
	(void)re;
	if (!s || !*s || !pat || pat->text[0] == '\0')
		return false;
	regex_t rx;
	if (regcomp(&rx, pat->text, REG_EXTENDED) != 0)
		return false;
	bool ok = regexec(&rx, (*s)->text, 0, NULL, 0) == 0;
	regfree(&rx);
	return ok;
}

/* [D422] `String.Match(ref string s, string pat) -> bool`：整体匹配判断。
 *
 * 与 `ReplaceRegex` 同一套 POSIX regex，按字节处理（GBK 串）。
 * 命中 true；模式空或编译失败按「不匹配」降级，不抛错。 */
static bool String_Match(struct string **s, struct string *pat)
{
	if (!s || !*s || !pat)
		return false;
	if (pat->text[0] == '\0')
		return false;
	regex_t re;
	if (regcomp(&re, pat->text, REG_EXTENDED) != 0)
		return false;
	bool ok = regexec(&re, (*s)->text, 0, NULL, 0) == 0;
	regfree(&re);
	return ok;
}

static struct string *String_Replace(struct string **s, struct string *from, struct string *to)
{
	if (!s || !*s || !from || !to || from->text[0] == '\0')
		return string_dup(*s ? *s : &EMPTY_STRING);
	size_t fl = strlen(from->text), tl = strlen(to->text);
	size_t cap = strlen((*s)->text) + 1, len = 0;
	char *buf = malloc(cap);
	buf[0] = '\0';
	const char *p = (*s)->text;
	const char *q;
	while ((q = strstr(p, from->text)) != NULL) {
		size_t seg = (size_t)(q - p);
		if (len + seg + tl + 1 > cap) {
			cap = (len + seg + tl + 1) * 2;
			buf = realloc(buf, cap);
		}
		memcpy(buf + len, p, seg);
		len += seg;
		memcpy(buf + len, to->text, tl);
		len += tl;
		p = q + fl;
	}
	size_t rest = strlen(p);
	if (len + rest + 1 > cap) {
		cap = len + rest + 1;
		buf = realloc(buf, cap);
	}
	memcpy(buf + len, p, rest + 1);
	struct string *out = make_string(buf, len + rest);
	free(buf);
	return out;
}

/* [D197] `String.ReplaceRegex(ref string, string Regex, string Replacer) -> string`。
 *
 * 之前这里只有 HLL_WARN_UNIMPLEMENTED 的函数体，却**没登记进下面的
 * HLL_LIBRARY(String, …)** —— 所以 C 侧 `fun = NULL`，运行时会直接
 * VM_ERROR("Unimplemented HLL function")。全量 HLL 普查显示它在 AIN 里有
 * 4 个调用点，其中两个是 `BattleLog@Width::get`（战斗日志排版）：
 * 战斗一开就会撞上。模式串是 GBK 字节串，POSIX regex 按字节处理即可。
 *
 * 编译失败时原样返回（降级），宁可多留几个标记也不能把整段日志吞掉。 */
static struct string *String_ReplaceRegex(struct string **s,
		struct string *pat, struct string *to)
{
	if (!s || !*s || !pat || !to)
		return string_dup(s && *s ? *s : &EMPTY_STRING);

	regex_t re;
	if (pat->text[0] == '\0' || regcomp(&re, pat->text, REG_EXTENDED) != 0)
		return string_dup(*s);

	size_t cap = strlen((*s)->text) + 1, len = 0;
	char *buf = malloc(cap);
	buf[0] = '\0';
	const char *p = (*s)->text;
	regmatch_t m;
	size_t tl = strlen(to->text);

	while (*p && regexec(&re, p, 1, &m, 0) == 0) {
		size_t so = (size_t)m.rm_so, eo = (size_t)m.rm_eo;
		size_t rest = strlen(p + eo);
		if (len + so + tl + rest + 1 > cap) {
			cap = (len + so + tl + rest + 1) * 2;
			buf = realloc(buf, cap);
		}
		memcpy(buf + len, p, so);
		len += so;
		memcpy(buf + len, to->text, tl);
		len += tl;
		p += eo;
		/* 零长度匹配（如 `^` / `a*`）会让 p 不前进，必须推进一个字节，
		 * 否则就是死循环。多字节字符按字节推进不影响结果正确性。 */
		if (eo == so) {
			if (!*p) break;
			if (len + 1 + strlen(p + 1) + 1 > cap) {
				cap = (len + strlen(p) + 1) * 2;
				buf = realloc(buf, cap);
			}
			buf[len++] = *p++;
		}
	}
	size_t rest = strlen(p);
	if (len + rest + 1 > cap) {
		cap = len + rest + 1;
		buf = realloc(buf, cap);
	}
	memcpy(buf + len, p, rest + 1);
	struct string *out = make_string(buf, len + rest);
	free(buf);
	regfree(&re);
	return out;
}

static struct string *String_GetPartLen(struct string **s, int pos, int count);

static struct string *String_GetPart(struct string **s, int pos)
{
	/* [D147] 原来的实现整体差一位：`GetPart("Tab1", 3)` 返回 'b' 而不是 '1'。
	 *
	 * 循环在 `i == pos` 时 **break 而没有先 p++**，于是 p 停在"第 pos 个
	 * 字符"的前一个字符上（即下标 pos-1）。单字节字符上看不出来，因为
	 * 调用方常把返回值当"第 pos 个字符"直接比大小写。
	 *
	 * 实锤：`Game/Party/OrganizationCardView@Init` 里的用户组件回调拿
	 * `Int(GetPart(PartsName, 3)) - 1` 当 `m_btn` 的下标（部件名 Tab1..Tab10），
	 * 差一位后 Tab1 得到 -1，`m_btn[-1]` 是空引用，CALLMETHOD 直接
	 * `invalid function index -1`，整个部队编成界面 core dump。
	 *
	 * 语义与 `GetPart(s, pos, 1)` 一致（那个实现是正确的），直接复用。 */
	{
		/* [D152] 上一条（D147）只修对了一半：`GetPart(ref string, Index)`
		 * 是**从 Index 截到末尾**，不是"取一个字符"——三参版才是
		 * `(Index, Length)`。证据（XSYS4_STR_INT_DBG）：
		 *   in="Tab10" pos=3 -> "1"     ← 错：应为 "10"
		 * 于是 Tab10 被当成 Tab1：`m_btn[0]` 初始化两次、`m_btn[9]` 永远
		 * 是空的。编成界面有 10 个页签，越靠后的页签越容易在后续事件里
		 * 拿到空引用。
		 *
		 * 按 pos 走 pos 个字符（跳过 SJIS 前导字节的低位），剩下全取。 */
		struct string *r;
		if (!s || !*s || pos < 0) {
			r = string_dup(&EMPTY_STRING);
		} else {
			const unsigned char *p = (const unsigned char *)(*s)->text;
			for (int i = 0; i < pos && *p; i++) {
				p++;
				while (*p && (*p & 0xC0) == 0x80)
					p++;
			}
			r = make_string((const char *)p, strlen((const char *)p));
		}
		/* [D151] 同一个开关：输入/输出一起看，才能判断是"取错位"还是
		 * "输入本身就是空的"。 */
		if (getenv("XSYS4_STR_INT_DBG") && s && *s && (*s)->text &&
		    !strncmp((*s)->text, "Tab", 3)) {
			/* display_sjis0 返回**静态缓冲**，一次 printf 里调两次会
			 * 互相覆盖（本条日志一开始就把 "1" 打印成了 "Tab1"，
			 * 差点误判成切片没生效）。先各自拷成本地串。 */
			char in[64], out[64];
			snprintf(in, sizeof(in), "%s", display_sjis0((*s)->text));
			snprintf(out, sizeof(out), "%s",
			         (r && r->text) ? display_sjis0(r->text) : "");
			NOTICE("[getpart] in=\"%s\" pos=%d -> \"%s\"", in, pos, out);
		}

		return r;
	}
}

static struct string *String_GetPartLen(struct string **s, int pos, int count)
{
	/* D63：旧实现在推进 p 之前就跳出了拷贝循环，
	 * 所以 count==1 永远返回空串。main() 把
	 * GetPart(s,6,1) -> Int() 当循环条件，于是死循环。 */
	if (!s || !*s || count <= 0)
		return string_dup(&EMPTY_STRING);
	const unsigned char *p = (const unsigned char *)(*s)->text;
	for (int i = 0; i < pos && *p; i++) {
		p++;
		while (*p && (*p & 0xC0) == 0x80) p++;
	}
	const unsigned char *st = p;
	for (int i = 0; i < count && *p; i++) {
		p++;
		while (*p && (*p & 0xC0) == 0x80) p++;
	}
	return make_string((const char *)st, (size_t)(p - st));
}
static struct string *String_PadLeft(struct string **s, int width)
{
	return String_PadLeftEx(s, width, ' ');
}

static struct string *String_PadLeftEx(struct string **s, int width, int ch)
{
	if (!s || !*s)
		return string_dup(&EMPTY_STRING);
	int len = String_Length(s);
	if (len >= width)
		return string_dup(*s);
	int pad = width - len;
	char *buf = malloc((size_t)(pad + (int)strlen((*s)->text) + 1));
	memset(buf, ch, (size_t)pad);
	strcpy(buf + pad, (*s)->text);
	struct string *r = make_string(buf, strlen(buf));
	free(buf);
	return r;
}

static struct string *String_PadRight(struct string **s, int width)
{
	return String_PadRightEx(s, width, ' ');
}

static struct string *String_PadRightEx(struct string **s, int width, int ch)
{
	if (!s || !*s)
		return string_dup(&EMPTY_STRING);
	int len = String_Length(s);
	if (len >= width)
		return string_dup(*s);
	int pad = width - len;
	size_t l = strlen((*s)->text);
	char *buf = malloc((size_t)(l + pad + 1));
	memcpy(buf, (*s)->text, l);
	memset(buf + l, ch, (size_t)pad);
	buf[l + pad] = '\0';
	struct string *r = make_string(buf, l + pad);
	free(buf);
	return r;
}

static struct string *String_ToLower(struct string **s)
{
	if (!s || !*s)
		return string_dup(&EMPTY_STRING);
	struct string *r = string_dup(*s);
	for (char *p = r->text; *p; p++)
		*p = (char)tolower((unsigned char)*p);
	return r;
}

static struct string *String_ToUpper(struct string **s)
{
	if (!s || !*s)
		return string_dup(&EMPTY_STRING);
	struct string *r = string_dup(*s);
	for (char *p = r->text; *p; p++)
		*p = (char)toupper((unsigned char)*p);
	return r;
}

static struct string *String_Trim(struct string **s)
{
	if (!s || !*s)
		return string_dup(&EMPTY_STRING);
	const char *b = (*s)->text;
	const char *e = b + strlen(b);
	while (*b && isspace((unsigned char)*b))
		b++;
	while (e > b && isspace((unsigned char)e[-1]))
		e--;
	return make_string(b, (size_t)(e - b));
}

static struct string *String_TrimChars(struct string **s, struct string *chars)
{
	if (!s || !*s || !chars)
		return string_dup(&EMPTY_STRING);
	const char *b = (*s)->text;
	const char *e = b + strlen(b);
	while (*b && strchr(chars->text, *b))
		b++;
	while (e > b && strchr(chars->text, e[-1]))
		e--;
	return make_string(b, (size_t)(e - b));
}

static struct string *String_TrimStart(struct string **s)
{
	if (!s || !*s)
		return string_dup(&EMPTY_STRING);
	const char *b = (*s)->text;
	while (*b && isspace((unsigned char)*b))
		b++;
	return make_string(b, strlen(b));
}

static struct string *String_TrimStartChars(struct string **s, struct string *chars)
{
	if (!s || !*s || !chars)
		return string_dup(&EMPTY_STRING);
	const char *b = (*s)->text;
	while (*b && strchr(chars->text, *b))
		b++;
	return make_string(b, strlen(b));
}

static struct string *String_TrimEnd(struct string **s)
{
	if (!s || !*s)
		return string_dup(&EMPTY_STRING);
	const char *b = (*s)->text;
	const char *e = b + strlen(b);
	while (e > b && isspace((unsigned char)e[-1]))
		e--;
	return make_string(b, (size_t)(e - b));
}

static struct string *String_TrimEndChars(struct string **s, struct string *chars)
{
	if (!s || !*s || !chars)
		return string_dup(&EMPTY_STRING);
	const char *b = (*s)->text;
	const char *e = b + strlen(b);
	while (e > b && strchr(chars->text, e[-1]))
		e--;
	return make_string(b, (size_t)(e - b));
}

/* String.Split(ref string self, string Separators, bool ContainSeparator) -> ref string[]
 * AIN 库30 的 f[35]（声明见引擎侧 libs.x:1527）。
 *
 * [D292] 第三个参数**不是** keep_empty，而是 `ContainSeparator`：「每段是否
 * 把分隔符本身带在段尾」。这条搞错的代价是整个游戏**所有立绘名字都少一个 `／`**：
 *   `AdvStand::ResolveCgName` 无条件调 `AdvStand::ResolveSaintGirlMonsterCgName`，
 *   后者 `Split(name, "／", true)` 后用 `S_PLUSA2` 把各段**不带分隔符**拼回去
 *   （`code.x` fno 23183 / 23186）。只有 Split 保留分隔符，这个「切开再拼上」
 *   才是恒等变换。丢掉分隔符就得到 `立／希露基本`，`Ａ＿ＣＧ存在确认` 回假，
 *   `AlternativeImage::GetCg` 回退成 `立／？？？` 占位图 —— 这就是用户看到的
 *   「立绘只有头发/整块黑」的真因（实测 [cgmiss] '立／希露基本' NOT FOUND）。
 *
 * 分隔符仍是**子串**语义（兰斯10 用的都是 "／" "\\" "\n" 这类，多字节也成立）。
 * 两个方向都有真实调用点，所以这个标志**必须真的被用上**：
 *   - contain=1：`AdvStand@ResolveSaintGirlMonsterCgName`（fno 23183）Split 后
 *     逐段 `S_PLUSA2` 拼回 —— 要求「切开再拼」恒等，段尾必须带分隔符；
 *   - contain=0：`AdvNamePlate@ResolveFaceCgName`（fno 23144）Split 后
 *     `StringHelper::Join(list, "／")` 拼回 —— 段尾**不能**带分隔符，否则
 *     拼出 `シス／名卡／希露／／基本`（实测未命中），真正存在的是
 *     `シス／名卡／希露／基本`。
 * 第一版只按 contain=1 实现、把标志 `(void)` 掉，等于把 contain=0 的调用方
 * 全弄坏；`Parts@ResolveFlatName`（fno 29529，`Split(name,"/",0)` 后取 Last）
 * 只是碰巧不受影响（末尾无分隔符）。 */
static struct page *String_Split(struct string **s, struct string *sep, bool contain_separator)
{
	if (!s || !*s || !sep || !sep->text)
		return NULL;
	const char *text = (*s)->text;
	/* [D292] 分隔符字节必须先**拷走**再用。实测 contain=0/1 的调用点里，
	 * 上一轮日志打出的 `sep=` 有时是输入串本身、有时是 `dce3 5605` 这类垃圾：
	 * `sep` 指向的堆字符串在切分过程中被后续 make_string/heap_string_assign
	 * 触碰（可写 COW 串的 buffer 复用），直接在循环里边用边引用不稳。
	 * 拷一份既去掉这个隐患，也让探针能如实打印分隔符字节。 */
	char sepbuf[64];
	size_t nlen = strlen(sep->text);
	if (nlen >= sizeof(sepbuf))
		nlen = sizeof(sepbuf) - 1;
	memcpy(sepbuf, sep->text, nlen);
	sepbuf[nlen] = '\0';
	const char *needle = sepbuf;

	int n = 1;
	if (nlen) {
		for (const char *p = text; (p = strstr(p, needle)) != NULL; p += nlen)
			n++;
	}
	union vm_value dims = { .i = n };
	struct page *arr = alloc_array(1, &dims, AIN_ARRAY_STRING, -1, false);
	if (!arr)
		return NULL;

	int i = 0;
	const char *start = text;
	const char *p = text;
	while (i < n) {
		const char *hit = nlen ? strstr(p, needle) : NULL;
		/* ContainSeparator：段尾是否吞掉分隔符本身。两个方向都有调用方
		 * 依赖（见函数头注释），不能只实现一边。 */
		size_t len = hit ? (size_t)(hit - start) + (contain_separator ? nlen : 0)
				 : strlen(start);
		heap_string_assign(arr->values[i].i, make_string(start, len));
		i++;
		if (!hit)
			break;
		p = hit + nlen;
		start = p;
	}
	if (getenv("XSYS4_SPLIT_DBG")) {
		/* 打 fno 才能定位调用点：`display_sjis0` 只认 SJIS，而兰斯10 的
		 * 字符串是 GBK，经它输出是乱码，判据改看 **fno + 恒等校验**。
		 * 分隔符按 sep_len + 十六进制字节打（`%s` 会随堆复用打出垃圾）；
		 * 再按 contain 语义做一次恒等校验：
		 *   contain=1: Σlen == strlen(text)                     （段尾自带分隔符）
		 *   contain=0: Σlen + (n-1)*nlen == strlen(text)        （分隔符被吃掉）
		 * MISMATCH 就是切分语义/参数语义错了，别去别处找。 */
		static int logged;
		if (logged < 120) {
			logged++;
			size_t total = 0;
			for (int k = 0; k < n; k++)
				total += strlen(heap_get_string(arr->values[k].i)->text);
			size_t expect = (size_t)strlen(text)
			                - (contain_separator ? 0 : (size_t)(n - 1) * nlen);
			char hexbuf[3 * 64];
			char *hp = hexbuf;
			for (size_t k = 0; k < nlen; k++)
				hp += snprintf(hp, 4, "%02x", (unsigned char)sepbuf[k]);
			NOTICE("[split] fno=%d contain=%d n=%d in=%zu out=%zu sep_len=%zu sep_hex='%s' %s",
			       hll_current_fno(), contain_separator, n, strlen(text), total,
			       nlen, hexbuf, total == expect ? "MATCH" : "MISMATCH");
			for (int k = 0; k < n; k++)
				NOTICE("[split]   [%d] '%s'", k,
				       heap_get_string(arr->values[k].i)->text);
		}
	}
	/* [HACK][D119-SPLIT] ret=t79 会落到 ffi.c 的默认分支，那里
	 * 压的是 C 的原始返回值；而 AIN 是按堆槽寻址页的。所以这里先注册
	 * 数组页，再把槽号通过指针类型的返回值交回去。
	 * 重构方向：给 static_hll_function 加一个显式的 returns_page 标志，
	 * 由 ffi.c 统一做 heap_alloc_page()。见技术债。 */
	return (struct page *)(intptr_t)(int32_t)heap_alloc_page(arr);
}

HLL_LIBRARY(String,
	HLL_EXPORT(Int, String_Int),
	HLL_EXPORT(Float, String_Float),
	HLL_EXPORT(Length, String_Length),
	HLL_EXPORT(LengthByte, String_LengthByte),
	HLL_EXPORT(Empty, String_Empty),
	HLL_EXPORT(PushBack, String_PushBack),
	HLL_EXPORT(PopBack, String_PopBack),
	HLL_EXPORT(Erase, String_Erase),
	HLL_EXPORT(Insert, String_Insert),
	HLL_EXPORT(Find, String_Find),
	HLL_EXPORT(FindLast, String_FindLast),
	HLL_EXPORT(Contains, String_Contains),
	HLL_EXPORT(StartsWith, String_StartsWith),
	HLL_EXPORT(EndsWith, String_EndsWith),
	HLL_EXPORT(Replace, String_Replace),
	HLL_EXPORT(ReplaceRegex, String_ReplaceRegex),
	HLL_EXPORT(Match, String_Match),
	HLL_EXPORT(Search, String_Search),
	HLL_EXPORT(SearchAll, String_SearchAll),
	HLL_EXPORT(Match2, String_Match2),
/* AIN v12 的 f[22] GetPart(ref string,int,int)->string 是 3 参；
	 * String_GetPart 是老的两参形式（D62）。 */
	/* [D152] 2 参与 3 参是两个重载，必须分开标 arity。
	 * 以前两条都叫 GetPart 却都指向 3 参实现，2 参调用读到垃圾 count。 */
	HLL_EXPORT_N(GetPart, String_GetPart, 2),
	HLL_EXPORT_N(GetPart, String_GetPartLen, 3),
	HLL_EXPORT_N(GetPartLen, String_GetPartLen, 3),
	HLL_EXPORT(PadLeft, String_PadLeft),
	HLL_EXPORT(PadLeftEx, String_PadLeftEx),
	HLL_EXPORT(PadRight, String_PadRight),
	HLL_EXPORT(PadRightEx, String_PadRightEx),
	HLL_EXPORT(ToLower, String_ToLower),
	HLL_EXPORT(ToUpper, String_ToUpper),
	HLL_EXPORT(Trim, String_Trim),
	HLL_EXPORT(TrimChars, String_TrimChars),
	HLL_EXPORT(TrimStart, String_TrimStart),
	HLL_EXPORT(TrimStartChars, String_TrimStartChars),
	HLL_EXPORT(TrimEnd, String_TrimEnd),
	HLL_EXPORT(TrimEndChars, String_TrimEndChars),
	/* AIN 库30 的 f[35]；按名字链接，保持表顺序兼容。 */
	HLL_EXPORT(Split, String_Split));
