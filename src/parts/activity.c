/* 版权所有 (C) 2026 Rance10 Android port
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

/*
 * D89：活动子系统以前是 hll/PartsEngine.c 里一堆手写的空桩
 * （f[22..52]），所以 ReadActivityFile() 永远返回 0，
 * 部件表一直是空的，画面什么都没画。
 *
 * 文件格式是 System4 EX（见 libsys4/src/ex.c: ex_read()）。一个
 * 活动文件装的是这样一棵树：
 *
 *   アクティビティ
 *     └─ ルートパーツ            (49 children = 47 field leaves + 2 sub-nodes)
 *          ├─ 座標 / 原点座標 / ... (47 shared fields)
 *          ├─ 种类別情报          (type discriminator + type specific fields)
 *          └─ 子パーツ            (one node per child part, recursive)
 *
 * 文件里的字符串是 GBK（这是中文版），而且必须在内存里保持 GBK：
 * AIN 会拿自己的 GBK 常量跟它们比较，例如
 * GetActivityPartsNumber(name, ルートパーツ)。所以字段名只在
 * 做匹配时才转成 UTF-8，存储时永远不转。
 *
 * 活动里的部件在这里（C 侧）实体化，因为 AIN 从不干这件事：
 * 整个游戏脚本只调了三次 Parts_SetPartsCG，
 * 远不足以搭出一棵部件树。ReadActivityFile 必须解析、构建并注册。
 * register.
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "system4.h"
#include "system4/archive.h"
#include "system4/ex.h"
#include "system4/string.h"
#include "system4/utfsjis.h"

#include "xsystem4.h"
#include "preview3d.h"
#include "asset_manager.h"
#include "vm/page.h"
#include "parts.h"
#include "parts_internal.h"

#define ACT_DEBUG(...) do { if (activity_debug) NOTICE(__VA_ARGS__); } while (0)

/* PE_SetComponentType() 接受、PE_GetComponentType() 返回的
 * 组件类型号。
 *
 * 这些是兰斯10 自己的编号，从游戏的
 * activityeditor::detail::CBatchRenameStack@GetPartsTypeName 那个 switch
 * （AIN switch #31）还原出来的，它把每个类型号映射到显示名。
 * 它们不是上游 xsystem4 的编号：兰斯10 把大多数 CG/gauge/flat
 * 槽位挪了几个值，所以沿用上游常量会让每个 Activity 组件都
 * 悄悄变成错误的类（例如按钮回来时是 CG 类型 11，
 * Activity@CreateComponent 会把它变成 SpinBox，
 * 随后 Activity@GetButton 的 ICAST 就失败）。 */
enum act_component {
	ACT_COMP_BUTTON          = 0,
	ACT_COMP_CHECKBOX        = 1,
	ACT_COMP_VSCROLLBAR      = 2,
	ACT_COMP_HSCROLLBAR      = 3,
	ACT_COMP_TEXTBOX         = 4,
	ACT_COMP_LISTBOX         = 5,
	ACT_COMP_COMBOBOX        = 6,
	ACT_COMP_MLTEXTBOX       = 7,
	ACT_COMP_LAYOUT_BOX      = 8,
	ACT_COMP_RADIOBUTTONBOX  = 9,
	ACT_COMP_MESSAGE_WINDOW  = 10,
	ACT_COMP_SPINBOX         = 11,
	ACT_COMP_PANEL           = 12,
	ACT_COMP_FORM            = 13,
	ACT_COMP_USER_COMPONENT  = 15,
	ACT_COMP_CG              = 17,
	ACT_COMP_ANIMATION_CG    = 18,
	ACT_COMP_TEXT            = 19,
	ACT_COMP_HGAUGE          = 20,
	ACT_COMP_VGAUGE          = 21,
	ACT_COMP_NUMERAL         = 22,
	ACT_COMP_RECT            = 23,
	ACT_COMP_CONSTRUCTION    = 24,
	ACT_COMP_RECT_DETECTION  = 25,
	ACT_COMP_FLAT            = 26,
	ACT_COMP_3DLAYER         = 27,
	ACT_COMP_MOVIE           = 28,
	/* 仅 EX：编辑器的通用基础部件，它的真实类型存放在
	 * 一般状態 子节点里。解析阶段就解析掉，
	 * 从不传给引擎。 */
	ACT_COMP_LOW_LEVEL       = -2,
};

struct activity_part {
	char *name;            /* GBK bytes, as stored in the file */
	int type;              /* ACT_COMP_* */
	int parts_no;          /* engine parts number assigned at build time */

	/* shared fields (see Rance10_AIN底层研究-20261110.md §5.5) */
	float x, y;
	int z;
	int origin_x, origin_y, origin_mode;
	/* [D216] EX「座標を率として認識する」：为 1 时 座標 是相对视图尺寸
	 * 的比率(0.0~1.0)，不是像素。MessageWindow01 的 SYS_消息窗口 就是
	 * 座標=(0,1,1) + 原点座標模式=7(bottom-left)，本意贴屏幕底边。 */
	bool pos_as_ratio;
	bool flip_x, flip_y, show;
	/* [D517d] EX `閉じている`（折叠/关闭）。**不是可见性开关**：
	 * 3D 设置页 19 个子页面全部 閉じている=1 且 表示=1，按「闭合=隐藏」
	 * 处理会让整页变空（d661 实测只剩底图）。原版可见性由
	 * `复选框表示連動`（选项卡联动）决定，此字段仅记录备查。 */
	bool closed;
	/* [D517d] EX `复选框表示連動` = 本部件的可见性跟随指定复选框的勾选态
	 * （3D 设置页的选项卡机制：19 个页面里只有一个绑定到「已勾选」的
	 * 选项卡，其余隐藏）。NULL/空串 = 无联动。 */
	char *show_link_name;
	/* [D517d] EX `选择状态`（复选框初始勾选态）。3D 设置页靠它决定
	 * 初始显示哪一页（'绘制模式选项卡'=1，其余=0）。 */
	int sel_state;
	int alpha;
	int add_rgb[3];
	int mul_rgb[3];
	float scale_x, scale_y;
	float rot_x, rot_y, rot_z;
	int surface_area[4];
	int clickable;
	int pass_cursor;

	/* 种类別情报 / 一般状态 */
	char *cg_name;         /* GBK bytes, empty when none */
	int deform;
	/* [D119] 按钮/flat 部件带的是 フラット名 + 尺寸，而不是 CG 名
	 * （见 NetworkConfirmation.pactex 的 exdump）。没有这些，
	 * 部件尺寸就是 0x0、命中盒 0x0，按钮永远点不到。 */
	char *flat_name;       /* GBK bytes, NULL when absent */
	int w, h;              /* explicit 尺寸, 0 when absent */
	/* [D217] 消息窗口组件的 种类別情报（EX 原文见 MessageWindow01.pactex） */
	int text_area[4];      /* 文本エリア = (470,233,0,0) */
	char *part_text;       /* [D498] 文本パーツ 的 `文本` leaf（asra 设置页面的标签全在这里烘焙） */
	int text_align;        /* [D498] `文本位置`（对齐模式） */
	int font_size;         /* 字体尺寸 = 39（此前退化成默认 16px） */
	int font_color[3];     /* 字体色 = (255,255,255) */
	int char_space, line_space; /* 文字间距 = -2 / 行间距 = 7 */
	/* [D250] 布局盒的 `配置`（0=FREE / 1=纵向 / 2=横向，与
	 * enum parts_layout_type 同值域）。EX 原文见 LargeSkillButton.pactex：
	 * 活动根是 `レイアウトボックス`、`配置 = 1`，而它的 5 个子部件
	 * （Button/SkillIcon/SkillName/Desc/ApBase）座标**全是 (0,0,1)** ——
	 * 完全靠布局盒重排。D214 为修回想对话框把 LAYOUT_BOX 一律设 FREE，
	 * 于是这 5 个部件全叠在原点，画面上只剩文字没有底板。 */
	int layout_arrangement;

	/* [D238] 数字 / 计量部件（EX 原文见 HpBarPlayer.pactex）。
	 * 类型识别修好后这些负载仍没读，于是 PARTS_NUMERAL/HGAUGE 有类型
	 * 没纹理 —— 建部件路径只给 ACT_COMP_CG 装 CG，HP 条和回合数字
	 * 就一直是空的。
	 *   数字パーツ：ＣＧ名 + 幅リスト(12，COMBINED 条带) + 数值 +
	 *              字间距 + コンマ表示 + 桁数
	 *   横向计量：  ＣＧ名 + 分子/分母(float) */
	int num_widths[12];    /* 幅リスト = 12 个字形宽度 */
	int num_value;         /* 数值 */
	int num_space;         /* 字间距 */
	int num_show_comma;    /* コンマ表示 */
	int num_length;        /* 桁数（0 = 按数值自动） */
	float gauge_num, gauge_denom; /* 分子 / 分母 */

	/* [D271] `矩形パーツ`（组件类型 23）的几何负载。
	 * EX 原文（LargeSkillButton.pactex 的 ApBase）：
	 *
	 *   パーツ类型 = "矩形パーツ"
	 *   矩形模式 = 1
	 *   左上 = {0, 0}   右上 = {77, 0}
	 *   左下 = {0, 30}  右下 = {77, 30}
	 *
	 * 此前一个字段都没读（activity.c 全文 grep `矩形模式`/`左上` 零命中），
	 * 于是这类部件只能拿到 0x0 尺寸。`PARTS_RECT_DETECTION` 本来就不画
	 * 像素（render.c 直接 break），尺寸再是 0，结果就是彻底不可见 ——
	 * 战斗技能按钮的 ApBase 底板全黑正是这条。
	 * 注意：这些不是“四个独立点”，而是同一个四边形在 矩形模式 下的
	 * 两种表示（见 rect_corners_to_size 的注释）。 */
	int rect_mode;         /* 矩形模式 */
	float rect_pts[4][2];  /* 左上/右上/左下/右下 */
	bool rect_have[4];     /* 每个角点是否真的读到了 */
	bool has_rect;         /* EX 是否真的给了矩形负载 */

	/* 用户组件（component type 15）：EX 负载是
	 *   数据 = ['オブジェクト名', 'TextButton', '参数', '标题,从头开始']
	 * 也就是引擎的一个内建控件加上它逗号分隔的参数。
	 * 兰斯10 的标题菜单完全由它们构成。 */
	char *uc_object;       /* GBK bytes: 'TextButton', ... */
	char *uc_params;       /* GBK bytes: '标题,从头开始' */
	/* [D122] EX 的 数据 是 [键,值,键,值,...]。此前只抽取了
	 * 'オブジェクト名'/'参数' 供 C 侧画文字，没有把原始键值写回引擎的
	 * 用户组件数据袋，于是 AIN 的 ActivityUserObject::GetParam(22526) 调
	 * PartsEngine.GetUserComponentData(no, "参数") 拿到 (nil)，
	 * TextButton@Init 第一步（参数个数 >= 2）就 return，8 个菜单按钮的
	 * 点击委托全都没注册。原样留存整个列表，build_part 时逐项写回。 */
	char **uc_data;        /* flat [key, value, key, value, ...], GBK bytes */
	int uc_data_n;

	int nr_children;
	struct activity_part **children;
};

struct activity {
	char *name;            /* activity name (the "Name" argument) */
	int version;
	int parts_version;
	char *background;      /* 背景图片, GBK bytes */

	struct activity_part *root;
	int nr_parts;
	struct activity_part **flat;

	struct activity *next;
};

static bool activity_debug = false;
static bool activity_debug_checked = false;

static struct activity *activity_list = NULL;
/* [D500-g] 活动步进计数：CreateActivity 新建 +1，release 移除 -1。
 * 帧泵每帧据此调 AIN 的 UpdateActivity(254)——原版由任务体系每帧
 * 接口虚调用（CALLMETHOD 经 vtable，静态扫描不可见），我们的引擎
 * 缺这条驱动 ⇒ 活动的按钮点击检测/把手动画/键盘导航全断。 */
static int activity_open_cnt = 0;

/* ------------------------------------------------------------------ */
/* helpers                                                            */
/* ------------------------------------------------------------------ */

static bool name_is(struct string *s, const char *utf8)
{
	if (!s || !s->text)
		return false;
	char *u = sjis2utf(s->text, strlen(s->text));
	bool eq = u && !strcmp(u, utf8);
	free(u);
	return eq;
}

static char *dup_text(struct string *s)
{
	if (!s || !s->text)
		return NULL;
	return strdup(s->text);
}

static struct ex_tree *get_child(struct ex_tree *t, const char *utf8_name);

/* 在「用户组件」的负载列表里查一个键
 * （数据 = ['オブジェクト名','TextButton','参数','...']），把它的值当作
 * GBK 字节返回。`key` 在源码里是 UTF-8，会转成
 * EX 文件用的编码。 */
static char *uc_param(struct ex_tree *type_info, const char *key)
{
	struct ex_tree *c = get_child(type_info, "数据");
	char *gbk = utf2sjis(key, strlen(key));
	if (!c || !c->is_leaf || c->leaf.value.type != EX_LIST) {
		free(gbk);
		return NULL;
	}
	struct ex_list *l = c->leaf.value.list;
	if (!l) {
		free(gbk);
		return NULL;
	}
	if (!gbk)
		return NULL;
	char *out = NULL;
	for (uint32_t i = 0; i + 1 < l->nr_items; i += 2) {
		struct ex_value *k = &l->items[i].value;
		struct ex_value *v = &l->items[i + 1].value;
		if (k->type != EX_STRING || !k->s || !k->s->text)
			continue;
		if (strcmp(k->s->text, gbk) != 0)
			continue;
		if (v->type == EX_STRING && v->s)
			out = strdup(v->s->text);
		break;
	}
	free(gbk);
	return out;
}

/* [D122] 把 数据 列表原样拷进 part->uc_data，供 build_part 写回引擎。
 * 键和值都是 EX 文件自带的 GBK/SJIS 字节，不做转码——AIN 侧查询时用的
 * 也是同一套字节。非字符串项跳过（游戏的用户组件 数据 全是字符串）。 */
/* [D517] XSYS4_EX_SUBTREE=<名字 ASCII 子串>：递归 dump 命中部件的
 * 种类別情报 子树（键名 + 叶子值）。回答「3D 页 radio box 项的状态
 * 图像字段长什么样」——类型映射修好后画面仍黑，需要看它的图像
 * 引用藏在哪个键里。只打前 60 行防刷屏。 */
