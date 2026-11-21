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
#include <ctype.h>
#include <dirent.h>
#include <ctype.h>
#include <getopt.h>
#include <time.h>
#include <math.h>
#include <limits.h>

#include "system4.h"
#include "system4/ain.h"
#include "system4/file.h"
#include "system4/ini.h"
#include "system4/instructions.h"
#include "system4/little_endian.h"
#include "system4/string.h"
#include "system4/utfsjis.h"

#include "xsystem4.h"
#include "asset_manager.h"
#include "debugger.h"
#include "gfx/gfx.h"
#include "gfx/font.h"
#include "vm.h"

#include "version.h"

void set_msgskip_delay(struct ain *ain, unsigned ms);
void apply_game_specific_hacks(struct ain *ain);

struct config config = {
	.game_name = NULL,
	.boot_name = NULL,
	.ain_filename = NULL,
	.vm_name = NULL,
	.game_dir = NULL,
	.save_dir = NULL,
	.home_dir = NULL,
	.view_width = 800,
	.view_height = 600,
	.mixer_nr_channels = 0,
	.mixer_channels = NULL,
	.default_volume = 100,
	.joypad = false,
	.echo = false,
	.text_x_scale = 1.0,
	.manual_text_x_scale = false,
	.save_format = SAVE_FORMAT_RSM,
	.msgskip_delay = 0,

	.bgi_path = NULL,
	.wai_path = NULL,
	.ex_path = NULL,
	.fnl_path = NULL,
	.font_paths = { NULL, NULL },
};

// System40.ini 里的 SaveFolder。只有在读完 .xsys4rc 之后
// 才把它转成 config.save_dir 里的路径，因为那些文件可能改编码。
static char *ini_save_folder = NULL;

static struct string *ini_string(struct ini_entry *entry)
{
	if (entry->value.type != INI_STRING)
		ERROR("ini value for '%s' is not a string", entry->name->text);
	return entry->value.s;
}

static int ini_integer(struct ini_entry *entry)
{
	if (entry->value.type != INI_INTEGER)
		ERROR("ini value for '%s' is not an integer", entry->name->text);
	return entry->value.i;
}

static int ini_boolean(struct ini_entry *entry)
{
	if (entry->value.type != INI_BOOLEAN)
		ERROR("ini value for '%s' is not a boolean", entry->name->text);
	return entry->value.i;
}

static void read_mixer_channels(struct ini_entry *entry)
{
	if (entry->value.type != INI_LIST)
		ERROR("ini value for 'VolumeValancer' is not a list");

	config.mixer_nr_channels = entry->value.list_size;
	config.mixer_channels = xcalloc(entry->value.list_size, sizeof(char*));
	config.mixer_volumes = xcalloc(entry->value.list_size, sizeof(int));
	for (size_t i = 0; i < entry->value.list_size; i++) {
		if (entry->value.list[i].type == INI_NULL) {
			config.mixer_channels[i] = strdup("Unnamed");
			config.mixer_volumes[i] = 0;
		} else if (entry->value.list[i].type == INI_STRING) {
			config.mixer_channels[i] = strdup(entry->value.list[i].s->text);
			// XXX：假定 default_volume 已经初始化过了
			config.mixer_volumes[i] = config.default_volume;
		} else if (entry->value.list[i].type == INI_LIST) {
			if (entry->value.list[i].list_size != 2)
				ERROR("ini value for 'VolumeValancer' is list of wrong size");
			if (entry->value.list[i].list[0].type != INI_STRING)
				ERROR("ini value for 'VolumeValancer' name is not a string");
			if (entry->value.list[i].list[1].type != INI_INTEGER)
				ERROR("ini value for 'VolumeValancer' level is not an integer");
			config.mixer_channels[i] = strdup(entry->value.list[i].list[0].s->text);
			config.mixer_volumes[i] = entry->value.list[i].list[1].i;
		}
	}
}

static bool read_config(const char *path)
{
	int ini_size;
	struct ini_entry *ini = ini_parse(path, &ini_size);
	if (!ini)
		return false;

	for (int i = 0; i < ini_size; i++) {
		if (!strcmp(ini[i].name->text, "GameName")) {
			config.game_name = strdup(ini_string(&ini[i])->text);
		} else if (!strcmp(ini[i].name->text, "BootName")) {
			config.boot_name = strdup(ini_string(&ini[i])->text);
		} else if (!strcmp(ini[i].name->text, "CodeName")) {
			config.ain_filename = strdup(ini_string(&ini[i])->text);
		} else if (!strcmp(ini[i].name->text, "MainVM")) {
			config.vm_name = strdup(ini_string(&ini[i])->text);
		} else if (!strcmp(ini[i].name->text, "SaveFolder")) {
			ini_save_folder = strdup(ini_string(&ini[i])->text);
		} else if (!strcmp(ini[i].name->text, "ViewWidth")) {
			config.view_width = ini_integer(&ini[i]);
		} else if (!strcmp(ini[i].name->text, "ViewHeight")) {
			config.view_height = ini_integer(&ini[i]);
		} else if (!strcmp(ini[i].name->text, "VolumeValancer")) {
			read_mixer_channels(&ini[i]);
		} else if (!strcmp(ini[i].name->text, "DefaultVolumeRate")) {
			config.default_volume = ini_integer(&ini[i]);
		} else if (!strcmp(ini[i].name->text, "UseJoypad")) {
			config.joypad = ini_boolean(&ini[i]);
		}
		ini_free_entry(&ini[i]);
	}
	free(ini);
	return true;
}

static char *user_config_path(struct ini_entry *entry, const char *basedir)
{
	const char *path = ini_string(entry)->text;
	if (basedir && !is_absolute_path(path)) {
		return path_join(basedir, path);
	}
	return xstrdup(path);
}

static bool set_encoding(const char *name)
{
	if (!strcasecmp(name, "sjis")) {
		use_gbk = false;
	} else if (!strcasecmp(name, "gbk")) {
		use_gbk = true;
	} else {
		return false;
	}
	return true;
}

