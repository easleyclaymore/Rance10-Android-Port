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

#ifndef SYSTEM4_SAVEDATA_H
#define SYSTEM4_SAVEDATA_H

#include "cJSON.h"
#include "vm.h"

void json_load_page(struct page *page, cJSON *vars, bool call_dtors);

int save_json(const char *filename, cJSON *json);
cJSON *load_json(const char *filename);
int save_globals(const char *keyname, const char *filename, const char *group_name, int *n);
int load_globals(const char *keyname, const char *filename, const char *group_name, int *n);
int delete_save_file(const char *filename);

#endif /* SYSTEM4_SAVEDATA_H */
