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

#include <stdbool.h>
#include <stdlib.h>

#include "system4/flat.h"

#include "parts.h"
#include "parts_internal.h"

void PE_SetComponentMargin(int parts_no, int top, int bottom, int left, int right)
{
	struct parts *parts = parts_get(parts_no);
	parts->margin_top = top;
	parts->margin_bottom = bottom;
	parts->margin_left = left;
	parts->margin_right = right;
}

int PE_GetComponentMarginTop(int parts_no)
{
	return parts_get(parts_no)->margin_top;
}

int PE_GetComponentMarginBottom(int parts_no)
{
	return parts_get(parts_no)->margin_bottom;
}

int PE_GetComponentMarginLeft(int parts_no)
{
	return parts_get(parts_no)->margin_left;
}

int PE_GetComponentMarginRight(int parts_no)
{
	return parts_get(parts_no)->margin_right;
}

void PE_SetLayoutBoxLayoutType(int parts_no, int type)
{
	struct parts *parts = parts_get(parts_no);
	struct parts_layout_box *lb = parts_get_layout_box(parts);
	if (lb->layout_type != type) {
		lb->layout_type = type;
		parts_component_dirty(parts);
	}
}

int PE_GetLayoutBoxLayoutType(int parts_no)
{
	struct parts *parts = parts_try_get(parts_no);
	if (!parts || parts->states[0].type != PARTS_LAYOUT_BOX)
		return PARTS_LAYOUT_FREE;
	return parts->states[0].layout_box.layout_type;
}

void PE_SetLayoutBoxReturn(int parts_no, bool return_flag, int return_size)
{
	struct parts *parts = parts_get(parts_no);
	struct parts_layout_box *lb = parts_get_layout_box(parts);
	lb->wrap = return_flag;
	lb->wrap_size = return_size;
	parts_component_dirty(parts);
}

bool PE_IsLayoutBoxReturn(int parts_no)
{
	struct parts *parts = parts_try_get(parts_no);
	if (!parts || parts->states[0].type != PARTS_LAYOUT_BOX)
		return false;
	return parts->states[0].layout_box.wrap;
}

int PE_GetLayoutBoxReturnSize(int parts_no)
{
	struct parts *parts = parts_try_get(parts_no);
	if (!parts || parts->states[0].type != PARTS_LAYOUT_BOX)
		return 0;
	return parts->states[0].layout_box.wrap_size;
}

void PE_SetLayoutBoxAlign(int parts_no, int align)
{
	struct parts *parts = parts_get(parts_no);
	struct parts_layout_box *lb = parts_get_layout_box(parts);
	if (lb->align != align) {
		lb->align = align;
		parts_component_dirty(parts);
	}
}

int PE_GetLayoutBoxAlign(int parts_no)
{
	struct parts *parts = parts_try_get(parts_no);
	if (!parts || parts->states[0].type != PARTS_LAYOUT_BOX)
		return 0;
	return parts->states[0].layout_box.align;
}

void PE_set_layoutbox_padding(int parts_no, int top, int bottom, int left, int right)
{
	struct parts *parts = parts_get(parts_no);
	struct parts_layout_box *lb = parts_get_layout_box(parts);
	lb->padding_top = top;
	lb->padding_bottom = bottom;
	lb->padding_left = left;
	lb->padding_right = right;
	parts_component_dirty(parts);
}

int PE_get_layoutbox_padding_top(int parts_no)
{
	struct parts *parts = parts_try_get(parts_no);
	if (!parts || parts->states[0].type != PARTS_LAYOUT_BOX)
		return 0;
	return parts->states[0].layout_box.padding_top;
}

int PE_get_layoutbox_padding_bottom(int parts_no)
{
	struct parts *parts = parts_try_get(parts_no);
	if (!parts || parts->states[0].type != PARTS_LAYOUT_BOX)
		return 0;
	return parts->states[0].layout_box.padding_bottom;
}

