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

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "system4.h"
#include "system4/ain.h"
#include "system4/string.h"
#include "vm.h"
#include "vm/heap.h"

#define DIGIT_MAX 512

enum fmt_type {
	FMT_VOID,
	FMT_INT,
	FMT_FLOAT,
	FMT_STRING,
	FMT_CHAR,
	FMT_BOOL
};

struct fmt_spec {
	enum fmt_type type;
	int precision;
	int padding;
	bool zero_pad;
	bool zenkaku;
};

static inline bool is_integer_type(enum string_format_type t)
{
	return t == STRFMT_INT
		|| t == STRFMT_BOOL
		|| t == STRFMT_LONG_INT;
}

static int read_number(const char **_fmt)
{
	int n = 0;
	const char *fmt = *_fmt;
	while (*fmt && *fmt >= '0' && *fmt <= '9') {
		n *= 10;
		n += *fmt - '0';
		fmt++;
	}
	*_fmt = fmt;
	return n;
}

static bool parse_fmt_spec(const char **_fmt, struct fmt_spec *spec, enum string_format_type target)
{
	bool r = false;
	const char *fmt = (*_fmt) + 1;
	memset(spec, 0, sizeof(struct fmt_spec));
	spec->precision = 6;
	while (*fmt) {
		switch (*fmt) {
		case 'd':
			spec->type = FMT_INT;
			r = is_integer_type(target);
			goto end;
		case 'D':
			spec->type = FMT_INT;
			spec->zenkaku = true;
			r = is_integer_type(target);
			goto end;
		case 'f':
			spec->type = FMT_FLOAT;
			r = target == STRFMT_FLOAT;
			goto end;
		case 'F':
			spec->type = FMT_FLOAT;
			spec->zenkaku = true;
			r = target == STRFMT_FLOAT;
			goto end;
		case 's':
			spec->type = FMT_STRING;
			r = target == STRFMT_STRING;
			goto end;
		case 'c':
			spec->type = FMT_CHAR;
			r = is_integer_type(target);
			goto end;
		case 'b':
			spec->type = FMT_BOOL;
			r = is_integer_type(target);
			goto end;
		case '%':
			/* [D202] `%%` 在 string_format 层已折叠为字面 '%'；
			 * 走到这里说明是孤立百分号，按不可识别处理。 */
			goto warn;
		case '0':
			spec->zero_pad = true;
			fmt++;
			break;
		case '1':
		case '2':
		case '3':
		case '4':
		case '5':
		case '6':
		case '7':
		case '8':
		case '9':
			spec->padding = read_number(&fmt);
			break;
		case '.':
			fmt++;
			spec->precision = read_number(&fmt);
			break;
		default:
			goto warn;
		}
	}
warn:
	WARNING("Invalid format specifier: %s", *_fmt);
end:
	if (r)
		*_fmt = ++fmt;
	return r;
}

static void append_fmt(struct string **s, struct fmt_spec *spec, union vm_value arg)
{
	int len;
	char buf[DIGIT_MAX] = { [DIGIT_MAX-1] = '\0' };
	switch (spec->type) {
	case FMT_VOID:
		return;
	case FMT_INT:
		len = int_to_cstr(buf, DIGIT_MAX, arg.i, spec->padding, spec->zero_pad, spec->zenkaku);
		string_append_cstr(s, buf, len);
		break;
	case FMT_FLOAT:
		len = float_to_cstr(buf, DIGIT_MAX, arg.f, spec->padding, spec->zero_pad, spec->precision, spec->zenkaku);
		string_append_cstr(s, buf, len);
		break;
	case FMT_STRING: {
		/* The value is a string reference, but Rance10 also formats optional
		 * fields that were never set: a null, freed or non-string slot must
		 * read as the empty string instead of dereferencing whatever the slot
		 * holds (heap_get_string() only rejects out-of-range indices). */
		struct string *val = NULL;
		if (arg.i > 0 && (size_t)arg.i < heap_size &&
		    heap[arg.i].type == VM_STRING)
			val = heap[arg.i].s;
		if (!val) {
			heap_unref(arg.i);
			val = &EMPTY_STRING;
			break;
		}
		int pad_len = spec->padding - (int)val->size;
		if (pad_len > 0) {
			for (int i = 0; i < pad_len; i++) {
				string_push_back(s, ' ');
			}
		}
		string_append(s, val);
		heap_unref(arg.i);
		break;
	}
	case FMT_CHAR:
		string_push_back(s, arg.i);
		break;
	case FMT_BOOL:
		if (arg.i)
			string_append_cstr(s, "true", 4);
		else
			string_append_cstr(s, "false", 5);
		break;
	}
}

struct string *string_format(struct string *fmt, union vm_value arg, enum string_format_type type)
{
	/* [D202] AIN 的 S_MOD 每次只替换一个格式符，`%%` 是**字面 % 转义**、
	 * 不消耗参数。Rance10 用它做「格式串生成器」：
	 *   `%%0%dD` --S_MOD(位数 3)--> `%03D` --S_MOD(节点 id)--> `００１`
	 * 见 QuestMapObject@ResolveStringNodeId(28494)：var[4] 存生成的格式串，
	 * var[5] 用它格式化 id，再在 EX 节点名列表里查找。
	 *
	 * D201 曾把 parse_fmt_spec 里的 '%%' 当成「格式前缀的一部分」直接跳过，
	 * 于是 `%%0%dD` 被整体解析成零填充半角整数，产出 `3D` 而不是 `%03D`；
	 * 编号解析失败 → KeyBase 变成 `任务数据.<地图>.` → GetQuestEventList
	 * 查不到 `故事/战斗/动画`，事件列表为空，第一章卡在 RunAdvAndBattleEvent。
	 *
	 * 正确语义：逐段复制字面内容，遇 `%%` 折叠成一个 '%' 并继续扫描，
	 * 遇到第一个真正的格式符才用 arg 替换，其后原样保留（供下一次 S_MOD）。 */
	struct string *out = string_ref(&EMPTY_STRING);
	const char *lit = fmt->text;
	for (const char *s = fmt->text; *s; s++) {
		if (*s != '%')
			continue;
		if (s[1] == '%') {
			string_append_cstr(&out, lit, s - lit);
			string_push_back(&out, '%');
			s++;
			lit = s + 1;
			continue;
		}
		struct fmt_spec spec;
		const char *p = s;
		if (parse_fmt_spec(&p, &spec, type)) {
			string_append_cstr(&out, lit, s - lit);
			append_fmt(&out, &spec, arg);
			string_append_cstr(&out, p, strlen(p));
			return out;
		}
	}
	string_append_cstr(&out, lit, strlen(lit));
	return out;
}
