/* String / Int / Float - System 4.3 string & number helpers for Rance X.
 */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "system4/string.h"

#include "hll.h"
#include "vm.h"
#include "vm/heap.h"
#include "vm/page.h"

/* ---------------- Int / Float ---------------- */

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

/* ---------------- String ---------------- */

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
	/* count characters (UTF-8 aware) */
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

HLL_WARN_UNIMPLEMENTED(false, bool, String, Search, struct string **s, struct page **re, struct string *pat);
HLL_WARN_UNIMPLEMENTED(false, bool, String, SearchAll, struct string **s, struct page **re, struct string *pat);
HLL_WARN_UNIMPLEMENTED(false, bool, String, Match, struct string **s, struct string *pat);
HLL_WARN_UNIMPLEMENTED(false, bool, String, Match2, struct string **s, struct page **re, struct string *pat);

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

HLL_WARN_UNIMPLEMENTED(string_dup(&EMPTY_STRING), struct string *, String, ReplaceRegex,
		       struct string **s, struct string *pat, struct string *to);

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
	return String_GetPartLen(s, pos, 1);
}

static struct string *String_GetPartLen(struct string **s, int pos, int count)
{
	/* D63: the old implementation broke out of the copy loop before advancing
	 * p, so count==1 always returned an empty string. main() uses
	 * GetPart(s,6,1) -> Int() as a loop condition and spun forever. */
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

/* String.Split(ref string s, string sep, bool keep_empty) -> ref string[]
 * AIN lib30 f[35], used by Parts@ResolveFlatName to split flat asset paths.
 * Byte-wise splitting is safe here: Rance10 uses ASCII separators ("/", "\\"),
 * and the payload is kept as GBK bytes just like every other string in the VM. */
static struct page *String_Split(struct string **s, struct string *sep, bool keep_empty)
{
	(void)keep_empty;
	if (!s || !*s || !sep || !sep->text)
		return NULL;
	const char *text = (*s)->text;
	const char *needle = sep->text;
	size_t nlen = strlen(needle);

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
		size_t len = hit ? (size_t)(hit - start) : strlen(start);
		heap_string_assign(arr->values[i].i, make_string(start, len));
		i++;
		if (!hit)
			break;
		p = hit + nlen;
		start = p;
	}
	/* [HACK][D119-SPLIT] ret=t79 falls through ffi.c's default branch, which
	 * pushes the raw C return value; AIN addresses pages by heap slot. Register
	 * the array page here and hand the slot back through the pointer-typed
	 * return. Refactor: add an explicit "returns_page" flag to static_hll_function
	 * and let ffi.c do heap_alloc_page() generically. See TECH_DEBT. */
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
/* AIN v12 f[22] GetPart(ref string,int,int)->string is 3-arg;
	 * String_GetPart is the legacy 2-arg form (D62). */
	HLL_EXPORT(GetPart, String_GetPartLen),
	HLL_EXPORT(GetPartLen, String_GetPartLen),
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
	/* AIN lib30 f[35]; linked by name so the table order stays compatible. */
	HLL_EXPORT(Split, String_Split));
