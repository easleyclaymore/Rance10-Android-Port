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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>

#include "system4/file.h"
#include "system4/string.h"
#include "system4/utfsjis.h"

#include "xsystem4.h"
#include "vm/heap.h"
#include "vm/page.h"
#include "hll.h"

static bool FileOperation_ExistFile(struct string *file_name)
{
	char *path = unix_path(file_name->text);
	ustat s;
	bool result = stat_utf8(path, &s) == 0 && S_ISREG(s.st_mode);
	free(path);
	return result;
}

//bool FileOperation_DeleteFile(string FileName);

static bool FileOperation_CopyFile(struct string *dest_file_name, struct string *src_file_name)
{
	char *dest = unix_path(dest_file_name->text);
	char *src = unix_path(src_file_name->text);
	bool result = file_copy(src, dest);
	free(src);
	free(dest);
	return result;
}

//bool FileOperation_GetFileCreationTime(string FileName, ref int nYear, ref int nMonth, ref int nDay, ref int nWeek, ref int nHour, ref int nMin, ref int nSecond);
//bool FileOperation_GetFileLastAccessTime(string FileName, ref int nYear, ref int nMonth, ref int nDay, ref int nWeek, ref int nHour, ref int nMin, ref int nSecond);
//bool FileOperation_GetFileLastWriteTime(string FileName, ref int nYear, ref int nMonth, ref int nDay, ref int nWeek, ref int nHour, ref int nMin, ref int nSecond);

static bool FileOperation_GetFileSize(struct string *file_name, int *size)
{
	char *path = unix_path(file_name->text);
	ustat s;
	if (stat_utf8(path, &s) < 0) {
		WARNING("stat(\"%s\"): %s", display_utf0(path), strerror(errno));
		free(path);
		return false;
	}
	if (!S_ISREG(s.st_mode)) {
		WARNING("stat(\"%s\"): not a regular file", display_utf0(path));
		free(path);
		return false;
	}
	*size = s.st_size;
	free(path);
	return true;
}

static bool FileOperation_ExistFolder(struct string *folder_name)
{
	char *path = unix_path(folder_name->text);
	bool result = is_directory(path);
	free(path);
	return result;
}

static bool FileOperation_CreateFolder(struct string *folder_name)
{
	char *path = unix_path(folder_name->text);
	bool result = mkdir_p(path) == 0;
	if (!result)
		WARNING("mkdir_p(%s): %s", path, strerror(errno));
	free(path);
	return result;
}

static bool rmtree(const char *path)
{
	UDIR *d = opendir_utf8(path);
	if (!d) {
		WARNING("opendir(\"%s\"): %s", display_utf0(path), strerror(errno));
		return false;
	}
	bool ok = true;
	char *d_name;
	while (ok && (d_name = readdir_utf8(d)) != NULL) {
		if (d_name[0] == '.' && (d_name[1] == '\0' || (d_name[1] == '.' && d_name[2] == '\0'))) {
			free(d_name);
			continue;
		}

		char *utf8_path = path_join(path, d_name);
		ustat s;
		if (stat_utf8(utf8_path, &s) < 0) {
			WARNING("stat(\"%s\"): %s", display_utf0(utf8_path), strerror(errno));
			ok = false;
		} else {
			if (S_ISDIR(s.st_mode)) {
				if (!rmtree(utf8_path))
					ok = false;
			} else {
				if (remove_utf8(utf8_path) < 0) {
					WARNING("remove(\"%s\"): %s", display_utf0(utf8_path), strerror(errno));
					ok = false;
				}
			}
		}
		free(utf8_path);
		free(d_name);
	}
	closedir_utf8(d);
	if (ok) {
		if (rmdir_utf8(path) < 0) {
			WARNING("rmdir(\"%s\"): %s", display_utf0(path), strerror(errno));
			ok = false;
		}
	}
	return ok;
}

static bool FileOperation_DeleteFolder(struct string *folder_name)
{
	char *path = unix_path(folder_name->text);
	bool result = rmtree(path);
	free(path);
	return result;
}

/* [D497] 建名字数组。调用点（0xb7956/0xb7912）把返回值经数组赋值
 * （X_SET）拷进局部变量——所以按 Split 的 [D119-SPLIT] 槽号约定交回：
 * heap_alloc_page 注册后把槽号当指针返回。原来的出参形态在 1 参 cif
 * 下 `out` 读的是垃圾（arity 审计揪出）。 */
static struct page *build_name_list(const char *dir_name, bool folders, bool sub);

static struct page *get_file_list(struct string *folder_name, bool folders, bool sub)
{
	char *dir_name = unix_path(folder_name->text);
	struct page *page = build_name_list(dir_name, folders, sub);
	free(dir_name);
	return page;
}