static void ex_dump_subtree_rec(struct ex_tree *t, int depth, int *budget)
{
	if (!t || *budget <= 0)
		return;
	(*budget)--;
	char ind[64] = "";
	for (int i = 0; i < depth && i < 20; i++)
		snprintf(ind + strlen(ind), sizeof(ind) - strlen(ind), "  ");
	const char *nm = t->name ? display_sjis0(t->name->text) : "(null)";
	if (t->is_leaf) {
		struct ex_value *v = &t->leaf.value;
		switch (v->type) {
		case EX_INT:
			NOTICE("[ext]%s %s = %d", ind, nm, v->i);
			break;
		case EX_FLOAT:
			NOTICE("[ext]%s %s = %f", ind, nm, v->f);
			break;
		case EX_STRING:
			NOTICE("[ext]%s %s = '%s'", ind, nm,
			       v->s ? display_sjis0(v->s->text) : "(null)");
			break;
		case EX_TABLE:
			NOTICE("[ext]%s %s = TABLE(%u fields x %u rows)", ind,
			       nm, v->t ? v->t->nr_fields : 0,
			       v->t ? v->t->nr_rows : 0);
			break;
		case EX_LIST:
			NOTICE("[ext]%s %s = LIST(%u items)", ind, nm,
			       v->list ? v->list->nr_items : 0);
			/* [D517] 列表项展开：radio box 的图像定义就在
			 * 复选框リスト 的条目里。 */
			if (v->list) {
				for (uint32_t li = 0; li < v->list->nr_items;
				     li++) {
					struct ex_value *lv =
						&v->list->items[li].value;
					if (lv->type == EX_STRING)
						NOTICE("[ext]%s   [%u] '%s'",
						       ind, li,
						       lv->s ? display_sjis0(
								  lv->s->text)
							     : "(null)");
					else if (lv->type == EX_INT)
						NOTICE("[ext]%s   [%u] %d", ind,
						       li, lv->i);
					else if (lv->type == EX_TREE)
						ex_dump_subtree_rec(lv->tree,
								    depth + 1,
								    budget);
					else
						NOTICE("[ext]%s   [%u] type%d",
						       ind, li, (int)lv->type);
				}
			}
			break;
		default:
			NOTICE("[ext]%s %s = type%d", ind, nm, (int)v->type);
		}
		return;
	}
	NOTICE("[ext]%s [%s] (%u children)", ind, nm, t->nr_children);
	for (uint32_t i = 0; i < t->nr_children; i++)
		ex_dump_subtree_rec(&t->children[i], depth + 1, budget);
}

static void uc_read_data(struct activity_part *part, struct ex_tree *type_info)
{
	struct ex_tree *c = get_child(type_info, "数据");
	if (!c || !c->is_leaf || c->leaf.value.type != EX_LIST)
		return;
	struct ex_list *l = c->leaf.value.list;
	if (!l || l->nr_items < 2)
		return;
	part->uc_data = xcalloc(l->nr_items, sizeof(char *));
	for (uint32_t i = 0; i + 1 < l->nr_items; i += 2) {
		struct ex_value *k = &l->items[i].value;
		struct ex_value *v = &l->items[i + 1].value;
		if (k->type != EX_STRING || !k->s || !k->s->text)
			continue;
		part->uc_data[part->uc_data_n++] = strdup(k->s->text);
		part->uc_data[part->uc_data_n++] =
			(v->type == EX_STRING && v->s && v->s->text)
				? strdup(v->s->text) : strdup("");
	}
}

/* 用户组件的 参数 形如 "幅１９６,ＭＥＮＵ" 或 "标题,从头开始"。
 * 前半段若以「幅」开头，后面那串（全角或半角）数字就是按钮宽度。 */
static int uc_parse_width(const char *params)
{
	if (!params)
		return 0;
	char *fu = utf2sjis("幅", strlen("幅"));
	if (!fu)
		return 0;
	bool has = strncmp(params, fu, strlen(fu)) == 0;
	free(fu);
	if (!has)
		return 0;
	const unsigned char *p = (const unsigned char *)params + 2; /* skip 幅 */
	int w = 0, digits = 0;
	while (*p && *p != ',') {
		if (*p >= '0' && *p <= '9') {          /* half width */
			w = w * 10 + (*p - '0');
			digits++;
			p++;
		} else if (p[0] == 0xA3 && p[1] >= 0xB0 && p[1] <= 0xB9) {
			w = w * 10 + (p[1] - 0xB0);        /* full width ０..９ */
			digits++;
			p += 2;
		} else {
			return 0;
		}
	}
	return digits ? w : 0;
}

/* 用户组件的 参数 形如 "幅１９６,ＭＥＮＵ" / "标题,从头开始"：
 * 逗号前那段是宽度声明（`幅Ｗ`）或控件名，逗号后那段才是显示文字。
 * 没有逗号时整串就是显示文字。返回值是 GBK 指针（指向 params 内部）。 */
static const char *uc_parse_label(const char *params)
{
	if (!params)
		return NULL;
	const char *comma = strchr(params, ',');
	return comma ? comma + 1 : params;
}

/* [D141] 参数 的第一个逗号字段是按钮样式（标题 / 幅１９６ ...）。
 * 「标题」对应说明书 r02 里三枚红色圆形主按钮，其余对应金色小按钮。
 * 样式名是 GBK 字节串，这里只按字节前缀判断，不做编码转换。 */
static char *uc_parse_style(const char *params)
{
	if (!params)
		return NULL;
	const char *comma = strchr(params, ',');
	size_t n = comma ? (size_t)(comma - params) : strlen(params);
	return strndup(params, n);
}

static struct ex_tree *get_child(struct ex_tree *t, const char *utf8_name)
{
	if (!t || t->is_leaf)
		return NULL;
	for (uint32_t i = 0; i < t->nr_children; i++) {
		if (name_is(t->children[i].name, utf8_name))
			return &t->children[i];
	}
	return NULL;
}

static bool get_int(struct ex_tree *t, const char *name, int *out)
{
	struct ex_tree *c = get_child(t, name);
	if (!c || !c->is_leaf || c->leaf.value.type != EX_INT)
		return false;
	*out = c->leaf.value.i;
	return true;
}

static char *get_string(struct ex_tree *t, const char *name)
{
	struct ex_tree *c = get_child(t, name);
	if (!c || !c->is_leaf || c->leaf.value.type != EX_STRING)
		return NULL;
	return dup_text(c->leaf.value.s);
}

/* list<int|float> 字段 -> int 数组 */
static bool get_int_list(struct ex_tree *t, const char *name, int *out, int n)
{
	struct ex_tree *c = get_child(t, name);
	if (!c || !c->is_leaf || c->leaf.value.type != EX_LIST)
		return false;
	struct ex_list *l = c->leaf.value.list;
	for (uint32_t i = 0; i < l->nr_items && (int)i < n; i++) {
		struct ex_value *v = &l->items[i].value;
		if (v->type == EX_INT)
			out[i] = v->i;
		else if (v->type == EX_FLOAT)
			out[i] = (int)v->f;
	}
	return true;
}

static bool get_float_list(struct ex_tree *t, const char *name, float *out, int n)
{
	struct ex_tree *c = get_child(t, name);
	if (!c || !c->is_leaf || c->leaf.value.type != EX_LIST)
		return false;
	struct ex_list *l = c->leaf.value.list;
	for (uint32_t i = 0; i < l->nr_items && (int)i < n; i++) {
		struct ex_value *v = &l->items[i].value;
		if (v->type == EX_FLOAT)
			out[i] = v->f;
		else if (v->type == EX_INT)
			out[i] = (float)v->i;
	}
	return true;
}

/* [D238] 计量部件的 分子/分母 是 EX float（100.000000），get_int 只认
 * EX_INT 会静默失败，需要单独读一个标量。 */
static bool get_float(struct ex_tree *t, const char *name, float *out)
{
	struct ex_tree *c = get_child(t, name);
	if (!c || !c->is_leaf)
		return false;
	if (c->leaf.value.type == EX_FLOAT) {
		*out = c->leaf.value.f;
		return true;
	}
	if (c->leaf.value.type == EX_INT) {
		*out = (float)c->leaf.value.i;
		return true;
	}
	return false;
}

/* [D271] 把 `矩形パーツ` 的四个角点换算成 (宽, 高)。
 *
 * 关键点是：这四个点**不是四个独立点** —— 这个名字会误导人。
 * EX 里它们就是同一个四边形的两个对角：
 *
 *   左上=(0,0)  右上=(77,0)
 *   左下=(0,30) 右下=(77,30)
 *
 * 取水平方向的 max-min（左上→右上 = 77，左下→右下 = 77）
 * 与垂直方向的 max-min（左上→左下 = 30，右上→右下 = 30），
 * 两对互相校验。正常数据下两对一致；不一致时取较大的那个
 * （宁可命中盒偏大也不能偏小 —— 偏小会让按钮点不到，而点不到
 * 在日志里表现为「委托注册了但永远不触发」，比“底板少几像素”难查得多）。
 *
 * 缺角时退回用能凑出的那两个点算。
 */
/* [D375] `矩形検出` 是否也按 EX 四角负载定尺寸。见调用处注释。 */
static int act_ex_rect_det_on(void)
{
	static int on = -1;
	if (on < 0) {
		const char *e = getenv("XSYS4_EX_RECT_DET_SIZE");
		on = !(e && !strcmp(e, "0"));
	}
	return on;
}

/* [D387] `矩形検出` 既没有四角负载、EX 尺寸又只是 `1×1` 占位时，是否按父件铺满。
 * 见 build_part 尾部的调用处。`XSYS4_EX_RECT_FILL_PARENT=0` 回退。 */
static int act_ex_rect_fill_parent_on(void)
{
	static int on = -1;
	if (on < 0) {
		const char *e = getenv("XSYS4_EX_RECT_FILL_PARENT");
		on = !(e && !strcmp(e, "0"));
	}
	return on;
}

static void rect_corners_to_size(const float pts[4][2], bool have[4],
		int *out_w, int *out_h)
{
	/* 下标：0=左上 1=右上 2=左下 3=右下 */
	float hspan = 0.0f, vspan = 0.0f;
	if (have[0] && have[1]) {
		float d = pts[1][0] - pts[0][0];
		if (d < 0)
			d = -d;
		if (d > hspan)
			hspan = d;
	}
	if (have[2] && have[3]) {
		float d = pts[3][0] - pts[2][0];
		if (d < 0)
			d = -d;
		if (d > hspan)
			hspan = d;
	}
	if (have[0] && have[2]) {
		float d = pts[2][1] - pts[0][1];
		if (d < 0)
			d = -d;
		if (d > vspan)
			vspan = d;
	}
	if (have[1] && have[3]) {
		float d = pts[3][1] - pts[1][1];
		if (d < 0)
			d = -d;
		if (d > vspan)
			vspan = d;
	}
	/* 只剩对角两颗时（0/3 或 1/2）用包围盒。 */
	if (hspan == 0.0f || vspan == 0.0f) {
		int n = 0;
		float minx = 0, maxx = 0, miny = 0, maxy = 0;
		for (int i = 0; i < 4; i++) {
			if (!have[i])
				continue;
			if (n == 0) {
				minx = maxx = pts[i][0];
				miny = maxy = pts[i][1];
			} else {
				if (pts[i][0] < minx) minx = pts[i][0];
				if (pts[i][0] > maxx) maxx = pts[i][0];
				if (pts[i][1] < miny) miny = pts[i][1];
				if (pts[i][1] > maxy) maxy = pts[i][1];
			}
			n++;
		}
		if (n >= 2) {
			if (hspan == 0.0f) hspan = maxx - minx;
			if (vspan == 0.0f) vspan = maxy - miny;
		}
	}
	*out_w = (int)(hspan + 0.5f);
	*out_h = (int)(vspan + 0.5f);
}

/* 把 EX 文件里的 パーツ类型 名映射成兰斯10 的组件类型号。
 * 名字不认识时返回 -1。 */
static int component_type_from_name(const char *type_name)
{
	if (!type_name)
		return -1;
	/* EX 字符串存的是游戏的原生编码（这个版本是 GBK）；
	 * 这里转一次，好让下面的比较能用 UTF-8 字面量。 */
	char *u = sjis2utf(type_name, strlen(type_name));
	if (!u)
		return -1;

	static const struct { const char *name; int type; } table[] = {
		{ "按钮",              ACT_COMP_BUTTON },
		{ "检查ＢＯＸ",         ACT_COMP_CHECKBOX },
		{ "检查BOX",           ACT_COMP_CHECKBOX },
		{ "复选框",             ACT_COMP_CHECKBOX },
		{ "纵向滚动条栏",        ACT_COMP_VSCROLLBAR },
		{ "横向滚动条栏",        ACT_COMP_HSCROLLBAR },
		{ "文本ＢＯＸ",         ACT_COMP_TEXTBOX },
		{ "文本BOX",           ACT_COMP_TEXTBOX },
		{ "リストＢＯＸ",        ACT_COMP_LISTBOX },
		{ "リストBOX",         ACT_COMP_LISTBOX },
		{ "ＣＯＭＢＯＢＯＸ",     ACT_COMP_COMBOBOX },
		{ "ＭＬ文本ＢＯＸ",      ACT_COMP_MLTEXTBOX },
		{ "レイアウトＢＯＸ",     ACT_COMP_LAYOUT_BOX },
		{ "レイアウトボックス",    ACT_COMP_LAYOUT_BOX },
		{ "ラジオ按钮ＢＯＸ",     ACT_COMP_CHECKBOX },
		/* [D517] 3D 页的变体：片假名ボックス（汉化组两种写法并存）。
		 * RADIOBUTTONBOX 枚举有值但引擎零实现（渲染/交互都不认），
		 * 降级 CG 后查不到图 => 整页黑屏只剩 caption 白字。
		 * 单选组在本作的渲染与交互路径与检查ＢＯＸ一致
		 * （01 页的单选圆圈就是 flat+caption 工作的），互斥由 AIN 侧
		 * 复选框リスト 逻辑负责，引擎层不区分。 */
		{ "ラジオ按钮ボックス",   ACT_COMP_CHECKBOX },
		{ "消息窗口",            ACT_COMP_MESSAGE_WINDOW },
		{ "スピンＢＯＸ",        ACT_COMP_SPINBOX },
		{ "面板",               ACT_COMP_PANEL },
		{ "フォーム",            ACT_COMP_FORM },
		{ "用户组件",            ACT_COMP_USER_COMPONENT },
		{ "ＣＧパーツ",          ACT_COMP_CG },
		{ "ＣＧ",               ACT_COMP_CG },
		{ "循环ＣＧ",            ACT_COMP_ANIMATION_CG },
		{ "文本パーツ",          ACT_COMP_TEXT },
		{ "文本",               ACT_COMP_TEXT },
		{ "横向计量",            ACT_COMP_HGAUGE },
		{ "纵向计量",            ACT_COMP_VGAUGE },
		{ "数字",               ACT_COMP_NUMERAL },
		{ "矩形パーツ",          ACT_COMP_RECT },
		{ "矩形",               ACT_COMP_RECT },
		{ "構築",               ACT_COMP_CONSTRUCTION },
		{ "ＣＧ判定",            ACT_COMP_RECT_DETECTION },
		{ "フラットパーツ",       ACT_COMP_FLAT },
		{ "フラット",            ACT_COMP_FLAT },
		{ "３Ｄ层",             ACT_COMP_3DLAYER },
		{ "动画",               ACT_COMP_MOVIE },
		{ "低レベルパーツ",       ACT_COMP_LOW_LEVEL },
		{ NULL, 0 },
	};

	int type = -1;
	for (int i = 0; table[i].name; i++) {
		if (!strcmp(u, table[i].name)) {
			type = table[i].type;
			break;
		}
	}
	/* [D238] 同一类型在 EX 里有两种写法：编辑器基础名（"数字" /
	 * "横向计量"）与带 "パーツ" 后缀的完整名（"数字パーツ" /
	 * "横向计量パーツ"）。"低レベルパーツ" 的真实类型就存放在
	 * 一般状態 的 "パーツ类型" 里，而且带后缀 —— 只认短名会解析失败，
	 * Round / Hp / Gauge 便一起回退成 CG，整条数字条带被画成一张图。
	 * 后缀是通用写法，统一剥掉再查一次，不必逐个枚举变体。 */
	if (type < 0) {
		static const char kSuffix[] = "パーツ";
		size_t ulen = strlen(u);
		size_t slen = sizeof kSuffix - 1;
		if (ulen > slen && !memcmp(u + ulen - slen, kSuffix, slen)) {
			char base[128];
			size_t n = ulen - slen;
			if (n < sizeof base) {
				memcpy(base, u, n);
				base[n] = '\0';
				for (int i = 0; table[i].name; i++) {
					if (!strcmp(base, table[i].name)) {
						type = table[i].type;
						break;
					}
				}
			}
		}
	}
	free(u);
	return type;
}