static void read_user_config_file(const char *path, const char *basedir)
{
	int ini_size;
	struct ini_entry *ini = ini_parse(path, &ini_size);
	if (!ini)
		return;

	for (int i = 0; i < ini_size; i++) {
		if (!strcmp(ini[i].name->text, "font-mincho")) {
			free(config.font_paths[FONT_MINCHO]);
			config.font_paths[FONT_MINCHO] = user_config_path(&ini[i], basedir);
		} else if (!strcmp(ini[i].name->text, "font-gothic")) {
			free(config.font_paths[FONT_GOTHIC]);
			config.font_paths[FONT_GOTHIC] = user_config_path(&ini[i], basedir);
		} else if (!strcmp(ini[i].name->text, "font-fnl")) {
			free(config.fnl_path);
			config.fnl_path = user_config_path(&ini[i], basedir);
		} else if (!strcmp(ini[i].name->text, "font-x-scale")) {
			float f = strtof(ini_string(&ini[i])->text, NULL);
			if (fabsf(f) < 0.01) {
				WARNING("Invalid value for font-x-scale in config: \"%s\"",
						ini_string(&ini[i])->text);
			} else {
				config.manual_text_x_scale = true;
				config.text_x_scale = f;
			}
		} else if (!strcmp(ini[i].name->text, "msgskip-delay")) {
			config.msgskip_delay = ini_integer(&ini[i]);
			if (config.msgskip_delay < 0) {
				WARNING("Invalid value for msgskip-delay in config: \"%s\"",
						ini_string(&ini[i])->text);
				config.msgskip_delay = 0;
			}
		} else if (!strcmp(ini[i].name->text, "save-folder")) {
			free(config.save_dir);
			config.save_dir = user_config_path(&ini[i], basedir);
		} else if (!strcmp(ini[i].name->text, "save-format")) {
			if (!strcmp(ini_string(&ini[i])->text, "json")) {
				config.save_format = SAVE_FORMAT_JSON;
			} else if (!strcmp(ini_string(&ini[i])->text, "rsm")) {
				config.save_format = SAVE_FORMAT_RSM;
			} else {
				WARNING("Invalid value for save-format in config: \"%s\"",
						ini_string(&ini[i])->text);
			}
		} else if (!strcmp(ini[i].name->text, "encoding")) {
			if (!set_encoding(ini_string(&ini[i])->text)) {
				WARNING("Invalid value for encoding in config: \"%s\"",
						ini_string(&ini[i])->text);
			}
		} else if (!strcmp(ini[i].name->text, "fullscreen")) {
			/* [D377] `fullscreen = 1`：启动就进真全屏（无窗口边框 / 无标题栏）。
			 * 也接受 `true`。环境变量 `XSYS4_FULLSCREEN` 仍可覆盖（见 video.c）。 */
			if (ini[i].value.type == INI_BOOLEAN) {
				config.fullscreen = ini_boolean(&ini[i]);
			} else if (ini[i].value.type == INI_STRING) {
				const char *v = ini_string(&ini[i])->text;
				config.fullscreen = !strcmp(v, "1") || !strcasecmp(v, "true") ||
						    !strcasecmp(v, "yes");
			} else {
				config.fullscreen = ini_integer(&ini[i]) != 0;
			}
		}
		ini_free_entry(&ini[i]);
	}
	free(ini);
}

static void read_user_config(void)
{
	char *path;

	// 全局配置
	path = xmalloc(PATH_MAX);
	snprintf(path, PATH_MAX-1, "%s/.xsys4rc", config.home_dir);
	read_user_config_file(path, NULL);
	free(path);

	// 游戏相关的配置
	path = gamedir_path(".xsys4rc");
	read_user_config_file(path, config.game_dir);
	free(path);
}

static char *get_xsystem4_home(void)
{
	// $XSYSTEM4_HOME
	char *env_home = getenv("XSYSTEM4_HOME");
	if (env_home && *env_home) {
		return xstrdup(env_home);
	}

	// $XDG_DATA_HOME/xsystem4
	env_home = getenv("XDG_DATA_HOME");
	if (env_home && *env_home) {
		char *home = xmalloc(strlen(env_home) + strlen("/xsystem4") + 1);
		strcpy(home, env_home);
		strcat(home, "/xsystem4");
		return home;
	}

	// $HOME/.xsystem4
	env_home = getenv("HOME");
	if (env_home && *env_home) {
		char *home = xmalloc(strlen(env_home) + strlen("/.xsystem4") + 1);
		strcpy(home, env_home);
		strcat(home, "/.xsystem4");
		return home;
	}

	// 全都不行就用当前目录
	return realpath_utf8(".");
}

static char *get_save_path(const char *dir_name)
{
	if (!dir_name)
		dir_name = "SaveData";

	char *utf8_game_name = sjis2utf(config.game_name, strlen(config.game_name));
	char *utf8_dir_name = sjis2utf(dir_name, strlen(dir_name));
	char *save_dir = xmalloc(strlen(config.home_dir) + 1 + strlen(utf8_game_name) + 1 + strlen(utf8_dir_name) + 1);
	strcpy(save_dir, config.home_dir);
	strcat(save_dir, "/");
	strcat(save_dir, utf8_game_name);
	strcat(save_dir, "/");
	strcat(save_dir, utf8_dir_name);
	free(utf8_game_name);
	free(utf8_dir_name);
	return save_dir;
}

static void config_init(void)
{
	config.home_dir = get_xsystem4_home();
	if (!config.game_name)
		config.game_name = strdup(config.ain_filename);
}

static bool config_init_with_ini(const char *ini_path)
{
	if (!read_config(ini_path))
		return false;
	if (!config.ain_filename)
		ERROR("No AIN filename specified in %s", ini_path);
	char *tmp = strdup(ini_path);
	config.game_dir = strdup(path_dirname(tmp));
	free(tmp);
	config_init();
	return true;
}