int PE_get_layoutbox_padding_left(int parts_no)
{
	struct parts *parts = parts_try_get(parts_no);
	if (!parts || parts->states[0].type != PARTS_LAYOUT_BOX)
		return 0;
	return parts->states[0].layout_box.padding_left;
}

int PE_get_layoutbox_padding_right(int parts_no)
{
	struct parts *parts = parts_try_get(parts_no);
	if (!parts || parts->states[0].type != PARTS_LAYOUT_BOX)
		return 0;
	return parts->states[0].layout_box.padding_right;
}

static void parts_get_layout_size(struct parts *parts, int *w, int *h)
{
	struct parts_state *state = &parts->states[parts->state];
	if (state->type == PARTS_FLAT && state->flat.flat) {
		// Flat 的布局尺寸写在文件头里，可能和
		// 命中盒尺寸不一样。
		*w = state->flat.flat->hdr.width;
		*h = state->flat.flat->hdr.height;
	} else {
		*w = state->common.w;
		*h = state->common.h;
	}
}

static int align_offset_x(int align, int total_w)
{
	switch (align) {
	case 2: case 5: case 8: return -(total_w / 2);
	case 3: case 6: case 9: return -total_w;
	default: return 0;
	}
}

static int align_offset_y(int align, int total_h)
{
	switch (align) {
	case 4: case 5: case 6: return -(total_h / 2);
	case 7: case 8: case 9: return -total_h;
	default: return 0;
	}
}

// 按行累加子部件宽度，算出水平布局盒的总尺寸。
static void parts_layout_calc_size_horizontal(struct parts_layout_box *lb, struct parts *parts,
		int *out_w, int *out_h)
{
	int row_w = 0, max_row_w = 0, max_row_h = 0, total_h = 0;
	struct parts *child;
	PARTS_FOREACH_CHILD(child, parts) {
		int child_w, child_h;
		parts_get_layout_size(child, &child_w, &child_h);
		int cw = child->margin_left + child_w + child->margin_right;
		int ch = child->margin_top + child_h + child->margin_bottom;
		if (lb->wrap && row_w + cw > lb->wrap_size && row_w > 0) {
			if (row_w > max_row_w)
				max_row_w = row_w;
			total_h += max_row_h;
			row_w = 0;
			max_row_h = 0;
		}
		row_w += cw;
		if (ch > max_row_h)
			max_row_h = ch;
	}
	if (row_w > max_row_w)
		max_row_w = row_w;
	total_h += max_row_h;
	*out_w = max_row_w + lb->padding_left + lb->padding_right;
	*out_h = total_h + lb->padding_top + lb->padding_bottom;
}

// 按列累加子部件高度，算出垂直布局盒的总尺寸。
static void parts_layout_calc_size_vertical(struct parts_layout_box *lb, struct parts *parts,
		int *out_w, int *out_h)
{
	int col_h = 0, max_col_h = 0, max_col_w = 0, total_w = 0;
	struct parts *child;
	PARTS_FOREACH_CHILD(child, parts) {
		int child_w, child_h;
		parts_get_layout_size(child, &child_w, &child_h);
		int cw = child->margin_left + child_w + child->margin_right;
		int ch = child->margin_top + child_h + child->margin_bottom;
		if (lb->wrap && col_h + ch > lb->wrap_size && col_h > 0) {
			if (col_h > max_col_h)
				max_col_h = col_h;
			total_w += max_col_w;
			col_h = 0;
			max_col_w = 0;
		}
		col_h += ch;
		if (cw > max_col_w)
			max_col_w = cw;
	}
	if (col_h > max_col_h)
		max_col_h = col_h;
	total_w += max_col_w;
	*out_w = total_w + lb->padding_left + lb->padding_right;
	*out_h = max_col_h + lb->padding_top + lb->padding_bottom;
}

