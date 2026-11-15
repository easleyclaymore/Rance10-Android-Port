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

#include "system4/string.h"

#include "hll.h"
#include "gfx/gfx.h"
#include "gfx/font.h"
#include "sact.h"

static int size = 20;
static int weight = 1;  // in pixels
static int pitch = 0;

static bool DrawSimpleText_DrawToAlphaMap(int sp_no, int x, int y, struct string *s)
{
	struct sact_sprite *sp = sact_get_sprite(sp_no);
	if (!sp)
		return NULL;
	sprite_dirty(sp);

	int orig_weight = gfx_get_font_weight();
	int orig_size = gfx_get_font_size();
	int orig_space = gfx_get_font_space();
	gfx_set_font_weight(weight <= 1 ? FW_NORMAL : FW_BOLD);
	gfx_set_font_size(size);
	gfx_set_font_space(pitch);

	gfx_draw_text_to_amap(sprite_get_texture(sp), x, y, s->text);

	gfx_set_font_weight(orig_weight);
	gfx_set_font_size(orig_size);
	gfx_set_font_space(orig_space);
	return true;
}

static void DrawSimpleText_SetSize(int n)
{
	size = n;
}

static int DrawSimpleText_GetSize(void)
{
	return size;
}

static void DrawSimpleText_SetWeight(int n)
{
	weight = n;
}

static int DrawSimpleText_GetWeight(void)
{
	return weight;
}

static void DrawSimpleText_SetPitch(int n)
{
	pitch = n;
}

static int DrawSimpleText_GetPitch(void)
{
	return pitch;
}

HLL_LIBRARY(DrawSimpleText,
	    HLL_EXPORT(DrawToAlphaMap, DrawSimpleText_DrawToAlphaMap),
	    HLL_EXPORT(SetSize, DrawSimpleText_SetSize),
	    HLL_EXPORT(GetSize, DrawSimpleText_GetSize),
	    HLL_EXPORT(SetWeight, DrawSimpleText_SetWeight),
	    HLL_EXPORT(GetWeight, DrawSimpleText_GetWeight),
	    HLL_EXPORT(SetPitch, DrawSimpleText_SetPitch),
	    HLL_EXPORT(GetPitch, DrawSimpleText_GetPitch)
	    );
