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

#include <math.h>
#include <string.h>

#include "system4.h"
#include "system4/string.h"
#include "system4/utfsjis.h"

#include "xsystem4.h"
#include "parts_internal.h"

static int extract_sjis_char(const char *src, char *dst)
{
	if (SJIS_2BYTE(*src)) {
		dst[0] = src[0];
		dst[1] = src[1];
		dst[2] = '\0';
		return 2;
	}
	dst[0] = src[0];
	dst[1] = '\0';
	return 1;
}

struct string *parts_text_line_get(struct parts_text_line *line)
{
	struct string *s = make_string("", 0);
	for (int i = 0; i < line->nr_chars; i++) {
		string_append_cstr(&s, line->chars[i].ch, strlen(line->chars[i].ch));
	}
	return s;
}

struct string *parts_text_get(struct parts_text *t)
{
	struct string *s = make_string("", 0);
	for (int i = 0; i < t->nr_lines; i++) {
		if (i > 0)
			string_push_back(&s, '\n');
		struct parts_text_line *line = &t->lines[i];
		for (int i = 0; i < line->nr_chars; i++) {
			string_append_cstr(&s, line->chars[i].ch, strlen(line->chars[i].ch));
		}
	}
	return s;
}

static Point text_style_offset(struct text_style *ts)
{
	int x = max(ts->bold_width, ts->edge_left) * ts->scale_x;
	int y = max(ts->bold_width, ts->edge_up);
	return (Point){x,y};
}

static const char *parts_text_append_char(struct parts_text *t, const char *str)
{
	if (*str == '\n') {
		t->lines = xrealloc_array(t->lines, t->nr_lines, t->nr_lines + 1,
				sizeof(struct parts_text_line));
		t->lines[t->nr_lines].height = text_style_height(&t->ts);
		t->nr_lines++;
		return str + 1;
	}

	struct parts_text_line *line = &t->lines[t->nr_lines - 1];
	line->chars = xrealloc_array(line->chars, line->nr_chars, line->nr_chars + 1,
			sizeof(struct parts_text_char));
	struct parts_text_char *ch = &line->chars[line->nr_chars++];

	ch->off = text_style_offset(&t->ts);
	int len = extract_sjis_char(str, ch->ch);
	int width = gfx_size_char(&t->ts, ch->ch);
	int height = text_style_height(&t->ts);
	gfx_init_texture_rgba(&ch->t, width, height, (SDL_Color){0,0,0,0});
	ch->advance = ceilf(gfx_render_textf(&ch->t, 0, 0, ch->ch, &t->ts, false));
	line->width += ch->advance;
	line->height = max(line->height, height);
	return str + len;
}

void parts_text_append(struct parts *parts, struct parts_text *t, struct string *text)
{
	if (!t->nr_lines) {
		t->lines = xcalloc(1, sizeof(struct parts_text_line));
		t->nr_lines = 1;
	}

	const char *msgp = text->text;
	while (*msgp) {
		msgp = parts_text_append_char(t, msgp);
	}

	// 算出文本的尺寸
	float f_width = 0;
	int height = 0;
	for (int i = 0; i < t->nr_lines; i++) {
		f_width = max(f_width, t->lines[i].width);
		if (i > 0)
			height += t->line_space;
		height += t->lines[i].height;
	}
	int width = ceilf(f_width);
	if (!width || !height)
		return;

	parts_set_dims(parts, &t->common, width, height);
}

void parts_text_free(struct parts_text *t)
{
	gfx_delete_texture(&t->common.texture);
	for (int i = 0; i < t->nr_lines; i++) {
		struct parts_text_line *line = &t->lines[i];
		for (int i = 0; i < line->nr_chars; i++) {
			gfx_delete_texture(&line->chars[i].t);
		}
		free(line->chars);
	}
	free(t->lines);
	t->lines = NULL;
	t->nr_lines = 0;
}