/* ------------------------------------------------------------------ */
/* parsing                                                            */
/* ------------------------------------------------------------------ */

static void parse_part_fields(struct ex_tree *node, struct activity_part *part)
{
	int ivals[5];
	float fvals[3];

	memset(ivals, 0, sizeof(ivals));
	memset(fvals, 0, sizeof(fvals));

	part->x = 0;
	part->y = 0;
	part->z = 0;
	if (get_float_list(node, "座標", fvals, 2)) {
		part->x = fvals[0];
		part->y = fvals[1];
	}
	if (get_int_list(node, "座標", ivals, 3))
		part->z = ivals[2];

	part->origin_x = 0;
	part->origin_y = 0;
	if (get_int_list(node, "原点座標", ivals, 2)) {
		part->origin_x = ivals[0];
		part->origin_y = ivals[1];
	}
	get_int(node, "原点座標模式", &part->origin_mode);

	int b = 0;
	part->pos_as_ratio = get_int(node, "座標を率として認識する", &b) ? b != 0 : false;
	part->flip_x = get_int(node, "左右反转", &b) ? b != 0 : false;
	b = 0;
	part->flip_y = get_int(node, "上下反转", &b) ? b != 0 : false;
	b = 1;
	part->show = get_int(node, "表示", &b) ? b != 0 : true;

	/* [D517d] `閉じている`（折叠/关闭）**不是**可见性开关。
	 * 3D 设置页的 19 个子页面（绘制模式页面/光晕页面/…）全部是
	 * 閉じている=1 且 表示=1；如果按「闭合=隐藏」处理，整页会变空
	 * （d661 实测：只剩底图）。原版的可见性由 `复选框表示連動`
	 * （选项卡联动）决定，这里只记录字段值备查。
	 * XSYS4_EX_CLOSED=1 时退回「闭合=隐藏」的旧实验行为（A/B 用）。 */
	b = 0;
	part->closed = get_int(node, "閉じている", &b) ? b != 0 : false;
	{
		static int ex_closed_hide = -1;
		if (ex_closed_hide < 0) {
			const char *e = getenv("XSYS4_EX_CLOSED");
			ex_closed_hide = (e && *e != '0');
		}
		if (ex_closed_hide && part->closed)
			part->show = false;
	}

	/* [D517d] `复选框表示連動`：可见性跟随指定复选框（选项卡）。
	 * EX 里 19 个页面各写自己绑定的选项卡名（如 光晕页面 → '光晕选项卡'），
	 * 初始只有 '绘制模式选项卡' 是 选择状态=1，原版因此只画绘制模式页面。
	 * 名字解析成部件号在整棵树装完之后做（见 build 完成 pass）。 */
	part->show_link_name = get_string(node, "复选框表示連動");
	if (part->show_link_name && !*part->show_link_name) {
		free(part->show_link_name);
		part->show_link_name = NULL;
	}

	b = 255;
	part->alpha = get_int(node, "アルファ", &b) ? b : 255;

	if (get_int_list(node, "加法色", part->add_rgb, 3) != true)
		memset(part->add_rgb, 0, sizeof(part->add_rgb));
	part->mul_rgb[0] = part->mul_rgb[1] = part->mul_rgb[2] = 255;
	get_int_list(node, "乘法色", part->mul_rgb, 3);

	part->scale_x = 1.0f;
	part->scale_y = 1.0f;
	if (get_float_list(node, "拡大縮小", fvals, 2)) {
		part->scale_x = fvals[0];
		part->scale_y = fvals[1];
	}

	memset(fvals, 0, sizeof(fvals));
	if (get_float_list(node, "旋转", fvals, 3)) {
		part->rot_x = fvals[0];
		part->rot_y = fvals[1];
		part->rot_z = fvals[2];
	}

	get_int_list(node, "表示領域", part->surface_area, 4);
	b = 0;
	part->clickable = get_int(node, "点击許可", &b) ? b : 0;
	b = 0;
	part->pass_cursor = get_int(node, "光标选中透過", &b) ? b : 0;

	/* 种类別情报: part type + type specific payload */
	struct ex_tree *type_info = get_child(node, "种类別情报");
	/* [D517d] 复选框初始勾选态（EX `选择状态`，在 种类別情报 里）。
	 * 3D 设置页的选项卡组靠它决定初始显示哪一页。 */
	{
		int sel = 0;
		if (get_int(type_info, "选择状态", &sel) && sel != 0)
			part->sel_state = sel;
	}
	if (type_info) {
		char *tn = get_string(type_info, "パーツ类型");
		part->type = component_type_from_name(tn);

		/* "低レベルパーツ" (the editor's generic base part) carries its
		 * real rendering type in the 一般状態 child. */
		struct ex_tree *state = get_child(type_info, "一般状态");
		char stcopy[128] = "";
		if (part->type == ACT_COMP_LOW_LEVEL && state) {
			char *sn = get_string(state, "パーツ类型");
			int state_type = component_type_from_name(sn);
			snprintf(stcopy, sizeof stcopy, "%s",
				 sn ? display_sjis0(sn) : "(null)");
			free(sn);
			if (state_type >= 0)
				part->type = state_type;
		}
		if (part->type < 0) {
			/* [D238] 类型名要打出来才有下一步：只看部件名
			 * （'Round' / 'Hp'）猜不出它到底该归哪一类。
			 * display_sjis0 用同一块静态缓冲，两次调用会互相覆盖，
			 * 所以先把类型名拷出来。 */
			char tcopy[128];
			snprintf(tcopy, sizeof tcopy, "%s",
				 tn ? display_sjis0(tn) : "(null)");
			WARNING("Unknown part type '%s' (state='%s') for '%s', treating as CG",
				tcopy, stcopy, display_sjis0(part->name));
			/* [D238] XSYS4_PART_TYPE_DBG=1：把 种类別情报 的子键名
			 * 全打出来。低レベルパーツ（'Number'/'Gauge'/'Hp' 这类
			 * 数字与计量部件）的真实类型藏在某个子节点里，键名不对
			 * 就永远解析不出来 —— 只猜名字会一直猜错。 */
			if (getenv("XSYS4_PART_TYPE_DBG")) {
				NOTICE("[ptype] '%s' 种类別情报 子键 %u 个%s",
				       display_sjis0(part->name),
				       type_info->is_leaf ? 0 : type_info->nr_children,
				       state ? "（一般状态存在但类型未识别）" : "");
				for (uint32_t ci = 0;
				     !type_info->is_leaf && ci < type_info->nr_children;
				     ci++)
					NOTICE("[ptype]   子[%u] = '%s'", ci,
					       display_sjis0(type_info->children[ci].name
							     ? type_info->children[ci].name->text
							     : ""));
			}
			part->type = ACT_COMP_CG;
		}
		free(tn);

		/* 用户组件: 数据 里带 'オブジェクト名'（内置控件名）与 '参数'。 */
		if (part->type == ACT_COMP_USER_COMPONENT) {
			part->uc_object = uc_param(type_info, "オブジェクト名");
			part->uc_params = uc_param(type_info, "参数");
			uc_read_data(part, type_info);
		}

		struct ex_tree *src = state ? state : type_info;
		/* [D517] 子树 dump：见 ex_dump_subtree_rec 注释。 */
		{
			const char *want_sub = getenv("XSYS4_EX_SUBTREE");
			if (want_sub && *want_sub && part->name &&
			    strstr(part->name, want_sub)) {
				int budget = 150;
				NOTICE("[ext] ===== '%s' =====",
				       display_sjis0(part->name));
				ex_dump_subtree_rec(type_info, 0, &budget);
			}
		}
		/* [D217] 消息窗口的排版参数全在 种类別情报 里，此前一个都没读：
		 * 文本エリア=(470,233,0,0) / 字体尺寸=39 / 字体色=白 /
		 * 文字间距=-2 / 行间距=7。缺了它们，正文只剩 16px 默认字，
		 * 再加上底板 CG 没加载，画面上就只有名牌没有消息框。 */
		/* [D498] 文本パーツ：标签文字直接烘焙在 EX 里（asra 设置页面的
		 * 「消息速度」「ＡＵＴＯ模式」等全是这种部件）。此前一个字段
		 * 都不读，画面上只剩装饰框没有字。 */
		/* [D514] EX 的 `文本` leaf 到底有没有值 —— 单选组选项
		 * （SYS_画质设定一般 等）在活动树里是 flat 圆圈、不是 TEXT 件，
		 * 也没被 AIN SetText 过。如果它们的 EX 里其实带 `文本`，
		 * 那就得给 flat/CG 件补一个承载文字的路径（union 不能共存）。
		 * XSYS4_EX_TEXT_DBG=<no>：只为该件打印一次。 */
		if (getenv("XSYS4_EX_TEXT_DBG")) {
			/* 按**名字子串**匹配：本函数运行时 parts_no 还没分配
		 * （`part->parts_no = PE_GetFreeNumber()` 在更下面），
		 * 用号做判据永远不成立。 */
			const char *want = getenv("XSYS4_EX_TEXT_DBG");
			if (want && *want && part->name && strstr(part->name, want)) {
				char *t1 = get_string(type_info, "文本");
				char *t2 = get_string(src, "文本");
				NOTICE("[extx] type=%d name='%s' 文本(ti)='%s' 文本(src)='%s'",
				       part->type, part->name,
				       t1 ? t1 : "(null)", t2 ? t2 : "(null)");
			}
		}
		/* [D514] `文本` 不是 TEXT 件专属：单选组的选项件
		 * （`SYS_画质设定一般`，flat 圆圈）也带 `文本` = "画质优先"。
		 * 先无条件读出来，TEXT 件走 PE_SetText，其余类型走
		 * PE_SetPartsCaption（叠在 flat 上）。 */
		if (!part->part_text) {
			part->part_text = get_string(type_info, "文本");
			if (!part->part_text)
				part->part_text = get_string(src, "文本");
		}
		if (part->type == ACT_COMP_TEXT) {
			part->text_align = 0;
			get_int(type_info, "文本位置", &part->text_align);
			part->font_size = 0;
			get_int(type_info, "字体尺寸", &part->font_size);
			part->font_color[0] = part->font_color[1] = part->font_color[2] = 255;
			get_int_list(type_info, "字体色", part->font_color, 3);
			/* [D517f] 文本パーツ 也有 文字间距/行间距（EX 实测 行间距=4）。
			 * 此前只有消息窗口分支读它们，3D 页的说明文本用默认 16px +
			 * 默认行距，3 行挤在 48px 高里互相压字（d676 截图）。 */
			part->char_space = 0;
			part->line_space = 0;
			get_int(type_info, "文字间距", &part->char_space);
			get_int(type_info, "行间距", &part->line_space);
			ACT_DEBUG("[act] TEXT part '%s' text='%s' align=%d size=%d",
				   part->name,
				   part->part_text ? part->part_text : "(null)",
				   part->text_align, part->font_size);
		}
		if (part->type == ACT_COMP_MESSAGE_WINDOW) {
			memset(part->text_area, 0, sizeof(part->text_area));
			get_int_list(type_info, "文本エリア", part->text_area, 4);
			part->font_size = 0;
			get_int(type_info, "字体尺寸", &part->font_size);
			part->font_color[0] = part->font_color[1] = part->font_color[2] = 255;
			get_int_list(type_info, "字体色", part->font_color, 3);
			part->char_space = 0;
			part->line_space = 0;
			get_int(type_info, "文字间距", &part->char_space);
			get_int(type_info, "行间距", &part->line_space);
		}
		if (!part->cg_name)
			part->cg_name = get_string(src, "ＣＧ名");
		if (!part->cg_name)
			part->cg_name = get_string(type_info, "ＣＧ名");
		/* [D292] 消息窗口的 ＣＧ名 到底读到没有 —— 它是「无底板、无正文」的
		 * 最上游一环，读不到就不用往下查装载了。 */
		if (part->type == ACT_COMP_MESSAGE_WINDOW && getenv("XSYS4_ACT_DBG"))
			NOTICE("[act] MW name='%s' cg=%s src=%s ti=%s",
			       part->name,
			       (part->cg_name && *part->cg_name) ? "Y" : "EMPTY",
			       src ? "y" : "n", type_info ? "y" : "n");
		/* [D119] flat/button parts: フラット名 lives in 一般状态 or 种类別情报 */
		if (!part->flat_name)
			part->flat_name = get_string(src, "フラット名");
		if (!part->flat_name)
			part->flat_name = get_string(type_info, "フラット名");
		if (!get_int(src, "配置", &part->layout_arrangement))
			get_int(type_info, "配置", &part->layout_arrangement);
		int dims[2] = { 0, 0 };
		if (get_int_list(type_info, "尺寸", dims, 2) ||
		    get_int_list(src, "尺寸", dims, 2)) {
			part->w = dims[0];
			part->h = dims[1];
		}
		get_int(src, "变形", &part->deform);
		/* [D238] 数字 / 计量负载都在 一般状態（src）里 */
		if (part->type == ACT_COMP_NUMERAL) {
			get_int_list(src, "幅リスト", part->num_widths, 12);
			get_int(src, "数值", &part->num_value);
			get_int(src, "字间距", &part->num_space);
			get_int(src, "コンマ表示", &part->num_show_comma);
			get_int(src, "桁数", &part->num_length);
		}
		if (part->type == ACT_COMP_HGAUGE || part->type == ACT_COMP_VGAUGE) {
			part->gauge_denom = 100.0f;
			part->gauge_num = 100.0f;
			get_float(src, "分母", &part->gauge_denom);
			get_float(src, "分子", &part->gauge_num);
		}
		/* [D271] 矩形パーツ 的四角 + 模式。先按 src（一般状態）取，
		 * 取不到再退到 type_info —— D238 的数字/计量负载在 src 里，
		 * 但不同活动的写位置不完全一致，两处都试一次代价极小。 */
		/* [D375] `矩形検出`（ACT_COMP_RECT_DETECTION=25）也要读四角。
		 *
		 * 兰斯10 的技能卡命中件（`SmallSkillButton01:0` 的 `Normal/Alter/
		 * Stack/OnceUsed`）就是这一类，运行期实测 `w=1 h=1`；而 AIN
		 * 从不调用 `SetRectangleDetectionSize` 给它们设尺寸
		 * （`XSYS4_FLAT_DBG` 下 `[rect]` 全程 0 条命中它们）。
		 * 原判据只对 `矩形パーツ`(23) 读负载，于是这些件永远停在 1×1 占位。
		 * `XSYS4_EX_RECT_DET_SIZE=0` 回退。 */
		if (part->type == ACT_COMP_RECT ||
		    (act_ex_rect_det_on() && part->type == ACT_COMP_RECT_DETECTION)) {
			static const char *const corner_names[4] = {
				"左上", "右上", "左下", "右下"
			};
			int mode = 0;
			bool got = false;
			for (int i = 0; i < 4; i++) {
				if (!get_float_list(src, corner_names[i], part->rect_pts[i], 2) &&
				    !get_float_list(type_info, corner_names[i], part->rect_pts[i], 2))
					continue;
				part->rect_have[i] = true;
				got = true;
			}
			if (!get_int(src, "矩形模式", &mode))
				get_int(type_info, "矩形模式", &mode);
			part->rect_mode = mode;
			part->has_rect = got;
		}
	} else {
		part->type = ACT_COMP_CG;
	}

	if (!part->cg_name)
		part->cg_name = strdup("");
}

