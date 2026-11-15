/* 版权所有 (C) 2026 kichikuou <KichikuouChrome@gmail.com>
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

#include "sact.h"
#include "gfx/gfx.h"
#include "vm/page.h"
#include "hll.h"
#include "xsystem4.h"

static bool P3SquareSprite_Copy(int dest_handle, int src_handle, int size, struct page *matrix)
{
	struct sact_sprite *dest = sact_try_get_sprite(dest_handle);
	struct sact_sprite *src = sact_try_get_sprite(src_handle);

	if (!dest || !src)
		return false;

	Texture *dest_tex = sprite_get_texture(dest);
	Texture *src_tex = sprite_get_texture(src);

	int tile_w = src_tex->w;
	int tile_h = src_tex->h;
	int half_w = tile_w / 2;
	int half_h = tile_h / 2;

	int canvas_center_x = dest_tex->w / 2;
	int canvas_center_y = dest_tex->h / 2;

	int side_count = size * 2 + 1;

	if (matrix->nr_vars != side_count * side_count)
		VM_ERROR("Expected %d variables in array, got %d", side_count * side_count, matrix->nr_vars);

	int index = 0;
	for (int y = -size; y <= size; ++y) {
		for (int x = -size; x <= size; ++x) {
			// Check if we should draw this tile
			if (matrix->values[index++].i == 0)
				continue;

			// Isometric coordinate conversion
			int screen_pos_x = canvas_center_x + (x - y - 1) * half_w;
			int screen_pos_y = canvas_center_y + (x + y - 1) * half_h;

			// Draw the sprite
			gfx_copy_use_amap_border(dest_tex, screen_pos_x, screen_pos_y, src_tex, 0, 0, tile_w, tile_h, 1);
			gfx_copy_amap_max(dest_tex, screen_pos_x, screen_pos_y, src_tex, 0, 0, tile_w, tile_h);
		}
	}

	return true;
}

HLL_LIBRARY(P3SquareSprite,
	    HLL_EXPORT(Copy, P3SquareSprite_Copy)
	    );