/* [D515b] 静态绝对布局的撤回判定（装载完成后对每个 EX 声明的布局盒调一次）。
 *
 * EX 配置声明了纵/横 ≠ 该容器应该自动排版。d550 实测反例：07 设置页的
 * 页签框架 900000090（`配置`=纵）的 8 个页签按钮 EX 座标 (1607,119..919)
 * 是**静态绝对坐标**，parts_do_layout 却把它们逐个堆到 (0,0..950)，
 * 页签 hitbox 全被挪走 —— (1778,864) 只能命中全屏背景 900000088，
 * 3D 页签点不中（用户报「3D 选项打不开」的直接原因）。
 *
 * 判据沿用 D250：真正依赖布局盒重排的容器（LargeSkillButton /
 * CardApView / 3D 设置页 21 容器）子件座标**全为 (0,0)**；子件有非零
 * 座标的是静态绝对布局，排版只会破坏它。撤回时 states[state] 与
 * layout_box_data 副本都要改 —— 副本是 AIN 改 state 后 parts_do_layout
 * 的数据源，只改一份会在改 state 后复发。AIN 之后显式
 * SetLayoutBoxLayoutType 仍会覆盖，语义不冲突。 */
void parts_layout_veto_static_children(int parts_no)
{
	struct parts *parts = parts_try_get(parts_no);
	if (!parts)
		return;
	struct parts_layout_box *lb;
	if (parts->states[parts->state].type == PARTS_LAYOUT_BOX)
		lb = &parts->states[parts->state].layout_box;
	else if (parts->has_layout_box)
		lb = &parts->layout_box_data;
	else
		return;
	if (lb->layout_type == PARTS_LAYOUT_FREE)
		return;
	struct parts *child;
	PARTS_FOREACH_CHILD(child, parts) {
		if (child->local.pos.x != 0 || child->local.pos.y != 0) {
			lb->layout_type = PARTS_LAYOUT_FREE;
			if (parts->has_layout_box)
				parts->layout_box_data.layout_type = PARTS_LAYOUT_FREE;
			return;
		}
	}
}