/* 用户组件 (component type 15) 的文字：独立于 parts 状态的字形缓存。
 * 不调用 parts_text_append()，因为那条路径会用文字尺寸覆盖 parts 的尺寸
 * —— 用户组件的尺寸来自 EX 的「幅Ｗ」，不能被文字度量改掉。 */
void parts_uc_text_build(struct parts_text *t, const char *label, float size, SDL_Color color)
{
	memset(t, 0, sizeof(*t));
	if (!label || !*label)
		return;
	t->ts = *parts_default_text_style();
	t->ts.size = size;
	t->ts.color = color;
	t->ts.font_size = NULL;
	t->lines = xcalloc(1, sizeof(struct parts_text_line));
	t->nr_lines = 1;
	const char *p = label;
	while (*p)
		p = parts_text_append_char(t, p);
}

int parts_uc_text_width(struct parts_text *t)
{
	int w = 0;
	for (unsigned i = 0; i < t->nr_lines; i++)
		w = max(w, t->lines[i].width);
	return w;
}

int parts_uc_text_height(struct parts_text *t)
{
	int h = 0;
	for (unsigned i = 0; i < t->nr_lines; i++) {
		if (i > 0)
			h += t->line_space;
		h += t->lines[i].height;
	}
	return h;
}

static void parts_text_rerender(struct parts *parts, struct parts_text *t)
{
	if (!t->nr_lines)
		return;
	struct string *text = parts_text_get(t);
	parts_text_free(t);
	t->ts.font_size = NULL; // clear cached font
	t->lines = NULL;
	t->nr_lines = 0;
	parts_text_append(parts, t, text);
	free_string(text);
	parts_dirty(parts);
}

static void parts_text_clear(struct parts *parts, int state)
{
	struct parts_text *text = parts_get_text(parts, state);
	parts_text_free(text);
	text->lines = NULL;
	text->nr_lines = 0;
	text->cursor.x = 0;
	text->cursor.y = 0;
}

bool PE_SetText(int parts_no, struct string *text, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts *parts = parts_get(parts_no);
	parts_text_clear(parts, state);
	parts_text_append(parts, parts_get_text(parts, state), text);
	/* 记录原文供渲染层做"是否与内建控件标签重复"的判定。 */
	free(parts->text_plain);
	parts->text_plain = text ? strdup(text->text) : NULL;
	return true;
}

struct string *PE_GetText(int parts_no, int state)
{
	if (!parts_state_valid(--state))
		return NULL;

	return parts_text_get(parts_get_text(parts_get(parts_no), state));
}

bool PE_AddPartsText(int parts_no, struct string *text, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts *parts = parts_get(parts_no);
	parts_text_append(parts, parts_get_text(parts, state), text);
	return true;
}

bool PE_SetPartsTextSurfaceArea(int parts_no, int x, int y, int w, int h, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts *parts = parts_get(parts_no);
	struct parts_text *text = parts_get_text(parts, state);
	parts_set_surface_area(parts, &text->common, x, y, w, h);
	return true;
}

//bool PE_DeletePartsTopTextLine(int PartsNumber, int State);

bool PE_SetFont(int parts_no, int type, int size, int r, int g, int b, float bold_weight, int edge_r, int edge_g, int edge_b, float edge_weight, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts *parts = parts_get(parts_no);
	struct parts_text *text = parts_get_text(parts, state);
	text->ts.face = type;
	text->ts.size = size;
	text->ts.color = (SDL_Color) { r, g, b, 255 };
	text->ts.weight = bold_weight * 1000;
	text->ts.edge_color = (SDL_Color) { edge_r, edge_g, edge_b, 255 };
	text_style_set_edge_width(&text->ts, edge_weight);
	text->ts.bold_width = bold_weight;
	parts_text_rerender(parts, text);
	return true;
}

bool PE_SetPartsFontType(int parts_no, int type, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts *parts = parts_get(parts_no);
	struct parts_text *text = parts_get_text(parts, state);
	if (text->ts.face == type)
		return true;
	text->ts.face = type;
	parts_text_rerender(parts, text);
	return true;
}