/* XSYS4_USERCOMP_DBG：把用户组件部件的 EX 子树整个转储出来。
 * 兰斯10 的标题菜单（Menu / Chapter1 / Load / Enquite / Yakata / CardList）
 * 完全由用户组件构成（组件类型 15）。Activity@CreateComponent 的
 * switch 里没有 case 15，所以引擎本应实例化这棵子树里
 * 某处指定的一个子活动 —— 而我们现在把类型 15 映射成
 * PARTS_CG，它一个像素也不画，因为从来没设纹理。 */
static void dump_ex_tree(struct ex_tree *t, int depth)
{
	if (!t || depth > 4)
		return;
	for (uint32_t i = 0; i < t->nr_children; i++) {
		struct ex_tree *c = &t->children[i];
		const char *nm = c->name && c->name->text
			? display_sjis0(c->name->text) : "?";
		char namebuf[128];
		snprintf(namebuf, sizeof namebuf, "%s", nm);
		if (!c->is_leaf) {
			NOTICE("[uc] %*s%s/", depth * 2, "", namebuf);
			dump_ex_tree(c, depth + 1);
			continue;
		}
		struct ex_value *v = &c->leaf.value;
		if (v->type == EX_STRING && v->s)
			NOTICE("[uc] %*s%s = '%s'", depth * 2, "", namebuf,
			       display_sjis0(v->s->text));
		else if (v->type == EX_INT)
			NOTICE("[uc] %*s%s = %d", depth * 2, "", namebuf, v->i);
		else if (v->type == EX_LIST) {
			NOTICE("[uc] %*s%s = list[%u]", depth * 2, "", namebuf,
			       v->list ? v->list->nr_items : 0);
			for (uint32_t k = 0; v->list && k < v->list->nr_items && k < 8; k++) {
				struct ex_value *e = &v->list->items[k].value;
				if (e->type == EX_STRING && e->s)
					NOTICE("[uc] %*s  [%u] '%s'", depth * 2, "", k,
					       display_sjis0(e->s->text));
				else if (e->type == EX_INT)
					NOTICE("[uc] %*s  [%u] %d", depth * 2, "", k, e->i);
				else
					NOTICE("[uc] %*s  [%u] <type %d>", depth * 2, "", k,
					       e->type);
			}
		} else
			NOTICE("[uc] %*s%s = <type %d>", depth * 2, "", namebuf,
			       v->type);
	}
}

static struct activity_part *parse_part(struct ex_tree *node)
{
	struct activity_part *part = xcalloc(1, sizeof(struct activity_part));
	part->name = dup_text(node->name);
	parse_part_fields(node, part);
	if (part->type == ACT_COMP_USER_COMPONENT && getenv("XSYS4_USERCOMP_DBG")) {
		static int n;
		if (n++ < 3) {
			NOTICE("[uc] ==== part '%s' (type 15) ====",
			       display_sjis0(part->name));
			dump_ex_tree(node, 0);
		}
	}
	/* [D193] `XSYS4_EX_DUMP=<部件名>`：把该部件的 EX 子树整棵打出来。
	 * 用来确认「尺寸」到底写在哪个键上 —— 選択肢按钮的点击区 `Base`
	 * 在 PACT 里读到的尺寸是 0，而兄弟 `Button` 能读到 200x30。 */
	if (getenv("XSYS4_EX_DUMP") && part->name &&
	    !strcmp(display_sjis0(part->name), getenv("XSYS4_EX_DUMP"))) {
		NOTICE("[ex] ==== part '%s' type=%d ====",
		       display_sjis0(part->name), part->type);
		dump_ex_tree(node, 0);
	}

	struct ex_tree *children = get_child(node, "子パーツ");
	if (children && !children->is_leaf) {
		part->children = xcalloc(children->nr_children, sizeof(struct activity_part *));
		for (uint32_t i = 0; i < children->nr_children; i++) {
			struct ex_tree *c = &children->children[i];
			if (c->is_leaf)
				continue;
			part->children[part->nr_children++] = parse_part(c);
		}
	}
	return part;
}

static void count_parts(struct activity_part *part, int *n)
{
	(*n)++;
	for (int i = 0; i < part->nr_children; i++)
		count_parts(part->children[i], n);
}

static void flatten_parts(struct activity_part *part, struct activity_part **flat, int *n)
{
	flat[(*n)++] = part;
	for (int i = 0; i < part->nr_children; i++)
		flatten_parts(part->children[i], flat, n);
}

/* 给某个状态装载 flat 美术资源。压缩包里没有这个资源时返回 false
 * （这是正常情况：EX 的组名和压缩包布局
 * 按部件种类而不同）。 */
static bool set_part_flat(int parts_no, const char *name, int state)
{
	struct string *s = cstr_to_string(name);
	bool ok = PE_SetPartsFlat(parts_no, s, state);
	free_string(s);
	return ok;
}

/* ------------------------------------------------------------------ */
/* 构建引擎部件树                                                     */
/* ------------------------------------------------------------------ */

/* [D514] 非 TEXT 件的 `文本` 标签开关（默认开）。 */
static bool caption_enabled(void)
{
	static int v = -1;
	if (v < 0) {
		const char *e = getenv("XSYS4_CAPTION");
		v = (e && *e && !atoi(e)) ? 0 : 1;
	}
	return v != 0;
}

