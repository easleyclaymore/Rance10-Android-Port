/* 版权所有 (C) 2024 kichikuou <KichikuouChrome@gmail.com>
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

#include "effect.h"
#include "hll.h"
#include "sact.h"
#include "xsystem4.h"

static struct {
	bool on;
	int type;
	int dst;
	int old;
	int new;
} draw_effect = {false};

bool DrawEffect_Begin(int writeSurface, int wx, int wy, int destSurface, int dx, int dy, int srcSurface, int sx, int sy, int width, int height, int effect)
{
	// 只支持全屏效果
	if (wx || wy || dx || dy || sx || sy || width != config.view_width || height != config.view_height)
		return false;
	draw_effect.on = true;
	draw_effect.type = effect;
	draw_effect.dst = writeSurface;
	draw_effect.old = destSurface;
	draw_effect.new = srcSurface;
	return true;
}

void DrawEffect_Draw(int time, int totalTime)
{
	if (!draw_effect.on)
		return;
	struct sact_sprite *dst = sact_try_get_sprite(draw_effect.dst);
	struct sact_sprite *old = sact_try_get_sprite(draw_effect.old);
	struct sact_sprite *new = sact_try_get_sprite(draw_effect.new);
	if (!dst || !old || !new)
		return;

	Texture *dst_tex = sprite_get_texture(dst);
	Texture *old_tex = sprite_get_texture(old);
	Texture *new_tex = sprite_get_texture(new);
	effect_update_texture(draw_effect.type, dst_tex, old_tex, new_tex, (float)time / (float)totalTime);
	sprite_dirty(dst);
}

void DrawEffect_End(void)
{
	draw_effect.on = false;
}

HLL_LIBRARY(DrawEffect,
	    HLL_EXPORT(Begin, DrawEffect_Begin),
	    HLL_EXPORT(Draw, DrawEffect_Draw),
	    HLL_EXPORT(End, DrawEffect_End)
	    );