bool PE_SetPartsFontSize(int parts_no, int size, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts *parts = parts_get(parts_no);
	struct parts_text *text = parts_get_text(parts, state);
	if (text->ts.size == size)
		return true;
	text->ts.size = size;
	parts_text_rerender(parts, text);
	return true;
}

bool PE_SetPartsFontColor(int parts_no, int r, int g, int b, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts *parts = parts_get(parts_no);
	struct parts_text *text = parts_get_text(parts, state);
	SDL_Color color = { r, g, b, 255 };
	if (!memcmp(&text->ts.color, &color, sizeof(SDL_Color)))
		return true;
	text->ts.color = color;
	parts_text_rerender(parts, text);
	return true;
}

bool PE_SetPartsFontBoldWeight(int parts_no, float bold_weight, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts *parts = parts_get(parts_no);
	struct parts_text *text = parts_get_text(parts, state);
	if (text->ts.bold_width == bold_weight)
		return true;
	text->ts.bold_width = bold_weight;
	parts_text_rerender(parts, text);
	return true;
}

bool PE_SetPartsFontEdgeColor(int parts_no, int r, int g, int b, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts *parts = parts_get(parts_no);
	struct parts_text *text = parts_get_text(parts, state);
	SDL_Color color = { r, g, b, 255 };
	if (!memcmp(&text->ts.edge_color, &color, sizeof(SDL_Color)))
		return true;
	text->ts.edge_color = color;
	parts_text_rerender(parts, text);
	return true;
}

bool PE_SetPartsFontEdgeWeight(int parts_no, float edge_weight, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts *parts = parts_get(parts_no);
	struct parts_text *text = parts_get_text(parts, state);
	float old_left = text->ts.edge_left;
	float old_up = text->ts.edge_up;
	float old_right = text->ts.edge_right;
	float old_down = text->ts.edge_down;
	text_style_set_edge_width(&text->ts, edge_weight);
	if (text->ts.edge_left == old_left && text->ts.edge_up == old_up &&
	    text->ts.edge_right == old_right && text->ts.edge_down == old_down)
		return true;
	parts_text_rerender(parts, text);
	return true;
}

/* PE_SetTextCharSpace / PE_SetTextLineSpace 存下的字符/行间距。
 * 部件不存在或不是文本状态时读作 0，
 * 与渲染器「不额外加间距」的默认一致。 */
int PE_GetTextCharSpace(int parts_no, int state)
{
	if (!parts_state_valid(--state))
		return 0;
	struct parts *parts = parts_try_get(parts_no);
	if (!parts)
		return 0;
	return parts->states[state].text.ts.font_spacing;
}

int PE_GetTextLineSpace(int parts_no, int state)
{
	if (!parts_state_valid(--state))
		return 0;
	struct parts *parts = parts_try_get(parts_no);
	if (!parts)
		return 0;
	return parts->states[state].text.line_space;
}

/* [D294] 文本排版原点，来自 EX 的 `文本エリア = (x, y, 0, 0)`。 */
void PE_SetTextArea(int parts_no, int x, int y)
{
	struct parts *parts = parts_get(parts_no);
	if (parts->states[parts->state].type != PARTS_TEXT)
		return;
	struct parts_text *t = &parts->states[parts->state].text;
	t->area_x = x;
	t->area_y = y;
	parts_component_dirty(parts);
}

bool PE_SetTextCharSpace(int parts_no, int char_space, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts *parts = parts_get(parts_no);
	struct parts_text *text = parts_get_text(parts, state);
	if (text->ts.font_spacing == char_space)
		return true;
	text->ts.font_spacing = char_space;
	parts_text_rerender(parts, text);
	return true;
}

bool PE_SetTextLineSpace(int parts_no, int line_space, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts *parts = parts_get(parts_no);
	struct parts_text *text = parts_get_text(parts, state);
	if (text->line_space == line_space)
		return true;
	text->line_space = line_space;
	parts_text_rerender(parts, text);
	return true;
}