static bool config_init_with_dir(const char *dir)
{
	char path[PATH_MAX];
	snprintf(path, PATH_MAX, "%s/System40.ini", dir);
	if (!file_exists(path)) {
		snprintf(path, PATH_MAX, "%s/AliceStart.ini", dir);
		if (!file_exists(path))
			return false;
	}
	return config_init_with_ini(path);
}

static void config_init_with_ain(const char *ain_path)
{
	config.ain_filename = strdup(path_basename(ain_path));
	config.game_dir = strdup(path_dirname(ain_path));
	config_init();
}

/* 统计 CALLMETHOD / CALLFUNC 的参数分布，判断其语义 */
static void ain_stat_method(FILE *f, struct ain *ain)
{
	int total = 0, invalid = 0;
	long hist[32] = {0};
	int maxarg = 0;

	size_t last_addr = 0;
	for (size_t addr = 0; addr < ain->code_size;) {
		uint16_t opcode = LittleEndian_getW(ain->code, addr);
		if (opcode >= NR_OPCODES) {
			fprintf(f, "!! 扫描中断于 0x%08zx: 非法 opcode 0x%04x\n", addr, opcode);
			break;
		}
		last_addr = addr;
		if (opcode == CALLMETHOD) {
			uint32_t fno = LittleEndian_getDW(ain->code, addr + 2);
			total++;
			if (fno < 32) { hist[fno]++; if ((int)fno > maxarg) maxarg = fno; }
			if (fno >= (uint32_t)ain->nr_functions || !ain->functions[fno].name) {
				invalid++;
				if (invalid <= 5)
					fprintf(f, "  invalid CALLMETHOD arg=%u at 0x%08zx\n", fno, addr);
			}
		}
		addr += instruction_width(opcode);
	}

	fprintf(f, "扫描至 0x%08zx / code_size=0x%08zx\n", last_addr, ain->code_size);
	fprintf(f, "CALLMETHOD total=%d  invalid(空函数/越界)=%d\n", total, invalid);
	for (int i = 0; i <= maxarg && i < 32; i++)
		if (hist[i]) fprintf(f, "  arg=%-3d count=%ld\n", i, hist[i]);
	fflush(f);
}

