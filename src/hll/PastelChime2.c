/* 版权所有 (C) 2025 kichikuou <KichikuouChrome@gmail.com>
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

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <cglm/cglm.h>

#include "system4/ain.h"
#include "system4/buffer.h"
#include "system4/file.h"
#include "system4/string.h"

#include "hll.h"
#include "dungeon/dgn.h"
#include "dungeon/dungeon.h"
#include "dungeon/generator.h"
#include "dungeon/map.h"
#include "queue.h"
#include "sact.h"
#include "vm/heap.h"
#include "vm/page.h"
#include "xsystem4.h"

#define DISTANCE_BETWEEN_PLAYERS 1.f
#define NAMEPLATE_ACTIVATION_DISTANCE 0.5f
#define DOOR_OPEN_DISTANCE 0.4f
#define DOOR_CLOSE_PROXIMITY 0.6f
#define DOOR_OPEN_ANGLE 85.f

static inline int to_grid_coord(float f) {
	return roundf(f / 2.f);
}

struct line_segment {
	vec2 p1;
	vec2 p2;
};

TAILQ_HEAD(player_move_tailq, player_move_point);
struct player_move_point {
	vec3 position;
	int cg_index;
	float distance_to_prev;
	TAILQ_ENTRY(player_move_point) entry;
};

static struct player_move_tailq player_move_queue = TAILQ_HEAD_INITIALIZER(player_move_queue);
static int draw_field_sprite_number = 0;

static int find_struct_slot(struct ain_struct *s, const char *name)
{
	for (int i = 0; i < s->nr_members; i++) {
		if (strcmp(s->members[i].name, name) == 0)
			return i;
	}
	VM_ERROR("Cannot find slot %s in struct %s", name, s->name);
}

static union vm_value *struct_get_var(struct page *page, const char *name)
{
	assert(page->type == STRUCT_PAGE);
	struct ain_struct *s = &ain->structures[page->index];
	return &page->values[find_struct_slot(s, name)];
}

static char *dungeon_data_path(int id)
{
	char buf[20];
	snprintf(buf, sizeof(buf), "P2DGN%03d.DSD", id);
	return savedir_path(buf);
}

HLL_WARN_UNIMPLEMENTED(true, bool, PastelChime2, InitPastelChime2);

static size_t reserve_int32(struct buffer *out)
{
	size_t loc = out->index;
	buffer_write_int32(out, 0);
	return loc;
}

static void write_array(struct buffer *w, struct page *page, int array_type, int struct_type, bool with_size);

static void write_struct(struct buffer *w, struct page *page, bool with_size)
{
	assert(page->type == STRUCT_PAGE);
	struct ain_struct *s = &ain->structures[page->index];

	size_t data_size_loc = with_size ? reserve_int32(w) : 0;
	size_t origin = w->index;

	buffer_write_cstringz(w, "SCT");
	buffer_write_int32(w, 0);
	buffer_write_cstringz(w, s->name);
	buffer_write_int32(w, page->nr_vars);
	assert(page->nr_vars == s->nr_members);

	for (int i = 0; i < page->nr_vars; i++) {
		enum ain_data_type type = s->members[i].type.data;
		buffer_write_int32(w, type);
		switch (type) {
		case AIN_BOOL:
		case AIN_INT:
			buffer_write_int32(w, page->values[i].i);
			break;
		case AIN_FLOAT:
			buffer_write_float(w, page->values[i].f);
			break;
		case AIN_STRING:
			buffer_write_string(w, heap_get_string(page->values[i].i));
			break;
		case AIN_STRUCT:
			write_struct(w, heap_get_page(page->values[i].i), true);
			break;
		case AIN_ARRAY_STRUCT:
			write_array(w, heap_get_page(page->values[i].i), AIN_ARRAY_STRUCT, s->members[i].type.struc, true);
			break;
		default:
			VM_ERROR("unsupported type %d for slot %d in struct %s", type, i, display_sjis0(s->name));
		}
	}
	if (with_size) {
		buffer_write_int32_at(w, data_size_loc, w->index - origin);
	}
}

static void write_array(struct buffer *w, struct page *page, int array_type, int struct_type, bool with_size)
{
	size_t data_size_loc = with_size ? reserve_int32(w) : 0;
	size_t origin = w->index;

	buffer_write_cstringz(w, "ARY");
	buffer_write_int32(w, 0);
	buffer_write_int32(w, array_type);
	buffer_write_cstringz(w, array_type == AIN_ARRAY_STRUCT
		? ain->structures[struct_type].name : "");
	if (!page) {
		// 空数组
		buffer_write_int32(w, 0);
	} else {
		assert(page->type == ARRAY_PAGE);
		assert(page->a_type == array_type);
		if (array_type == AIN_ARRAY_STRUCT)
			assert(page->array.struct_type == struct_type);
		buffer_write_int32(w, page->nr_vars);
		for (int i = 0; i < page->nr_vars; i++) {
			switch (page->a_type) {
			case AIN_ARRAY_INT:
				buffer_write_int32(w, page->values[i].i);
				break;
			case AIN_ARRAY_STRING:
				buffer_write_string(w, heap_get_string(page->values[i].i));
				break;
			case AIN_ARRAY_STRUCT:
				write_struct(w, heap_get_page(page->values[i].i), true);
				break;
			default:
				VM_ERROR("unsupported type %d", page->a_type);
			}
		}
	}
	if (with_size) {
		buffer_write_int32_at(w, data_size_loc, w->index - origin);
	}
}

static bool PastelChime2_DungeonDataSave(int id, struct page **dungeon)
{
	struct dungeon_context *ctx = dungeon_get_context(draw_field_sprite_number);
	if (!ctx || !ctx->dgn)
		return false;

	struct buffer w;
	buffer_init(&w, NULL, 0);
	buffer_write_int32(&w, 0);
	buffer_write_int32(&w, 0);
	buffer_write_int32(&w, 0);
	buffer_write_int32(&w, 0);
	buffer_write_int32(&w, 0x20);  // header size?
	size_t data_size_loc = reserve_int32(&w);
	buffer_write_int32(&w, 0);
	buffer_write_int32(&w, 0);

	struct page *m_dsd = heap_get_page(struct_get_var(*dungeon, "m_dsd")->i);
	write_struct(&w, m_dsd, true);

	// 写入地牢单元格
	size_t prev = 0;
	for (int y = 0; y < ctx->dgn->size_y; y++) {
		for (int z = 0; z < ctx->dgn->size_z; z++) {
			for (int x = 0; x < ctx->dgn->size_x; x++) {
				struct dgn_cell *cell = dgn_cell_at(ctx->dgn, x, y, z);
				size_t current = w.index;
				buffer_write_int8(&w, 0);  // RLE count
				buffer_write_int32(&w, cell->floor);
				buffer_write_int32(&w, cell->ceiling);
				buffer_write_int32(&w, cell->north_wall);
				buffer_write_int32(&w, cell->south_wall);
				buffer_write_int32(&w, cell->east_wall);
				buffer_write_int32(&w, cell->west_wall);
				buffer_write_int32(&w, cell->north_door);
				buffer_write_int32(&w, cell->south_door);
				buffer_write_int32(&w, cell->east_door);
				buffer_write_int32(&w, cell->west_door);
				buffer_write_int32(&w, cell->stairs_texture);
				buffer_write_int32(&w, cell->stairs_orientation);
				buffer_write_int32(&w, cell->lightmap_floor);
				buffer_write_int32(&w, cell->lightmap_ceiling);
				buffer_write_int32(&w, cell->lightmap_north);
				buffer_write_int32(&w, cell->lightmap_south);
				buffer_write_int32(&w, cell->lightmap_east);
				buffer_write_int32(&w, cell->lightmap_west);
				for (int i = 0; i < 5; i++)
					buffer_write_int32(&w, -1);  // lightmap parameters for doors and stairs (unused)
				buffer_write_int32(&w, cell->north_door_lock);
				buffer_write_int32(&w, cell->west_door_lock);
				buffer_write_int32(&w, cell->south_door_lock);
				buffer_write_int32(&w, cell->east_door_lock);
				buffer_write_float(&w, cell->north_door_angle);
				buffer_write_float(&w, cell->west_door_angle);
				buffer_write_float(&w, cell->south_door_angle);
				buffer_write_float(&w, cell->east_door_angle);
				buffer_write_int32(&w, cell->walked);
				// RLE 压缩：若前一个单元格与当前相同，
				// 就把 RLE 计数加一，而不是再写一次单元格。
				if (prev && !memcmp(w.buf + prev + 1, w.buf + current + 1, 128)) {
					w.index -= 129;
					if (++w.buf[prev] == 0xff) {
						prev = 0;
					}
				} else {
					prev = current;
				}
			}
		}
	}

	buffer_write_int32_at(&w, data_size_loc, w.index - 0x20);

	char *path = dungeon_data_path(id);
	bool result = file_write(path, w.buf, w.index);
	free(path);
	free(w.buf);
	return result;
}

static struct page *read_array(struct buffer *r, bool with_size);

static struct page *read_struct(struct buffer *r, bool with_size)
{
	size_t orig_size = r->size;
	if (with_size) {
		uint32_t size = buffer_read_int32(r);
		r->size = min(r->index + size, orig_size);
	}

	if (!buffer_check_bytes(r, "SCT\0", 4) || buffer_read_int32(r) != 0) {
		VM_ERROR("invalid SCT header");
	}
	char *struct_name = buffer_skip_string(r);
	int struct_no = ain_get_struct(ain, struct_name);
	if (struct_no < 0) {
		VM_ERROR("unknown struct %s", display_sjis0(struct_name));
	}
	struct ain_struct *s = &ain->structures[struct_no];
	int nr_slots = buffer_read_int32(r);
	if (nr_slots != s->nr_members) {
		VM_ERROR("expected %d slots for struct %s, got %d", s->nr_members, display_sjis0(struct_name), nr_slots);
	}
	struct page *page = alloc_page(STRUCT_PAGE, struct_no, nr_slots);
	for (int i = 0; i < nr_slots; i++) {
		int type = buffer_read_int32(r);
		switch (type) {
		case AIN_BOOL:
		case AIN_INT:
			page->values[i].i = buffer_read_int32(r);
			break;
		case AIN_FLOAT:
			page->values[i].f = buffer_read_float(r);
			break;
		case AIN_STRING:
			page->values[i].i = heap_alloc_string(buffer_read_string(r));
			break;
		case AIN_STRUCT:
			page->values[i].i = heap_alloc_page(read_struct(r, true));
			break;
		case AIN_ARRAY_STRUCT:
			page->values[i].i = heap_alloc_page(read_array(r, true));
			break;
		default:
			VM_ERROR("unsupported type %d for slot %d in struct %s", type, i, display_sjis0(struct_name));
		}
	}
	if (with_size) {
		if (buffer_remaining(r) != 0) {
			VM_ERROR("trailing data after struct");
		}
		r->size = orig_size;
	}
	return page;
}

static struct page *read_array(struct buffer *r, bool with_size)
{
	size_t orig_size = r->size;
	if (with_size) {
		uint32_t size = buffer_read_int32(r);
		r->size = min(r->index + size, orig_size);
	}

	if (!buffer_check_bytes(r, "ARY\0", 4) || buffer_read_int32(r) != 0) {
		VM_ERROR("invalid ARY header");
	}
	int array_type = buffer_read_int32(r);
	char *struct_name = buffer_skip_string(r);
	int nr_vars = buffer_read_int32(r);
	int struct_type = 0;
	if (array_type == AIN_ARRAY_STRUCT) {
		struct_type = ain_get_struct(ain, struct_name);
		if (struct_type < 0) {
			VM_ERROR("unknown struct %s", display_sjis0(struct_name));
		}
	}
	struct page *array = alloc_page(ARRAY_PAGE, array_type, nr_vars);
	array->array.struct_type = struct_type;
	array->array.rank = 1;
	for (int i = 0; i < nr_vars; i++) {
		switch (array_type) {
		case AIN_ARRAY_INT:
			array->values[i].i = buffer_read_int32(r);
			break;
		case AIN_ARRAY_STRING:
			array->values[i].i = heap_alloc_string(buffer_read_string(r));
			break;
		case AIN_ARRAY_STRUCT:
			array->values[i].i = heap_alloc_page(read_struct(r, true));
			break;
		default:
			VM_ERROR("unsupported type %d", array_type);
		}
	}
	if (with_size) {
		if (buffer_remaining(r) != 0) {
			VM_ERROR("trailing data after array");
		}
		r->size = orig_size;
	}
	return array;
}

static bool PastelChime2_DungeonDataLoad(int id, struct page **dungeon)
{
	struct dungeon_context *ctx = dungeon_get_context(draw_field_sprite_number);
	if (!ctx)
		return false;

	char *path = dungeon_data_path(id);
	size_t size;
	uint8_t* buf = file_read(path, &size);
	if (!buf) {
		free(path);
		return false;
	}
	struct buffer r;
	buffer_init(&r, buf, size);
	if (buffer_read_int32(&r) != 0 ||
	    buffer_read_int32(&r) != 0 ||
	    buffer_read_int32(&r) != 0 ||
	    buffer_read_int32(&r) != 0 ||
	    buffer_read_int32(&r) != 0x20 ||  // header size?
	    buffer_read_int32(&r) != size - 0x20 ||
	    buffer_read_int32(&r) != 0 ||
	    buffer_read_int32(&r) != 0) {
		WARNING("%s: invalid DSD header", display_utf0(path));
		free(buf);
		free(path);
		return false;
	}
	int slot_m_dsd = find_struct_slot(&ain->structures[(*dungeon)->index], "m_dsd");
	struct page *m_dsd = read_struct(&r, true);
	variable_set(*dungeon, slot_m_dsd, AIN_STRUCT, vm_int(heap_alloc_page(m_dsd)));
	struct page *map_size = heap_get_page(struct_get_var(m_dsd, "xyzMapSize")->i);
	int size_x = struct_get_var(map_size, "x")->i;
	int size_y = struct_get_var(map_size, "y")->i;
	int size_z = struct_get_var(map_size, "z")->i;

	// 读取地牢单元格
	int index = 0;
	if (ctx->dgn)
		dgn_free(ctx->dgn);
	ctx->dgn = dgn_new(size_x, size_y, size_z);

	while (buffer_remaining(&r) > 0) {
		int count = buffer_read_u8(&r) + 1;
		uint8_t cell_buf[128];
		buffer_read_bytes(&r, cell_buf, sizeof(cell_buf));
		while (--count >= 0) {
			int y = index / (size_x * size_z);
			int z = (index / size_x) % size_z;
			int x = index % size_x;
			index++;
			struct dgn_cell *cell = dgn_cell_at(ctx->dgn, x, y, z);
			struct buffer cr;
			buffer_init(&cr, cell_buf, sizeof(cell_buf));
			cell->x = x;
			cell->y = y;
			cell->z = z;
			cell->floor = buffer_read_int32(&cr);
			cell->ceiling = buffer_read_int32(&cr);
			cell->north_wall = buffer_read_int32(&cr);
			cell->south_wall = buffer_read_int32(&cr);
			cell->east_wall = buffer_read_int32(&cr);
			cell->west_wall = buffer_read_int32(&cr);
			cell->north_door = buffer_read_int32(&cr);
			cell->south_door = buffer_read_int32(&cr);
			cell->east_door = buffer_read_int32(&cr);
			cell->west_door = buffer_read_int32(&cr);
			cell->stairs_texture = buffer_read_int32(&cr);
			cell->stairs_orientation = buffer_read_int32(&cr);
			cell->lightmap_floor = buffer_read_int32(&cr);
			cell->lightmap_ceiling = buffer_read_int32(&cr);
			cell->lightmap_north = buffer_read_int32(&cr);
			cell->lightmap_south = buffer_read_int32(&cr);
			cell->lightmap_east = buffer_read_int32(&cr);
			cell->lightmap_west = buffer_read_int32(&cr);
			buffer_skip(&cr, 5 * 4);  // lightmap parameters for doors and stairs (unused)
			cell->north_door_lock = buffer_read_int32(&cr);
			cell->west_door_lock = buffer_read_int32(&cr);
			cell->south_door_lock = buffer_read_int32(&cr);
			cell->east_door_lock = buffer_read_int32(&cr);
			cell->north_door_angle = buffer_read_float(&cr);
			cell->west_door_angle = buffer_read_float(&cr);
			cell->south_door_angle = buffer_read_float(&cr);
			cell->east_door_angle = buffer_read_float(&cr);
			cell->walked = buffer_read_int32(&cr);
		}
	}
	if (index != dgn_nr_cells(ctx->dgn)) {
		VM_ERROR("expected %d cells, got %d", dgn_nr_cells(ctx->dgn), index);
	}
	free(buf);
	free(path);

	dungeon_map_init(ctx);
	struct dgn_cell *cells_end = ctx->dgn->cells + dgn_nr_cells(ctx->dgn);
	for (struct dgn_cell *c = ctx->dgn->cells; c < cells_end; c++) {
		if (c->walked)
			dungeon_map_reveal(ctx, c->x, c->y, c->z, true);
	}
	return true;
}

static bool PastelChime2_DungeonDataExist(int id)
{
	char *path = dungeon_data_path(id);
	bool exists = file_exists(path);
	free(path);
	return exists;
}

static bool PastelChime2_DungeonDataToSaveData(int save_data_index)
{
	UDIR *dir = opendir_utf8(config.save_dir);
	if (!dir)
		return false;

	char prefix[4];
	snprintf(prefix, sizeof(prefix), "%03d", save_data_index);

	// 先删掉这个存档位的旧文件。
	char *filename;
	while ((filename = readdir_utf8(dir))) {
		const char *ext = file_extension(filename);
		if (ext && !strcasecmp(ext, "DSA") && !strncmp(filename, prefix, 3)) {
			char *path = path_join(config.save_dir, filename);
			remove_utf8(path);
			free(path);
		}
		free(filename);
	}
	closedir_utf8(dir);

	// 重新打开目录，把当前的 DSD 文件拷成 DSA 文件。
	dir = opendir_utf8(config.save_dir);
	if (!dir) {
		return false;
	}

	while ((filename = readdir_utf8(dir))) {
		const char *ext = file_extension(filename);
		if (ext && !strcasecmp(ext, "DSD")) {
			char *src_path = path_join(config.save_dir, filename);

			size_t bufsize = strlen(filename) + 4;
			char *dst_name_buf = xmalloc(bufsize);
			snprintf(dst_name_buf, bufsize, "%s%s", prefix, filename);
			strcpy(strrchr(dst_name_buf, '.'), ".DSA");

			char *dst_path = path_join(config.save_dir, dst_name_buf);
			file_copy(src_path, dst_path);
			free(dst_path);
			free(dst_name_buf);
			free(src_path);
		}
		free(filename);
	}

	closedir_utf8(dir);
	return true;
}

static bool PastelChime2_DungeonDataFromSaveData(int save_data_index)
{
	// 先删掉所有 *.DSD 文件
	UDIR *dir = opendir_utf8(config.save_dir);
	if (!dir) {
		return false;
	}

	char *filename;
	while ((filename = readdir_utf8(dir))) {
		const char *ext = file_extension(filename);
		if (ext && !strcasecmp(ext, "DSD")) {
			char *path = path_join(config.save_dir, filename);
			remove_utf8(path);
			free(path);
		}
		free(filename);
	}
	closedir_utf8(dir);

	// 重新打开目录拷文件
	dir = opendir_utf8(config.save_dir);
	if (!dir) {
		return false;
	}

	char prefix[4];
	snprintf(prefix, sizeof(prefix), "%03d", save_data_index);

	while ((filename = readdir_utf8(dir))) {
		const char *ext = file_extension(filename);
		if (ext && !strcasecmp(ext, "DSA") && !strncmp(filename, prefix, 3)) {
			char *src_path = path_join(config.save_dir, filename);

			const char *base_name = filename + 3;
			char *dst_name_buf = xstrdup(base_name);
			strcpy(strrchr(dst_name_buf, '.'), ".DSD");

			char *dst_path = path_join(config.save_dir, dst_name_buf);
			file_copy(src_path, dst_path);

			free(dst_path);
			free(dst_name_buf);
			free(src_path);
		}
		free(filename);
	}

	closedir_utf8(dir);
	return true;
}

// 求两条线段的交点。
static bool check_line_segment_intersection(
	struct line_segment *seg1, struct line_segment *seg2, vec2 out_intersection)
{
	// 设 seg1 为 P + t*r，seg2 为 Q + u*s，且 0 <= t, u <= 1。
	vec2 r, s;
	glm_vec2_sub(seg1->p2, seg1->p1, r); // r = P2 - P1
	glm_vec2_sub(seg2->p2, seg2->p1, s); // s = Q2 - Q1

	float r_cross_s = glm_vec2_cross(r, s);
	vec2 q_minus_p;
	glm_vec2_sub(seg2->p1, seg1->p1, q_minus_p);

	if (fabsf(r_cross_s) < 1e-8f) {
		return false;  // The lines are parallel or collinear
	}

	// 解出 t 和 u
	float t = glm_vec2_cross(q_minus_p, s) / r_cross_s;
	float u = glm_vec2_cross(q_minus_p, r) / r_cross_s;

	if (0.f <= t && t <= 1.f && 0.f <= u && u <= 1.f) {
		// 交点即 P + t*r
		out_intersection[0] = seg1->p1[0] + t * r[0];
		out_intersection[1] = seg1->p1[1] + t * r[1];
		return true;
	}
	return false;
}

// 求线段与圆的交点中
// 离线段起点最近的那个。
static bool find_closest_segment_circle_intersection(
	vec2 center, float r, struct line_segment *seg, vec2 out_intersection)
{
	// 设直线为 P1 + t*d，其中 d = P2 - P1。
	// 设圆为 |X - C|^2 = r^2。
	// 把 X = P1 + t*d 代入圆方程，得到关于 t 的二次方程：
	// a*t^2 + b*t + c = 0
	vec2 d;
	glm_vec2_sub(seg->p2, seg->p1, d); // d = direction vector of the line
	vec2 f;
	glm_vec2_sub(seg->p1, center, f); // f = vector from circle center to line start

	float a = glm_vec2_dot(d, d);
	float b = 2 * glm_vec2_dot(f, d);
	float c = glm_vec2_dot(f, f) - r * r;

	float discriminant = b * b - 4 * a * c;
	if (discriminant < 0) {
		return false; // No real intersection
	}

	// 用求根公式解 t。
	discriminant = sqrtf(discriminant);
	float t1 = (-b - discriminant) / (2 * a);
	float t2 = (-b + discriminant) / (2 * a);

	bool t1_valid = t1 >= 0.f && t1 <= 1.f;
	bool t2_valid = t2 >= 0.f && t2 <= 1.f;

	// 取最小的合法 t，它对应线段上的第一个交点。
	float t;
	if (t1_valid && t2_valid) {
		t = min(t1, t2);
	} else if (t1_valid) {
		t = t1;
	} else if (t2_valid) {
		t = t2;
	} else {
		return false; // No intersection on the segment.
	}

	glm_vec2_scale(d, t, out_intersection);
	glm_vec2_add(out_intersection, seg->p1, out_intersection);
	return true;
}

#define MAX_WALL_SEGMENTS_PER_CELL 12

static int get_wall_segments(struct dgn *dgn, struct line_segment *dst, int x, int y, int z, bool check_door)
{
	if (!dgn_is_in_map(dgn, x, y, z))
		return 0;
	struct dgn_cell *cell = dgn_cell_at(dgn, x, y, z);
	int i = 0;
	if (cell->north_wall != -1 || cell->north_door_lock ||
	    (check_door && cell->north_door != -1 && cell->north_door_angle == 0.f)) {
		dst[i].p1[0] = x - 0.5f;
		dst[i].p1[1] = z + 0.5f;
		dst[i].p2[0] = x + 0.5f;
		dst[i].p2[1] = z + 0.5f;
		i++;
	}
	if (cell->west_wall != -1 || cell->west_door_lock ||
	    (check_door && cell->west_door != -1 && cell->west_door_angle == 0.f)) {
		dst[i].p1[0] = x - 0.5f;
		dst[i].p1[1] = z - 0.5f;
		dst[i].p2[0] = x - 0.5f;
		dst[i].p2[1] = z + 0.5f;
		i++;
	}
	if (cell->south_wall != -1 || cell->south_door_lock ||
	    (check_door && cell->south_door != -1 && cell->south_door_angle == 0.f)) {
		dst[i].p1[0] = x - 0.5f;
		dst[i].p1[1] = z - 0.5f;
		dst[i].p2[0] = x + 0.5f;
		dst[i].p2[1] = z - 0.5f;
		i++;
	}
	if (cell->east_wall != -1 || cell->east_door_lock ||
	    (check_door && cell->east_door != -1 && cell->east_door_angle == 0.f)) {
		dst[i].p1[0] = x + 0.5f;
		dst[i].p1[1] = z - 0.5f;
		dst[i].p2[0] = x + 0.5f;
		dst[i].p2[1] = z + 0.5f;
		i++;
	}
	if (cell->ceiling != -1) {
		// 加上八条边，构成八边形。
		float r = (cell->ceiling == 10) ? 0.35f : 0.5f;
		float hr = 0.5f * r;

		vec2 v[8];
		v[0][0] = x - hr; v[0][1] = z - r;
		v[1][0] = x + hr; v[1][1] = z - r;
		v[2][0] = x + r;  v[2][1] = z - hr;
		v[3][0] = x + r;  v[3][1] = z + hr;
		v[4][0] = x + hr; v[4][1] = z + r;
		v[5][0] = x - hr; v[5][1] = z + r;
		v[6][0] = x - r;  v[6][1] = z + hr;
		v[7][0] = x - r;  v[7][1] = z - hr;

		for (int j = 0; j < 8; j++) {
			glm_vec2_copy(v[j], dst[i].p1);
			glm_vec2_copy(v[(j + 1) % 8], dst[i].p2);
			i++;
		}
	}
	return i;
}

static bool PastelChime2_Field_SetSprite(int draw_field_sprite_number_)
{
	draw_field_sprite_number = draw_field_sprite_number_;
	return true;
}

static bool PastelChime2_Field_CheckLook(float px, float py, float qx, float qy, int floor, float block_size)
{
	struct dungeon_context *ctx = dungeon_get_context(draw_field_sprite_number);
	if (!ctx || !ctx->dgn)
		return false;

	struct line_segment sight_line = {{px, py}, {qx, qy}};
	int min_x = roundf(min(px, qx));
    int max_x = roundf(max(px, qx));
	int min_y = roundf(min(py, qy));
	int max_y = roundf(max(py, qy));

	for (int y = min_y; y <= max_y; y++) {
		for (int x = min_x; x <= max_x; x++) {
			struct line_segment segments[MAX_WALL_SEGMENTS_PER_CELL];
			int nr_segments = get_wall_segments(ctx->dgn, segments, x, floor, y, true);
			for (int i = 0; i < nr_segments; i++) {
				vec2 v;
				if (check_line_segment_intersection(&segments[i], &sight_line, v))
					return false;
			}
		}
	}
	return true;
}

static void PastelChime2_Field_GetDoors(struct page **dst, int floor)
{
	struct dungeon_context *ctx = dungeon_get_context(draw_field_sprite_number);
	if (!ctx || !ctx->dgn)
		return;
	if (*dst) {
		delete_page_vars(*dst);
		free_page(*dst);
		*dst = NULL;
	}

	int struct_type = ain_get_struct(ain, "door_t");
	struct ain_struct *s = &ain->structures[struct_type];
	int slot_nx = find_struct_slot(s, "nX");
	int slot_ny = find_struct_slot(s, "nY");
	int slot_ndir = find_struct_slot(s, "nDir");
	int slot_nlock = find_struct_slot(s, "nLock");
	int slot_fx1 = find_struct_slot(s, "fX1");
	int slot_fy1 = find_struct_slot(s, "fY1");
	int slot_fx2 = find_struct_slot(s, "fX2");
	int slot_fy2 = find_struct_slot(s, "fY2");
	int slot_fxc = find_struct_slot(s, "fXc");
	int slot_fyc = find_struct_slot(s, "fYc");

	struct page *array = NULL;
	for (int z = 0; z < ctx->dgn->size_z; z++) {
		for (int x = 0; x < ctx->dgn->size_x; x++) {
			struct dgn_cell *c = dgn_cell_at(ctx->dgn, x, floor, z);
			if (c->north_door != -1) {
				struct page *page = alloc_page(STRUCT_PAGE, struct_type, s->nr_members);
				page->values[slot_nx].i = c->x;
				page->values[slot_ny].i = c->z;
				page->values[slot_ndir].i = 0;
				page->values[slot_nlock].i = c->north_door_lock;
				page->values[slot_fx1].f = c->x - 0.5f;
				page->values[slot_fy1].f = c->z + 0.5f;
				page->values[slot_fx2].f = c->x + 0.5f;
				page->values[slot_fy2].f = c->z + 0.5f;
				page->values[slot_fxc].f = c->x;
				page->values[slot_fyc].f = c->z + 0.5f;
				union vm_value v = { .i = heap_alloc_page(page) };
				array = array_pushback(array, v, AIN_ARRAY_STRUCT, struct_type);
			}
			if (c->south_door != -1) {
				struct page *page = alloc_page(STRUCT_PAGE, struct_type, s->nr_members);
				page->values[slot_nx].i = c->x;
				page->values[slot_ny].i = c->z;
				page->values[slot_ndir].i = 2;
				page->values[slot_nlock].i = c->south_door_lock;
				page->values[slot_fx1].f = c->x - 0.5f;
				page->values[slot_fy1].f = c->z - 0.5f;
				page->values[slot_fx2].f = c->x + 0.5f;
				page->values[slot_fy2].f = c->z - 1.5f;
				page->values[slot_fxc].f = c->x;
				page->values[slot_fyc].f = c->z - 0.5f;
				union vm_value v = { .i = heap_alloc_page(page) };
				array = array_pushback(array, v, AIN_ARRAY_STRUCT, struct_type);
			}
			if (c->west_door != -1) {
				struct page *page = alloc_page(STRUCT_PAGE, struct_type, s->nr_members);
				page->values[slot_nx].i = c->x;
				page->values[slot_ny].i = c->z;
				page->values[slot_ndir].i = 1;
				page->values[slot_nlock].i = c->west_door_lock;
				page->values[slot_fx1].f = c->x - 0.5f;
				page->values[slot_fy1].f = c->z - 0.5f;
				page->values[slot_fx2].f = c->x - 0.5f;
				page->values[slot_fy2].f = c->z + 0.5f;
				page->values[slot_fxc].f = c->x - 0.5f;
				page->values[slot_fyc].f = c->z;
				union vm_value v = { .i = heap_alloc_page(page) };
				array = array_pushback(array, v, AIN_ARRAY_STRUCT, struct_type);
			}
			if (c->east_door != -1) {
				struct page *page = alloc_page(STRUCT_PAGE, struct_type, s->nr_members);
				page->values[slot_nx].i = c->x;
				page->values[slot_ny].i = c->z;
				page->values[slot_ndir].i = 3;
				page->values[slot_nlock].i = c->east_door_lock;
				page->values[slot_fx1].f = c->x + 0.5f;
				page->values[slot_fy1].f = c->z - 0.5f;
				page->values[slot_fx2].f = c->x + 0.5f;
				page->values[slot_fy2].f = c->z + 0.5f;
				page->values[slot_fxc].f = c->x + 0.5f;
				page->values[slot_fyc].f = c->z;
				union vm_value v = { .i = heap_alloc_page(page) };
				array = array_pushback(array, v, AIN_ARRAY_STRUCT, struct_type);
			}
		}
	}
	*dst = array;
}

static void PastelChime2_Field_UpdateDoorNamePlate(
	struct page **doors, struct page *fxyz_player, bool walking, int floor,
	float block_size, struct page **rc_field, struct page **pos_mouse)
{
	struct dungeon_context *ctx = dungeon_get_context(draw_field_sprite_number);
	if (!ctx)
		return;
	if (!*doors)
		return;

	vec3 player_pos = {
		struct_get_var(fxyz_player, "x")->f / block_size,
		struct_get_var(fxyz_player, "y")->f / block_size,
		struct_get_var(fxyz_player, "z")->f / block_size
	};
	Rectangle field_sprite_rect = {
		.x = struct_get_var(*rc_field, "x")->i,
		.y = struct_get_var(*rc_field, "y")->i,
		.w = struct_get_var(*rc_field, "w")->i,
		.h = struct_get_var(*rc_field, "h")->i,
	};
	Point mouse_pos = {
		.x = struct_get_var(*pos_mouse, "x")->i,
		.y = struct_get_var(*pos_mouse, "y")->i,
	};

	// 遍历所有门，更新它们的铭牌。
	for (int i = 0; i < (*doors)->nr_vars; i++) {
		struct page *door = heap_get_page((*doors)->values[i].i);
		int sp_plate_normal = struct_get_var(door, "nSpPlateNormal")->i;
		int sp_plate_select = struct_get_var(door, "nSpPlateSelect")->i;
		if (!sp_plate_normal || !sp_plate_select) {
			continue;
		}

		// 玩家在走路时把铭牌藏起来。
		if (walking) {
			sact_SP_SetShow(sp_plate_normal, 0);
			sact_SP_SetShow(sp_plate_select, 0);
			continue;
		}

		// 检查玩家是否和门在同一格。
		if (struct_get_var(door, "nX")->i != roundf(player_pos[0]) ||
		    struct_get_var(door, "nY")->i != roundf(player_pos[2])) {
			continue;
		}

		// 检查玩家是否足够近，可以激活铭牌。
		float fXc = struct_get_var(door, "fXc")->f;
		float fYc = struct_get_var(door, "fYc")->f;
		vec2 player_to_door = { player_pos[0] - fXc, player_pos[2] - fYc };
		if (glm_vec2_norm2(player_to_door) > glm_pow2(NAMEPLATE_ACTIVATION_DISTANCE)) {
			continue;
		}

		// 把铭牌的 3D 世界坐标投影到 2D 屏幕坐标。
		vec3 nameplate_pos = {
			fXc * block_size,
			floor * block_size + 1.f,
			fYc * block_size
		};
		Point screen_pos;
		if (!dungeon_project_world_to_screen(ctx, nameplate_pos, &screen_pos) ||
		    screen_pos.x < 0 || screen_pos.y < 0 ||
		    screen_pos.x >= field_sprite_rect.w || screen_pos.y >= field_sprite_rect.h) {
			// 投影失败或落在屏幕外就藏起来。
			sact_SP_SetShow(sp_plate_normal, 0);
			sact_SP_SetShow(sp_plate_select, 0);
			continue;
		}

		// 算出铭牌最终的屏幕矩形。
		Rectangle nameplate_rect = {
			.w = sact_SP_GetWidth(sp_plate_normal),
			.h = sact_SP_GetHeight(sp_plate_normal)
		};
		nameplate_rect.x = field_sprite_rect.x + screen_pos.x - nameplate_rect.w / 2;
		nameplate_rect.y = field_sprite_rect.y + screen_pos.y - nameplate_rect.h / 2,
		sact_SP_SetPos(sp_plate_normal, nameplate_rect.x, nameplate_rect.y);
		sact_SP_SetPos(sp_plate_select, nameplate_rect.x, nameplate_rect.y);

		// 按鼠标位置显示普通或选中态精灵。
		bool is_mouse_over = SDL_PointInRect(&mouse_pos, &nameplate_rect);
		sact_SP_SetShow(sp_plate_normal, !is_mouse_over);
		sact_SP_SetShow(sp_plate_select, is_mouse_over);
	}
}

static void PastelChime2_Field_UpdateObjectNamePlate(struct page **dungeon, float enemy_range, float treasure_range, float trap_range, float default_range)
{
	struct dungeon_context *ctx = dungeon_get_context(draw_field_sprite_number);
	if (!ctx)
		return;

	struct page *m_dsd = heap_get_page(struct_get_var(*dungeon, "m_dsd")->i);
	struct page *m_drd = heap_get_page(struct_get_var(*dungeon, "m_drd")->i);

	// 取玩家和环境属性。
	struct page *fxyz_player = heap_get_page(struct_get_var(m_dsd, "fxyz")->i);
	int floor = struct_get_var(m_dsd, "nFloor")->i;
	struct page *objects = heap_get_page(struct_get_var(m_dsd, "aObject")->i);
	if (!objects)
		return;

	bool walking = struct_get_var(m_drd, "bWalking")->i;
	bool running = struct_get_var(m_drd, "bRun")->i;
	float block_size = struct_get_var(m_drd, "fBlockSize")->f;
	struct page *pos_mouse = heap_get_page(struct_get_var(m_drd, "posMouse")->i);

	vec3 player_pos = {
		struct_get_var(fxyz_player, "x")->f,
		struct_get_var(fxyz_player, "y")->f,
		struct_get_var(fxyz_player, "z")->f
	};
	Rectangle field_sprite_rect = { .x = 150, .y = 50, .w = 500, .h = 500 };
	Point mouse_pos = {
		.x = struct_get_var(pos_mouse, "x")->i,
		.y = struct_get_var(pos_mouse, "y")->i,
	};

	// 遍历所有对象，更新它们的铭牌。
	for (int i = 0; i < objects->nr_vars; i++) {
		struct page *object = heap_get_page(objects->values[i].i);
		int sp_plate_normal = struct_get_var(object, "nSpPlateNormal")->i;
		int sp_plate_select = struct_get_var(object, "nSpPlateSelect")->i;
		if (!sp_plate_normal || !sp_plate_select) {
			continue;
		}

		// 默认隐藏。
		sact_SP_SetShow(sp_plate_normal, 0);
		sact_SP_SetShow(sp_plate_select, 0);

		if (!struct_get_var(object, "bValidNamePlate")->i) {
			continue;
		}

		bool found = struct_get_var(object, "bFound")->i;
		if ((running && !found) || walking) {
			continue;
		}

		// 取对象的格坐标。
		struct page *fpos = heap_get_page(struct_get_var(object, "fpos")->i);
		float obj_grid_x = struct_get_var(fpos, "x")->f;
		float obj_grid_z = struct_get_var(fpos, "y")->f;

		// 把格坐标转成 3D 世界坐标。
		vec3 obj_world_pos = {
			obj_grid_x * block_size,
			floor * block_size,
			obj_grid_z * block_size
		};

		// 把世界坐标投影到屏幕坐标。
		Point screen_pos;
		if (!dungeon_project_world_to_screen(ctx, obj_world_pos, &screen_pos) ||
		    screen_pos.x < 0 || screen_pos.y < 0 ||
		    screen_pos.x >= field_sprite_rect.w || screen_pos.y >= field_sprite_rect.h) {
			continue; // Off-screen
		}

		// 按对象类型决定显示范围。
		float current_range = default_range;
		switch (struct_get_var(object, "nType")->i) {
		case 1: case 2: case 3:
			current_range = enemy_range;
			break;
		case 4:
			current_range = treasure_range;
			break;
		case 5:
			current_range = trap_range;
			break;
		}

		// 检查与玩家的距离。
		if (glm_vec3_distance2(player_pos, obj_world_pos) > glm_pow2(current_range)) {
			continue;
		}

		// 检查视线是否被挡。
		if (!PastelChime2_Field_CheckLook(player_pos[0] / block_size, player_pos[2] / block_size,
		                                 obj_grid_x, obj_grid_z, floor, block_size)) {
			continue;
		}

		// 所有检查都通过，就显示铭牌。
		Rectangle nameplate_rect = {
			.w = sact_SP_GetWidth(sp_plate_normal),
			.h = sact_SP_GetHeight(sp_plate_normal)
		};
		nameplate_rect.x = field_sprite_rect.x + screen_pos.x - nameplate_rect.w / 2;
		nameplate_rect.y = field_sprite_rect.y + screen_pos.y - nameplate_rect.h / 2;
		sact_SP_SetPos(sp_plate_normal, nameplate_rect.x, nameplate_rect.y);
		sact_SP_SetPos(sp_plate_select, nameplate_rect.x, nameplate_rect.y);

		// 按鼠标位置显示普通或选中态精灵。
		bool is_mouse_over = SDL_PointInRect(&mouse_pos, &nameplate_rect);
		sact_SP_SetShow(sp_plate_normal, !is_mouse_over);
		sact_SP_SetShow(sp_plate_select, is_mouse_over);
	}
}

static bool PastelChime2_Field_QuerySelectedObjectPlate(struct page **result, struct page **pos_mouse, struct page **dungeon)
{
	struct page *m_dsd = heap_get_page(struct_get_var(*dungeon, "m_dsd")->i);
	struct page *objects = heap_get_page(struct_get_var(m_dsd, "aObject")->i);
	if (!objects)
		return false;
	Point mouse_pos = {
		.x = struct_get_var(*pos_mouse, "x")->i,
		.y = struct_get_var(*pos_mouse, "y")->i,
	};

	for (int i = 0; i < objects->nr_vars; i++) {
		struct page *object = heap_get_page(objects->values[i].i);
		int sp_plate_select = struct_get_var(object, "nSpPlateSelect")->i;

		if (sp_plate_select && sact_SP_GetShow(sp_plate_select)) {
			Rectangle plate_rect = {
				.x = sact_SP_GetPosX(sp_plate_select),
				.y = sact_SP_GetPosY(sp_plate_select),
				.w = sact_SP_GetWidth(sp_plate_select),
				.h = sact_SP_GetHeight(sp_plate_select)
			};

			if (SDL_PointInRect(&mouse_pos, &plate_rect)) {
				if (*result) {
					delete_page_vars(*result);
					free_page(*result);
				}
				*result = copy_page(object);
				return true;
			}
		}
	}

	return false;
}

static bool PastelChime2_Field_QuerySelectedDoorPlate(struct page **result, struct page **pos_mouse, struct page **dungeon)
{
	struct page *m_drd = heap_get_page(struct_get_var(*dungeon, "m_drd")->i);
	struct page *door_table = heap_get_page(struct_get_var(m_drd, "aDoorTable")->i);
	if (!door_table)
		return false;
	Point mouse_pos = {
		.x = struct_get_var(*pos_mouse, "x")->i,
		.y = struct_get_var(*pos_mouse, "y")->i,
	};

	for (int i = 0; i < door_table->nr_vars; i++) {
		struct page *door = heap_get_page(door_table->values[i].i);
		int sp_plate_select = struct_get_var(door, "nSpPlateSelect")->i;

		if (sp_plate_select != 0 && sact_SP_GetShow(sp_plate_select)) {
			Rectangle plate_rect = {
				.x = sact_SP_GetPosX(sp_plate_select),
				.y = sact_SP_GetPosY(sp_plate_select),
				.w = sact_SP_GetWidth(sp_plate_select),
				.h = sact_SP_GetHeight(sp_plate_select)
			};

			if (SDL_PointInRect(&mouse_pos, &plate_rect)) {
				if (*result) {
					delete_page_vars(*result);
					free_page(*result);
				}
				*result = copy_page(door);
				return true;
			}
		}
	}

	return false;
}

static void PastelChime2_Field_CheckObjectHit(struct page **change_list, struct page **dungeon)
{
	if (*change_list) {
		delete_page_vars(*change_list);
		free_page(*change_list);
		*change_list = NULL;
	}

	struct page *m_dsd = heap_get_page(struct_get_var(*dungeon, "m_dsd")->i);
	struct page *m_drd = heap_get_page(struct_get_var(*dungeon, "m_drd")->i);
	float block_size = struct_get_var(m_drd, "fBlockSize")->f;
	struct page *fxyz = heap_get_page(struct_get_var(m_dsd, "fxyz")->i);
	vec2 player_pos = {
		struct_get_var(fxyz, "x")->f / block_size,
		struct_get_var(fxyz, "z")->f / block_size
	};
	struct page *objects = heap_get_page(struct_get_var(m_dsd, "aObject")->i);
	if (!objects)
		return;
	for (int i = 0; i < objects->nr_vars; i++) {
		struct page *object = heap_get_page(objects->values[i].i);
		struct page *fpos = heap_get_page(struct_get_var(object, "fpos")->i);
		vec2 object_pos = {
			struct_get_var(fpos, "x")->f,
			struct_get_var(fpos, "y")->f
		};
		float hit_range = struct_get_var(object, "fHitRange")->f;
		union vm_value *hit = struct_get_var(object, "bHit");
		int new_hit = glm_vec2_distance2(player_pos, object_pos) < glm_pow2(hit_range);
		if (new_hit != hit->i) {
			hit->i = new_hit;
			union vm_value v = { .i = heap_alloc_page(copy_page(object)) };
			*change_list = array_pushback(*change_list, v, AIN_ARRAY_STRUCT, object->index);
		}
	}
}

static void PastelChime2_Field_UpdateObjectAnime(int total_time, struct page **dungeon)
{
	struct page *m_dsd = heap_get_page(struct_get_var(*dungeon, "m_dsd")->i);
	struct page *objects = heap_get_page(struct_get_var(m_dsd, "aObject")->i);
	if (!objects)
		return;
	for (int i = 0; i < objects->nr_vars; i++) {
		struct page *object = heap_get_page(objects->values[i].i);
		int type = struct_get_var(object, "nType")->i;
		int chara = struct_get_var(object, "nChara")->i;
		int anime_frame_num = struct_get_var(object, "nAnimeFrameNum")->i;
		int appearance_count = struct_get_var(object, "nAppearanceCount")->i;
		switch (type) {
		case 1: case 2: case 3:
			dungeon_set_chara_cg(draw_field_sprite_number, chara, appearance_count
				? anime_frame_num * 2 - (appearance_count * 16) / 400
				: (total_time / 100 + chara) % anime_frame_num);
			break;
		case 11: case 12: case 13:
			dungeon_set_chara_cg(draw_field_sprite_number, chara,
				(total_time / 100 + chara) % anime_frame_num);
			break;
		}
	}
}

static float PastelChime2_Field_CalcY(float fx, float fz, int floor, float block_size)
{
	struct dungeon_context *ctx = dungeon_get_context(draw_field_sprite_number);
	if (!ctx || !ctx->dgn)
		return 0.f;

	int x = to_grid_coord(fx);
	int z = to_grid_coord(fz);

	struct dgn_cell *cell = dgn_cell_at(ctx->dgn, x, floor, z);
	if (cell && cell->stairs_texture != -1) {
		// 该格是当前楼层的一段上行楼梯。
		float position_in_tile = 0.f;
		switch (cell->stairs_orientation) {
		case 0:  // north-facing stair
			position_in_tile = fz - ((z - 0.5f) * block_size);
			break;
		case 1:  // west-facing stair
			position_in_tile = (x + 0.5f) * block_size - fx;
			break;
		case 2:  // south-facing stair
			position_in_tile = (z + 0.5f) * block_size - fz;
			break;
		case 3:  // east-facing stair
			position_in_tile = fx - ((x - 0.5f) * block_size);
			break;
		}
		return floor * block_size + ceilf(position_in_tile * 3.f) / 3.f;
	}

	// 检查下一层的楼梯。
	cell = dgn_cell_at(ctx->dgn, x, floor - 1, z);
	if (cell && cell->stairs_texture != -1) {
		// 该格是下一层的一段下行楼梯。
		float position_in_tile = 0.f;
		switch (cell->stairs_orientation) {
		case 0:  // north-facing stair
			position_in_tile = (z + 0.5f) * block_size - fz;
			break;
		case 1:  // west-facing stair
			position_in_tile = fx - (x - 0.5f) * block_size;
			break;
		case 2:  // south-facing stair
			position_in_tile = fz - (z - 0.5f) * block_size;
			break;
		case 3:  // east-facing stair
			position_in_tile = (x + 0.5f) * block_size - fx;
			break;
		}
		return floor * block_size - floorf(position_in_tile * 3.f) / 3.f;
	}

	// 平地
	return floor * block_size;
}

static void PastelChime2_Field_CalcCamera(float x, float y, float z, float deg, float pitch, float length)
{
	float yaw = glm_rad(deg);
	vec3 offset = { cosf(yaw), 0.f, sinf(yaw) };
	vec3 axis = { sinf(yaw), 0.f, -cosf(yaw) };
	glm_vec3_rotate(offset, -glm_rad(pitch), axis);
	glm_vec3_normalize(offset);
	glm_vec3_scale(offset, length, offset);
	dungeon_set_camera(draw_field_sprite_number, x + offset[0], y + offset[1], z + offset[2], deg - 270.f, -pitch);
}

static void PastelChime2_Field_UpdatePlayersUnit(
	float player1x, float player1y, float player1z, int chara_player1,
	float *player2x, float *player2y, float *player2z, int chara_player2,
	float *player3x, float *player3y, float *player3z, int chara_player3,
	int cg_index_player1, bool walking)
{
	// 把队长（P1）的移动路径记进队列
	vec3 p1pos = { player1x, player1y, player1z };
	struct player_move_point* new_point = xcalloc(1, sizeof(struct player_move_point));
	glm_vec3_copy(p1pos, new_point->position);
	new_point->cg_index = cg_index_player1;
	struct player_move_point* last_point = TAILQ_LAST(&player_move_queue, player_move_tailq);
	if (last_point) {
		new_point->distance_to_prev = glm_vec3_distance(last_point->position, p1pos);
	}
	if (!last_point || new_point->distance_to_prev > 0.f) {
		TAILQ_INSERT_TAIL(&player_move_queue, new_point, entry);
	} else {
		free(new_point); // Don't add if not moving
	}

	struct player_move_point *first_point = TAILQ_FIRST(&player_move_queue);
	struct player_move_point p2state = *first_point;
	struct player_move_point p3state = *first_point;

	// 反向遍历路径，算出跟随者的位置
	bool p2_calculated = false;
	float accumulated_dist = 0.f;
	struct player_move_point *current_point = NULL;
	struct player_move_point *prev_point = NULL;
	TAILQ_FOREACH_REVERSE(current_point, &player_move_queue, player_move_tailq, entry) {
		prev_point = TAILQ_PREV(current_point, player_move_tailq, entry);
		if (!prev_point) break;

		accumulated_dist += current_point->distance_to_prev;

		// 计算 P2 的位置
		if (!p2_calculated && accumulated_dist >= DISTANCE_BETWEEN_PLAYERS) {
			float t = (accumulated_dist - DISTANCE_BETWEEN_PLAYERS) / current_point->distance_to_prev;
			glm_vec3_lerp(prev_point->position, current_point->position, t, p2state.position);
			p2state.cg_index = current_point->cg_index;
			p2_calculated = true;
		}

		// 计算 P3 的位置
		if (accumulated_dist >= 2 * DISTANCE_BETWEEN_PLAYERS) {
			float t = (accumulated_dist - 2 * DISTANCE_BETWEEN_PLAYERS) / current_point->distance_to_prev;
			glm_vec3_lerp(prev_point->position, current_point->position, t, p3state.position);
			p3state.cg_index = current_point->cg_index;
			break;
		}
	}

	// prev_point 之前的点都不再需要，删掉
	while (prev_point) {
		struct player_move_point *first = TAILQ_FIRST(&player_move_queue);
		if (!first || first == prev_point)
			break;
		TAILQ_REMOVE(&player_move_queue, first, entry);
		free(first);
	}

	if (!walking) {
		p2state.cg_index &= ~3;
		p3state.cg_index &= ~3;
	}

	dungeon_set_chara_pos(draw_field_sprite_number, chara_player1, player1x, player1y, player1z);
	if (chara_player2) {
		dungeon_set_chara_pos(draw_field_sprite_number, chara_player2, p2state.position[0], p2state.position[1], p2state.position[2]);
		dungeon_set_chara_cg(draw_field_sprite_number, chara_player2, p2state.cg_index);
	}
	if (chara_player3) {
		dungeon_set_chara_pos(draw_field_sprite_number, chara_player3, p3state.position[0], p3state.position[1], p3state.position[2]);
		dungeon_set_chara_cg(draw_field_sprite_number, chara_player3, p3state.cg_index);
	}
	*player2x = p2state.position[0];
	*player2y = p2state.position[1];
	*player2z = p2state.position[2];
	*player3x = p3state.position[0];
	*player3y = p3state.position[1];
	*player3z = p3state.position[2];
}

static void PastelChime2_Field_ClearPlayerMoveQueue(void)
{
	struct player_move_point *point;
	while ((point = TAILQ_FIRST(&player_move_queue)) != NULL) {
		TAILQ_REMOVE(&player_move_queue, point, entry);
		free(point);
	}
}

// 检查运动中的圆与墙段之间的碰撞。
static bool judge_hit_wall(
	struct line_segment *move_seg, float radius, struct line_segment *wall,
	vec2 hit_out, struct line_segment *slide_line_out)
{
	vec2 wall_vec;
	glm_vec2_sub(wall->p2, wall->p1, wall_vec);

	vec2 normal = {-wall_vec[1], wall_vec[0]};
	glm_vec2_normalize(normal);

	// 检查与墙两侧的碰撞
	for (int i = 0; i < 2; i++) {
		float side = (i == 0) ? 1.f : -1.f;
		vec2 offset;
		glm_vec2_scale(normal, radius * side, offset);

		struct line_segment wall_offset_seg;
		glm_vec2_add(wall->p1, offset, wall_offset_seg.p1);
		glm_vec2_add(wall->p2, offset, wall_offset_seg.p2);

		if (check_line_segment_intersection(move_seg, &wall_offset_seg, hit_out)) {
			*slide_line_out = wall_offset_seg;
			return true;
		}
	}

	// 检查与墙端点（两端圆帽）的碰撞
	if (find_closest_segment_circle_intersection(wall->p1, radius, move_seg, hit_out)) {
		// 要贴着滑的线，是端点圆在碰撞点处的切线。
		glm_vec2_copy(hit_out, slide_line_out->p1);
		slide_line_out->p2[0] = hit_out[0] - (wall->p1[1] - hit_out[1]);
		slide_line_out->p2[1] = (wall->p1[0] - hit_out[0]) + hit_out[1];
		return true;
	}

	if (find_closest_segment_circle_intersection(wall->p2, radius, move_seg, hit_out)) {
		glm_vec2_copy(hit_out, slide_line_out->p1);
		slide_line_out->p2[0] = hit_out[0] - (wall->p2[1] - hit_out[1]);
		slide_line_out->p2[1] = (wall->p2[0] - hit_out[0]) + hit_out[1];
		return true;
	}

	return false;
}

static bool PastelChime2_JudgeHitWall(
	float old_x, float old_y, float new_x, float new_y, float radius,
	float *hit_x, float *hit_y,
	float *line_hit_x1, float *line_hit_y1, float *line_hit_x2, float *line_hit_y2,
	float wall_x1, float wall_y1, float wall_x2, float wall_y2, bool wallb)
{
	if (!wallb)
		return false;
	struct line_segment move_seg = {{old_x, old_y}, {new_x, new_y}};
	struct line_segment wall_seg = {{wall_x1, wall_y1}, {wall_x2, wall_y2}};
	vec2 hit_pos;
	struct line_segment slide_line;
	if (!judge_hit_wall(&move_seg, radius, &wall_seg, hit_pos, &slide_line))
		return false;
	*hit_x = hit_pos[0];
	*hit_y = hit_pos[1];
	*line_hit_x1 = slide_line.p1[0];
	*line_hit_y1 = slide_line.p1[1];
	*line_hit_x2 = slide_line.p2[0];
	*line_hit_y2 = slide_line.p2[1];
	return true;
}

static void adjust_move_pos(struct line_segment *move_seg, struct line_segment *wall)
{
	// 原本想要的移动向量。
	vec2 move_vec;
	glm_vec2_sub(move_seg->p2, move_seg->p1, move_vec);

	// 墙的方向向量（沿它滑动的那条切线）。
	vec2 wall_dir;
	glm_vec2_sub(wall->p2, wall->p1, wall_dir);
	glm_vec2_normalize(wall_dir);

	// 把移动向量投影到墙的方向向量上。
	// 这样就得到移动中与墙平行的那个分量。
	float slide_dist = glm_vec2_dot(move_vec, wall_dir);

	// 新的移动向量就是沿墙方向滑过的距离。
	vec2 slide_vec;
	glm_vec2_scale(wall_dir, slide_dist, slide_vec);

	// 最终位置 = 原位置 + 滑动向量。
	glm_vec2_add(move_seg->p1, slide_vec, move_seg->p2);
}

static bool PastelChime2_AdjustMovePos(
	float *new_x, float *new_y, float old_x, float old_y,
	float wall_p1x, float wall_p1y, float wall_p2x, float wall_p2y)
{
	struct line_segment move_seg = {{old_x, old_y}, {*new_x, *new_y}};
	struct line_segment wall = {{wall_p1x, wall_p1y}, {wall_p2x, wall_p2y}};
	adjust_move_pos(&move_seg, &wall);
	*new_x = move_seg.p2[0];
	*new_y = move_seg.p2[1];
	return true;
}

static bool PastelChime2_Field_CalcMove(
	float *new_x, float *new_y, float old_x, float old_y, float map_delta_x, float map_delta_y,
	float radius, float delta_time, float walk_step, int floor, float block_size, bool check_door)
{
	struct dungeon_context *ctx = dungeon_get_context(draw_field_sprite_number);
	if (!ctx || !ctx->dgn)
		return false;

	// 先算出理想位置，暂不考虑任何碰撞。
	*new_x = map_delta_x / walk_step * delta_time + old_x;
	*new_y = map_delta_y / walk_step * delta_time + old_y;

	// 收集角色当前位置周围 3x3 格内的
	// 潜在碰撞体。
	int cx = roundf(old_x);
	int cy = roundf(old_y);
	struct line_segment segments[9 * MAX_WALL_SEGMENTS_PER_CELL];
	int nr_segments = 0;
	for (int y = -1; y <= 1; y++) {
		for (int x = -1; x <= 1; x++) {
			nr_segments += get_wall_segments(ctx->dgn, segments + nr_segments, cx + x, floor, cy + y, check_door);
		}
	}

	if (nr_segments == 0)
		return true;  // No collision possible, movement is clear.

	// 找出最近的那次碰撞。
	float closest_hit_dist_sq = FLT_MAX;
	struct line_segment closest_slide_line;
	struct line_segment move_seg = {{old_x, old_y}, {*new_x, *new_y}};

	for (int i = 0; i < nr_segments; i++) {
		vec2 hit_pos;
		struct line_segment slide_line;
		if (!judge_hit_wall(&move_seg, radius, &segments[i], hit_pos, &slide_line))
			continue;

		float dist_sq = glm_vec2_distance2(hit_pos, move_seg.p1);
		if (closest_hit_dist_sq > dist_sq) {
			closest_hit_dist_sq = dist_sq;
			closest_slide_line = slide_line;
		}
	}
	if (closest_hit_dist_sq == FLT_MAX)
		return true;

	// 调整角色位置，沿着撞到的那面最近墙滑动。
	adjust_move_pos(&move_seg, &closest_slide_line);
	*new_x = move_seg.p2[0];
	*new_y = move_seg.p2[1];

	// 滑动之后再检查一遍：用新的移动路径做二次碰撞检测。
	// 这处理复杂的拐角，避免角色滑进另一面墙里。
	for (int i = 0; i < nr_segments; i++) {
		vec2 hit_pos;
		struct line_segment slide_line;
		// 如果调整后的路径仍然会撞，这一帧就完全不动。
		if (judge_hit_wall(&move_seg, radius, &segments[i], hit_pos, &slide_line)) {
			*new_x = old_x;
			*new_y = old_y;
			break;
		}
	}

	return true;
}

static void PastelChime2_Field_UpdateEnemyPos(int delta_time, struct page **dungeon)
{
	struct page *m_dsd = heap_get_page(struct_get_var(*dungeon, "m_dsd")->i);
	struct page *m_drd = heap_get_page(struct_get_var(*dungeon, "m_drd")->i);
	struct page *fxyz = heap_get_page(struct_get_var(m_dsd, "fxyz")->i);
	int floor = struct_get_var(m_dsd, "nFloor")->i;
	float block_size = struct_get_var(m_drd, "fBlockSize")->f;
	vec2 player_pos = {
		struct_get_var(fxyz, "x")->f / block_size,
		struct_get_var(fxyz, "z")->f / block_size
	};

	struct page *objects = heap_get_page(struct_get_var(m_dsd, "aObject")->i);
	if (!objects)
		return;
	for (int i = 0; i < objects->nr_vars; i++) {
		struct page *object = heap_get_page(objects->values[i].i);

		// 敌人处于出现动画时跳过移动。
		union vm_value *appearance_count = struct_get_var(object, "nAppearanceCount");
		if (appearance_count->i > 0) {
			appearance_count->i = max(appearance_count->i - delta_time, 0);
			continue;
		}

		// 敌人处于停止状态时跳过移动。
		union vm_value *nStopCount = struct_get_var(object, "nStopCount");
		if (nStopCount->i > 0) {
			nStopCount->i = max(nStopCount->i - delta_time, 0);
			continue;
		}

		float walk_step = struct_get_var(object, "fWalkStep")->f;
		float sense_range = struct_get_var(object, "fSenseRange")->f;
		if (walk_step == 0.f || sense_range == 0.f || !struct_get_var(object, "bFound")->i) {
			continue;
		}

		// 取敌人当前位置和基准位置。
		struct page *fpos = heap_get_page(struct_get_var(object, "fpos")->i);
		struct page *fpos_base = heap_get_page(struct_get_var(object, "fposBase")->i);
		vec2 enemy_pos = {
			struct_get_var(fpos, "x")->f,
			struct_get_var(fpos, "y")->f
		};
		vec2 enemy_base_pos = {
			struct_get_var(fpos_base, "x")->f,
			struct_get_var(fpos_base, "y")->f
		};

		// 决定目标：玩家，还是敌人的基准位置。
		bool can_see_player = PastelChime2_Field_CheckLook(
			enemy_pos[0], enemy_pos[1], player_pos[0], player_pos[1], floor, block_size);

		vec2 target_vec;
		if (can_see_player) {
			// 玩家可见就追玩家。
			glm_vec2_sub(player_pos, enemy_pos, target_vec);
			// 但玩家超出感知范围就什么都不做。
			if (glm_vec2_distance2(player_pos, enemy_pos) > glm_pow2(sense_range)) {
				struct_get_var(object, "nEventType")->i = 0;
				continue;
			}
		} else {
			// 玩家不可见就回基准位置。
			struct_get_var(object, "nEventType")->i = 0;
			glm_vec2_sub(enemy_base_pos, enemy_pos, target_vec);
		}

		float norm = glm_vec2_norm(target_vec);
		if (norm < 1e-3f) {
			continue; // Already at the target.
		}

		// 这里归一化移动向量的做法是有问题的？（但为了
		// 复现原版行为，必须这么做）
		vec2 move_vec = { norm / target_vec[0], norm / target_vec[1] };
		if (fabsf(target_vec[0]) < fabsf(move_vec[0]))
			move_vec[0] = target_vec[0];
		if (fabsf(target_vec[1]) < fabsf(move_vec[1]))
			move_vec[1] = target_vec[1];

		// 带碰撞检测地算出新位置。
		float hit_range = struct_get_var(object, "fHitRange")->f;
		float new_x, new_y;
		PastelChime2_Field_CalcMove(
			&new_x, &new_y, enemy_pos[0], enemy_pos[1], move_vec[0], move_vec[1],
			hit_range, delta_time, walk_step, floor, block_size, true);

		// 更新游戏状态里敌人的位置。
		struct_get_var(fpos, "x")->f = new_x;
		struct_get_var(fpos, "y")->f = new_y;

		// 更新 3D 模型里的位置。
		int chara = struct_get_var(object, "nChara")->i;
		dungeon_set_chara_pos(draw_field_sprite_number, chara, new_x * block_size, floor * block_size, new_y * block_size);
	}
}

//bool PastelChime2_JudgeHitPointCircle(float px, float py, float cpx, float cpy, float cr);
//bool PastelChime2_JudgeHitLineCircle(float lpx, float lpy, float lqx, float lqy, float cpx, float cpy, float cr);

#define SHOULD_OPEN_NORTH      (1 << 0)
#define SHOULD_OPEN_WEST       (1 << 1)
#define SHOULD_OPEN_SOUTH      (1 << 2)
#define SHOULD_OPEN_EAST       (1 << 3)
#define SHOULD_NOT_CLOSE_NORTH (1 << 4)
#define SHOULD_NOT_CLOSE_WEST  (1 << 5)
#define SHOULD_NOT_CLOSE_SOUTH (1 << 6)
#define SHOULD_NOT_CLOSE_EAST  (1 << 7)

static void PastelChime2_Field_UpdateDoors(
	bool *open_out, bool *close_out, int *open_tex_out, int *close_tex_out,
	float fx1, float fy1, float fz1,
	float fx2, float fy2, float fz2,
	float fx3, float fy3, float fz3,
	float block_size, bool b_player2, bool b_player3)
{
	struct dungeon_context *ctx = dungeon_get_context(draw_field_sprite_number);
	if (!ctx || !ctx->dgn)
		return;

	// 准备玩家位置数据：浮点和整数格坐标各一份。
	vec3 fpos[3] = {
		{ fx1, fy1, fz1 },
		{ fx2, fy2, fz2 },
		{ fx3, fy3, fz3 }
	};
	ivec3 pos[3] = {
		{ to_grid_coord(fx1), to_grid_coord(fy1), to_grid_coord(fz1) },
		{ to_grid_coord(fx2), to_grid_coord(fy2), to_grid_coord(fz2) },
		{ to_grid_coord(fx3), to_grid_coord(fy3), to_grid_coord(fz3) }
	};
	if (!dgn_is_in_map(ctx->dgn, pos[0][0], pos[0][1], pos[0][2]))
		return;

	// 算出玩家位置的包围盒，用来限定更新范围。
	ivec3 min_pos, max_pos;
	glm_ivec3_minv(pos[0], pos[1], min_pos);
	glm_ivec3_minv(min_pos, pos[2], min_pos);
	glm_ivec3_maxv(pos[0], pos[1], max_pos);
	glm_ivec3_maxv(max_pos, pos[2], max_pos);

	// 建一张临时网格存门的状态标志，留出边距。
	ivec3 base, size;
	glm_ivec3_subs(min_pos, 2, base);
	glm_ivec3_sub(max_pos, min_pos, size);
	glm_ivec3_adds(size, 5, size);
	uint8_t *flags = xcalloc(1, size[0] * size[1] * size[2]);

	// 第一遍：按玩家距离收集门的状态标志。
	for (int player = 0; player < 3; player++) {
		if (player == 1 && !b_player2)
			continue;
		if (player == 2 && !b_player3)
			continue;

		ivec3 idx;
		glm_ivec3_sub(pos[player], base, idx);
		int flag_index = (idx[0] * size[1] + idx[1]) * size[2] + idx[2];
		int x = pos[player][0];
		int y = pos[player][1];
		int z = pos[player][2];
		struct dgn_cell *cell = dgn_cell_at(ctx->dgn, x, y, z);

		// 分别检查玩家所在格四扇门的距离。
		if (cell->north_door != -1 && !cell->north_door_lock) {
			float dist = (z * 2.f + 1.f) - fpos[player][2];
			if (dist < DOOR_OPEN_DISTANCE)
				flags[flag_index] |= SHOULD_OPEN_NORTH;
			if (dist <= DOOR_CLOSE_PROXIMITY)
				flags[flag_index] |= SHOULD_NOT_CLOSE_NORTH;
		}
		if (cell->west_door != -1 && !cell->west_door_lock) {
			float dist = fpos[player][0] - (x * 2.f - 1.f);
			if (dist < DOOR_OPEN_DISTANCE)
				flags[flag_index] |= SHOULD_OPEN_WEST;
			if (dist <= DOOR_CLOSE_PROXIMITY)
				flags[flag_index] |= SHOULD_NOT_CLOSE_WEST;
		}
		if (cell->south_door != -1 && !cell->south_door_lock) {
			float dist = fpos[player][2] - (z * 2.f - 1.f);
			if (dist < DOOR_OPEN_DISTANCE)
				flags[flag_index] |= SHOULD_OPEN_SOUTH;
			if (dist <= DOOR_CLOSE_PROXIMITY)
				flags[flag_index] |= SHOULD_NOT_CLOSE_SOUTH;
		}
		if (cell->east_door != -1 && !cell->east_door_lock) {
			float dist = (x * 2.f + 1.f) - fpos[player][0];
			if (dist < DOOR_OPEN_DISTANCE)
				flags[flag_index] |= SHOULD_OPEN_EAST;
			if (dist <= DOOR_CLOSE_PROXIMITY)
				flags[flag_index] |= SHOULD_NOT_CLOSE_EAST;
		}
	}

	// 第二遍：遍历包围盒，更新门的状态。
	*open_out = false;
	*close_out = false;
	*open_tex_out = 0;
	*close_tex_out = 0;
	for (int z = min_pos[2] - 1; z <= max_pos[2] + 1; z++) {
		for (int y = min_pos[1] - 1; y <= max_pos[1] + 1; y++) {
			for (int x = min_pos[0] - 1; x <= max_pos[0] + 1; x++) {
				if (!dgn_is_in_map(ctx->dgn, x, y, z))
					continue;
				struct dgn_cell *cell = dgn_cell_at(ctx->dgn, x, y, z);

				int flag_index = ((x - base[0]) * size[1] + (y - base[1])) * size[2] + (z - base[2]);
				int flag_index_north = flag_index + 1;
				int flag_index_west = flag_index - size[1] * size[2];
				int flag_index_south = flag_index - 1;
				int flag_index_east = flag_index + size[1] * size[2];

				// 需要的话把门打开。
				if (flags[flag_index] & SHOULD_OPEN_NORTH && cell->north_door_angle == 0.f) {
					cell->north_door_angle = DOOR_OPEN_ANGLE;
					dgn_cell_at(ctx->dgn, x, y, z + 1)->south_door_angle = -DOOR_OPEN_ANGLE;
					*open_out = true;
					*open_tex_out = cell->north_door;
				}
				if (flags[flag_index] & SHOULD_OPEN_WEST && cell->west_door_angle == 0.f) {
					cell->west_door_angle = DOOR_OPEN_ANGLE;
					dgn_cell_at(ctx->dgn, x - 1, y, z)->east_door_angle = -DOOR_OPEN_ANGLE;
					*open_out = true;
					*open_tex_out = cell->west_door;
				}
				if (flags[flag_index] & SHOULD_OPEN_SOUTH && cell->south_door_angle == 0.f) {
					cell->south_door_angle = DOOR_OPEN_ANGLE;
					dgn_cell_at(ctx->dgn, x, y, z - 1)->north_door_angle = -DOOR_OPEN_ANGLE;
					*open_out = true;
					*open_tex_out = cell->south_door;
				}
				if (flags[flag_index] & SHOULD_OPEN_EAST && cell->east_door_angle == 0.f) {
					cell->east_door_angle = DOOR_OPEN_ANGLE;
					dgn_cell_at(ctx->dgn, x + 1, y, z)->west_door_angle = -DOOR_OPEN_ANGLE;
					*open_out = true;
					*open_tex_out = cell->east_door;
				}

				// 附近没有玩家就把门关上。
				if (!(flags[flag_index] & SHOULD_NOT_CLOSE_NORTH) &&
				    !(flags[flag_index_north] & SHOULD_NOT_CLOSE_SOUTH) &&
				    cell->north_door_angle != 0.f) {
					cell->north_door_angle = 0.f;
					dgn_cell_at(ctx->dgn, x, y, z + 1)->south_door_angle = 0.f;
					*close_out = true;
					*close_tex_out = cell->north_door;
				}
				if (!(flags[flag_index] & SHOULD_NOT_CLOSE_WEST) &&
				    !(flags[flag_index_west] & SHOULD_NOT_CLOSE_EAST) &&
				    cell->west_door_angle != 0.f) {
					cell->west_door_angle = 0.f;
					dgn_cell_at(ctx->dgn, x - 1, y, z)->east_door_angle = 0.f;
					*close_out = true;
					*close_tex_out = cell->west_door;
				}
				if (!(flags[flag_index] & SHOULD_NOT_CLOSE_SOUTH) &&
				    !(flags[flag_index_south] & SHOULD_NOT_CLOSE_NORTH) &&
				    cell->south_door_angle != 0.f) {
					cell->south_door_angle = 0.f;
					dgn_cell_at(ctx->dgn, x, y, z - 1)->north_door_angle = 0.f;
					*close_out = true;
					*close_tex_out = cell->south_door;
				}
				if (!(flags[flag_index] & SHOULD_NOT_CLOSE_EAST) &&
				    !(flags[flag_index_east] & SHOULD_NOT_CLOSE_WEST) &&
				    cell->east_door_angle != 0.f) {
					cell->east_door_angle = 0.f;
					dgn_cell_at(ctx->dgn, x + 1, y, z)->west_door_angle = 0.f;
					*close_out = true;
					*close_tex_out = cell->east_door;
				}
			}
		}
	}

	free(flags);
}

static struct page *make_pos_t_array(struct dgn *dgn, uint8_t *cell_flags, int flag)
{
	int struct_type = ain_get_struct(ain, "pos_t");
	struct ain_struct *s = &ain->structures[struct_type];
	int slot_x = find_struct_slot(s, "x");
	int slot_y = find_struct_slot(s, "y");

	int nr_points = 0;
	for (int i = 0; i < dgn->size_z * dgn->size_x; i++) {
		if (cell_flags[i] & flag) {
			nr_points++;
		}
	}

	struct page *array = alloc_page(ARRAY_PAGE, AIN_ARRAY_STRUCT, nr_points);
	array->array.struct_type = struct_type;
	array->array.rank = 1;
	int out_index = 0;
	for (int i = 0; i < dgn->size_z * dgn->size_x; i++) {
		if (!(cell_flags[i] & flag))
			continue;
		struct page *elem = alloc_page(STRUCT_PAGE, struct_type, s->nr_members);
		elem->values[slot_x].i = i % dgn->size_x;
		elem->values[slot_y].i = i / dgn->size_x;
		array->values[out_index++].i = heap_alloc_page(elem);
	}
	return array;
}

static void replace_object_slot(struct page *obj, const char *name, struct page *page)
{
	int slot = struct_get_var(obj, name)->i;
	delete_page(slot);
	heap_set_page(slot, page);
}

static void PastelChime2_AutoDungeonE_Create(
	int floor, int *ent_x, int *ent_y, int complex,
	int wall_arrange_method, int floor_arrange_method,
	int field_size_x, int field_size_y, int door_lock_percent, struct page **dci)
{
	struct dungeon_context *ctx = dungeon_get_context(draw_field_sprite_number);
	if (!ctx)
		return;

	uint8_t *cell_flags = xcalloc(field_size_y, field_size_x);
	uint32_t seed = SDL_GetTicks();
	NOTICE("PastelChime2.AutoDungeonE_Create: seed = %u, complexity = %d", seed, complex);
	struct dgn *dgn = dgn_generate_drawfield(
		floor, complex, wall_arrange_method, floor_arrange_method,
		field_size_x, field_size_y, door_lock_percent, seed, cell_flags);
	if (ctx->dgn)
		dgn_free(ctx->dgn);
	ctx->dgn = dgn;
	dungeon_map_init(ctx);

	replace_object_slot(*dci, "aposItem", make_pos_t_array(dgn, cell_flags, DF_FLAG_ITEM));
	replace_object_slot(*dci, "aposEnemy", make_pos_t_array(dgn, cell_flags, DF_FLAG_ENEMY));
	replace_object_slot(*dci, "aposTrap", make_pos_t_array(dgn, cell_flags, DF_FLAG_TRAP));
	replace_object_slot(*dci, "aposNoTreasure", make_pos_t_array(dgn, cell_flags, DF_FLAG_NO_TREASURE));
	replace_object_slot(*dci, "aposNoMonster", make_pos_t_array(dgn, cell_flags, DF_FLAG_NO_MONSTER));
	replace_object_slot(*dci, "aposNoTrap", make_pos_t_array(dgn, cell_flags, DF_FLAG_NO_TRAP));

	*ent_x = dgn->start_x;
	*ent_y = dgn->start_y;
	free(cell_flags);
}

//void PastelChime2_TestVMArray(struct page **a);

static void PastelChime2_str_erase_found(struct string **s, struct string **key)
{
	char *src = (*s)->text;
	char *found = strstr(src, (*key)->text);
	if (!found)
		return;

	char *buf = xmalloc((*s)->size);
	char *dst = buf;
	do {
		strncpy(dst, src, found - src);
		dst += found - src;
		src = found + (*key)->size;
		found = strstr(src, (*key)->text);
	} while (found);
	strcpy(dst, src);

	free_string(*s);
	*s = make_string(buf, strlen(buf));
	free(buf);
}

HLL_WARN_UNIMPLEMENTED(false, bool, PastelChime2, Field_PickUpRoadShape, struct page **a4, struct page **a3, struct page **a2, struct page **a1, struct page **a0, struct page **ac, int nFloor);
//void PastelChime2_SaveMapPicture(struct string *sFileName);
//bool PastelChime2_ShellOpen(struct string *sFileName);
//bool PastelChime2_File_GetTime(struct string *strFileName, int *nYear, int *nMonth, int *nDayOfWeek, int *nDay, int *nHour, int *nMinute, int *nSecond, int *nMilliseconds);

static void zero_rle_compress_to_file(FILE *fp, const uint8_t *data, uint32_t size)
{
	const uint8_t *p = data;
	const uint8_t *end = data + size;

	while (p < end) {
		fputc(*p, fp);
		if (*p == 0) {
			int zero_count = 0;
			while (p < end && *p == 0 && zero_count < 255) {
				zero_count++;
				p++;
			}
			fputc(zero_count, fp);
		} else {
			p++;
		}
	}
}

static bool zero_rle_decompress_from_file(FILE *fp, struct buffer *buf)
{
	while (true) {
		int c = fgetc(fp);
		if (c == EOF)
			break;
		if (c == 0) {
			int zero_count = fgetc(fp);
			if (zero_count == EOF || zero_count == 0)
				return false; // malformed file
			for (int i = 0; i < zero_count; i++) {
				buffer_write_int8(buf, 0);
			}
		} else {
			buffer_write_int8(buf, c);
		}
	}
	buf->size = buf->index;
	buf->index = 0;
	return true;
}

static bool save_array(struct string *file_name, struct page **page, int array_type, int struct_type)
{
	struct buffer buf;
	buffer_init(&buf, NULL, 0);
	write_array(&buf, *page, array_type, struct_type, false);

	bool result = false;
	char *path = gamedir_path(file_name->text);
	FILE *fp = file_open_utf8(path, "wb");
	if (fp) {
		zero_rle_compress_to_file(fp, buf.buf, buf.index);
		result = true;
		fclose(fp);
	}
	free(path);
	free(buf.buf);
	return result;
}

static bool PastelChime2_SaveArray(struct string *file_name, struct page **page)
{
	return save_array(file_name, page, AIN_ARRAY_INT, -1);
}

static bool PastelChime2_SaveStringArray(struct string *file_name, struct page **page)
{
	return save_array(file_name, page, AIN_ARRAY_STRING, -1);
}

static bool PastelChime2_LoadArray(struct string *file_name, struct page **page)
{
	bool result = false;
	char *path = gamedir_path(file_name->text);
	FILE *fp = file_open_utf8(path, "rb");
	if (fp) {
		struct buffer buf;
		buffer_init(&buf, NULL, 0);
		if (zero_rle_decompress_from_file(fp, &buf)) {
			if (*page) {
				delete_page_vars(*page);
				free_page(*page);
			}
			*page = read_array(&buf, false);
			result = true;
		}
		free(buf.buf);
		fclose(fp);
	}
	free(path);
	return result;
}

static bool PastelChime2_File_Delete(struct string *file_name)
{
	char *path = gamedir_path(file_name->text);
	int r = remove_utf8(path);
	free(path);
	return r == 0;
}

static bool PastelChime2_File_CreateDummy(struct string *file_name)
{
	char *path = gamedir_path(file_name->text);
	uint8_t data[1] = {0};
	bool r = file_write(path, data, sizeof(data));
	free(path);
	return r;
}

HLL_LIBRARY(PastelChime2,
	    HLL_EXPORT(InitPastelChime2, PastelChime2_InitPastelChime2),
	    HLL_EXPORT(DungeonDataSave, PastelChime2_DungeonDataSave),
	    HLL_EXPORT(DungeonDataLoad, PastelChime2_DungeonDataLoad),
	    HLL_EXPORT(DungeonDataExist, PastelChime2_DungeonDataExist),
	    HLL_EXPORT(DungeonDataToSaveData, PastelChime2_DungeonDataToSaveData),
	    HLL_EXPORT(DungeonDataFromSaveData, PastelChime2_DungeonDataFromSaveData),
	    HLL_EXPORT(Field_SetSprite, PastelChime2_Field_SetSprite),
	    HLL_EXPORT(Field_GetDoors, PastelChime2_Field_GetDoors),
	    HLL_EXPORT(Field_UpdateDoorNamePlate, PastelChime2_Field_UpdateDoorNamePlate),
	    HLL_EXPORT(Field_UpdateObjectNamePlate, PastelChime2_Field_UpdateObjectNamePlate),
	    HLL_EXPORT(Field_QuerySelectedObjectPlate, PastelChime2_Field_QuerySelectedObjectPlate),
	    HLL_EXPORT(Field_QuerySelectedDoorPlate, PastelChime2_Field_QuerySelectedDoorPlate),
	    HLL_EXPORT(Field_UpdateEnemyPos, PastelChime2_Field_UpdateEnemyPos),
	    HLL_EXPORT(Field_CheckObjectHit, PastelChime2_Field_CheckObjectHit),
	    HLL_EXPORT(Field_UpdateObjectAnime, PastelChime2_Field_UpdateObjectAnime),
	    HLL_EXPORT(Field_CalcY, PastelChime2_Field_CalcY),
	    HLL_EXPORT(Field_CalcCamera, PastelChime2_Field_CalcCamera),
	    HLL_EXPORT(Field_CheckLook, PastelChime2_Field_CheckLook),
	    HLL_EXPORT(Field_CalcMove, PastelChime2_Field_CalcMove),
	    HLL_EXPORT(Field_UpdatePlayersUnit, PastelChime2_Field_UpdatePlayersUnit),
	    HLL_EXPORT(Field_ClearPlayerMoveQueue, PastelChime2_Field_ClearPlayerMoveQueue),
	    HLL_EXPORT(JudgeHitWall, PastelChime2_JudgeHitWall),
	    HLL_EXPORT(AdjustMovePos, PastelChime2_AdjustMovePos),
	    HLL_TODO_EXPORT(JudgeHitPointCircle, PastelChime2_JudgeHitPointCircle),
	    HLL_TODO_EXPORT(JudgeHitLineCircle, PastelChime2_JudgeHitLineCircle),
	    HLL_EXPORT(Field_UpdateDoors, PastelChime2_Field_UpdateDoors),
	    HLL_EXPORT(AutoDungeonE_Create, PastelChime2_AutoDungeonE_Create),
	    HLL_TODO_EXPORT(TestVMArray, PastelChime2_TestVMArray),
	    HLL_EXPORT(str_erase_found, PastelChime2_str_erase_found),
	    HLL_EXPORT(Field_PickUpRoadShape, PastelChime2_Field_PickUpRoadShape),
	    HLL_TODO_EXPORT(SaveMapPicture, PastelChime2_SaveMapPicture),
	    HLL_TODO_EXPORT(ShellOpen, PastelChime2_ShellOpen),
	    HLL_TODO_EXPORT(File_GetTime, PastelChime2_File_GetTime),
	    HLL_EXPORT(SaveArray, PastelChime2_SaveArray),
	    HLL_EXPORT(LoadArray, PastelChime2_LoadArray),
	    HLL_EXPORT(SaveStringArray, PastelChime2_SaveStringArray),
	    HLL_EXPORT(LoadStringArray, PastelChime2_LoadArray),
	    HLL_EXPORT(File_Delete, PastelChime2_File_Delete),
	    HLL_EXPORT(File_CreateDummy, PastelChime2_File_CreateDummy)
	    );
