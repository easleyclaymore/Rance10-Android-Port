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

#include "system4/cg.h"
#include "system4/string.h"
#include "asset_manager.h"
#include "hll.h"

static bool CGManager_Init(void *imain_system, int cg_cache_size)
{
	return true;
}

static bool CGManager_LoadArchive(struct string *archive_name)
{
	return asset_manager_load_archive(ASSET_CG, archive_name->text);
}

static bool CGManager_IsExist(struct string *cg_name)
{
	if (!cg_name)
		return false;
	return asset_exists_by_name(ASSET_CG, cg_name->text, NULL);
}

static bool CGManager_GetInfo(struct string *cg_name, int *width, int *height, int *bpp, bool *exist_pixel, bool *exist_alpha)
{
	struct cg_metrics metrics;
	if (!cg_name || !asset_cg_get_metrics_by_name(cg_name->text, &metrics))
		return false;
	*width = metrics.w;
	*height = metrics.h;
	*bpp = metrics.bpp;
	*exist_pixel = metrics.has_pixel;
	*exist_alpha = metrics.has_alpha;
	return true;
}

/*
 * D103: Rance10.ain imports GetSize/GetWidth/GetHeight (fno 2..4) but neither
 * this fork nor upstream implements them. They are thin wrappers over the CG
 * metrics we already use in GetInfo, so a missing CG yields 0 instead of
 * aborting the VM at call time (VM_ERROR is _Noreturn).
 */
static void CGManager_GetSize(struct string *cg_name, int *width, int *height)
{
	struct cg_metrics metrics;
	*width = 0;
	*height = 0;
	if (!cg_name || !asset_cg_get_metrics_by_name(cg_name->text, &metrics))
		return;
	*width = metrics.w;
	*height = metrics.h;
}

static int CGManager_GetWidth(struct string *cg_name)
{
	struct cg_metrics metrics;
	if (!cg_name || !asset_cg_get_metrics_by_name(cg_name->text, &metrics))
		return 0;
	return metrics.w;
}

static int CGManager_GetHeight(struct string *cg_name)
{
	struct cg_metrics metrics;
	if (!cg_name || !asset_cg_get_metrics_by_name(cg_name->text, &metrics))
		return 0;
	return metrics.h;
}

//static int CGManager_GetCountOfDataFromArchive(void);
//static void CGManager_GetTitleByIndexFromArchive(int index, struct string **cg_name);
//static int CGManager_SearchTitleFromArchive(struct string *cg_name);
//static int CGManager_PrefixSearchTitleFromArchive(struct string *cg_name);
//static int CGManager_SuffixSearchTitleFromArchive(struct string *cg_name);
//static int CGManager_GetCountOfSearchDataFromArchive(void);
//static void CGManager_GetSearchTitleByIndexFromArchive(int index, struct string **cg_name);

HLL_LIBRARY(CGManager,
	    HLL_EXPORT(Init, CGManager_Init),
	    HLL_EXPORT(LoadArchive, CGManager_LoadArchive),
	    HLL_EXPORT(IsExist, CGManager_IsExist),
	    HLL_EXPORT(GetInfo, CGManager_GetInfo),
	    HLL_EXPORT(GetSize, CGManager_GetSize),
	    HLL_EXPORT(GetWidth, CGManager_GetWidth),
	    HLL_EXPORT(GetHeight, CGManager_GetHeight),
	    HLL_TODO_EXPORT(GetCountOfDataFromArchive, CGManager_GetCountOfDataFromArchive),
	    HLL_TODO_EXPORT(GetTitleByIndexFromArchive, CGManager_GetTitleByIndexFromArchive),
	    HLL_TODO_EXPORT(SearchTitleFromArchive, CGManager_SearchTitleFromArchive),
	    HLL_TODO_EXPORT(PrefixSearchTitleFromArchive, CGManager_PrefixSearchTitleFromArchive),
	    HLL_TODO_EXPORT(SuffixSearchTitleFromArchive, CGManager_SuffixSearchTitleFromArchive),
	    HLL_TODO_EXPORT(GetCountOfSearchDataFromArchive, CGManager_GetCountOfSearchDataFromArchive),
	    HLL_TODO_EXPORT(GetSearchTitleByIndexFromArchive, CGManager_GetSearchTitleByIndexFromArchive));

