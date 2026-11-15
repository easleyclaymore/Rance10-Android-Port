/* 版权所有 (C) 2024 Nunuhara Cabbage <nunuhara@haniwa.technology>
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

#include "gfx/font.h"
#include "sact.h"
#include "sprite.h"
#include "vm/page.h"
#include "xsystem4.h"
#include "hll.h"

static Point NewFont_cursor = { 0, 0 };
static struct text_render_metrics NewFont_metrics = {0};
static struct text_render_metrics NewFont_shadow_metrics = {0};

// XXX: not used
//static void NewFont_AlphaComposite(int dst_sp, int dst_x, int dst_y, int src_sp, int src_x, int src_y, int w, int h)

static int NewFont_SetTextMetrics(struct page **_tm)
{
	union vm_value *tm = (*_tm)->values;
	float edge_width = max(tm[6].i, max(tm[7].i, max(tm[8].i, tm[9].i)));
	enum font_weight weight = gfx_int_to_font_weight(tm[4].i);
	struct font_size *font_size = gfx_font_get_size(tm[5].i, tm[3].i);
	NewFont_metrics = (struct text_render_metrics) {
		.color = {
			.r = tm[0].i,
			.g = tm[1].i,
			.b = tm[2].i,
			.a = 255
		},
		.weight = weight,
		.edge_width = 0,
		.scale_x = 1.f,
		.space_scale_x = 1.f,
		.font_spacing = 0.f,
		.edge_spacing = 0.f,
		.mode = edge_width < 0.1 ? RENDER_COPY : RENDER_BLENDED,
		.font_size = font_size
	};
	NewFont_shadow_metrics = (struct text_render_metrics) {
		.color = {
			.r = tm[10].i,
			.g = tm[11].i,
			.b = tm[12].i,
			.a = 255
		},
		.weight = weight,
		.edge_width = edge_width,
		.scale_x = 1.f,
		.space_scale_x = 1.f,
		.font_spacing = 0.f,
		.edge_spacing = 0.f,
		.mode = RENDER_COPY,
		.font_size = font_size
	};

	return 1;
}

static void NewFont_SetTextX(int x)
{
	NewFont_cursor.x = x;
}

static void NewFont_SetTextY(int y)
{
	NewFont_cursor.y = y;
}

static int NewFont_GetTextX(void)
{
	return NewFont_cursor.x;
}

static int NewFont_GetTextY(void)
{
	return NewFont_cursor.y;
}

static void NewFont_SetTextPos(int x, int y)
{
	NewFont_cursor.x = x;
	NewFont_cursor.y = y;
}

static void NewFont_GetTextPos(int *x, int *y)
{
	*x = NewFont_cursor.x;
	*y = NewFont_cursor.y;
}

static int NewFont_DrawChar(int sp_no, int c)
{
	struct sact_sprite *sp = sact_get_sprite(sp_no);
	if (!sp)
		return 0;

	char text[3] = { c & 0xff, c >> 8, 0 };
	struct texture *t = sprite_get_texture(sp);
	NewFont_metrics.x = NewFont_cursor.x;
	NewFont_metrics.y = NewFont_cursor.y;
	_gfx_render_text(t, text, &NewFont_metrics);
	return 1;
}

static int NewFont_DrawShadowChar(int sp_no, int c)
{
	struct sact_sprite *sp = sact_get_sprite(sp_no);
	if (!sp)
		return 0;

	char text[3] = { c & 0xff, c >> 8, 0 };
	struct texture *t = sprite_get_texture(sp);
	NewFont_shadow_metrics.x = NewFont_cursor.x;
	NewFont_shadow_metrics.y = NewFont_cursor.y;
	_gfx_render_text(t, text, &NewFont_shadow_metrics);
	return 1;
}

static int NewFont_MeasureChar(int c1, int c2)
{
	struct font_size *size = NewFont_metrics.font_size;
	//return lround(size->font->size_char_kerning(size, c1, c2));
	return ceilf(size->font->size_char_kerning(size, c1, c2));
}

// XXX: not used
//static void NewFont_MultiplyAlphaByOriginalImageAlpha(int dst_sp, int dst_x, int dst_y, int src_sp, int src_x, int src_y, int w, int h)

HLL_LIBRARY(NewFont,
	HLL_TODO_EXPORT(AlphaComposite, NewFont_AlphaComposite),
	HLL_EXPORT(SetTextMetrics, NewFont_SetTextMetrics),
	HLL_EXPORT(SetTextX, NewFont_SetTextX),
	HLL_EXPORT(SetTextY, NewFont_SetTextY),
	HLL_EXPORT(GetTextX, NewFont_GetTextX),
	HLL_EXPORT(GetTextY, NewFont_GetTextY),
	HLL_EXPORT(SetTextPos, NewFont_SetTextPos),
	HLL_EXPORT(GetTextPos, NewFont_GetTextPos),
	HLL_EXPORT(DrawChar, NewFont_DrawChar),
	HLL_EXPORT(DrawShadowChar, NewFont_DrawShadowChar),
	HLL_EXPORT(MeasureChar, NewFont_MeasureChar),
	HLL_TODO_EXPORT(MultiplyAlphaByOriginalImageAlpha, NewFont_MultiplyAlphaByOriginalImageAlpha)
);
