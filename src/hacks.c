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

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "system4.h"
#include "system4/ain.h"
#include "system4/buffer.h"
#include "system4/instructions.h"
#include "system4/string.h"
#include "system4/utfsjis.h"

#include "gfx/gfx.h"
#include "input.h"
#include "xsystem4.h"

#include "hll/hll.h"

bool game_daibanchou_en = false;
bool game_rance02_mg = false;
bool game_rance6_mg = false;
bool game_rance7_mg = false;
bool game_rance8 = false;
bool game_rance8_mg = false;
bool game_dungeons_and_dolls = false;

static void write_instruction0(struct buffer *out, enum opcode op)
{
	buffer_write_int16(out, op);
}

static void write_instruction1(struct buffer *out, enum opcode op, int32_t arg)
{
	buffer_write_int16(out, op);
	buffer_write_int32(out, arg);
}

static void write_instruction3(struct buffer *out, enum opcode op, int32_t arg0, int32_t arg1,
		int32_t arg2)
{
	buffer_write_int16(out, op);
	buffer_write_int32(out, arg0);
	buffer_write_int32(out, arg1);
	buffer_write_int32(out, arg2);
}

void set_msgskip_delay(struct ain *ain, unsigned ms)
{
	int orig_fno = ain_get_function(ain, "A");
	if (orig_fno <= 0) {
		WARNING("No 'A' function when applying msgskip delay");
		return;
	}

	int new_fno = ain_dup_function(ain, orig_fno);
	struct ain_function *override_f = &ain->functions[orig_fno];

	// override void A(void) {
	//     super();
	//     if (key_is_down(VK_CONTROL))
	//         System.sleep(ms);
	// }
	struct buffer out;
	buffer_init(&out, NULL, 0);
	write_instruction1(&out, FUNC, orig_fno);
	write_instruction1(&out, CALLFUNC, new_fno);
	write_instruction1(&out, PUSH, VK_CONTROL);
	write_instruction1(&out, CALLSYS, VM_XSYS_KEY_IS_DOWN);
	uint32_t ifz_addr = out.index;
	write_instruction1(&out, IFZ, 0);
	write_instruction1(&out, PUSH, ms);
	write_instruction1(&out, CALLSYS, SYS_SLEEP);
	buffer_write_int32_at(&out, ifz_addr + 2, ain->code_size + out.index);
	write_instruction0(&out, RETURN);
	write_instruction1(&out, ENDFUNC, orig_fno);

	override_f->address = ain->code_size + 6;
	ain->code = xrealloc(ain->code, ain->code_size + out.index);
	memcpy(ain->code + ain->code_size, out.buf, out.index);
	ain->code_size += out.index;
}

// 兰斯02（MangaGamer 版）
// -----------------------------
// 它用的是自定义的比例字体实现，对字体做了些假设。
// 我们把 _CalculateWidth 换成一个与字体无关的版本
// implementation.
//
// '^' 字符映射成 'é'（Rosé）
// '}' 字符映射成双宽的破折号
static void apply_rance02_hacks(struct ain *ain)
{
	int fno = ain_get_function(ain, "_CalculateWidth");
	if (fno <= 0) {
		WARNING("No '_CalculateWidth' function when applying Rance 02 compatibility hack");
		return;
	}
	struct ain_function *f = &ain->functions[fno];

	// 用户没设过就把文字缩放降下来
	if (!config.manual_text_x_scale) {
		config.text_x_scale = 0.9375;
	}

	// 重写函数
	struct buffer out;
	buffer_init(&out, ain->code, ain->code_size);
	buffer_seek(&out, f->address);

	// _CalculateWidth(ref string str, int nFontSize, int nWeightSize) {
	//     if (str[0] > 255 || str[0] == '}')
	//         return nFontSize + nWeightSize;
	//     return nFontSize / 2 + nWeightSize;
	// }

	// if (str[0] > 255 || str[0] == '}')
	write_instruction1(&out, SH_LOCALREF, 0);
	write_instruction1(&out, PUSH, 0);
	write_instruction0(&out, C_REF);
	write_instruction0(&out, DUP);
	write_instruction1(&out, PUSH, 255);
	write_instruction0(&out, GT);
	write_instruction0(&out, SWAP);
	write_instruction1(&out, PUSH, '}');
	write_instruction0(&out, EQUALE);
	write_instruction0(&out, OR);
	int ifz_addr = out.index;
	write_instruction1(&out, IFZ, 0);

	// return nFontSize + nWeightSize
	write_instruction1(&out, SH_LOCALREF, 1);
	write_instruction1(&out, SH_LOCALREF, 2);
	write_instruction0(&out, ADD);
	int jump_addr = out.index;
	write_instruction1(&out, JUMP, 0);

	// return nFontSize / 2 + nWeightSize;
	buffer_write_int32_at(&out, ifz_addr + 2, out.index);
	write_instruction1(&out, SH_LOCALREF, 1);
	write_instruction1(&out, PUSH, 2);
	write_instruction0(&out, DIV);
	write_instruction1(&out, SH_LOCALREF, 2);
	write_instruction0(&out, ADD);

	// 乘上 config.text_x_scale（如果不等于 1.0）
	buffer_write_int32_at(&out, jump_addr + 2, out.index);
	if (fabsf(1.0f - config.text_x_scale) > 0.01) {
		union { int32_t i; float f; } cast = { .f = config.text_x_scale };
		write_instruction0(&out, ITOF);
		write_instruction1(&out, F_PUSH, cast.i);
		write_instruction0(&out, F_MUL);
		write_instruction0(&out, FTOI);
	}

	write_instruction0(&out, RETURN);
	write_instruction1(&out, ENDFUNC, fno);

	game_rance02_mg = true;
}

