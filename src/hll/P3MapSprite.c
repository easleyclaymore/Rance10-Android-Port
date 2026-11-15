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

#include "gfx/gfx.h"
#include "vm/page.h"
#include "hll.h"
#include "sact.h"
#include "xsystem4.h"

static struct {
	Texture bg;
	Texture grid;
	int blend_rate;
} p3map;

// Initializes the grid texture with isometric tiles based on the provided array.
static void init_grid_texture(Texture *tile, int offset_x, int offset_y, int rows, int cols, struct page *array)
{
	if (array->nr_vars != rows * cols)
		VM_ERROR("Expected %d variables in array, got %d", rows * cols, array->nr_vars);
	for (int row = 0; row < rows; row++) {
		for (int col = 0; col < cols; col++) {
			if (array->values[row * cols + col].i != 0)
				continue;
			// Isometric Coordinates.
			int x = ((col - row) - 1) * (tile->w / 2) + offset_x;
			int y = (col + row) * (tile->h / 2) + offset_y;
			gfx_copy_with_alpha_map(&p3map.grid, x, y, tile, 0, 0, tile->w, tile->h);
		}
	}
}

// Creates the map background and grid textures.
static bool P3MapSprite_Create(int src_sprite, int square_sprite, int x, int y, int line, int column, int blend_rate, struct page *array)
{
	struct sact_sprite *src = sact_try_get_sprite(src_sprite);
	struct sact_sprite *square = sact_try_get_sprite(square_sprite);
	if (!src || !square)
		return false;
	Texture *tx_src = sprite_get_texture(src);
	Texture *tx_square = sprite_get_texture(square);
	int width = tx_src->w;
	int height = tx_src->h;

	if (p3map.bg.handle)
		gfx_delete_texture(&p3map.bg);
	gfx_init_texture_blank(&p3map.bg, width, height);
	gfx_copy(&p3map.bg, 0, 0, tx_src, 0, 0, width, height);

	if (p3map.grid.handle)
		gfx_delete_texture(&p3map.grid);
	gfx_init_texture_rgba(&p3map.grid, width, height, COLOR(0, 0, 0, 0));
	init_grid_texture(tx_square, x, y, line, column, array);

	p3map.blend_rate = blend_rate;
	return true;
}

// Deletes the map background and grid textures.
static void P3MapSprite_Delete(void)
{
	if (p3map.grid.handle)
		gfx_delete_texture(&p3map.grid);
	if (p3map.bg.handle)
		gfx_delete_texture(&p3map.bg);
}

// Copies the map background and optionally blends the grid to a destination sprite.
static bool P3MapSprite_Copy(int dest_sprite, int src_x, int src_y, bool copy_square)
{
	struct sact_sprite *dst = sact_get_sprite(dest_sprite);
	if (!dst)
		return false;
	Texture *tx_dst = sprite_get_texture(dst);
	int w = sprite_get_width(dst);
	int h = sprite_get_height(dst);
	gfx_copy(tx_dst, 0, 0, &p3map.bg, src_x, src_y, w, h);
	if (copy_square)
		gfx_blend_amap_alpha(tx_dst, 0, 0, &p3map.grid, src_x, src_y, w, h, p3map.blend_rate);
	return true;
}

// Sets the blend rate for map rendering.
static bool P3MapSprite_CreateMatrixPixel(int blend_rate)
{
	p3map.blend_rate = blend_rate;
	return true;
}

// Blends the grid texture onto a destination sprite.
static bool P3MapSprite_CopyMatrix(int dest_sprite, int dest_x, int dest_y, int src_x, int src_y, int width, int height)
{
	struct sact_sprite *dst = sact_get_sprite(dest_sprite);
	if (!dst)
		return false;
	Texture *tx_dst = sprite_get_texture(dst);
	gfx_blend_amap(tx_dst, dest_x, dest_y, &p3map.grid, src_x, src_y, width, height);
	gfx_copy_amap_max(tx_dst, dest_x, dest_y, &p3map.grid, src_x, src_y, width, height);
	return true;
}

HLL_LIBRARY(P3MapSprite,
	    HLL_EXPORT(Create, P3MapSprite_Create),
	    HLL_EXPORT(Delete, P3MapSprite_Delete),
	    HLL_EXPORT(Copy, P3MapSprite_Copy),
	    HLL_EXPORT(CreateMatrixPixel, P3MapSprite_CreateMatrixPixel),
	    HLL_EXPORT(CopyMatrix, P3MapSprite_CopyMatrix)
	    );