static int build_part(struct activity_part *part, int parent_no)
{
	part->parts_no = PE_GetFreeNumber();

	/* [D193] 「按钮没有几何」的最后一环：PACT 里的 4 个 part 到底解析出了
	 * 什么（尺寸/表示領域/flat 名）。此前只能看到最终 parts 层的 0x0，
	 * 分不清是 PACT 没写、我们没读到，还是 AIN 运行时才给。 */
	if (getenv("XSYS4_ACT_PART_DBG"))
		NOTICE("[actpart] no=%d name='%s' type=%d w=%d h=%d sa=(%d,%d,%d,%d) "
		       "flat='%s' cg='%.24s'",
		       part->parts_no,
		       part->name ? display_sjis0(part->name) : "(null)",
		       part->type, part->w, part->h,
		       part->surface_area[0], part->surface_area[1],
		       part->surface_area[2], part->surface_area[3],
		       part->flat_name ? display_sjis0(part->flat_name) : "",
		       part->cg_name ? display_sjis0(part->cg_name) : "");

	PE_SetComponentType(part->parts_no, part->type, 1);
	/* [D214] EX 容器里的子元素全部是**静态绝对坐标**（座標），但
	 * parts_state_reset() 给 PARTS_LAYOUT_BOX 的默认 layout_type 是
	 * VERTICAL；parts_do_layout() 因而把子部件按各自高度依次往下堆，
	 * 整批推到屏幕外。实测回想记录对话框：1920x1080 -> 1080 -> 1306
	 * (+226) -> 2016 (+710) -> 2056 (+40)，与垂直累加逐项吻合，
	 * 两个 TextButton 因此永远点不到，SceneStack@Exit 不触发，剧情卡死。
	 *
	 * 上游默认 VERTICAL 是给「运行时用 SetLayoutBoxLayoutType 显式声明
	 * 自动布局」的容器用的；EX 装载的静态容器不该走自动布局。
	 * 这里只改 EX 装载时的初值，AIN 之后显式调用仍会覆盖，语义不冲突。
	 * D214 A/B 已验证（按钮从屏幕外 y=2016+ 回到 EX 声明的 (736,880)，
	 * 标题菜单渲染与命中区一致），默认生效；XSYS4_EX_LAYOUT_FREE=0 回退。 */
	static int ex_layout_free = -1;
	if (ex_layout_free < 0) {
		const char *e = getenv("XSYS4_EX_LAYOUT_FREE");
		ex_layout_free = e && (!strcmp(e, "0")) ? 0 : 1;
	}
	/* [D250] 真布局盒（EX 明确声明 `レイアウトボックス`）要按 `配置` 重排：
	 * LargeSkillButton / CardApView 这类容器的子部件座标全在 (0,0)，
	 * 不重排就全叠在原点。而 PANEL / FORM / 用户组件 是被
	 * PE_SetComponentType 误映射成布局盒的静态容器（D214 的病灶），
	 * 它们必须保持 FREE。开关隔离，默认沿用 D214 的旧行为。 */
	/* [D515] 改为**默认开**：3D 设置页（Asra\设置／０８／３Ｄ页面）的
	 * 21 个容器 EX `配置` 明确声明了纵向/横向，一律 FREE 会全部叠在 (0,0)。
	 * 只对真 `ACT_COMP_LAYOUT_BOX` 生效，PANEL/FORM/用户组件 仍 FREE
	 * （D214 的回想对话框病灶靠这一点隔离）。XSYS4_EX_LAYOUT_BY_CONFIG=0 回退。 */
	static int ex_layout_by_cfg = -1;
	if (ex_layout_by_cfg < 0) {
		const char *e = getenv("XSYS4_EX_LAYOUT_BY_CONFIG");
		ex_layout_by_cfg = !(e && *e == '0');
	}
	/* [D515] 记下 EX 的布局声明，state 被 AIN 改成 CG 后仍能排版。 */
	if (part->type == ACT_COMP_LAYOUT_BOX) {
		if (getenv("XSYS4_LAYOUT_DBG"))
			NOTICE("[cfg] no=%d name='%s' 配置=%d", part->parts_no,
			       part->name ? display_sjis0(part->name) : "?",
			       part->layout_arrangement);
		parts_remember_layout_box(part->parts_no,
					  part->layout_arrangement >= 0 &&
					  part->layout_arrangement <= 2
					  ? part->layout_arrangement
					  : PARTS_LAYOUT_VERTICAL, 1);
	}
	if (ex_layout_free) {
		if (part->type == ACT_COMP_LAYOUT_BOX && ex_layout_by_cfg &&
		    part->layout_arrangement >= 0 && part->layout_arrangement <= 2)
			PE_SetLayoutBoxLayoutType(part->parts_no, part->layout_arrangement);
		else if (part->type == ACT_COMP_LAYOUT_BOX ||
			 part->type == ACT_COMP_PANEL ||
			 part->type == ACT_COMP_FORM ||
			 part->type == ACT_COMP_USER_COMPONENT)
			PE_SetLayoutBoxLayoutType(part->parts_no, 0 /* PARTS_LAYOUT_FREE */);
	}
	/* 用户组件：先给一个真实尺寸，命中矩形才有面积。
	 * 宽度优先取 参数 里的「幅Ｗ」，其次 表示領域，最后用 196（Rance10 的
	 * 菜单默认宽度）。高度暂用 40；控件层做出来后再由文字度量决定。 */
	if (part->type == ACT_COMP_USER_COMPONENT) {
		int w = uc_parse_width(part->uc_params);
		if (w <= 0 && part->surface_area[2] > 0)
			w = part->surface_area[2];
		if (w <= 0)
			w = 196;
		int h = part->surface_area[3] > 0 ? part->surface_area[3] : 40;
		PE_SetPartsSize(part->parts_no, w, h, 1);
		/* 控件种类与显示文字交给 parts 层，绘制时用它画底板和居中文字。 */
		PE_SetUserComponent(part->parts_no, part->uc_object,
				uc_parse_label(part->uc_params));
		/* 样式供绘制层区分主按钮/小按钮；用内部 key，避免与 AIN 侧
		 * GetUserComponentData("参数") 使用的键冲突。 */
		char *uc_style = uc_parse_style(part->uc_params);
		if (uc_style) {
			/* EX 字符串是 GBK；用 utf2sjis 转换后再比较，别拿源码里的
			 * UTF-8 字面量直接比字节（uc_parse_width 也是这么做的）。
			 * 归一化成 ASCII 标记，绘制层就不必关心编码。 */
			char *title = utf2sjis("标题", strlen("标题"));
			bool round = title && strncmp(uc_style, title, strlen(title)) == 0;
			free(title);
			PE_SetUserComponentData(part->parts_no, "__style",
					round ? "round" : "flat");
			free(uc_style);
		}
		/* [D122] 原始 数据 键值写回引擎。AIN 的 TextButton@Init 靠
		 * GetUserComponentData(no, "参数") 取参数并按逗号 Split；
		 * 缺了这一步 8 个标题菜单按钮全都不会注册点击委托。 */
		for (int i = 0; i + 1 < part->uc_data_n; i += 2)
			PE_SetUserComponentData(part->parts_no,
					part->uc_data[i], part->uc_data[i + 1]);
	} else if (part->type == ACT_COMP_CHECKBOX && !part->flat_name) {
		/* [D517] 3D 页检查件：EX 无 フラット名（EX 里只有 尺寸/字体色/
		 * 按钮の色/选择状态 这类程序化字段，视觉由 checkbox 组件
		 * 代码绘制）。flat 装载必然为空 -> 画面全黑只剩 caption。
		 * 不注册用户组件（会与 AIN 自己的 WrapCheckBox 冲突，
		 * d635 实测页面循环装载）——渲染钩子在 parts.c 的
		 * PE_SetCheckBoxSize（widget render_kind = CHECKBOX）+
		 * render.c 的 flat 空库分支，标签沿用 D514 caption。 */
	} else if (part->w > 0 && part->h > 0) {
		/* [D192] 非用户组件也要用 PACT 里的「尺寸」。FLAT 部件可能是
		 * **空 flat**（`hdr=0x0`、`nr_libraries=0`），`parts_flat_load`
		 * 于是不会设 `common.w/h`，AIN 也不再给 `surface_area` ——
		 * 这时 PACT 的「尺寸」就是唯一几何来源，漏掉它 hitbox 恒为空。
		 * 任务地图点完对象后的「確認／放棄」選択肢按钮正是这种形态：
		 * 流程能建出来、日志一切正常，但既画不出也点不到，回合推不动。 */
		PE_SetPartsSize(part->parts_no, part->w, part->h, 1);
	}
	/* [D271] `矩形パーツ` 的几何在**四角坐标**里，不在「尺寸」里。
	 * 上面的 `part->w > 0 && part->h > 0` 分支对它是永远进不去的 ——
	 * 统计过所有 EX：`尺寸` 字段只出现在 `按钮` 类型上（`低レベルパーツ`
	 * 40 个全都没有），而 ApBase 这类矩形只有 左上/右上/左下/右下。
	 *
	 * 实测后果（`XSYS4_PARTS_DUMP`，d271probe）：`st=11`
	 * (`PARTS_RECT_DETECTION`) 去重 64 个部件里 **60 个是 `0x0`**，
	 * 只有 AIN 运行时用 SetRectangleDetectionSize 显式设过的 4 个非 0。
	 * 而 `render.c` 对 `PARTS_RECT_DETECTION` 本来就直接 break、一个像素
	 * 都不画 —— 尺寸 0 叠上「从不绘制」，结果就是彻底不可见；
	 * 战斗技能按钮的 ApBase 底板全黑正是这条链。
	 *
	 * 本步只补几何，不动绘制（绘制必须单独 A/B：命中盒已在这条路上
	 * 生效，不能因为加了绘制而改掉点击行为）。
	 * `XSYS4_EX_RECT_SIZE=0` 回退做 A/B。 */
	if ((part->type == ACT_COMP_RECT ||
	     (act_ex_rect_det_on() && part->type == ACT_COMP_RECT_DETECTION)) &&
	    part->has_rect) {
		static int ex_rect_size = -1;
		if (ex_rect_size < 0) {
			const char *e = getenv("XSYS4_EX_RECT_SIZE");
			ex_rect_size = e && !strcmp(e, "0") ? 0 : 1;
		}
		if (ex_rect_size) {
			int rw = 0, rh = 0;
			rect_corners_to_size((const float (*)[2])part->rect_pts,
					part->rect_have, &rw, &rh);
			/* 已被别人设过尺寸时不覆盖：AIN 运行时的
			 * SetRectangleDetectionSize 比 EX 静态值更可信。 */
			/* [D366] `1×1` 也要当成"没设过"。
			 *
			 * 兰斯10 的 `矩形パーツ`（技能卡的 `Normal`/`Alter`/`Stack`/
			 * `OnceUsed`）在 EX 里没有显式尺寸时，引擎给的是 **1×1 占位**，
			 * 而不是 0×0。原判据只看 `<= 0`，于是这些件永远拿不到 EX 的
			 * 四角矩形，命中盒只有 1 个像素 —— 画面上的技能卡看得见、
			 * 点不中（`d360`：`900000942` `st=11` `w=1 h=1 hb=(636,730,1,1)`，
			 * 而同组的 `900000938` 底板是 206×147）。
			 * AIN 运行期的 `SetRectangleDetectionSize` 仍优先（它写的是
			 * 真值，一般不是 1×1）。`XSYS4_EX_RECT_MIN1X1=0` 回退。 */
			static int ex_rect_1x1 = -1;
			if (ex_rect_1x1 < 0) {
				const char *e1 = getenv("XSYS4_EX_RECT_MIN1X1");
				ex_rect_1x1 = !(e1 && !strcmp(e1, "0"));
			}
			int cw = PE_GetPartsWidth(part->parts_no, 1);
			int ch = PE_GetPartsHeight(part->parts_no, 1);
			bool cur_empty = cw <= 0 || ch <= 0 ||
					 (ex_rect_1x1 && cw <= 1 && ch <= 1);
			if (rw > 0 && rh > 0 && cur_empty) {
				PE_SetPartsSize(part->parts_no, rw, rh, 1);
				ACT_DEBUG("[act] %s 矩形 模式=%d 四角→%dx%d",
					  part->name, part->rect_mode, rw, rh);
			} else if (getenv("XSYS4_EX_RECT_DBG")) {
				NOTICE("[actrect] %s 模式=%d 四角→%dx%d (保留现有 %dx%d)",
				       part->name, part->rect_mode, rw, rh,
				       PE_GetPartsWidth(part->parts_no, 1),
				       PE_GetPartsHeight(part->parts_no, 1));
			}
		}
	}
	/* [D216] 比率坐标 → 视图像素。此前直接 (int)part->x/y，把 MessageWindow01
	 * 的 (0.0, 1.0) 当成 (0,1) 像素，消息窗口整体留在屏幕左上角外，
	 * 截图只剩名牌、没有消息框与正文（[ADV-TEXT-RENDER]）。
	 * 只在 EX 装载期换算一次；AIN 之后显式 SetPos 仍可覆盖。
	 * XSYS4_EX_RATIO_POS=0 回退做 A/B。 */
	static int ex_ratio_pos = -1;
	if (ex_ratio_pos < 0) {
		const char *e = getenv("XSYS4_EX_RATIO_POS");
		ex_ratio_pos = e && !strcmp(e, "0") ? 0 : 1;
	}
	int px = (int)part->x;
	int py = (int)part->y;
	/* [D292] 比例坐标的第二判据：**对齐模式 + 0..1 的取值**。
	 *
	 * `MessageWindow02` 的 `SYS_消息窗口` 是
	 *     `座標 = (0.000000, 1.000000, 1)`、`原点座標模式 = 7`（底部左对齐）
	 * 但**没有** `座標を率として認識する` 字段。D216 只看那个字段，于是
	 * `pos_as_ratio=false` → `py=(int)1.0=1` → 加上 mode 7 的 `-h`，
	 * 整个消息窗口落在屏幕上方外，正文与立绘都看不见（`s79` 里对话区
	 * 候选全是 `hb=(0,0,1920,1080)` 的全屏遮罩，没有消息框）。
	 *
	 * 判据：对齐模式 ≥4（居中/右/下对齐）且两个坐标都落在 [0,1] 且至少
	 * 一个非零 —— 像素坐标几乎不会把部件放在 (0,1) 这种角落，
	 * 而比例坐标在对齐模式下必然是这种形态。`XSYS4_EX_RATIO_HEURISTIC=0` 回退。 */
	static int ex_ratio_heur = -1;
	if (ex_ratio_heur < 0) {
		const char *e = getenv("XSYS4_EX_RATIO_HEURISTIC");
		ex_ratio_heur = e && !strcmp(e, "0") ? 0 : 1;
	}
	bool ratio = part->pos_as_ratio;
	if (!ratio && ex_ratio_heur && part->origin_mode >= 4 &&
	    part->x >= 0 && part->x <= 1.0f &&
	    part->y >= 0 && part->y <= 1.0f &&
	    (part->x > 0 || part->y > 0))
		ratio = true;
	if (ex_ratio_pos && ratio) {
		px = (int)(part->x * config.view_width);
		py = (int)(part->y * config.view_height);
	}
	PE_SetPos(part->parts_no, px, py);
	PE_SetZ(part->parts_no, part->z);
	PE_SetShow(part->parts_no, part->show);
	PE_SetAlpha(part->parts_no, part->alpha);
	PE_SetAddColor(part->parts_no, part->add_rgb[0], part->add_rgb[1], part->add_rgb[2]);
	PE_SetMultiplyColor(part->parts_no, part->mul_rgb[0], part->mul_rgb[1], part->mul_rgb[2]);
	PE_SetPartsOriginPosMode(part->parts_no, part->origin_mode);
	PE_SetPartsMagX(part->parts_no, part->scale_x);
	PE_SetPartsMagY(part->parts_no, part->scale_y);
	PE_SetPartsRotateX(part->parts_no, part->rot_x);
	PE_SetPartsRotateY(part->parts_no, part->rot_y);
	PE_SetPartsRotateZ(part->parts_no, part->rot_z);
	if (getenv("XSYS4_ACT_CLICKABLE_ALL"))
		part->clickable = 1;
	if (part->clickable)
		PE_SetClickable(part->parts_no, true);
	if (part->pass_cursor)
		PE_SetPassCursor(part->parts_no, true);

	/* [D517d] 复选框初始勾选态（EX `选择状态`）。**不能放在上面的
	 * `!part->flat_name` 分支里**：选项卡件的 フラット名 是空串不是 NULL，
	 * 会走 `part->w>0` 分支，勾选态就永远设不上（d663 实测 ck 全 0，
	 * 19 个页面全被联动判掉，画面看不到任何选项组）。 */
	if (part->type == ACT_COMP_CHECKBOX && part->sel_state)
		PE_CheckBoxChecked(part->parts_no, true);
	if (part->type == ACT_COMP_CHECKBOX && getenv("XSYS4_CB_INIT_DBG"))
		NOTICE("[cb-init] no=%d sel=%d checked_now=%d name='%s'",
		       part->parts_no, part->sel_state,
		       (int)PE_IsCheckBoxChecked(part->parts_no),
		       part->name ? display_sjis0(part->name) : "?");

	if (part->type == ACT_COMP_LAYOUT_BOX) {
		/* 布局盒自己决定子部件的位置 */
	}

	/* [D292] ＣＧ名 不只有 `ＣＧパーツ` 才有。
	 *
	 * `MessageWindow02` 的消息窗口部件类型是 `消息窗口`（ACT_COMP_MESSAGE_WINDOW），
	 * 但它的 `种类別情报` 里同样有 `ＣＧ名 = シス／ＡＤＶ／事件／提示`。
	 * 这里原来只放行 `ACT_COMP_CG`，于是消息窗口**从未装载底板图**：
	 * 尺寸保持 0 → `parts_common_recalculate_hitbox()` 退化到视图尺寸
	 * （`s79/s80` 候选表里 `hb=(0,0,1920,1080)` 就是它），正文与立绘
	 * 自然都看不见。面板/表单也是同类（EX 里同样带 ＣＧ名）。
	 * 按钮不算：它带的是 `フラット名`，走 `set_part_flat()`。 */
	/* [D498] 文本パーツ：把 EX 烘焙的标签文字设进去。PE_SetText 会把
	 * state 转成 PARTS_TEXT，文字渲染走 parts_text 管线（字号先用
	 * 引擎默认；EX 的 字体尺寸/字体色 字段已解析，等 parts 层支持
	 * 逐部件字号后再接上）。 */
	if (part->type == ACT_COMP_TEXT && part->part_text && *part->part_text) {
		struct string *txt = cstr_to_string(part->part_text);
		bool ok = PE_SetText(part->parts_no, txt, 1);
		free_string(txt);
		/* [D517f] 字号/颜色/间距沿用 EX 声明。**必须放在 PE_SetText 之后**
		 * （它会把 state 重置成 PARTS_TEXT）。此前只有消息窗口分支设这些，
		 * 文本パーツ 一律默认 16px：3D 页的说明文本（EX 字体尺寸=11、
		 * 行间距=4、框高 48）被画成 3 行 × 20px 互相压字（d676 截图）。 */
		if (part->font_size > 0)
			PE_SetPartsFontSize(part->parts_no, part->font_size, 1);
		PE_SetPartsFontColor(part->parts_no, part->font_color[0],
				part->font_color[1], part->font_color[2], 1);
		PE_SetTextCharSpace(part->parts_no, part->char_space, 1);
		PE_SetTextLineSpace(part->parts_no, part->line_space, 1);
		ACT_DEBUG("[act] %s parts_no=%d text set=%d align=%d size=%d",
			  part->name, part->parts_no, ok, part->text_align,
			  part->font_size);
	} else if (part->type != ACT_COMP_TEXT && part->part_text && *part->part_text
		   && caption_enabled()) {
		/* [D514] 非 TEXT 件的 `文本` 当标签画（flat 圆圈右侧）。
		 * 默认开；XSYS4_CAPTION=0 可整体关掉（若某界面出现多余文字）。 */
		PE_SetPartsCaption(part->parts_no, part->part_text);
		ACT_DEBUG("[act] %s parts_no=%d caption='%s'",
			  part->name, part->parts_no, part->part_text);
	}

	if (part->cg_name && *part->cg_name &&
	    (part->type == ACT_COMP_CG ||
	     part->type == ACT_COMP_MESSAGE_WINDOW ||
	     part->type == ACT_COMP_PANEL ||
	     part->type == ACT_COMP_FORM)) {
		struct string *cg = cstr_to_string(part->cg_name);
		bool ok = PE_SetPartsCG(part->parts_no, cg, part->deform, 1);
		ACT_DEBUG("[act] %s parts_no=%d pos=(%d,%d,%d) cg='%s' set=%d size=%dx%d",
			  part->name, part->parts_no, (int)part->x, (int)part->y, part->z,
			  part->cg_name, ok,
			  PE_GetPartsWidth(part->parts_no, 1),
			  PE_GetPartsHeight(part->parts_no, 1));
		free_string(cg);
	} else if (part->type == ACT_COMP_NUMERAL
		   && part->cg_name && *part->cg_name) {
		/* [D238] 数字：EX 给的是 COMBINED 条带（幅リスト 12 个宽度）。
		 * 不能走 PE_SetNumeralCG —— 那是 SEPARATE 布局，会按名字去找
		 * 10 张独立数字 CG，一张都找不到。 */
		/* 顺序要紧：parts_set_dims() 在 SetNumeralLinked... 里按 **当时**
		 * 的 origin_mode 算 origin_offset。Hp 是 mode=9（右下），
		 * 先装 CG 再改模式 → 数字被推到屏幕外。 */
		PE_SetPartsOriginPosMode(part->parts_no, part->origin_mode);
		struct string *cg = cstr_to_string(part->cg_name);
		bool ok = PE_SetNumeralLinkedCGNumberWidthWidthList(part->parts_no,
				cg,
				part->num_widths[0], part->num_widths[1],
				part->num_widths[2], part->num_widths[3],
				part->num_widths[4], part->num_widths[5],
				part->num_widths[6], part->num_widths[7],
				part->num_widths[8], part->num_widths[9],
				part->num_widths[10], part->num_widths[11],
				1);
		free_string(cg);
		PE_SetNumeralNumber(part->parts_no, part->num_value, 1);
		PE_SetNumeralSpace(part->parts_no, part->num_space, 1);
		PE_SetNumeralShowComma(part->parts_no, part->num_show_comma, 1);
		if (part->num_length > 0)
			PE_SetNumeralLength(part->parts_no, part->num_length, 1);
		ACT_DEBUG("[act] %s parts_no=%d numeral cg='%s' font=%d value=%d size=%dx%d",
			  part->name, part->parts_no, part->cg_name, ok,
			  part->num_value,
			  PE_GetPartsWidth(part->parts_no, 1),
			  PE_GetPartsHeight(part->parts_no, 1));
	} else if ((part->type == ACT_COMP_HGAUGE
		    || part->type == ACT_COMP_VGAUGE)
		   && part->cg_name && *part->cg_name) {
		/* [D238] 计量：CG 是整条底的条带，rate 决定露出多少像素。 */
		/* 同 numeral：先定原点模式，否则 parts_set_dims() 用 mode=0
		 * 算的 offset 会让 Gauge(mode=1) 偏到别处。 */
		PE_SetPartsOriginPosMode(part->parts_no, part->origin_mode);
		struct string *cg = cstr_to_string(part->cg_name);
		bool ok;
		if (part->type == ACT_COMP_HGAUGE) {
			ok = PE_SetHGaugeCG(part->parts_no, cg, 1);
			PE_SetHGaugeRate(part->parts_no, part->gauge_num,
					 part->gauge_denom, 1);
		} else {
			ok = PE_SetVGaugeCG(part->parts_no, cg, 1);
			PE_SetVGaugeRate(part->parts_no, part->gauge_num,
					 part->gauge_denom, 1);
		}
		free_string(cg);
		ACT_DEBUG("[act] %s parts_no=%d gauge cg='%s' set=%d %.1f/%.1f",
			  part->name, part->parts_no, part->cg_name, ok,
			  part->gauge_num, part->gauge_denom);
	} else {
		ACT_DEBUG("[act] %s parts_no=%d type=%d (no cg)", part->name,
			  part->parts_no, part->type);
	}

	/* [D217] 消息窗口：EX 声明的 39px 字体 + 底板ＣＧ。
	 * parts_state 是 union，CG 与 TEXT 不能共存 —— PE_SetText() 会把
	 * state 重置成 PARTS_TEXT，任何先设在同部件上的 CG 都会被抹掉。
	 * 所以底板用一个独立 CG 部件承载（AIN 按名字找部件，不按号找它），
	 * 文字部件保留原号继续接 SetMessageWindowText。
	 * XSYS4_MSGWIN_FULL=0 回退做 A/B。 */
	static int msgwin_full = -1;
	if (msgwin_full < 0) {
		const char *e = getenv("XSYS4_MSGWIN_FULL");
		msgwin_full = e && !strcmp(e, "0") ? 0 : 1;
	}
	if (msgwin_full && part->type == ACT_COMP_MESSAGE_WINDOW) {
		if (part->font_size > 0)
			PE_SetPartsFontSize(part->parts_no, part->font_size, 1);
		PE_SetPartsFontColor(part->parts_no, part->font_color[0],
				part->font_color[1], part->font_color[2], 1);
		PE_SetTextCharSpace(part->parts_no, part->char_space, 1);
		PE_SetTextLineSpace(part->parts_no, part->line_space, 1);

		if (part->cg_name && *part->cg_name) {
			int frame_no = PE_GetFreeNumber();
			/* 顺序：先定原点模式/位置，再 SetPartsCG —— 后者的
			 * parts_set_dims() 会按当时的 origin_mode 算 origin_offset，
			 * 顺序反了 CG 就会落到 (0,1080) 以下的屏幕外。 */
			PE_SetPartsOriginPosMode(frame_no, part->origin_mode);
			PE_SetPos(frame_no, px, py);
			/* [D293] 底板 frame 的 z 不能直接用 EX 的 `part->z`(=1)。
			 * AIN 之后会给**文本件**调 `SetZ(342, 5000)` 把消息窗口
			 * 提到最上层，但 frame 是我们新分配的号，AIN 根本不知道它，
			 * 于是它永远停在 z=1 —— 而地图/UI 的 z 在 1500~4349，
			 * 整块底板被压在最底下（`s83` dump：`no=900000343 z=1`，
			 * 尺寸/纹理/位置却全对）。这里给一个略低于文字件的基线，
			 * 保证它盖住地图又不会压住正文。`XSYS4_MSGWIN_FRAME_Z` 可调。 */
			static int frame_zbase = -1;
			if (frame_zbase < 0) {
				const char *e = getenv("XSYS4_MSGWIN_FRAME_Z");
				frame_zbase = e && *e ? atoi(e) : 4900;
			}
			PE_SetZ(frame_no, frame_zbase + part->z);
			struct string *cg = cstr_to_string(part->cg_name);
			bool ok = PE_SetPartsCG(frame_no, cg, part->deform, 1);
			if (ok) {
				PE_SetShow(frame_no, part->show);
				PE_SetAlpha(frame_no, part->alpha);
				PE_SetPassCursor(frame_no, true);
			}
			ACT_DEBUG("[act] msgwin frame no=%d cg='%s' set=%d",
				  frame_no, part->cg_name, ok);
			/* [D294] `文本エリア = (x, y)` 是相对**消息窗口左上角**的，
			 * 而文本件的 pos 落在底边（y=1080）且自身高度为 0
			 * （`parts_render_text` 只拿 pos+origin_offset 起画，
			 * 所以正文整个跑到屏幕外）。这里换算成相对文本件的偏移：
			 * area_y = y - 底板高度。 */
			if (ok && part->text_area[1] > 0) {
				int fh = PE_GetPartsHeight(frame_no, 1);
				/* [D306] 少补偿一个字号：文本件的 `origin_offset.y`
				 * 是 `-字形高`（mode 7 底对齐，`s131` 实测 `offy=-39`），
				 * 而 `文本エリア.y` 给的是「正文第一行**顶边**距窗口顶」。
				 * 不补偿时 y = 1080 - 39 - 184 = **857**，比应有的
				 * `panel_top(663) + 233 = 896` 高出整整一个字号，
				 * 于是正文上半 29px 被名牌（底边 886）盖住，屏幕上只剩
				 * 最下面约 10 行墨迹（`s126` 的「半个字」）。
				 * `XSYS4_MSGWIN_TEXT_BASELINE=0` 回退到旧行为。 */
				static int baseline_fix = -1;
				if (baseline_fix < 0) {
					const char *e = getenv("XSYS4_MSGWIN_TEXT_BASELINE");
					baseline_fix = e && !strcmp(e, "0") ? 0 : 1;
				}
				int ay = part->text_area[1] - fh;
				if (baseline_fix && part->font_size > 0)
					ay += part->font_size;
				PE_SetTextArea(part->parts_no, part->text_area[0], ay);
			}
			free_string(cg);
		}
	}

	/* [D119] flat/按钮部件从 FLAT 压缩包装载美术资源。
	 * PE_SetPartsFlat() 会设部件尺寸（parts_set_dims），
	 * 从而建出一个非空的命中盒；
	 * 这正是 OK/Cancel 按钮能被鼠标点到的原因。 */
	/* flat 美术资源。控件类部件（button、checkbox…）存的是「flat 组」名
	 * group* name (e.g. "共通素材\シス／按钮／幅４７６"); the archive holds
	 * 每个视觉状态一个资源，用全角斜杠连接：
	 * "<group>／一般", "<group>／オン", "<group>／DOWN", ... Plain picture
	 * 部件存的则是完整名，所以先按原名试，
	 * then the 一般 variant. */
	if (part->flat_name && *part->flat_name) {
		char *variant = NULL;
		bool flat_ok = set_part_flat(part->parts_no, part->flat_name, 1);
		if (!flat_ok) {
			/* 分隔符和状态名都用的是
			 * 游戏自己的编码。 */
			char *sep = utf2sjis("／", strlen("／"));
			char *suffix = utf2sjis("一般", strlen("一般"));
			if (sep && suffix) {
				variant = xmalloc(strlen(part->flat_name)
						  + strlen(sep) + strlen(suffix) + 1);
				sprintf(variant, "%s%s%s", part->flat_name, sep, suffix);
				flat_ok = set_part_flat(part->parts_no, variant, 1);
			}
			free(sep);
			free(suffix);
		}
		/* [D504] HSCROLLBAR 的 flat 基准名比普通按钮多一层 `栏`：
		 * EX 写 `设置\シス／设置／计量`，档案里是
		 * `设置\シス／设置／计量／栏／{一般,オン,DOWN}.flat` 与
		 * `…／计量／背景.flat`（afa_ls 实测 8 条）。
		 * 只试「原名 / ＋一般」会全部落空 ⇒ w=0 h=0、无纹理，
		 * 把手永远看不见也点不中。这里补第三种变体：`<base>／栏／一般`。 */
		if (!flat_ok && part->type == ACT_COMP_HSCROLLBAR) {
			char *sep = utf2sjis("／", strlen("／"));
			char *bar = utf2sjis("栏", strlen("栏"));
			char *suffix = utf2sjis("一般", strlen("一般"));
			if (sep && bar && suffix) {
				free(variant);
				variant = xmalloc(strlen(part->flat_name)
						  + strlen(sep) + strlen(bar)
						  + strlen(sep) + strlen(suffix) + 1);
				sprintf(variant, "%s%s%s%s%s", part->flat_name,
					sep, bar, sep, suffix);
				flat_ok = set_part_flat(part->parts_no, variant, 1);
				ACT_DEBUG("[act] %s HSCROLLBAR 变体 '%s' set=%d",
					  part->name, variant, flat_ok);
			}
			free(sep);
			free(bar);
			free(suffix);
		}
		/* [D505] 三态美术：flat 一般 装进 DEFAULT 后，把 オン/DOWN 分别
		 * 装进 HOVERED(2)/CLICKED(3)。PE_SetPartsFlat 的 state 是 1-based
		 * （内部 --state），parts 层的 hover/click 检测会自动切状态，
		 * 不需要我们在 input 里手动换图。 */
		if (flat_ok && part->type == ACT_COMP_HSCROLLBAR) {
			static const struct { const char *zh; int state; } tri[] = {
				{ "オン", 2 },   /* HOVERED */
				{ "DOWN", 3 },   /* CLICKED */
			};
			char *sep = utf2sjis("／", strlen("／"));
			char *bar = utf2sjis("栏", strlen("栏"));
			if (sep && bar) {
				for (int t = 0; t < 2; t++) {
					char *st = utf2sjis(tri[t].zh, strlen(tri[t].zh));
					if (!st)
						continue;
					char *v = xmalloc(strlen(part->flat_name)
							  + strlen(sep) + strlen(bar)
							  + strlen(sep) + strlen(st) + 1);
					sprintf(v, "%s%s%s%s%s", part->flat_name,
						sep, bar, sep, st);
					bool ok = set_part_flat(part->parts_no, v, tri[t].state);
					ACT_DEBUG("[act] %s HSCROLLBAR 状态%d '%s' set=%d",
						  part->name, tri[t].state, v, ok);
					free(v);
					free(st);
				}
			}
			free(sep);
			free(bar);
		}
		/* [D498] flat 两种变体都失败、但部件带 ＣＧ名 时回退 CG 装载。
		 * 实例：asra 设置页面的滑块 `SYS_信息速度滑块`（type=3），EX 的
		 * フラット名 是目录名 `设置\シス／设置／计量`（档案里只有
		 * `计量／栏／{DOWN,オン,一般,背景}.flat` 四个状态件），而它的
		 * ＣＧ名 `シス／设置／计量` 指向 **AJP 动画**（Rance10CG2.
		 * `シス／设置／计量.ajp`）——同页的「消息速度下地」就是走 CG
		 * 分支用同一个 ajp 装载成功的（928x164）。 */
		if (!flat_ok && part->cg_name && *part->cg_name) {
			struct string *cg = cstr_to_string(part->cg_name);
			flat_ok = PE_SetPartsCG(part->parts_no, cg, part->deform, 1);
			free_string(cg);
			ACT_DEBUG("[act] %s flat 失败回退 CG '%s' set=%d",
				   part->name, part->cg_name, flat_ok);
		}
		ACT_DEBUG("[act] %s parts_no=%d pos=(%d,%d,%d) gpos=(%d,%d) show=%d alpha=%d "
			  "mag=(%.2f,%.2f) click=%d flat='%s' set=%d size=%dx%d",
			  part->name, part->parts_no, (int)part->x, (int)part->y,
			  part->z,
			  PE_GetPartsGlobalX(part->parts_no),
			  PE_GetPartsGlobalY(part->parts_no),
			  part->show, part->alpha, part->scale_x,
			  part->scale_y, part->clickable, part->flat_name,
			  flat_ok, PE_GetPartsWidth(part->parts_no, 1),
			  PE_GetPartsHeight(part->parts_no, 1));
		free(variant);
	}

	if (part->uc_object && getenv("XSYS4_USERCOMP_DBG")) {
		char obj[128], par[256];
		snprintf(obj, sizeof obj, "%s", display_sjis0(part->uc_object));
		snprintf(par, sizeof par, "%s",
			 part->uc_params ? display_sjis0(part->uc_params) : "");
		int px = parent_no >= 0 ? PE_GetPartsX(parent_no) : 0;
		int py = parent_no >= 0 ? PE_GetPartsY(parent_no) : 0;
		NOTICE("[act] %s 用户组件 no=%d '%s' params='%s' rel=(%d,%d) "
		       "parent=(%d,%d) abs=(%d,%d) size=%dx%d show=%d alpha=%d di=%d",
		       part->name, part->parts_no, obj, par,
		       PE_GetPartsX(part->parts_no), PE_GetPartsY(part->parts_no),
		       px, py,
		       px + PE_GetPartsX(part->parts_no),
		       py + PE_GetPartsY(part->parts_no),
		       PE_GetPartsWidth(part->parts_no, 1),
		       PE_GetPartsHeight(part->parts_no, 1),
		       part->show, part->alpha, PE_GetDelegateIndex(part->parts_no));
	}

	if (parent_no >= 0)
		PE_SetParentPartsNumber(part->parts_no, parent_no);

	/* [D263 探针] 把「部件号 → activity 部件名」对号。
	 * 起因：战斗里技能面板 `900000340` 的父链是 `340 → 392`，与战斗 UI 层
	 * `704` 不是同一条链，但 `[act]` 日志只在 CG 类型上打 parts_no，
	 * 无法判断 340 是「当前战斗面板」还是「旧场景残留」。
	 * 判据：`XSYS4_ACT_BUILD_DBG=1` 时能直接看到 `no=900000340 name='...'`。 */
	if (getenv("XSYS4_ACT_BUILD_DBG")) {
		static int bn;
		if (bn++ < 3000)
			NOTICE("[actbuild] no=%d name='%s' parent=%d type=%d",
			       part->parts_no, part->name ? part->name : "(null)",
			       parent_no, (int)part->type);
	}

	for (int i = 0; i < part->nr_children; i++)
		build_part(part->children[i], part->parts_no);

	/* [D387] `矩形検出`（`ACT_COMP_RECT_DETECTION`）在 EX 里常常既没有
	 * 四角负载（`has_rect == false`），`尺寸` 又只是 `1×1` 占位 ——
	 * AIN 运行期也从不给它 `SetRectangleDetectionSize`，于是命中盒只有
	 * 一个像素：卡片看得见、点不中（`SmallSkillButton01:0` 的
	 * `Normal/Alter/Stack/OnceUsed`，D372–D375）。
	 *
	 * 语义上 `矩形検出` 就是「这块区域的点击检测」，EX 不给几何时只能是
	 * 父件的可视区域。所以在**子件都建完之后**（父件尺寸此时已知）按父件
	 * 铺满。判据：技能卡命中件的 `w/h` 从 `1×1` 变成父件尺寸（底板
	 * `900000938` 是 `206×147`），点技能卡能走到 `OnClickSkill`。
	 * `XSYS4_EX_RECT_FILL_PARENT=0` 回退做 A/B。 */
	if (part->type == ACT_COMP_RECT_DETECTION && parent_no >= 0 &&
	    act_ex_rect_fill_parent_on()) {
		int w = PE_GetPartsWidth(part->parts_no, 1);
		int h = PE_GetPartsHeight(part->parts_no, 1);
		if (w <= 1 && h <= 1) {
			int pw = PE_GetPartsWidth(parent_no, 1);
			int ph = PE_GetPartsHeight(parent_no, 1);
			if (pw > 1 && ph > 1) {
				PE_SetPartsSize(part->parts_no, pw, ph, 1);
				if (getenv("XSYS4_EX_RECT_DBG"))
					NOTICE("[actrect] %s no=%d 1x1 -> 父件 %dx%d",
					       part->name, part->parts_no, pw, ph);
			}
		}
	}

	return part->parts_no;
}

