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

#ifndef SYSTEM4_HLL_CHRLOADER_H
#define SYSTEM4_HLL_CHRLOADER_H

#include <stddef.h>

struct archive_data;
struct string;

void chr_loader_init(const char *dir);
bool chr_loader_exists(int id);
struct string *chr_loader_get_name(int id);
struct string *chr_loader_get_string(int id, int index);
int chr_loader_get_id(int id);
int chr_loader_get_status(int id, int type);
int chr_loader_get_blob_handle(int id, int index);
void *chr_loader_get_blob(int blob_handle, size_t *size);
struct archive_data *chr_loader_get_blob_as_archive_data(int blob_handle);

#endif /* SYSTEM4_HLL_CHRLOADER_H */