/* 按地址范围反汇编；CALLHLL / CALLFUNC 会还原出符号名，用于定位 NULL 调用 */
static void ain_disasm(FILE *f, struct ain *ain, size_t start, size_t len)
{
	size_t end = (start + len > ain->code_size) ? ain->code_size : start + len;
	/* [D397] CALLMETHOD 的立即数是**逻辑实参个数**，不是函数号。
	 *
	 * VM 侧 `method_call(nargs, …)` 取 fno 用的是 `stack_peek(nargs).i`
	 * （栈布局 `[obj, fno, args…]`，fno 压在实参下面一层）。旧版这里把
	 * `addr+2` 当函数号打印，于是**全表 CALLMETHOD 的名字都是错的**：
	 * nargs=0 打成 `NULL @0x0`，nargs=1 恰好等于 `GetMessageSpeedRate`
	 * 的 fno，于是满屏假名。据此读出的调用链不可信 —— TURNSTALL 里
	 * 「23463 触发 FadeOut」就是照着假名推出来的，实际 23463 只做
	 * `ThumbnailCapture + LocalSave`。
	 *
	 * 静态反汇编只能还原「常量压栈」这一种最常见形式：编译器固定生成
	 * `PUSH <fno>` 紧接 CALLMETHOD（中间只隔 REF/SWAP/DUP）。这里回溯
	 * 最近压入的立即数，并排除 `PUSHSTRUCTPAGE + PUSH <structno>`
	 * 这种**结构页号**（它不是函数号，取值和 fno 空间重叠）。
	 * 回溯不到就打印 `?` —— 诚实地说「这是动态方法号」，
	 * 比编一个错名字有用。
	 */
	struct {
		unsigned int raw;
		bool struct_page;
		uint16_t prev_op, next_op;
	} pushes[16];
	int n_pushes = 0;
	uint16_t prev_opcode = 0xffff;

	for (size_t addr = start; addr < end;) {
		uint16_t opcode = LittleEndian_getW(ain->code, addr);
		if (opcode >= NR_OPCODES) {
			fprintf(f, "0x%08zx: <invalid opcode 0x%04x>\n", addr, opcode);
			break;
		}
		/* 回填「上一条 PUSH 的下一条指令」——回溯 fno 时要用它区分
		 * 「方法号」和「随后压入的实参」：fno 后面跟的是实参压栈或
		 * CALLMETHOD，而实参 PUSH 后面跟的是另一条实参压栈/SWAP。 */
		if (n_pushes > 0 && pushes[n_pushes - 1].next_op == 0xffff)
			pushes[n_pushes - 1].next_op = opcode;

		const struct instruction *instr = &instructions[opcode];
		int w = instruction_width(opcode);
		unsigned int raw = 0;
		if (w >= 6)
			raw = LittleEndian_getDW(ain->code, addr + 2);
		fprintf(f, "0x%08zx: %-18s", addr, instr->name);
		if (w >= 6)
			fprintf(f, " arg=%-8u(0x%x)", raw, raw);
		/* [D400] `DG_CALL <dg_no> <end_addr>` 宽 10、有两个操作数。
		 * 第二个是「委托链走完之后的跳转目标」：`delegate_call()` 在
		 * `!dg_ok`（index 越界）时执行 `instr_ptr = get_argument(1)`。
		 * 旧版这里只打第一个操作数，于是 AIN 的标准 foreach 写法
		 *   `DG_CALLBEGIN n / DG_CALL n,END / JUMP DG_CALL`
		 * 在反汇编里看起来像**死循环**，`Scenes::Run` 就是这样被误读的。
		 * 只补打印，不改语义。 */
		if (opcode == DG_CALL && w >= 10)
			fprintf(f, " dg=%u end=0x%06x", raw,
			        LittleEndian_getDW(ain->code, addr + 6));

		if (opcode == CALLHLL) {
			uint32_t lib = LittleEndian_getDW(ain->code, addr + 2);
			uint32_t fun = LittleEndian_getDW(ain->code, addr + 6);
			if (lib < (uint32_t)ain->nr_libraries &&
			    fun < (uint32_t)ain->libraries[lib].nr_functions) {
				fprintf(f, "%s.%s", ain->libraries[lib].name,
					ain->libraries[lib].functions[fun].name);
			} else {
				fprintf(f, "lib=%u fun=%u <out of range>", lib, fun);
			}
		} else if (opcode == CALLMETHOD) {
			/* 回溯 fno：从新到旧找**最像方法号**的那条 PUSH。
			 * 排除三类：
			 *   ① 夹在实参压栈中间那条（prev 是 PUSH/F_PUSH/S_PUSH）；
			 *   ② 后一条不是 CALLMETHOD/实参压栈/栈整理的（那是成员号或槽号）；
			 *
			 * **[D427] 「排除 PUSHSTRUCTPAGE 之后的 PUSH」是错的**：PUSHSTRUCTPAGE
			 * 是零操作数指令（`OP(PUSHSTRUCTPAGE, 0, (), (), (T_PAGE))`），它后面
			 * 那条 PUSH 是独立指令。而方法调用最常见的编码正是
			 * `PUSHSTRUCTPAGE; PUSH <fno>; [args]; CALLMETHOD n` —— 排除掉就等于
			 * 把绝大多数方法调用打成 `?`。成员号那种情形由「下一条是 REF」挡掉。
			 * 第一条 CALLMETHOD 之后会把该位置及之后的记录丢掉（已消费）。 */
			fprintf(f, " nargs=%u ->", raw);
			int hit = -1;
			for (int k = n_pushes - 1; k >= 0; k--) {
				if (pushes[k].raw >= (unsigned int)ain->nr_functions)
					continue;
				/* prev 是 SWAP 时一定是 `PUSHLOCALPAGE; SWAP; PUSH n; SWAP; ASSIGN`
				 * 里的**局部槽号**，不是 fno。 */
				if (pushes[k].prev_op == PUSH || pushes[k].prev_op == F_PUSH ||
				    pushes[k].prev_op == S_PUSH || pushes[k].prev_op == SWAP)
					continue;
				uint16_t nx = pushes[k].next_op;
				/* SWAP/DUP2/DUP_X2 不能收：`PUSH 2; SWAP; …` 这种成员号/槽号
				 * 会被误认成 fno=2，实测把一条 `local2.xxx()` 打成了
				 * `message::detail::GetReadMessageTextColor`。 */
				/* `obj.Method(localVar)` 的编码是
				 *   `PUSH <fno>; PUSHLOCALPAGE; PUSH k; REF; CALLMETHOD n`
				 * 所以 fno 后面也可能是 PUSHLOCALPAGE / PUSHGLOBALPAGE /
				 * PUSHSTRUCTPAGE。漏掉它们时 `CreateButton` 这类
				 * 「传局部/全局变量作实参」的调用一条都定不出来。 */
				if (nx != CALLMETHOD && nx != F_PUSH && nx != S_PUSH &&
				    nx != PUSH && nx != SWAP && nx != PUSHLOCALPAGE &&
				    nx != PUSHGLOBALPAGE && nx != PUSHSTRUCTPAGE)
					continue;
				hit = k;
				break;
			}
			if (hit >= 0) {
				uint32_t fun = pushes[hit].raw;
				fprintf(f, " [%u] %s @0x%08x", fun, ain->functions[fun].name,
					ain->functions[fun].address);
				n_pushes = hit;   /* 该 PUSH 及之后的压栈已被这条调用消费 */
			} else {
				fprintf(f, " ?");
			}
		} else if (opcode == CALLFUNC) {
			uint32_t fun = raw;   /* CALLFUNC 的立即数确实是函数号 */
			if (fun < (uint32_t)ain->nr_functions) {
				fprintf(f, " [%u] %s @0x%08x", fun, ain->functions[fun].name,
					ain->functions[fun].address);
			} else {
				fprintf(f, " [%u] <OUT OF RANGE, nr_functions=%d>", fun, ain->nr_functions);
			}
		}

		if (opcode == PUSH && w >= 6) {
			if (n_pushes == (int)(sizeof(pushes) / sizeof(pushes[0]))) {
				memmove(&pushes[0], &pushes[1], sizeof(pushes[0]) * 15);
				n_pushes = 15;
			}
			pushes[n_pushes].raw = raw;
			pushes[n_pushes].struct_page = (prev_opcode == PUSHSTRUCTPAGE);
			pushes[n_pushes].prev_op = prev_opcode;
			pushes[n_pushes].next_op = 0xffff;
			n_pushes++;
		}
		prev_opcode = opcode;

		fprintf(f, "\n");
		addr += instruction_width(opcode);
	}
	fflush(f);
}

/* [D397] dump 一个结构体的成员表（序号 / 名字 / 类型）。
 *
 * 为什么需要：反汇编里到处是 `this.m4.m2.m5`，而「m5 是什么」决定了
 * 「这个 lambda 挂上去以后由谁触发」。以前只能猜，猜错的代价是整条
 * 排查链建立在假前提上（TURNSTALL 这两周就是）。
 *
 * 用法：`xsystem4 --struct SceneStack` 或 `--struct 1118`。
 */
static void ain_dump_struct(FILE *f, struct ain *ain, const char *query)
{
	int no = -1;
	char *end = NULL;
	long v = strtol(query, &end, 0);
	if (end && end != query && *end == 0) {
		no = (int)v;
	} else {
		for (int i = 0; i < ain->nr_structures; i++) {
			if (ain->structures[i].name && !strcmp(ain->structures[i].name, query)) {
				no = i;
				break;
			}
		}
	}
	if (no < 0 || no >= ain->nr_structures) {
		fprintf(f, "no such struct: %s (nr_structures=%d)\n", query, ain->nr_structures);
		return;
	}
	struct ain_struct *s = &ain->structures[no];
	fprintf(f, "struct %d %s  members=%d ctor=%d dtor=%d ifaces=%d\n",
	        no, s->name, s->nr_members, s->constructor, s->destructor, s->nr_interfaces);
	for (int i = 0; i < s->nr_members; i++) {
		struct ain_variable *m = &s->members[i];
		fprintf(f, "  m%-3d %-24s %s\n", i,
		        m->name ? m->name : "?",
		        ain_variable_to_string(ain, m));
	}
}

