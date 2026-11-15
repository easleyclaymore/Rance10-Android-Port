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

#include "system4/string.h"

#include "hll.h"
#include "movie.h"
#include "gfx/gfx.h"

static struct movie_context *mc;

static int PlayMovie_Init(void)
{
	return 1;
}

static int PlayMovie_Load(struct string *filename)
{
	if (mc)
		movie_free(mc);
	mc = movie_load(filename->text);
	return !!mc;
}

static int PlayMovie_Play(void)
{
	if (!mc)
		return false;
	return movie_play(mc);
}

HLL_WARN_UNIMPLEMENTED(, void, PlayMovie, Stop, void);

// PlayMovie is used like this:
//
//   PlayMovie.Load(filename);
//   PlayMovie.Play();
//   while (PlayMovie.IsPlay()) {
//       system.Peek();
//       system.Sleep(1);
//   }
//
// SACT.Update is not called inside the loop, so we do rendering in IsPlay().
static int PlayMovie_IsPlay(void)
{
	if (!mc)
		return false;
	if (!movie_draw(mc, NULL))
		return false;
	return !movie_is_end(mc);
}

static void PlayMovie_Release(void)
{
	if (mc)
		movie_free(mc);
	mc = NULL;
}

HLL_LIBRARY(PlayMovie,
	    HLL_EXPORT(Init, PlayMovie_Init),
	    HLL_EXPORT(Load, PlayMovie_Load),
	    HLL_EXPORT(Play, PlayMovie_Play),
	    HLL_EXPORT(Stop, PlayMovie_Stop),
	    HLL_EXPORT(IsPlay, PlayMovie_IsPlay),
	    HLL_EXPORT(Release, PlayMovie_Release)
	);