// 兰斯VI（MangaGamer 版）
// -----------------------------
// 'ﾉ' character is mapped to 'é' (Rosé)
static void apply_rance6_hacks(struct ain *ain)
{
	if (ain_get_function(ain, "GetTextWidth") < 0)
		return;
	game_rance6_mg = true;
}

// 兰斯IX
// --------
// T_PartsNo 这个类的实现极其低效，
// 用 UI 时（例如打开道具界面）会有明显的性能问题。
// the items screen).
// 我们重写这个类来提速。
static void apply_rance9_hacks(struct ain *ain)
{
	int add_fno = ain_get_function(ain, "T_PartsNo@add");
	int erase_fno = ain_get_function(ain, "T_PartsNo@erase");
	if (add_fno < 0 || erase_fno < 0) {
		WARNING("Couldn't location T_PartsNo@add or T_PartsNo@erase");
		return;
	}

	struct buffer out;
	buffer_init(&out, ain->code, ain->code_size);

	// bool T_PartsNo::add(int nNo)
	// {
	//     if (nNo < 10000 || nNo > 999999)
	//         return false;
	//     int idx = nNo - 10000;
	//     if (this.m_abUsed[idx])
	//         return false;
	//     return (this.m_abUsed[idx] = true);
	// }
	int32_t return_false_addr[3];
	struct ain_function *f = &ain->functions[add_fno];
	buffer_seek(&out, f->address);
	// if (nNo < 10000 || nNo > 999999) return false;
	write_instruction3(&out, SH_IF_LOC_LT_IMM, 0, 10000, 0);
	return_false_addr[0] = out.index - 4;
	write_instruction3(&out, SH_IF_LOC_GT_IMM, 0, 999999, 0);
	return_false_addr[1] = out.index - 4;
	// int idx = nNo - 10000;
	write_instruction0(&out, PUSHLOCALPAGE);
	write_instruction1(&out, PUSH, 1);
	write_instruction1(&out, SH_LOCALREF, 0);
	write_instruction1(&out, PUSH, 10000);
	write_instruction0(&out, SUB);
	write_instruction0(&out, ASSIGN);
	write_instruction0(&out, POP);
	// if (this.m_abUsed[idx]) return false;
	write_instruction1(&out, SH_STRUCTREF, 1);
	write_instruction1(&out, SH_LOCALREF, 1);
	write_instruction0(&out, REF);
	write_instruction1(&out, IFNZ, 0);
	return_false_addr[2] = out.index - 4;
	// return (this.m_abUsed[idx] = true);
	write_instruction1(&out, SH_STRUCTREF, 1);
	write_instruction1(&out, SH_LOCALREF, 1);
	write_instruction1(&out, PUSH, 1);
	write_instruction0(&out, ASSIGN);
	write_instruction0(&out, RETURN);
	// [return false]
	buffer_write_int32_at(&out, return_false_addr[0], out.index);
	buffer_write_int32_at(&out, return_false_addr[1], out.index);
	buffer_write_int32_at(&out, return_false_addr[2], out.index);
	write_instruction1(&out, PUSH, 0);
	write_instruction0(&out, RETURN);

	// bool T_PartsNo::erase(int nNo)
	// {
	//     if (nNo < 10000 || nNo > 999999)
	//         return false;
	//     this.m_abUsed[nNo - 10000] = false;
	//     return true;
	// }
	f = &ain->functions[erase_fno];
	buffer_seek(&out, f->address);
	// if (nNo < 10000 || nNo > 999999) return false;
	write_instruction3(&out, SH_IF_LOC_LT_IMM, 0, 10000, 0);
	return_false_addr[0] = out.index - 4;
	write_instruction3(&out, SH_IF_LOC_GT_IMM, 0, 999999, 0);
	return_false_addr[1] = out.index - 4;
	// this.m_abUsed[nNo - 10000] = false;
	write_instruction1(&out, SH_STRUCTREF, 1);
	write_instruction1(&out, SH_LOCALREF, 0);
	write_instruction1(&out, PUSH, 10000);
	write_instruction0(&out, SUB);
	write_instruction1(&out, PUSH, 0);
	write_instruction0(&out, ASSIGN);
	write_instruction0(&out, POP);
	// return true;
	write_instruction1(&out, PUSH, 1);
	write_instruction0(&out, RETURN);
	// [return false]
	buffer_write_int32_at(&out, return_false_addr[0], out.index);
	buffer_write_int32_at(&out, return_false_addr[1], out.index);
	write_instruction1(&out, PUSH, 0);
	write_instruction0(&out, RETURN);
}