static void ain_audit(FILE *f, struct ain *ain)
{
	init_libraries();

	for (size_t addr = 0; addr < ain->code_size;) {
		uint16_t opcode = LittleEndian_getW(ain->code, addr);
		const struct instruction *instr = &instructions[opcode];
		if (opcode >= NR_OPCODES) {
			ERROR("0x%08zx: Invalid/unknown opcode: %x", opcode);
		}
		if (!instr->implemented) {
			fprintf(f, "0x%08zx: %s (unimplemented instruction)\n", addr, instr->name);
		}
		if (opcode == CALLSYS) {
			uint32_t syscode = LittleEndian_getDW(ain->code, addr + 2);
			if (syscode >= NR_SYSCALLS) {
				ERROR("0x%08zx: CALLSYS system.(0x%x)\n", addr, syscode);
			}
			const char * const name = syscalls[syscode].name;
			if (!name) {
				fprintf(f, "0x%08zx: CALLSYS system.(0x%x)\n", addr, syscode);
			} else if (!syscalls[syscode].implemented) {
				fprintf(f, "0x%08zx: CALLSYS %s (unimplemented system call)\n", addr, name);
			}

		}
		if (opcode == CALLHLL) {
			uint32_t lib = LittleEndian_getDW(ain->code, addr+2);
			uint32_t fun = LittleEndian_getDW(ain->code, addr+6);
			if (!library_exists(lib)) {
				fprintf(f, "0x%08zx: CALLHLL %s.%s (unimplemented library)\n", addr,
					ain->libraries[lib].name, ain->libraries[lib].functions[fun].name);
			} else if (!library_function_exists(lib, fun)) {
				fprintf(f, "0x%08zx: CALLHLL %s.%s (unimplemented function)\n", addr,
					ain->libraries[lib].name, ain->libraries[lib].functions[fun].name);
			}
		}
		// TODO：审查各库的调用
		addr += instruction_width(opcode);
	}
	fflush(f);
}

static void usage(void)
{
	puts("Usage: xsystem4 [options] [inifile-or-directory]");
	puts("    -h, --help           Display this message and exit");
	puts("    -v, --version        Display the version and exit");
	puts("    -a, --audit          Audit AIN file for xsystem4 compatibility");
	puts("    -e, --echo-message   Echo in-game messages to standard output");
	puts("        --stat-method    Histogram CALLMETHOD/CALLFUNC arguments and exit");
	puts("        --struct <name|no>  Dump a structure's member table and exit");
	puts("        --dump-switch <no>  Dump a switch's case table and exit");
	puts("        --dump-delegate <no> Dump a delegate type's signature and exit");
	puts("        --dump-signatures   Dump every AIN function's arity and variable count");
	puts("        --dump-hll          Dump every AIN HLL declaration (lib, fno, name, ret, argtypes)");
	puts("        --dump-structs      Dump the struct table (index, name, nr_members)");
	puts("        --dump-strings     Dump the string table (index, text)");
	puts("        --encoding       Specify the game's character encoding. sjis (default) or gbk");
	puts("        --font-mincho    Specify the path to the mincho font to use (path[:index] for .ttc)");
	puts("        --font-gothic    Specify the path to the gothic font to use (path[:index] for .ttc)");
	puts("        --font-fnl       Specify the path to a .fnl font library to use");
	puts("        --font-x-scale   Specify the x scale for text rendering (1.0 = default scale)");
	puts("    -j, --joypad         Enable joypad");
	puts("        --msgskip-delay  Specify the delay in ms to add when skipping messages with CTRL");
	puts("        --save-folder    Override save folder location");
	puts("        --save-format    Specify the resume save file format. json (default) or rsm");
#ifdef DEBUGGER_ENABLED
	puts("        --nodebug        Disable debugger");
	puts("        --debug          Start in debugger");
	puts("        --debug-info     Specify the path to the debug information file");
#endif
}

static _Noreturn void _usage_error(const char *fmt, ...)
{
	usage();
	puts("");

	va_list ap;
	va_start(ap, fmt);
	sys_verror(fmt, ap);
}
#define usage_error(fmt, ...) _usage_error("Error: " fmt "\n", ##__VA_ARGS__)

enum {
	LOPT_HELP = 256,
	LOPT_VERSION,
	LOPT_AUDIT,
	LOPT_DISASM,
	LOPT_STAT_METHOD,
	LOPT_STRUCT,
	LOPT_DUMP_SWITCH,
	LOPT_DUMP_DELEGATE,
	LOPT_DUMP_SIGNATURES,
	LOPT_DUMP_HLL,
	LOPT_DUMP_STRUCTS,
	LOPT_DUMP_STRINGS,
	LOPT_ECHO_MESSAGE,
	LOPT_ENCODING,
	LOPT_FONT_MINCHO,
	LOPT_FONT_GOTHIC,
	LOPT_FONT_FNL,
	LOPT_FONT_X_SCALE,
	LOPT_JOYPAD,
	LOPT_MSGSKIP_DELAY,
	LOPT_SAVE_FOLDER,
	LOPT_SAVE_FORMAT,
#ifdef DEBUGGER_ENABLED
	LOPT_NODEBUG,
	LOPT_DEBUG,
	LOPT_DEBUG_API,
	LOPT_DEBUG_INFO,
#endif
};

static void error_handler(const char *msg)
{
	SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "xsystem4", msg, NULL);
}