/* ------------------------------------------------------------------ */
/* 对外 API                                                           */
/* ------------------------------------------------------------------ */

static struct activity *find_activity(const char *name)
{
	for (struct activity *a = activity_list; a; a = a->next) {
		if (a->name && name && !strcmp(a->name, name))
			return a;
	}
	return NULL;
}

bool activity_exists(const char *name)
{
	return find_activity(name) != NULL;
}

struct activity *activity_get(const char *name)
{
	return find_activity(name);
}

/* [D498] Asra 设置页面按 `.pact` 路径查询（AIN：`Asra\设置／０１／消息速度页面.pact`），
 * 档案条目是 `.pactex`。asset 层的 basename 匹配同时吸收这个扩展名差异与路径
 * 分隔符差异（AIN 用 `/`，档案用 `\`），装载直接走 get_by_name 即可。
 * 存在性检查唯一的坑是**实例号**：`….pact:N` 的 basename 与页面本体相同，
 * 会全部命中 —— AIN 的 GetFreeName 枚举空闲实例号会死循环（实测 1267 万行
 * 日志风暴、画面黑屏）。所以这里拒绝带 `:` 的名字；`IsExistActivity` 的
 * 文件兜底只在名字不含 `:` 时才调到这里。 */
bool activity_is_exist_file(const char *file)
{
	if (!file || !*file || strchr(file, ':'))
		return false;
	int id = -1;
	return asset_exists_by_name(ASSET_PACT, file, &id);
}