// Daibanchou (English fan translation)
// ------------------------------------
// 为了绕开渲染问题，Gpx2Plus.CopyStretchReduceAMap 的行为要改：
// 那问题来自游戏在 StructRegionalSelect::DrawMenuItem 里
// 计算文字宽度的方式。
static void apply_daibanchou_hacks(struct ain *ain)
{
	if (ain->nr_strings > 2 && !strcmp(ain->strings[2]->text, "Seijou Academy")) {
		game_daibanchou_en = true;
	}
}

/*
 * [D204] AIN v12 去掉了 MSGF 段，但 _MSG 指令仍然
 * 会去调 ain->msgf。libsys4 的 ain_open_conv() 把 msgf 留在
 * xcalloc 的默认值上（0 = NULL 函数），于是 _MSG 把
 * (msg_no, nr_messages, text) 压进一个空操作帧，每一句对白都被悄悄吞掉。
 * 把 msgf 设成 <0 也不行：_MSG 是 JMP 类指令
 * （ip_inc == 0），`if (ain->msgf < 0) break;` 会在同一条
 * 指令上死循环。
 *
 * 编译器仍然把那个处理函数以裸名 "message" 编进 FUNC，
 * 局部变量正好是 _MSG 压的那 3 个（nMsgNum、nNumofMsg、szText）
 * （兰斯10：函数号 6100）。函数号 6109（"S"）甚至直接把它当
 * message(0, 0, text) 调。从这个函数把 msgf 找回来。
 *
 * 从 D204 起默认打开（已验证：０２／ＳＴＡＲＴ 能跑，对白文本
 * 能到达 SetMessageWindowText，点击能推进到下一句 MSG）。
 * XSYS4_MSGF_FIX=0 可关掉它做 A/B。
 */
static void resolve_v12_msgf(struct ain *ain)
{
	/* 只对真正丢了 MSGF、但仍带 MSG1 表的 AIN 生效。 */
	if (ain->MSGF.present || !ain->MSG1.present || ain->msgf > 0)
		return;
	for (int i = 1; i < ain->nr_functions; i++) {
		struct ain_function *f = &ain->functions[i];
		if (!f->name || f->nr_args != 3 || strcmp(f->name, "message"))
			continue;
		ain->msgf = i;
		NOTICE("[msgf] v12: no MSGF section; resolved handler to fno=%d (%s)",
		       i, f->name);
		return;
	}
	NOTICE("[msgf] v12: no MSGF section and no 'message'/3 function; msgf stays %d",
	       ain->msgf);
}

void apply_game_specific_hacks(struct ain *ain)
{
	/* [D204] 默认生效；XSYS4_MSGF_FIX=0 可关掉做 A/B 对照。 */
	const char *msgf_off = getenv("XSYS4_MSGF_FIX");
	if (!msgf_off || strcmp(msgf_off, "0"))
		resolve_v12_msgf(ain);

	char *game_name = sjis2utf(config.game_name, 0);
	if (!strcmp(game_name, "大番長")) {
		apply_daibanchou_hacks(ain);
	} else if (!strcmp(game_name, "Rance 02")) {
		apply_rance02_hacks(ain);
	} else if (!strcmp(game_name, "Rance6")) {
		apply_rance6_hacks(ain);
	} else if (!strcmp(game_name, "ランス・クエスト")) {
		game_rance8 = true;
	} else if (!strcmp(game_name, "Rance Quest")) {
		game_rance8 = true;
		game_rance8_mg = true;
	} else if (!strcmp(game_name, "ＤＵＮＧＥＯＮＳ＆ＤＯＬＬＳ")) {
		game_dungeons_and_dolls = true;
	} else if (!strcmp(game_name, "ランス９")
			|| !strcmp(game_name, "Rance 9 - The Helmanian Revolution")) {
		apply_rance9_hacks(ain);
	}
	free(game_name);

	// XXX：我们不依赖 game_name，因为 mod 会改它
	if (ain_get_library(ain, "SengokuRanceFont") >= 0)
		game_rance7_mg = true;
}