static struct page *build_name_list(const char *dir_name, bool folders, bool sub)
{
	UDIR *d = opendir_utf8(dir_name);
	if (!d) {
		WARNING("opendir(\"%s\"): %s", display_utf0(dir_name), strerror(errno));
		return NULL;
	}

	struct string **names = NULL;
	int nr_names = 0;

	char *d_name;
	while ((d_name = readdir_utf8(d)) != NULL) {
		if (d_name[0] == '.') {
			free(d_name);
			continue;
		}

		char *utf8_path = path_join(dir_name, d_name);
		ustat s;
		if (stat_utf8(utf8_path, &s) < 0) {
			WARNING("stat(\"%s\"): %s", display_utf0(utf8_path), strerror(errno));
			goto loop_next;
		}
		if (folders) {
			if (!S_ISDIR(s.st_mode))
				goto loop_next;
		} else {
			if (!S_ISREG(s.st_mode))
				goto loop_next;
		}

		char *sjis_name = utf2sjis(d_name, 0);
		names = xrealloc_array(names, nr_names, nr_names+1, sizeof(struct string*));
		names[nr_names++] = cstr_to_string(sjis_name);
		free(sjis_name);
		if (sub && S_ISDIR(s.st_mode)) {
			/* [D497] WithSubFolder 变体：递归收集子目录 */
			char *subpath = path_join(dir_name, d_name);
			struct page *subpage = build_name_list(subpath, folders, true);
			free(subpath);
			if (subpage && subpage->nr_vars) {
				int base = nr_names;
				names = xrealloc_array(names, nr_names,
						nr_names + subpage->nr_vars,
						sizeof(struct string*));
				nr_names += subpage->nr_vars;
				for (int i = 0; i < subpage->nr_vars; i++)
					names[base + i] = string_dup(
						heap_get_string(subpage->values[i].i));
			}
			if (subpage)
				free_page(subpage);
		}
	loop_next:
		free(utf8_path);
		free(d_name);
	}
	closedir_utf8(d);

	union vm_value dim = { .i = nr_names };
	struct page *page = alloc_array(1, &dim, AIN_ARRAY_STRING, 0, false);
	for (int i = 0; i < nr_names; i++) {
		page->values[i].i = heap_alloc_string(names[i]);
	}
	free(names);

	return page;
}


static struct page *alloc_empty_string_array(void)
{
	union vm_value dim = { .i = 0 };
	return alloc_array(1, &dim, AIN_ARRAY_STRING, 0, false);
}

/* [D119-SPLIT] 约定：ret=t79 落 ffi default 分支压原始返回值，
 * AIN 按堆槽寻址 → 先 heap_alloc_page 注册、槽号当指针交回。 */
static struct page *FileOperation_GetFileList(struct string *folder_name)
{
	struct page *page = get_file_list(folder_name, false, false);
	return page ? (struct page *)(intptr_t)heap_alloc_page(page)
		    : (struct page *)(intptr_t)heap_alloc_page(
			      alloc_empty_string_array());
}

static struct page *FileOperation_GetFolderList(struct string *folder_name)
{
	struct page *page = get_file_list(folder_name, true, false);
	return page ? (struct page *)(intptr_t)heap_alloc_page(page)
		    : (struct page *)(intptr_t)heap_alloc_page(
			      alloc_empty_string_array());
}

/* GetFileListWithSubFolder：递归变体（此前连 TODO 导出都没有） */
static struct page *FileOperation_GetFileListWithSubFolder(struct string *folder_name)
{
	struct page *page = get_file_list(folder_name, false, true);
	return page ? (struct page *)(intptr_t)heap_alloc_page(page)
		    : (struct page *)(intptr_t)heap_alloc_page(
			      alloc_empty_string_array());
}


HLL_LIBRARY(FileOperation,
	    HLL_EXPORT(ExistFile, FileOperation_ExistFile),
	    HLL_TODO_EXPORT(DeleteFile, FileOperation_DeleteFile),
	    HLL_EXPORT(CopyFile, FileOperation_CopyFile),
	    HLL_TODO_EXPORT(GetFileCreationTime, FileOperation_GetFileCreationTime),
	    HLL_TODO_EXPORT(GetFileLastAccessTime, FileOperation_GetFileLastAccessTime),
	    HLL_TODO_EXPORT(GetFileLastWriteTime, FileOperation_GetFileLastWriteTime),
	    HLL_EXPORT(GetFileSize, FileOperation_GetFileSize),
	    HLL_EXPORT(ExistFolder, FileOperation_ExistFolder),
	    HLL_EXPORT(CreateFolder, FileOperation_CreateFolder),
	    HLL_EXPORT(DeleteFolder, FileOperation_DeleteFolder),
	    HLL_EXPORT(GetFileList, FileOperation_GetFileList),
	    HLL_EXPORT(GetFolderList, FileOperation_GetFolderList),
	    HLL_EXPORT(GetFileListWithSubFolder, FileOperation_GetFileListWithSubFolder));