static void free_part(struct activity_part *part)
{
	for (int i = 0; i < part->nr_children; i++)
		free_part(part->children[i]);
	free(part->children);
	free(part->name);
	free(part->cg_name);
	free(part->part_text);
	free(part->flat_name);
	free(part->uc_object);
	free(part->uc_params);
	for (int i = 0; i < part->uc_data_n; i++)
		free(part->uc_data[i]);
	free(part->uc_data);
	free(part);
}

/* [D517n] 配置页判定：活动名以 "Asra\设置／０" 开头（GBK 字节，实测
 * 装载日志 `'Asra\设置／０１／消息速度页面.pact'` =
 * `41 73 72 61 5c c9 e8 d6 c3 a3 af a3 b0 a3 b1`）。8 个页签（０１…
 * ０８/３Ｄ）是 8 个独立 pact，前缀共用 `Asra\设置／０`——注意**不能
 * 写成 `／０８`**：那只能匹配 3D 页，０１-０７（…a3 b0 a3 b1..a3 b7）
 * 全部落空（首版实测犯过）。背景的实时 3D 场景全页共用，只给 3D 页
 * 画预览的话，其余页底下就是标题活动，标题画面会透出来（用户实测
 * 「菜单的背景不正确」）。 */
static bool activity_is_config_page(const struct activity *act)
{
	static const char prefix[] = "Asra\\"
		"\xc9\xe8"	/* 设 */
		"\xd6\xc3"	/* 置 */
		"\xa3\xaf"	/* ／ */
		"\xa3\xb0";	/* ０ */
	return act && act->name &&
	       strncmp(act->name, prefix, sizeof(prefix) - 1) == 0;
}

void activity_free(struct activity *act)
{
	if (!act)
		return;
	if (activity_is_config_page(act)) {
		/* 切页时新页先装载（head 插入）、旧页后释放——此时 act 已
		 * 移出链表，栈里还有别的配置页就不关预览，避免闪烁。 */
		bool still_open = false;
		for (const struct activity *a = activity_list; a; a = a->next) {
			if (activity_is_config_page(a)) {
				still_open = true;
				break;
			}
		}
		if (!still_open)
			preview3d_set_active(false);
	}
	if (act->root)
		free_part(act->root);
	free(act->flat);
	free(act->name);
	free(act->background);
	free(act);
}

/* [D270] `ReleaseActivity` 的 EraseNumberList 出参。
 *
 * AIN 的实现（`parts::detail::EraseLayer` 8296 与 `parts::detail::Release`
 * 391）拿到这个列表后交给 `CPartsMessageManager@ReleaseFunctionSetList`
 * (8007)，后者逐个调 `ReleaseFunctionSetWithoutGC` (8005) 把该部件的
 * CLICK / MOUSE_ON / KEY_DOWN 等**委托下标**从消息分发表里摘掉。
 *
 * 原来这里 `(void)erase_number_list` —— 列表恒空，于是 activity 释放后
 * 委托仍然登记在分发表里，指向已经销毁的部件号。同一个部件号被复用时
 * 就会命中**上一次活动的委托**（打空炮或调到错误的处理函数）。
 *
 * 写进去的是 `delegate_index` 而不是部件号：与 `PE_RemoveController()`
 * 用的是同一个字段、同一口径（那里早已这么写），两侧必须一致。 */
static void collect_activity_delegate_indices(struct activity *act,
		struct page **erase_number_list)
{
	int n = 0;
	for (int i = 0; i < act->nr_parts; i++) {
		int di = PE_GetDelegateIndex(act->flat[i]->parts_no);
		if (di < 0)
			continue;
		*erase_number_list = array_pushback(*erase_number_list,
				(union vm_value){.i = di}, AIN_ARRAY_INT, -1);
		n++;
	}
	/* [D270] 判据：这里必须是非 0。为 0 说明出参丢与否根本没区别，
	 * 那就不算修好了语义 —— 别拿“编译通过”当证据。 */
	if (getenv("XSYS4_ACT_RELEASE_DGLIST_DBG"))
		NOTICE("[actrel] '%s' parts=%d → delegate indices=%d",
		       act->name ? act->name : "(null)", act->nr_parts, n);
}

/* 默认生效（这是语义补齐，不是实验）；`XSYS4_ACT_RELEASE_DGLIST=0`
 * 回退到旧的「丢弃出参」行为做 A/B。 */
static bool act_release_dglist_enabled(void)
{
	static int on = -1;
	if (on < 0) {
		const char *e = getenv("XSYS4_ACT_RELEASE_DGLIST");
		on = e && !strcmp(e, "0") ? 0 : 1;
	}
	return on != 0;
}

bool activity_release_ex(const char *name, struct page **erase_number_list)
{
	if (getenv("XSYS4_ACT_DBG"))
		NOTICE("[act] release '%s'", name ? name : "(null)");
	struct activity **p = &activity_list;
	while (*p) {
		if ((*p)->name && name && !strcmp((*p)->name, name)) {
			struct activity *dead = *p;
			*p = dead->next;
			if (activity_open_cnt > 0)
				activity_open_cnt--;
			if (getenv("XSYS4_ACT_STEP"))
				NOTICE("[actstep] release '%s' open=%d",
				       dead->name ? dead->name : "?", activity_open_cnt);
			if (erase_number_list && act_release_dglist_enabled())
				collect_activity_delegate_indices(dead, erase_number_list);
			for (int i = 0; i < dead->nr_parts; i++)
				PE_ReleaseParts(dead->flat[i]->parts_no);
			activity_free(dead);
			return true;
		}
		p = &(*p)->next;
	}
	return false;
}

bool activity_release(const char *name)
{
	return activity_release_ex(name, NULL);
}

void activity_release_all(void)
{
	while (activity_list) {
		struct activity *dead = activity_list;
		activity_list = dead->next;
		if (activity_open_cnt > 0)
			activity_open_cnt--;
		for (int i = 0; i < dead->nr_parts; i++)
			PE_ReleaseParts(dead->flat[i]->parts_no);
		activity_free(dead);
	}
}

int activity_nr_parts(struct activity *act)
{
	return act ? act->nr_parts : 0;
}

struct activity_part *activity_find_parts(struct activity *act, const char *parts_name)
{
	if (!act || !parts_name)
		return NULL;
	for (int i = 0; i < act->nr_parts; i++) {
		if (act->flat[i]->name && !strcmp(act->flat[i]->name, parts_name))
			return act->flat[i];
	}
	return NULL;
}

int activity_find_parts_no(struct activity *act, const char *parts_name)
{
	struct activity_part *p = activity_find_parts(act, parts_name);
	return p ? p->parts_no : 0;
}

/* [D545] 诊断：列出**所有活动**里名字含子串的部件（活动名/部件名/部件号）。
 * 用途：AIN 侧按名字找按钮（`Activity@GetButton("ButtonTurnEnd")`），
 * 而我们的 PARTS_DUMP 只打活动名不打部件名——没有这个探针就只能靠猜号。
 * `XSYS4_FIND_PART=<substr>`，由 PE_DebugDumpParts 调用（只打一次）。 */
void activity_debug_find_part(const char *sub)
{
	if (!sub || !*sub)
		return;
	int hits = 0;
	for (struct activity *a = activity_list; a; a = a->next) {
		for (int i = 0; i < a->nr_parts; i++) {
			struct activity_part *ap = a->flat[i];
			if (!ap || !ap->name)
				continue;
			if (!strstr(ap->name, sub))
				continue;
			NOTICE("[findpart] act='%s' name='%s' no=%d",
			       a->name ? a->name : "?", ap->name, ap->parts_no);
			if (++hits >= 60) {
				NOTICE("[findpart] ... 截断（>=60 条）");
				return;
			}
		}
	}
	NOTICE("[findpart] '%s' 命中 %d 个", sub, hits);
}

/* [D517j] 把 3D 页当前的勾选态翻译成预览参数。
 *
 * **为什么用 GBK 字节字面量**：EX 里的部件名是 GBK（汉化版重打包），
 * 实测 `SYS_绘制模式` = `53 59 53 5f bb e6 d6 c6 c4 a3 ca bd`；源码里的
 * UTF-8 中文字面量与它 strcmp 永远不等。每个宏的注释写明对应中文名。
 * 这样就把 D517j 早期「按部件号 900000194/195/196 硬取」的脆弱绑定
 * 换成了语义绑定 —— 部件号会随活动装载顺序变化，名字不会。 */
#define PQ_NAME_DRAW_SOFT	/* "SYS_绘制模式_检查１" 软件 */	\
	"SYS_\xbb\xe6\xd6\xc6\xc4\xa3\xca\xbd_\xbc\xec\xb2\xe9\xa3\xb1"
#define PQ_NAME_DRAW_FLAT	/* "SYS_绘制模式_检查２" 着色無 */	\
	"SYS_\xbb\xe6\xd6\xc6\xc4\xa3\xca\xbd_\xbc\xec\xb2\xe9\xa3\xb2"
#define PQ_NAME_FOG_ON		/* "SYS_雾化_检查２" 存在 */	\
	"SYS_\xce\xed\xbb\xaf_\xbc\xec\xb2\xe9\xa3\xb2"
#define PQ_NAME_DOF_ON		/* "SYS_景深_检查２" 存在 */	\
	"SYS_\xbe\xb0\xc9\xee_\xbc\xec\xb2\xe9\xa3\xb2"
#define PQ_NAME_EDGE_ON		/* "SYS_边缘_检查２" 绘制 */	\
	"SYS_\xb1\xdf\xd4\xb5_\xbc\xec\xb2\xe9\xa3\xb2"
#define PQ_NAME_GLOW_ON		/* "SYS_光晕_检查２" 存在 */	\
	"SYS_\xb9\xe2\xd4\xce_\xbc\xec\xb2\xe9\xa3\xb2"

static int pq_find_gbk(struct activity *act, const char *gbk_name)
{
	if (!act || !gbk_name)
		return 0;
	for (int i = 0; i < act->nr_parts; i++) {
		struct activity_part *p = act->flat[i];
		if (p->name && !strcmp(p->name, gbk_name))
			return p->parts_no;
	}
	return 0;
}

static bool pq_on(struct activity *act, const char *gbk_name)
{
	int no = pq_find_gbk(act, gbk_name);
	return no > 0 && PE_IsCheckBoxChecked(no);
}

void preview3d_sync_params(void)
{
	if (!preview3d_active())
		return;

	/* 从活动栈里找 3D 页（按名字，不假设它就是栈顶）。 */
	struct activity *act = NULL;
	for (struct activity *a = activity_list; a; a = a->next) {
		if (pq_find_gbk(a, PQ_NAME_DRAW_SOFT) ||
		    pq_find_gbk(a, PQ_NAME_DRAW_FLAT)) {
			act = a;
			break;
		}
	}
	if (!act)
		return; /* 名字还没回来（装载中），保持上一帧的参数 */

	int mode = 2; /* 默认「着色存在」，与 EX 初始 选择状态 一致 */
	if (pq_on(act, PQ_NAME_DRAW_SOFT))
		mode = 0;
	else if (pq_on(act, PQ_NAME_DRAW_FLAT))
		mode = 1;

	int fog = pq_on(act, PQ_NAME_FOG_ON);
	int edge = pq_on(act, PQ_NAME_EDGE_ON);
	int dof = pq_on(act, PQ_NAME_DOF_ON);
	int glow = pq_on(act, PQ_NAME_GLOW_ON);
	preview3d_set_param(mode, fog, edge, dof, glow);

	/* [取证] 按名查找是否命中（部件号 0 = 名字没对上，参数会退回默认）。 */
	if (getenv("XSYS4_PQ_DBG")) {
		static int n;
		if (n++ % 120 == 0)
			NOTICE("[pq] mode=%d fog=%d edge=%d dof=%d glow=%d "
			       "(no %d/%d/%d/%d/%d)",
			       mode, fog, edge, dof, glow,
			       pq_find_gbk(act, PQ_NAME_DRAW_SOFT),
			       pq_find_gbk(act, PQ_NAME_FOG_ON),
			       pq_find_gbk(act, PQ_NAME_EDGE_ON),
			       pq_find_gbk(act, PQ_NAME_DOF_ON),
			       pq_find_gbk(act, PQ_NAME_GLOW_ON));
	}
}

int activity_part_no_at(struct activity *act, int index)
{
	if (!act || index < 0 || index >= act->nr_parts)
		return 0;
	return act->flat[index]->parts_no;
}

const char *activity_part_name_at(struct activity *act, int index)
{
	if (!act || index < 0 || index >= act->nr_parts)
		return "";
	return act->flat[index]->name;
}

int activity_part_cg_name_at(struct activity *act, int index, const char **out)
{
	if (!act || index < 0 || index >= act->nr_parts) {
		if (out) *out = "";
		return 0;
	}
	if (out) *out = act->flat[index]->cg_name;
	return 1;
}

/*
 * 从 PACT 压缩包读一个活动文件并构建部件树。
 * `name` 是 AIN 之后要用的活动名，`file` 是压缩包里的条目名
 * （实际两者都不带扩展名）。
 */
