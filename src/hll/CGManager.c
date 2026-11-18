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

#include <string.h>

#include "system4/cg.h"
#include "system4/utfsjis.h"
#include "system4/string.h"
#include "xsystem4.h"   /* [D292] display_sjis0：CG 存在性探针要打名字 */
#include "asset_manager.h"
#include "hll.h"
#include "vm.h"   /* [D292] vm_stack_trace：未命中 CG 时看是谁在问 */

static bool CGManager_Init(void *imain_system, int cg_cache_size)
{
	return true;
}

static bool CGManager_LoadArchive(struct string *archive_name)
{
	return asset_manager_load_archive(ASSET_CG, archive_name->text);
}

/* [D292] 立绘回退成「立／？？？」（原版占位图）的判定点在 AIN 侧：
 * AlternativeImage::GetCg 先问 `_<base><target>`、再问 `<base><target>`，
 * 两次都是假就返回 `<base>？？？`。所以「希露只画出头发」这类问题第一件事
 * 不是看渲染，而是看 AIN 到底问了哪些名字、引擎答了什么 —— 一旦 AIN 从来没
 * 问过 `立／希露／基本`，渲染再对也没用。值=1 打全部，值=子串只打含该子串的。 */
static bool CGManager_IsExist(struct string *cg_name)
{
	if (!cg_name)
		return false;
	bool r = asset_exists_by_name(ASSET_CG, cg_name->text, NULL);

	/* [D292] 两个探针分开计数，别互相吃名额。
	 *
	 * 这条分支的教训：第一次把 `cgmiss` 挂在 `cgexist` 的过滤器+400 行名额
	 * 下面，结果 `cgexist` 名额被立绘清单（几千个 立／xxx 名字）打光，
	 * 真正想要的「哪个名字没命中」一条都没留下 —— 又一次 [PROBE-GATED]。 */
	const char *filter = getenv("XSYS4_CG_EXIST_DBG");
	if (filter) {
		/* 过滤器按 UTF-8 子串工作：写 `XSYS4_CG_EXIST_DBG=希露` 就能敲中文。 */
		bool match = !filter[0] || filter[0] == '1';
		if (!match) {
			char *u = sjis2utf(cg_name->text, cg_name->size);
			match = u && strstr(u, filter);
			free(u);
		}
		static int logged;
		if (match && logged < 400) {
			logged++;
			NOTICE("[cgexist] '%s' -> %d fno~%d", display_sjis0(cg_name->text), r,
			       hll_current_fno());
		}
	}

	/* 未命中探针：AIN 的 AlternativeImage::GetCg/GetFaceCg/GetFlat 命中
	 * 不了就把 `<base>？？？` 当结果，于是画面上出现「立绘／？？？」占位图。
	 * 要定位是**哪个名字**没命中，只能在这里打 —— 渲染层已经看不到原名了。 */
	if (!r && getenv("XSYS4_CG_MISS_DBG")) {
		/* 按名字去重。这是本探针的第二次迭代：第一版给了 200 行名额，
		 * 全被 `シス／准备／人类军／＊＊` 这类每帧重试的名字吃掉，
		 * 真正想看的 `立／xxx` 一条都没留下 —— [PROBE-GATED] 的典型。
		 * 同一个名字只打第一次，上限按**不同名字**算。 */
		static struct { char *name[512]; int nr; } seen;
		bool dup = false;
		for (int i = 0; i < seen.nr; i++) {
			if (!strcmp(seen.name[i], cg_name->text)) { dup = true; break; }
		}
		if (!dup && seen.nr < 512) {
			seen.name[seen.nr++] = strdup(cg_name->text);
			NOTICE("[cgmiss] '%s' NOT FOUND fno~%d",
			       display_sjis0(cg_name->text), hll_current_fno());
			if (getenv("XSYS4_CG_MISS_STACK"))
				vm_stack_trace();
		}
	}

	return r;
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
 * D103：Rance10.ain 导入了 GetSize/GetWidth/GetHeight（fno 2..4），但
 * 这个分支和上游都没实现。它们只是 GetInfo 里已经在用的那些 CG
 * 度量的薄包装，所以缺 CG 时返回 0，而不是在调用时中止虚拟机
 * （VM_ERROR 是 _noreturn）。
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