void parts_do_layout(struct parts *parts)
{
	/* [D515] 探针：`XSYS4_LAYOUT_DBG=1` 打印每个布局盒的判据。
	 * 3D 设置页（Asra\设置／０８／３Ｄ页面）21 个容器全停在 (0,0)、
	 * 子件互相重叠 —— 先分清「不是布局盒」「layout_type=FREE」
	 * 「子件没挂上」三种情况。 */
	if (getenv("XSYS4_LAYOUT_DBG")) {
		static int n;
		if (n++ < 400) {
			int nc = 0;
			struct parts *c;
			PARTS_FOREACH_CHILD(c, parts) { nc++; }
			NOTICE("[lay] no=%d state=%d st0=%d type=%d children=%d "
			       "w=%d h=%d pos=(%d,%d)",
			       parts->no, (int)parts->state,
			       parts->states[parts->state].type,
			       parts->has_layout_box ? (int)parts->layout_box_data.layout_type : -1, nc,
			       parts->states[parts->state].common.w,
			       parts->states[parts->state].common.h,
			       parts->global.pos.x, parts->global.pos.y);
		}
	}
	/* [D515] 取布局数据：当前 state 是布局盒就用它（游戏可能改过
	 * padding/wrap），否则退回装载时存的独立副本（state 被 AIN 改成 CG
	 * 后仍然要排版）。 */
	struct parts_layout_box *lb;
	if (parts->states[parts->state].type == PARTS_LAYOUT_BOX) {
		lb = &parts->states[parts->state].layout_box;
	} else if (parts->has_layout_box) {
		lb = &parts->layout_box_data;
	} else {
		return;
	}
	if (lb->layout_type == PARTS_LAYOUT_FREE)
		return;

	int total_w, total_h;
	if (lb->layout_type == PARTS_LAYOUT_HORIZONTAL)
		parts_layout_calc_size_horizontal(lb, parts, &total_w, &total_h);
	else
		parts_layout_calc_size_vertical(lb, parts, &total_w, &total_h);

	int align = lb->align;
	bool is_left   = (align == 1 || align == 4 || align == 7);
	bool is_right  = (align == 3 || align == 6 || align == 9);
	bool is_top    = (align == 1 || align == 2 || align == 3);
	bool is_bottom = (align == 7 || align == 8 || align == 9);

	int origin_x = align_offset_x(parts->origin_mode, total_w);
	int origin_y = align_offset_y(parts->origin_mode, total_h);
	int align_x = align_offset_x(align, total_w);
	int align_y = align_offset_y(align, total_h);

	int start_x = (is_left ? lb->padding_left : -lb->padding_right)
		+ (origin_x - align_x);
	int start_y = (is_top ? lb->padding_top : -lb->padding_bottom)
		+ (origin_y - align_y);

	int x_dir = is_right ? -1 : 1;
	int y_dir = is_bottom ? -1 : 1;

	int cur_x = start_x;
	int cur_y = start_y;
	int max_cross = 0;

	struct parts *child;
	PARTS_FOREACH_CHILD(child, parts) {
		int child_w, child_h;
		parts_get_layout_size(child, &child_w, &child_h);
		int cw = child->margin_left + child_w + child->margin_right;
		int ch = child->margin_top + child_h + child->margin_bottom;

		// 布局盒会强制子部件的 origin_mode 与自己的对齐方式一致。
		if (child->origin_mode != align) {
			child->origin_mode = align;
			parts_recalculate_hitbox(child);
		}

		if (lb->layout_type == PARTS_LAYOUT_HORIZONTAL) {
			if (lb->wrap && abs((cur_x + cw * x_dir) - start_x) > lb->wrap_size
					&& max_cross > 0) {
				cur_y += max_cross * y_dir;
				max_cross = 0;
				cur_x = start_x;
			}
		} else {
			if (lb->wrap && abs((cur_y + ch * y_dir) - start_y) > lb->wrap_size
					&& max_cross > 0) {
				cur_x += max_cross * x_dir;
				max_cross = 0;
				cur_y = start_y;
			}
		}

		// 边距偏移取决于对齐方向
		int mx = is_left ? child->margin_left : is_right ? -child->margin_right : 0;
		int my = is_top ? child->margin_top : is_bottom ? -child->margin_bottom : 0;

		int pos_x = cur_x + mx;
		int pos_y = cur_y + my;
		if (child->local.pos.x != pos_x || child->local.pos.y != pos_y) {
			child->local.pos.x = pos_x;
			child->local.pos.y = pos_y;
			parts_recalculate_hitbox(child);
			parts_component_dirty(child);
		}

		if (lb->layout_type == PARTS_LAYOUT_HORIZONTAL) {
			cur_x += cw * x_dir;
			if (ch > max_cross)
				max_cross = ch;
		} else {
			cur_y += ch * y_dir;
			if (cw > max_cross)
				max_cross = cw;
		}
	}

	/* [D515] 把算出的总量写回布局盒**自身尺寸**。
	 *
	 * 原来 total_w/total_h 算完就丢掉了，布局盒永远是 w=h=0。单层还能靠
	 * 子件的绝对坐标蒙对，**嵌套**就全崩：外层 `parts_get_layout_size()`
	 * 拿到 0，把内层当 0 高度，所有嵌套内容堆在同一个点上
	 * （3D 设置页 21 个容器、文字全叠成一团的主因）。
	 * 只在游戏没显式给过尺寸时写，避免覆盖 EX 声明的固定大小。 */
	if (parts->states[parts->state].common.w <= 0 &&
	    parts->states[parts->state].common.h <= 0 &&
	    total_w > 0 && total_h > 0)
		parts_set_dims(parts, &parts->states[parts->state].common,
			       total_w, total_h);
}