bool activity_read(struct string *name, struct string *file)
{
	if (!activity_debug_checked) {
		activity_debug = getenv("XSYS4_ACT_DBG") != NULL;
		activity_debug_checked = true;
	}
	if (!name || !name->text || !file || !file->text)
		return false;

	/* [D517o] 同名活动去重（name 或 file 任一命中）：重复点击同一页签时
	 * AIN 会再次 LoadActivity，旧活动不释放的话部件号段翻倍（D515 遗留②）。
	 * 实测 AIN 两次传的 name 不一致（`…页面.pact:0` / `…页面:0`），所以
	 * 还要按 file 做 basename 包含匹配吸收差异。原版语义 = LoadActivity
	 * 对已存在活动先 release 再装载。 */
	{
		struct activity *dup = find_activity(name->text);
		if (!dup) {
			for (struct activity *a = activity_list; a; a = a->next) {
				if (a->name && file->text && strstr(a->name, file->text)) {
					dup = a;
					break;
				}
			}
		}
		if (dup) {
			ACT_DEBUG("[act] '%s' already open, releasing before reload",
				  dup->name);
			activity_release_ex(dup->name, NULL);
		}
	}

	int id = -1;
	/* basename 匹配（见 activity_is_exist_file 注释）：扩展名与路径分隔符
	 * 差异都由它吸收；实例号歧义由 IsExistActivity 的 ':' 守卫挡住。 */
	struct archive_data *ad = asset_get_by_name(ASSET_PACT, file->text, &id);
	if (!ad || !ad->data) {
		ACT_DEBUG("[act] '%s' not found in PACT archive", file->text);
		return false;
	}

	struct ex *ex = ex_read((const uint8_t *)ad->data, ad->size);
	if (!ex) {
		ACT_DEBUG("[act] ex_read failed for '%s'", file->text);
		return false;
	}
	if (ex->nr_blocks < 1 || ex->blocks[0].val.type != EX_TREE) {
		ACT_DEBUG("[act] block 0 of '%s' is not a tree", file->text);
		ex_free(ex);
		return false;
	}

	struct activity *act = xcalloc(1, sizeof(struct activity));
	act->name = strdup(name->text);
	if (!act->name[0])
		act->name = strdup(file->text);
	/* [D500-g] .pactex 装载路径的创建也要计数（CreateActivity HLL 只是
	 * 另一条入口；配置页活动走这里）。同名替换由下方 release 抵消。 */
	activity_open_cnt++;
	if (getenv("XSYS4_ACT_STEP"))
		NOTICE("[actstep] read '%s' open=%d", act->name, activity_open_cnt);

	/* 元数据块大致按位置排，但块数会变（4/5/6 块），
	 * 所以按类型遍历，而不是按下标。 */
	int nr_ints = 0, nr_strs = 0;
	for (uint32_t i = 1; i < ex->nr_blocks; i++) {
		struct ex_value *v = &ex->blocks[i].val;
		if (v->type == EX_INT) {
			if (nr_ints == 0)
				act->version = v->i;
			else if (nr_ints == 1)
				act->parts_version = v->i;
			nr_ints++;
		} else if (v->type == EX_STRING) {
			/* first string block is 拡大, second is 背景图片 */
			if (nr_strs == 1)
				act->background = dup_text(v->s);
			nr_strs++;
		}
	}

	struct ex_tree *top = ex->blocks[0].val.tree;
	struct ex_tree *root = top->is_leaf ? NULL : &top->children[0];
	if (!root || root->is_leaf) {
		ACT_DEBUG("[act] '%s' has no root parts", file->text);
		ex_free(ex);
		activity_free(act);
		return false;
	}

	act->root = parse_part(root);
	count_parts(act->root, &act->nr_parts);
	act->flat = xcalloc(act->nr_parts, sizeof(struct activity_part *));
	int n = 0;
	flatten_parts(act->root, act->flat, &n);

	ex_free(ex);

	/* 替换同名的上一次装载 */
	struct activity *old = find_activity(act->name);
	if (old) {
		activity_release(act->name);
	}

	/* [D289] 部件创建要归到「即将创建的那一层」，而不是当前 active。
	 * AIN 的顺序是 Activity@Load（建部件）→ SceneStack@AddLayer（建层），
	 * 所以这里开一个迁移窗口：这期间建出的部件由 PE_AddController 收走。
	 * 没有后续 AddLayer 时（比如只是替换同名活动），pending-end 只告警不迁移，
	 * 行为与旧版一致。 */
	PE_ControllerPendingBegin();
	build_part(act->root, -1);
	PE_ControllerPendingEnd();

	/* 建树时部件还没有父节点，PE_SetPos() 只能算出 global = local，
	 * 之后父节点是 pending 状态、父链只在 PE_UpdateComponent() 里才挂上，
	 * 而它又会跳过"父节点还脏"的子部件 —— 于是子部件的 global.pos 永远
	 * 停在建树瞬间。渲染与命中都用 global.pos，整棵子树就会画错位置。
	 * PE_RefreshGlobalTree() 只挂父链 + 重算参数，不触发 layout
	 * （layout 会覆盖 EX 给的绝对位置）。详见 parts.c。 */
	PE_RefreshGlobalTree();

	/* [D515b] EX 配置声明的布局盒逐盒过静态布局判定：直接子件有非零
	 * 座标的是静态绝对布局（如设置页页签框架），撤回自动排版，
	 * 否则每帧 parts_do_layout 会把 EX 绝对座标覆盖掉。 */
	for (int i = 0; i < act->nr_parts; i++)
		parts_layout_veto_static_children(act->flat[i]->parts_no);
	/* [D517c] 镜像规则：LAYOUT_BOX + 子件座标全 (0,0) + 当前 FREE =
	 * 运行期排布容器，启用 VERTICAL。
	 * **默认已关（D517d 翻案）**：3D 设置页 19 个子页面本来就在 (0,0)
	 * 互相重叠，原版靠 `复选框表示連動`（选项卡联动）互斥显示。
	 * D517c 的强制纵排把重叠误当成「没排」，排出来的「选项列表」是假象
	 * （总高 6729px 溢出屏幕）。XSYS4_RUNTIME_LAYOUT=1 可重新打开做对照。 */
	{
		static int rl = -1;
		if (rl < 0) {
			const char *e = getenv("XSYS4_RUNTIME_LAYOUT");
			rl = (e && *e == '1');
		}
		if (rl)
			for (int i = 0; i < act->nr_parts; i++)
				parts_layout_enable_runtime_children(act->flat[i]->parts_no);
	}

	/* [D517d] 选项卡联动：`复选框表示連動` 写的是**部件名**，整棵树
	 * 装完之后才能解析成部件号。3D 设置页 19 个页面各绑一个选项卡。 */
	{
		int nlink = 0, nmiss = 0;
		for (int i = 0; i < act->nr_parts; i++) {
			struct activity_part *lp = act->flat[i];
			if (!lp->show_link_name || !*lp->show_link_name)
				continue;
			int link_no = activity_find_parts_no(act, lp->show_link_name);
			if (link_no > 0) {
				PE_SetShowLink(lp->parts_no, link_no);
				nlink++;
			} else {
				nmiss++;
				if (getenv("XSYS4_LINK_DBG"))
					NOTICE("[link] no=%d '%s' -> '%s' MISS",
					       lp->parts_no,
					       lp->name ? display_sjis0(lp->name) : "?",
					       lp->show_link_name);
			}
		}
		if (nlink || nmiss)
			NOTICE("[link] act='%s' resolved=%d miss=%d",
			       act->name ? display_sjis0(act->name) : "?",
			       nlink, nmiss);
	}

	/* 建树后的权威快照：此时父链与 global 参数都已成型，
	 * gpos 就是渲染/命中真正用的坐标。 */
	if (getenv("XSYS4_ACT_DEBUG")) {
		int i;
		for (i = 0; i < act->nr_parts; i++) {
			struct activity_part *p = act->flat[i];
			NOTICE("[act] layout %-14s no=%d gpos=(%d,%d) size=%dx%d "
			       "show=%d alpha=%d click=%d",
			       p->name, p->parts_no,
			       PE_GetPartsGlobalX(p->parts_no),
			       PE_GetPartsGlobalY(p->parts_no),
			       PE_GetPartsWidth(p->parts_no, 1),
			       PE_GetPartsHeight(p->parts_no, 1),
			       PE_GetPartsShow(p->parts_no),
			       PE_GetPartsAlpha(p->parts_no),
			       PE_GetPartsClickable(p->parts_no));
		}
	}

	act->next = activity_list;
	activity_list = act;

	/* [D517j/D517n] 配置页（全部 8 个页签）背景 = 实时 3D 场景（EX 里
	 * ＣＧ名 全空），原版由 PartsEngine 内部渲染，引擎侧零实现 ——
	 * 这里打开我们自己的预览场景（src/parts/preview3d.c）。
	 * D517n 起**整个配置页存活期间都开**，不只 3D 页（唯一 version
	 * 5/30 的 pact，其余页 6/x）：否则 01-07 页底下就是标题画面。 */
	if (activity_is_config_page(act))
		preview3d_set_active(true);

	NOTICE("[act] loaded '%s' from '%s' (version %d/%d, %d parts)",
	       act->name, file->text, act->version, act->parts_version, act->nr_parts);
	/* [D128] XSYS4_PARTS_DUMP=1：每次活动装载后拍一张"当前在显示的部件"快照，
	 * 用来确认旧场景的部件是否还活着、挂在哪个 controller 上。 */
	if (getenv("XSYS4_PARTS_DUMP"))
		PE_DebugDumpParts(act->name);
	return true;
}

/* [D526] 检查点恢复：按**装载顺序**导出现有活动名（activity_list 是头插，
 * 逆置后即装载序）。调用方负责逐个 free。返回个数。 */
/* [D529] 同 activity_collect_names，但附带**层归属**（活动第一个部件的
 * controller_no，即装载时的 active 栈下标）。恢复侧必须逐活动先设
 * active 再装载——否则全部活动都挂到同一个层，AIN 一切层，
 * `input_active_layer_only()` 的过滤（controller_no == active）就把
 * 所有部件滤掉（d810 实证：按下帧候选只剩 1 个）。 */
/* [D532] 旧签名包装（不带部件号列表）：调用方不需要 hint 时用。 */
int activity_collect_snapshot(char ***names, int **ctrls)
{
	char **pl = NULL;
	int n = activity_collect_snapshot_parts(names, ctrls, &pl);
	for (int i = 0; i < n; i++)
		free(pl[i]);
	free(pl);
	return n;
}

int activity_collect_snapshot_parts(char ***names, int **ctrls,
		char ***part_lists)
{
	int n = 0;
	for (struct activity *a = activity_list; a; a = a->next)
		n++;
	char **nm = xcalloc(n > 0 ? n : 1, sizeof(char *));
	int *ct = xcalloc(n > 0 ? n : 1, sizeof(int));
	char **pl = xcalloc(n > 0 ? n : 1, sizeof(char *));
	int i = n;
	for (struct activity *a = activity_list; a; a = a->next) {
		i--;
		nm[i] = a->name ? strdup(a->name) : NULL;
		ct[i] = -1;
		if (a->nr_parts > 0) {
			struct parts *pt = parts_try_get(a->flat[0]->parts_no);
			if (pt)
				ct[i] = pt->controller_no;
		}
	}
	/* [D532] 同时导出每个活动的**部件号列表**（逗号分隔），恢复时
	 * 用它做 PE_PushNumberHints，保证重建号与快照一致。 */
	for (int k = 0; k < n; k++)
		pl[k] = NULL;
	int j = n;
	for (struct activity *a = activity_list; a; a = a->next) {
		j--;
		size_t cap = (size_t)a->nr_parts * 12 + 1;
		char *buf = xcalloc(cap, 1);
		size_t off = 0;
		for (int q = 0; q < a->nr_parts; q++)
			off += snprintf(buf + off, cap - off, q ? ",%d" : "%d",
			                 a->flat[q]->parts_no);
		pl[j] = buf;
	}
	*names = nm;
	*ctrls = ct;
	*part_lists = pl;
	return n;
}

int activity_collect_names(char ***out)
{
	int n = 0;
	for (struct activity *a = activity_list; a; a = a->next)
		n++;
	char **names = xcalloc(n > 0 ? n : 1, sizeof(char *));
	int i = n;
	for (struct activity *a = activity_list; a; a = a->next)
		names[--i] = a->name ? strdup(a->name) : NULL;
	*out = names;
	return n;
}

struct activity *activity_create(const char *name)
{
	struct activity *act = find_activity(name);
	if (act)
		return act;
	act = xcalloc(1, sizeof(struct activity));
	act->name = strdup(name ? name : "");
	act->next = activity_list;
	activity_list = act;
	activity_open_cnt++;
	if (getenv("XSYS4_ACT_STEP"))
		NOTICE("[actstep] create '%s' open=%d", act->name, activity_open_cnt);
	return act;
}

int activity_open_count(void)
{
	return activity_open_cnt;
}

/* [D511] **菜单类活动**是否打开（`CommonMenu` / 配置页 `.pact`）。
 *
 * 用途：输入层的按下沿门（`parts_began_click`）在这两类界面里必须放宽——
 * AIN 卡在菜单自己的循环里，既不调 BeginInput 也不调 EndInput，门会把
 * 每一次点击都吞掉（用户实测「点菜单后所有按钮失灵」）。
 * ATL/ADV/标题不在此列：那里 AIN 有正常的 Begin/EndInput 周期，
 * 放宽反而会让过场里的点击误命中（d523/d524 实测：全局放宽后标题
 * 「从头开始」不再进游戏）。 */
bool activity_menu_open(void)
{
	for (struct activity *a = activity_list; a; a = a->next) {
		if (!a->name)
			continue;
		if (strstr(a->name, "CommonMenu") || strstr(a->name, ".pact"))
			return true;
	}
	return false;
}

bool activity_add_parts(struct string *name, struct string *parts_name, int parts_no)
{
	struct activity *act = find_activity(name ? name->text : NULL);
	if (!act || !parts_name)
		return false;
	/* 已经注册过了（例如它来自文件） */
	for (int i = 0; i < act->nr_parts; i++) {
		if (act->flat[i]->name && !strcmp(act->flat[i]->name, parts_name->text))
			return true;
	}
	struct activity_part *p = xcalloc(1, sizeof(struct activity_part));
	p->name = strdup(parts_name->text);
	p->parts_no = parts_no;
	act->flat = xrealloc(act->flat, sizeof(struct activity_part *) * (act->nr_parts + 1));
	act->flat[act->nr_parts++] = p;
	return true;
}

bool activity_remove_parts(struct string *name, struct string *parts_name)
{
	struct activity *act = find_activity(name ? name->text : NULL);
	if (!act || !parts_name)
		return false;
	for (int i = 0; i < act->nr_parts; i++) {
		if (act->flat[i]->name && !strcmp(act->flat[i]->name, parts_name->text)) {
			PE_ReleaseParts(act->flat[i]->parts_no);
			memmove(&act->flat[i], &act->flat[i + 1],
				sizeof(struct activity_part *) * (act->nr_parts - i - 1));
			act->nr_parts--;
			return true;
		}
	}
	return false;
}

void activity_remove_all_parts(struct string *name)
{
	struct activity *act = find_activity(name ? name->text : NULL);
	if (!act)
		return;
	for (int i = 0; i < act->nr_parts; i++)
		PE_ReleaseParts(act->flat[i]->parts_no);
	act->nr_parts = 0;
}

const char *activity_background(struct activity *act)
{
	return act && act->background ? act->background : "";
}