int main(int argc, char *argv[])
{
	sys_error_handler = error_handler;

	char *ainfile;
	int err = AIN_SUCCESS;
	bool audit = false;
	bool disasm = false;
	bool stat_method = false;
	size_t disasm_start = 0, disasm_len = 0x400;
	char *encoding = NULL;
	char *struct_query = NULL;
	int dump_switch_no = -1;
	int dump_delegate_no = -1;
	bool dump_signatures = false;
	bool dump_hll = false;
	bool dump_structs = false;
	bool dump_strings = false;

	char *font_mincho = NULL;
	char *font_gothic = NULL;
	char *font_fnl = NULL;
	char *joypad = NULL;
	char *savedir = NULL;
	char *debug_info_path = NULL;

	while (1) {
		static struct option long_options[] = {
			{ "help",          no_argument,       0, LOPT_HELP },
			{ "version",       no_argument,       0, LOPT_VERSION },
			{ "audit",         no_argument,       0, LOPT_AUDIT },
			{ "disasm",        required_argument, 0, LOPT_DISASM },
			{ "stat-method",    no_argument,       0, LOPT_STAT_METHOD },
			{ "struct",        required_argument, 0, LOPT_STRUCT },
			{ "dump-switch",   required_argument, 0, LOPT_DUMP_SWITCH },
			{ "dump-delegate", required_argument, 0, LOPT_DUMP_DELEGATE },
			{ "dump-signatures", no_argument, 0, LOPT_DUMP_SIGNATURES },
			{ "dump-hll",        no_argument, 0, LOPT_DUMP_HLL },
			{ "dump-structs",    no_argument, 0, LOPT_DUMP_STRUCTS },
			{ "dump-strings",    no_argument, 0, LOPT_DUMP_STRINGS },
			{ "echo-message",  no_argument,       0, LOPT_ECHO_MESSAGE },
			{ "encoding",      required_argument, 0, LOPT_ENCODING },
			{ "font-mincho",   required_argument, 0, LOPT_FONT_MINCHO },
			{ "font-gothic",   required_argument, 0, LOPT_FONT_GOTHIC },
			{ "font-fnl",      required_argument, 0, LOPT_FONT_FNL },
			{ "font-x-scale",  required_argument, 0, LOPT_FONT_X_SCALE },
			{ "joypad",        optional_argument, 0, LOPT_JOYPAD },
			{ "msgskip-delay", required_argument, 0, LOPT_MSGSKIP_DELAY },
			{ "save-folder",   required_argument, 0, LOPT_SAVE_FOLDER },
			{ "save-format",   required_argument, 0, LOPT_SAVE_FORMAT },
#ifdef DEBUGGER_ENABLED
			{ "nodebug",       no_argument,       0, LOPT_NODEBUG },
			{ "debug",         no_argument,       0, LOPT_DEBUG },
			{ "debug-api",     no_argument,       0, LOPT_DEBUG_API },
			{ "debug-info",    required_argument, 0, LOPT_DEBUG_INFO },
#endif
			{ 0 }
		};
		int option_index = 0;
		int c = getopt_long(argc, argv, "haej::v", long_options, &option_index);
		if (c == -1)
			break;

		switch (c) {
		case 'h':
		case LOPT_HELP:
			usage();
			return 0;
		case 'v':
		case LOPT_VERSION:
			NOTICE("xsystem4 " XSYSTEM4_VERSION);
			return 0;
		case 'a':
		case LOPT_AUDIT:
			audit = true;
			break;
		case LOPT_STAT_METHOD:
			stat_method = true;
			break;
		case LOPT_STRUCT:
			struct_query = optarg;
			break;
		case LOPT_DUMP_SWITCH:
			dump_switch_no = atoi(optarg);
			break;
		case LOPT_DUMP_DELEGATE:
			dump_delegate_no = atoi(optarg);
			break;
		case LOPT_DUMP_SIGNATURES:
			dump_signatures = true;
			break;
		case LOPT_DUMP_HLL:
			dump_hll = true;
			break;
		case LOPT_DUMP_STRUCTS:
			dump_structs = true;
			break;
		case LOPT_DUMP_STRINGS:
			dump_strings = true;
			break;
		case LOPT_DISASM: {
			unsigned long ds = 0, dl = 0x400;
			sscanf(optarg, "%lx:%lx", &ds, &dl);
			disasm_start = ds;
			disasm_len = dl ? dl : 0x400;
			disasm = true;
			break;
		}
		case 'e':
		case LOPT_ECHO_MESSAGE:
			config.echo = true;
			break;
		case LOPT_ENCODING:
			encoding = optarg;
			break;
		case LOPT_FONT_MINCHO:
			font_mincho = optarg;
			break;
		case LOPT_FONT_GOTHIC:
			font_gothic = optarg;
			break;
		case LOPT_FONT_FNL:
			font_fnl = optarg;
			break;
		case LOPT_FONT_X_SCALE:
			config.manual_text_x_scale = true;
			config.text_x_scale = strtof(optarg, NULL);
			if (fabsf(config.text_x_scale) < 0.01) {
				WARNING("Invalid value for --font-x-scale option: \"%s\"", optarg);
				config.manual_text_x_scale = false;
				config.text_x_scale = 1.0;
			}
			break;
		case 'j':
		case LOPT_JOYPAD:
			joypad = optarg ? optarg : "on";
			break;
		case LOPT_MSGSKIP_DELAY:
			config.msgskip_delay = atoi(optarg);
			if (config.msgskip_delay <= 0) {
				WARNING("Invalid value for --msgskip-delay: \"%s\"", optarg);
				config.msgskip_delay = 0;
			}
			break;
		case LOPT_SAVE_FOLDER:
			savedir = optarg;
			break;
		case LOPT_SAVE_FORMAT:
			if (!strcmp(optarg, "json")) {
				config.save_format = SAVE_FORMAT_JSON;
			} else if (!strcmp(optarg, "rsm")) {
				config.save_format = SAVE_FORMAT_RSM;
			} else {
				WARNING("Invalid value for --save-format option: \"%s\"", optarg);
			}
			break;
#ifdef DEBUGGER_ENABLED
		case LOPT_NODEBUG:
			dbg_enabled = false;
			break;
		case LOPT_DEBUG:
			dbg_start_in_debugger = true;
			break;
		case LOPT_DEBUG_API:
			dbg_dap = true;
			sys_silent = true;
			break;
		case LOPT_DEBUG_INFO:
			debug_info_path = optarg;
			break;
#endif
		}
	}
	argc -= optind;
	argv += optind;

	if (argc < 1) {
		if (!config_init_with_dir(".")) {
			if (!config_init_with_dir(".."))
				usage_error("Failed to find game in current or parent directory");
		}
	} else if (argc > 1) {
		usage_error("Too many arguments");
	} else if (is_directory(argv[0])) {
		if (!config_init_with_dir(argv[0]))
			usage_error("Failed to find game in '%s'", argv[0]);
	} else if (!strcasecmp(file_extension(argv[0]), "ini")) {
		if (!config_init_with_ini(argv[0]))
			usage_error("Failed to read .ini file '%s'", argv[0]);
	} else if (!strcasecmp(file_extension(argv[0]), "ain")) {
		config_init_with_ain(argv[0]);
	} else {
		usage_error("Can't initialize game with argument '%s'", argv[0]);
	}
	ainfile = gamedir_path_icase(config.ain_filename);
	if (!ainfile)
		ERROR("AIN file not found: %s", display_sjis0(config.ain_filename));

	read_user_config();

	// 注意：有些命令行选项在这里处理，这样它们
	//       会覆盖 .xsys4rc 里的设置
	if (encoding && !set_encoding(encoding))
		WARNING("Invalid value for --encoding option: \"%s\"", encoding);
	if (font_mincho)
		config.font_paths[FONT_MINCHO] = font_mincho;
	if (font_gothic)
		config.font_paths[FONT_GOTHIC] = font_gothic;
	if (font_fnl)
		config.fnl_path = font_fnl;
	if (joypad) {
		if (!strcmp(joypad, "on"))
			config.joypad = true;
		else if (!strcmp(joypad, "off"))
			config.joypad = false;
		else
			WARNING("Invalid value for 'joypad' option (must be 'on' or 'off')");
	}
	if (savedir) {
		free(config.save_dir);
		config.save_dir = strdup(savedir);
	}
	if (!config.save_dir)
		config.save_dir = get_save_path(ini_save_folder);
	free(ini_save_folder);

	if (!(ain = ain_open(ainfile, &err))) {
		ERROR("%s", ain_strerror(err));
	}

	/* functions[0] hack 已移除：CALLMETHOD 语义修正后不再需要 */

	if (stat_method) {
		NOTICE("stat-method");
		ain_stat_method(stdout, ain);
		ain_free(ain);
		return 0;
	}

	/* [D403] `--dump-switch <no>`：把 `ain->switches[no]` 的 case 表原样倒出来。
	 * 相位状态机全是 `SWITCH <no>`，而 `--disasm` 只显示编号、不显示表，
	 * 读流程时只能猜「这个 case 对应哪个 Phase 值」。和 `--struct` 同性质。 */
/* [D466] `--dump-strings`：字符串表（索引 / 文本）。
 *
 * 为什么需要：反汇编里 `S_PUSH arg=3409` 压的是**字符串号**，
 * 而不是文本。想知道 `Activity@GetButton(S_PUSH 3409)` 取的是哪个按钮、
 * `Activity@Load(S_PUSH 3409)` 装的是哪个活动，没有这张表就只能猜 ——
 * 猜错一个，「哪个按钮是什么」就全错。文本里的空白/换行换成 `_`，
 * 保持一行一条可 grep。 */
	if (dump_strings) {
		for (int i = 0; i < ain->nr_strings; i++) {
			struct string *str = ain->strings[i];
			if (!str)
				continue;
			printf("%d\t", i);
			for (char *c = str->text; c && *c; c++)
				putchar(*c == '\n' || *c == '\t' || *c == ' ' ? '_' : *c);
			putchar('\n');
		}
		ain_free(ain);
		return 0;
	}

	/* [D453] `--dump-structs`：结构表（索引 / 名字 / 成员数）。
	 *
	 * 为什么需要：反汇编里 `NEW <n>` 的操作数是**结构索引**，不是函数号。
	 * 想知道 `Scenes::RunIScene(-1, dg{ NEW 786 })` 到底 new 了哪个活动类，
	 * 没有这张表就只能猜 —— 而猜错一个，整条「哪个场景被创建」的结论就废了。
	 */
	if (dump_structs) {
		/* [D496] 第 4 列起补**成员类型表**（名字 + 类型码），与
		 * --dump-hll 的参数类型列同理：存档序列化（savedata.c）判断
		 * "泛型数组能不能从声明侧定型"时，需要知道 AIN 到底把成员
		 * 声明成了 array<int> 还是 array<?>。只打类型码够用，
		 * 结构体成员的 struc 另走 --dump-structs 的行号查。 */
		for (int i = 0; i < ain->nr_structures; i++) {
			struct ain_struct *st = &ain->structures[i];
			printf("%d\t%d\t%s", i, st->nr_members,
			       st->name ? st->name : "");
			for (int j = 0; j < st->nr_members; j++)
				printf("\t%s:%d", st->members[j].name ? st->members[j].name : "",
				       (int)st->members[j].type.data);
			putchar('\n');
		}
		ain_free(ain);
		return 0;
	}

	/* [D443] `--dump-hll`：AIN 侧**声明**的每个 HLL 函数（库名 / 库内序号 /
	 * 名字 / 实参个数 / 返回类型）。用途是跟 C 侧导出表做交叉核对：
	 *
	 * `HLL_TODO_EXPORT` 只是"这个名字我没有实现"，它不能区分下面两种完全不同的情况：
	 *   ① 引擎真的没写这个功能；
	 *   ② 功能写了，但**导出名和 AIN 里的名字对不上**（D164 的 `Parts_SetNumeral*`
	 *      就是这样：以为实现的都是死条目，真被调的还在未实现清单里）。
	 * 有了这份声明表 + `nm`/源码里的导出名，②类可以直接扫出来。
	 *
	 * [D493] 第 6 列补**参数类型表**（`ain_hll_argument.type`）。第 3 列只有
	 * 实参个数，分不出 `string` 和 `ref string` —— D459 当年就是因为这个
	 * 分不清，把 HTTPDownloader 四条编码函数晾着不敢写。声明表里类型是现成的，
	 * 打出来就不用再猜字节码形状了。 */
	if (dump_hll) {
		for (int i = 0; i < ain->nr_libraries; i++) {
			struct ain_library *lib = &ain->libraries[i];
			for (int j = 0; j < lib->nr_functions; j++) {
				struct ain_hll_function *f = &lib->functions[j];
				printf("%s\t%d\t%d\t%s\t%d", lib->name, j,
				       (int)f->nr_arguments, f->name ? f->name : "",
				       (int)f->return_type.data);
				for (int k = 0; k < f->nr_arguments; k++)
					printf("\t%d", (int)f->arguments[k].type.data);
				putchar('\n');
			}
		}
		ain_free(ain);
		return 0;
	}

	/* [D438] `--dump-signatures`：每个 AIN 函数的 (fno, nr_args, nr_vars, 名字)。
	 *
	 * 为什么需要：读 `CALLFUNC <fno>` 时，反汇编里**没有实参个数**，而
	 * `function_call()` 是按 `function_argument_slots(f)` 弹栈的。不知道 arity，
	 * 静态还原调用就只能猜 —— 猜错一个，后面整段伪代码的栈全是歪的。
	 * 而 AIN 表里 `nr_args` / `nr_vars` 是现成的，直接导出来，让离线工具
	 * 有确定信息可用。 */
	if (dump_signatures) {
		for (int i = 0; i < ain->nr_functions; i++) {
			struct ain_function *fn = &ain->functions[i];
			char *nm = fn->name;
			/* 名字里有空格会破坏 "列" 的解析，换成下划线。 */
			if (nm) {
				for (char *c = nm; *c; c++)
					if (*c == ' ' || *c == '\t' || *c == '\n')
						*c = '_';
			}
			printf("%d\t%d\t%d\t%s\n", i, fn->nr_args, fn->nr_vars,
			       nm ? nm : "");
		}
		ain_free(ain);
		return 0;
	}

	/* [D428] `--dump-delegate <no>`：委托类型签名。
	 * `DG_CALLBEGIN n` 用 `delegate_argument_slots()` 从栈上取 dg_page，
	 * 槽数取决于每个参数占几槽（AIN_IFACE/WRAP/OPTION 占两槽）。
	 * 不知道签名时读 DG_CALLBEGIN 的栈布局只能猜。 */
	if (dump_delegate_no >= 0) {
		if (dump_delegate_no >= ain->nr_delegates) {
			fprintf(stderr, "delegate %d out of range (nr=%d)\n",
			        dump_delegate_no, ain->nr_delegates);
		} else {
			struct ain_function_type *dg = &ain->delegates[dump_delegate_no];
			int slots = 0, args = 0;
			printf("delegate %d: %s  ret=%d args=%d vars=%d\n",
			       dump_delegate_no, dg->name ? dg->name : "?",
			       (int)dg->return_type.data, dg->nr_arguments,
			       dg->nr_variables);
			while (args < dg->nr_arguments && slots < dg->nr_variables) {
				enum ain_data_type t = dg->variables[slots].type.data;
				printf("  arg%-2d slot%-2d type=%-2d %-16s struc=%d rank=%d\n",
				       args, slots, (int)t,
				       dg->variables[slots].name ? dg->variables[slots].name : "",
				       dg->variables[slots].type.struc,
				       dg->variables[slots].type.rank);
				args++;
				slots++;
				if (t == AIN_IFACE || t == AIN_WRAP || t == AIN_OPTION)
					while (slots < dg->nr_variables &&
					       dg->variables[slots].type.data == AIN_VOID)
						slots++;
			}
			printf("  => delegate_argument_slots = %d\n", slots);
		}
		ain_free(ain);
		return 0;
	}

	if (dump_switch_no >= 0) {
		if (dump_switch_no >= ain->nr_switches) {
			fprintf(stderr, "switch %d out of range (nr=%d)\n",
			        dump_switch_no, ain->nr_switches);
		} else {
			struct ain_switch *sw = &ain->switches[dump_switch_no];
			printf("switch %d: type=%d cases=%d default=0x%x\n",
			       dump_switch_no, (int)sw->case_type, sw->nr_cases,
			       sw->default_address);
			for (int i = 0; i < sw->nr_cases; i++)
				printf("  case %-8d -> 0x%06x\n",
				       sw->cases[i].value, sw->cases[i].address);
		}
		ain_free(ain);
		return 0;
	}

	if (struct_query) {
		/* [D397] 只读 ain 结构，和 --disasm 一样不需要 init_libraries()。 */
		ain_dump_struct(stdout, ain, struct_query);
		ain_free(ain);
		return 0;
	}

	if (disasm) {
		/* 反汇编只读 ain 结构，不需要 init_libraries()（那会连带加载 .ex 文件） */
		NOTICE("disasm 0x%08zx..0x%08zx  code_size=0x%zx nr_functions=%d",
		       disasm_start, disasm_start + disasm_len, ain->code_size,
		       ain->nr_functions);
		ain_disasm(stdout, ain, disasm_start, disasm_len);
		ain_free(ain);
		return 0;
	}

	if (audit) {
		ain_audit(stdout, ain);
		ain_free(ain);
		return 0;
	}

	mkdir_p(config.save_dir);
	apply_game_specific_hacks(ain);
	if (config.msgskip_delay)
		set_msgskip_delay(ain, config.msgskip_delay);
	asset_manager_init();
	dbg_init(debug_info_path);
	sys_exit(vm_execute_ain(ain));
}
