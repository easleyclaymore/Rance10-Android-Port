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

#include <stdlib.h>
#include <string.h>

#include "system4/string.h"
#include "system4/archive.h"
#include "asset_manager.h"
#include "hll.h"

/* AIN AFAFactory type ids (see afa::CG/Flat/Sound_SearchTitle):
 * 2 = CG, 3 = FLAT, 5 = SOUND. Return int, not enum: an unsigned enum turned
 * the -1 sentinel into 0xffffffff and crashed asset_exists_by_name(). */
static int get_asset_type(int type)
{
	switch (type) {
	case 2: return ASSET_CG;
	case 3: return ASSET_FLAT;
	case 5: return ASSET_SOUND;
	default: return -1;
	}
}

HLL_WARN_UNIMPLEMENTED(false, bool, AFAFactory, IsExistArchive, struct string *ArchiveName);

bool AFAFactory_LoadArchive(int type, struct string *archive_name)
{
	int t = get_asset_type(type);
	if (t < 0) {
		WARNING("Unknown asset type: %d", type);
		return false;
	}
	return asset_manager_load_archive(t, archive_name->text);
}

/* AFAFactory.SearchTitle(Type, Name) -> archive entry id, or -1.
 * Used by the title scene to resolve flat asset names. */
static int AFAFactory_SearchTitle(int type, struct string *name)
{
	int t = get_asset_type(type);
	if (t < 0 || !name)
		return -1;
	int id = -1;
	return asset_exists_by_name(t, name->text, &id) ? id : -1;
}

/* [D209] AFAFactory 搜索族（Rance10 v12 战斗路径）。
 *
 * Parts@ResolveFlatName(fno 附近 0x47a6xx) 的流程是：
 *   Prefix/SuffixSearchTitle 建立搜索集 → GetCountOfSearchData 看数量
 *   → GetSearchTitleByIndex(0) 取第一个全名 → 之后按名字取资源。
 * 引擎的归档按 basename 索引（asset_exists_by_name），且战斗中传入的
 * 就是完整资源名（实测日志 `シス／战斗／ログ／下地` 能取到 CG 尺寸），
 * 所以把搜索词本身作为唯一候选返回是正确的最小实现：名字解析交给
 * 后续 asset_get_by_name。若将来出现模糊前缀解析失败，应升级为遍历
 * archive_ops->for_each 的真实搜索，而不是在这里猜后缀。 */
static struct {
	int type;
	char *name;
} afa_search;

static int afa_set_search(int type, struct string *name)
{
	int t = get_asset_type(type);
	free(afa_search.name);
	afa_search.name = name ? strdup(name->text) : NULL;
	afa_search.type = t;
	return afa_search.name ? 1 : 0;
}

static int AFAFactory_GetCountOfData(int type)
{
	/* 归档内条目总数：当前 asset_manager 未导出 nr_files，且本函数
	 * 在 Rance10 的 AIN 里没有调用点（ain_xref hll 3 2）。返回 0 表示
	 * 「不提供全量枚举」，不要伪装成真实计数。 */
	(void)type;
	return 0;
}

/* AIN 声明 ret=string：ffi 用返回值传递，不是 out 参数。
 * 实测把签名写成 (int,int,struct string**) 会拿到栈上垃圾指针而 SEGV
 * （core: AFAFactory_GetSearchTitleByIndex out=<__func__.6>）。 */
static struct string *AFAFactory_GetTitleByIndex(int type, int index)
{
	int t = get_asset_type(type);
	struct archive_data *d = (t >= 0) ? asset_get(t, index + 1) : NULL;
	const char *n = (d && d->name) ? d->name : "";
	return make_string(n, strlen(n));
}

static int AFAFactory_PrefixSearchTitle(int type, struct string *name)
{
	return afa_set_search(type, name);
}

static int AFAFactory_SuffixSearchTitle(int type, struct string *name)
{
	return afa_set_search(type, name);
}

static int AFAFactory_GetCountOfSearchData(int type)
{
	(void)type;
	return afa_search.name ? 1 : 0;
}

static struct string *AFAFactory_GetSearchTitleByIndex(int type, int index)
{
	(void)type;
	const char *n = (index == 0 && afa_search.name) ? afa_search.name : "";
	return make_string(n, strlen(n));
}

HLL_LIBRARY(AFAFactory,
	    HLL_EXPORT(IsExistArchive, AFAFactory_IsExistArchive),
	    HLL_EXPORT(LoadArchive, AFAFactory_LoadArchive),
	    HLL_EXPORT(GetCountOfData, AFAFactory_GetCountOfData),
	    HLL_EXPORT(GetTitleByIndex, AFAFactory_GetTitleByIndex),
	    HLL_EXPORT(SearchTitle, AFAFactory_SearchTitle),
	    HLL_EXPORT(PrefixSearchTitle, AFAFactory_PrefixSearchTitle),
	    HLL_EXPORT(SuffixSearchTitle, AFAFactory_SuffixSearchTitle),
	    HLL_EXPORT(GetCountOfSearchData, AFAFactory_GetCountOfSearchData),
	    HLL_EXPORT(GetSearchTitleByIndex, AFAFactory_GetSearchTitleByIndex)
	    );
