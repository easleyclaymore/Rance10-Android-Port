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

#ifndef SYSTEM4_ASSET_MANAGER_H
#define SYSTEM4_ASSET_MANAGER_H

#include <stdbool.h>

struct archive_data;
struct cg;
struct cg_metrics;

enum asset_type {
	ASSET_BGM,
	ASSET_SOUND,
	ASSET_VOICE,
	ASSET_CG,
	ASSET_FLAT,
	ASSET_PACT,
	ASSET_DATA,
	ASSET_FLASH
};
#define ASSET_TYPE_MAX (ASSET_FLASH+1)

const char *asset_strtype(enum asset_type type);
void asset_manager_init(void);
bool asset_manager_load_archive(enum asset_type type, const char *archive_name);
bool asset_exists(enum asset_type type, int no);
bool asset_exists_by_name(enum asset_type type, const char *name, int *id_out);
/* [D498] 精确名字查找。asset 层的 `*_by_name` 走 **basename** 语义（忽略扩展名
 * 与大小写），会让 `Asra\….pact:N`（任意实例号）全部命中 —— AIN 的
 * `GetFreeName` 找空闲实例号会死循环（实测 1267 万行日志风暴）。 */
bool asset_exists_by_name_exact(enum asset_type type, const char *name, int *id_out);
struct archive_data *asset_get_by_name_exact(enum asset_type type, const char *name, int *id_out);
struct archive_data *asset_get(enum asset_type type, int no);
struct archive_data *asset_get_by_name(enum asset_type type, const char *name, int *id_out);

struct cg *asset_cg_load(int no);
struct cg *asset_cg_load_by_name(const char *name, int *id_out);
bool asset_cg_get_metrics(int no, struct cg_metrics *metrics);
bool asset_cg_get_metrics_by_name(const char *name, struct cg_metrics *metrics);

#endif /* SYSTEM4_ASSET_MANAGER_H */
