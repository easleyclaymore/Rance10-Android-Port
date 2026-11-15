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

#ifndef SYSTEM4_EFFECT_H
#define SYSTEM4_EFFECT_H

struct texture;

enum effect {
	EFFECT_CROSSFADE              = 1,
	EFFECT_FADEOUT                = 2,
	EFFECT_FADEIN                 = 3,
	EFFECT_WHITEOUT               = 4,
	EFFECT_WHITEIN                = 5,
	EFFECT_CROSSFADE_MOSAIC       = 6,
	EFFECT_BLIND_DOWN             = 7,
	EFFECT_BLIND_LR               = 8,
	EFFECT_BLIND_DOWN_LR          = 9,
	EFFECT_ZOOM_BLEND_BLUR        = 10,
	EFFECT_LINEAR_BLUR            = 11,
	EFFECT_UP_DOWN_CROSSFADE      = 12,
	EFFECT_DOWN_UP_CROSSFADE      = 13,
	EFFECT_PENTAGRAM_IN_OUT       = 14,
	EFFECT_PENTAGRAM_OUT_IN       = 15,
	EFFECT_HEXAGRAM_IN_OUT        = 16,
	EFFECT_HEXAGRAM_OUT_IN        = 17,
	EFFECT_AMAP_CROSSFADE         = 18,
	EFFECT_VERTICAL_BAR_BLUR      = 19,
	EFFECT_ROTATE_OUT             = 20,
	EFFECT_ROTATE_IN              = 21,
	EFFECT_ROTATE_OUT_CW          = 22,
	EFFECT_ROTATE_IN_CW           = 23,
	EFFECT_BLOCK_DISSOLVE         = 24,
	EFFECT_POLYGON_ROTATE_Y       = 25,
	EFFECT_POLYGON_ROTATE_Y_CW    = 26,
	EFFECT_OSCILLATE              = 27,
	EFFECT_POLYGON_ROTATE_X_CW    = 28,
	EFFECT_POLYGON_ROTATE_X       = 29,
	EFFECT_ROTATE_ZOOM_BLEND_BLUR = 30,
	EFFECT_ZIGZAG_CROSSFADE       = 31,
	EFFECT_TV_SWITCH_OFF          = 32,
	EFFECT_TV_SWITCH_ON           = 33,
	EFFECT_POLYGON_EXPLOSION      = 34,
	EFFECT_NOISE_CROSSFADE        = 35,
	EFFECT_TURN_PAGE              = 36,
	EFFECT_SEPIA_NOISE_CROSSFADE  = 37,
	EFFECT_CRUMPLED_PAPER_PULL    = 38,
	EFFECT_HORIZONTAL_ZIGZAG      = 39,
	EFFECT_LINEAR_BLUR_HD         = 40,
	EFFECT_VERTICAL_BAR_BLUR_HD   = 41,
	EFFECT_AMAP_CROSSFADE2        = 42,
	EFFECT_ZOOM_LR                = 43,
	EFFECT_ZOOM_RL                = 44,
	EFFECT_CROSSFADE_LR           = 45,
	EFFECT_CROSSFADE_RL           = 46,
	EFFECT_PIXEL_EXPLOSION        = 47,
	EFFECT_ZOOM_IN_CROSSFADE      = 48,
	EFFECT_PIXEL_DROP             = 49,
	EFFECT_BLUR_FADEOUT           = 50,
	EFFECT_BLUR_CROSSFADE         = 51,
	EFFECT_2ROT_ZOOM_BLEND_BLUR   = 52,
	EFFECT_VWAVE_SCROLL_CROSSFADE = 55,
	EFFECT_VWAVE_CROSSFADE        = 60,
	NR_EFFECTS
};

extern const char *effect_names[NR_EFFECTS];

int effect_init(enum effect type);
void effect_update_texture(int type, struct texture *dst, struct texture *old, struct texture *new, float rate);
int effect_update(float rate);
int effect_fini(void);

#endif /* SYSTEM4_EFFECT_H */
