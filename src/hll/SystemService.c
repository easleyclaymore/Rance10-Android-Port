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

#include <string.h>
#include <stdlib.h>
#include <assert.h>

#include "system4/ain.h"
#include "system4/string.h"
#include "system4/utfsjis.h"

#include "cJSON.h"
#include "input.h"
#include "gfx/gfx.h"
#include "mixer.h"
#include "savedata.h"
#include "vm.h"
#include "xsystem4.h"
#include "hll.h"
#include "system4/file.h"
#include "pal.h"
#include <SDL3/SDL.h>

/* [D444] `string SystemService.GetOSInfo()`（AIN 库 SystemService f[60]，
 * args=0 / ret=12=AIn_STRING）。
 *
 * 注意形状与 `PCInfo.GetOSInfo` **不同**：那个是 `void GetOSInfo(ref string)`，
 * 靠出参回填；这个是 0 参直返。所以不能复用 PCInfo 的那个函数，得单独包一层。
 * 底层数据同一个来源：`pal_get()->info->os_description()`。
 *
 * 实现前它是未实现导出，启动时每次刷一条
 * `Unimplemented library function: SystemService.GetOSInfo`。 */
static struct string *SystemService_GetOSInfo(void)
{
	const char *desc = pal_get()->info->os_description();
	if (!desc)
		desc = "";
	return make_string(desc, strlen(desc));
}

/* [D495] `void SystemService.GetCPUInfo(ref string desc, ref int clock,
 * ref int cores, ref string detail)`（AIN f[58]，dump-hll 类型表
 * `(20,18,18,20)` / ret void）。
 *
 * 消费点：`pcinfo::GetCPUInfo(16914)` 包装 → `AFL_PCInfo_GetCPUInfo(16909)`
 * → `enquate::detail::CSystemInfo@SetInfoList` —— 设置菜单"系统信息"页的
 * CPU 显示行（包装函数返回的是第 4 个出参 `detail`）。
 * 数据源 /proc/cpuinfo：Linux 桌面常规来源，离线可写。
 * 形状判据：`ref int` 出参在 C 侧是普通指针（D202 运行期验证先例），
 * `ref string` 是 `struct string **`（ffi.c 写回 heap 槽）。 */
static void SystemService_GetCPUInfo(struct string **desc, int *clock_mhz,
				     int *nr_cores, struct string **detail)
{
	*desc = make_string("", 0);
	*clock_mhz = 0;
	*nr_cores = 0;
	*detail = make_string("", 0);

	FILE *fp = fopen("/proc/cpuinfo", "r");
	if (!fp)
		return;
	char model[256] = "";
	float mhz = 0;
	int cores = 0;
	char line[512];
	while (fgets(line, sizeof(line), fp)) {
		if (!model[0] && sscanf(line, "model name : %[^\n]", model) == 1)
			continue;
		if (mhz == 0 && sscanf(line, "cpu MHz : %f", &mhz) == 1)
			continue;
		if (cores == 0 && sscanf(line, "cpu cores : %d", &cores) == 1)
			continue;
		if (!cores && sscanf(line, "processor : %d", &cores) == 1)
			cores++; /* 没列 cores 的平台按 processor 计数兜底 */
	}
	fclose(fp);

	if (!model[0])
		strncpy(model, "unknown CPU", sizeof(model) - 1);
	if (cores <= 0)
		cores = 1;

	char buf[512];
	if (mhz > 0)
		snprintf(buf, sizeof(buf), "%s x%d @ %.0fMHz", model, cores, mhz);
	else
		snprintf(buf, sizeof(buf), "%s x%d", model, cores);

	free_string(*desc);
	*desc = make_string(model, strlen(model));
	*clock_mhz = (int)(mhz > 0 ? mhz : 0);
	*nr_cores = cores;
	free_string(*detail);
	*detail = make_string(buf, strlen(buf));
}

/* [D495] `string SystemService.GetScreenInfo()`（AIN f[61]，args=0 / ret
 * string）。同页"画面"显示行：桌面显示器分辨率。 */
static struct string *SystemService_GetScreenInfo(void)
{
	SDL_Rect bounds;
	if (!SDL_GetDisplayBounds(SDL_GetPrimaryDisplay(), &bounds))
		return make_string("unknown", 7);
	char buf[64];
	int n = snprintf(buf, sizeof(buf), "%dx%d", bounds.w, bounds.h);
	return make_string(buf, n > 0 ? (size_t)n : 7);
}

static int SystemService_GetMixerName(int n, struct string **name)
{
	const char *r = mixer_get_name(n);
	if (!r)
		return 0;
	*name = make_string(r, strlen(r));
	return 1;
}

static bool SystemService_GetMixerDefaultVolume(int n, int *volume)
{
	if (n < 0 || (unsigned)n >= config.mixer_nr_channels)
		return false;
	*volume = config.mixer_volumes[n];
	return true;
}

static bool SystemService_SetMixerName(int n, struct string *name)
{
	return mixer_set_name(n, name->text);
}

static int SystemService_GetGameVersion(void)
{
	return ain->game_version;
}

/* AIN f[1]：string GetGameName() —— v12 直接返回字符串，
 * 老那套出参形式是遗留物（D62）。 */
static struct string *SystemService_GetGameName(void)
{
	return cstr_to_string(config.game_name ? config.game_name : "");
}

HLL_WARN_UNIMPLEMENTED(false, bool, SystemService, AddURLMenu, struct string *title, struct string *url);

static bool SystemService_IsFullScreen(void)
{
	return gfx_is_fullscreen();
}

static bool SystemService_ChangeNormalScreen(void)
{
	return gfx_set_fullscreen(false);
}

static bool SystemService_ChangeFullScreen(void)
{
	return gfx_set_fullscreen(true);
}

HLL_WARN_UNIMPLEMENTED(false, bool, SystemService, InitMainWindowPosAndSize);

//static bool SystemService_UpdateView(void);
HLL_QUIET_UNIMPLEMENTED(false, bool, SystemService, UpdateView);

static int SystemService_GetViewWidth(void)
{
	return config.view_width;
}

static int SystemService_GetViewHeight(void)
{
	return config.view_height;
}

static bool SystemService_MoveMouseCursorPosImmediately(int x, int y)
{
	mouse_set_pos(x, y);
	return true;
}

static bool SystemService_SetHideMouseCursorByGame(bool hide)
{
	return mouse_show_cursor(!hide);
}

//bool SystemService_GetHideMouseCursorByGame(void);
//bool SystemService_GetUsePower2Texture(void);
//int SystemService_GetAntiAliasingMode(void);

enum window_settings_asect_ratio {
	ASPECT_RATIO_NORMAL,
	ASPECT_RATIO_FIXED
};

enum window_settings_scaling_type {
	SCALING_NORMAL,
	SCALING_BICUBIC,
};

struct window_settings {
	enum window_settings_asect_ratio aspect_ratio;
	enum window_settings_scaling_type scaling_type;
	bool wait_vsync;
	bool record_pos_size;
	bool minimize_by_full_screen_inactive;
	bool back_to_title_confirm;
	bool close_game_confirm;
};

static struct window_settings window_settings = {
	.aspect_ratio = ASPECT_RATIO_NORMAL,
	.scaling_type = SCALING_NORMAL,
	.wait_vsync = false,
	.record_pos_size = false,
	.minimize_by_full_screen_inactive = true,
	.back_to_title_confirm = true,
	.close_game_confirm = true,
};

enum window_settings_id {
	WINDOW_SETTINGS_ASPECT_RATIO = 0,
	WINDOW_SETTINGS_SCALING_TYPE = 1,
	WINDOW_SETTINGS_WAIT_VSYNC = 2,
	WINDOW_SETTINGS_RECORD_POS_SIZE = 3,
	WINDOW_SETTINGS_MINIMIZE_BY_FULL_SCREEN_INACTIVE = 4,
	WINDOW_SETTINGS_BACK_TO_TITLE_CONFIRM = 5,
	WINDOW_SETTINGS_CLOSE_GAME_CONFIRM = 6,
};

static void save_window_settings(void)
{
	cJSON *root = cJSON_CreateObject();
	cJSON_AddBoolToObject(root, "wait_vsync", window_settings.wait_vsync);
	save_json("WindowSetting.json", root);
	cJSON_Delete(root);
}

static void load_window_settings(void)
{
	cJSON *root = load_json("WindowSetting.json");
	if (!root)
		return;
	cJSON *v;
	if ((v = cJSON_GetObjectItem(root, "wait_vsync"))) {
		window_settings.wait_vsync = cJSON_IsTrue(v);
		gfx_set_wait_vsync(window_settings.wait_vsync);
	}
	cJSON_Delete(root);
}

static bool SystemService_SetWindowSetting(int type, int value)
{
	switch (type) {
	case WINDOW_SETTINGS_ASPECT_RATIO:
		window_settings.aspect_ratio = value;
		break;
	case WINDOW_SETTINGS_SCALING_TYPE:
		window_settings.scaling_type = value;
		break;
	case WINDOW_SETTINGS_WAIT_VSYNC:
		window_settings.wait_vsync = value;
		gfx_set_wait_vsync(value);
		break;
	case WINDOW_SETTINGS_RECORD_POS_SIZE:
		window_settings.record_pos_size = value;
		break;
	case WINDOW_SETTINGS_MINIMIZE_BY_FULL_SCREEN_INACTIVE:
		window_settings.minimize_by_full_screen_inactive = value;
		break;
	case WINDOW_SETTINGS_BACK_TO_TITLE_CONFIRM:
		window_settings.back_to_title_confirm = value;
		break;
	case WINDOW_SETTINGS_CLOSE_GAME_CONFIRM:
		window_settings.close_game_confirm = value;
		break;
	default:
		WARNING("Invalid window setting type: %d", type);
		return false;
	}
	save_window_settings();
	return true;
}

static bool SystemService_GetWindowSetting(int type, int *value)
{
	switch (type) {
	case WINDOW_SETTINGS_ASPECT_RATIO:
		*value = window_settings.aspect_ratio;
		break;
	case WINDOW_SETTINGS_SCALING_TYPE:
		*value = window_settings.scaling_type;
		break;
	case WINDOW_SETTINGS_WAIT_VSYNC:
		*value = window_settings.wait_vsync;
		break;
	case WINDOW_SETTINGS_RECORD_POS_SIZE:
		*value = window_settings.record_pos_size;
		break;
	case WINDOW_SETTINGS_MINIMIZE_BY_FULL_SCREEN_INACTIVE:
		*value = window_settings.minimize_by_full_screen_inactive;
		break;
	case WINDOW_SETTINGS_BACK_TO_TITLE_CONFIRM:
		*value = window_settings.back_to_title_confirm;
		break;
	case WINDOW_SETTINGS_CLOSE_GAME_CONFIRM:
		*value = window_settings.close_game_confirm;
		break;
	default:
		WARNING("Invalid window setting type: %d", type);
		return false;
	}
	return true;
}

// XXX：Haru Urare 里 type 大于 1 就是非法值，其他游戏可能不同
#define NR_MOUSE_CURSOR_CONFIG 1
static int mouse_cursor_config[NR_MOUSE_CURSOR_CONFIG] = {0};

static bool SystemService_SetMouseCursorConfig(int type, int value)
{
	if (type < 0 || type >= NR_MOUSE_CURSOR_CONFIG) {
		WARNING("Invalid mouse cursor config type: %d", type);
		return false;
	}
	mouse_cursor_config[type] = value;
	return true;
}

static bool SystemService_GetMouseCursorConfig(int type, int *value)
{
	if (type < 0 || type >= NR_MOUSE_CURSOR_CONFIG) {
		WARNING("Invalid mouse cursor config type: %d", type);
		return false;
	}
	// XXX：这里返回的值永远是 1 或 0
	*value = !!mouse_cursor_config[type];
	return true;
}

//bool SystemService_RunProgram(struct string *program_file_name, struct string *parameter);
//bool SystemService_IsOpenedMutex(struct string *mutex_name);

void SystemService_GetGameFolderPath(struct string **folder_path)
{
	char *sjis = utf2sjis(config.game_dir, 0);
	*folder_path = make_string(sjis, strlen(sjis));
	free(sjis);
}

static void SystemService_GetTime(int *hour, int *min, int *sec)
{
	int ms;
	get_time(hour, min, sec, &ms);
}

static bool SystemService_IsResetOnce(void)
{
	return vm_reset_once;
}

static bool SystemService_IsResetOnce_Drapeko(struct string **text)
{
	*text = cstr_to_string("XXX TTT YYY"); // ???
	return vm_reset_once;
}

static char *get_manual_filename(void) {
	char *manual_path = path_join("Manual", "index.html");
	char *file_path = path_join(config.game_dir, manual_path);
	free(manual_path);
	return file_path;
}

static bool SystemService_IsExistPlayingManual(void) {
	char *filename = get_manual_filename();
	const bool exists = file_exists(filename);
	free(filename);
	return exists;
}

#ifndef _WIN32
static char *percent_encode(const char *str) {
	const char *hex = "0123456789ABCDEF";
	// 最坏情况是所有字符都被百分号编码
	char *encoded = xmalloc(strlen(str) * 3 + 1);

	char *p = encoded;
	while (*str) {
		const unsigned char c = *str++;
		if ((c >= 'a' && c <= 'z') ||
			(c >= 'A' && c <= 'Z') ||
			(c >= '0' && c <= '9') ||
			c == '-' || c == '_' || c == '.' || c == '~' || c == '/') {
			*p++ = (char)c;
		} else {
			*p++ = '%';
			*p++ = hex[c >> 4];
			*p++ = hex[c & 15];
		}
	}

	*p = '\0';
	return encoded;
}
#endif

static void SystemService_OpenPlayingManual(void) {
	if (!SystemService_IsExistPlayingManual()) {
		return;
	}
#ifdef __ANDROID__
	const int COMMAND_OPEN_PLAYING_MANUAL = 0x8000;
	SDL_AndroidSendMessage(COMMAND_OPEN_PLAYING_MANUAL, 0);
#else
	char *filename = get_manual_filename();

	char *real_path = realpath_utf8(filename);
	free(filename);
	if (!real_path) {
		return;
	}

#ifdef _WIN32
	const char *prefix = "file:///";
	char *path_component = real_path;
#else
	const char *prefix = "file://";
	char *path_component = percent_encode(real_path);
	free(real_path);
#endif
	char *url = xmalloc(strlen(prefix) + strlen(path_component) + 1);
	strcpy(url, prefix);
	strcat(url, path_component);

	if (SDL_OpenURL(url) < 0) {
		WARNING("Failed to open manual at '%s': %s", url, SDL_GetError());
	}

	free(url);
	free(path_component);
#endif
}

/* [D395] 系统消息队列。
 *
 * 语义：引擎向游戏推送的**系统级事件**（窗口失焦/关闭、音量被系统改、断电告警…），
 * AIN 侧用 `while (IsExistSystemMessage()) { PopSystemMessage(&m); … }` 轮询。
 *
 * 本引擎目前**不产生任何系统消息**（SDL 事件全部自己消费掉，没有映射成
 * System4 的消息码）—— 所以队列恒空、`IsExist` 恒 false。这不是桩：
 * 「没有消息」就是事实。队列和 push 接口是真的，将来要把 SDL 的 QUIT /
 * FOCUS_LOST 之类映射进来时，只需要在引擎事件处调 `ss_push_system_message()`，
 * AIN 侧不用改。
 *
 * 消息码沿用 System4 的约定（1=窗口关闭、2=失焦……），具体码值没有公开文档，
 * 所以第一版不预先定义常量，push 方按需给。
 */
struct ss_sysmsg { int code; char *arg; };
#define SS_SYSMSG_MAX 64
static struct ss_sysmsg ss_msgs[SS_SYSMSG_MAX];
static int ss_msg_head, ss_msg_count;

/* 供引擎事件处调用（当前无调用者，见上面的说明）。 */
void ss_push_system_message(int code, const char *arg)
{
	if (ss_msg_count >= SS_SYSMSG_MAX)
		return;
	int i = (ss_msg_head + ss_msg_count) % SS_SYSMSG_MAX;
	ss_msgs[i].code = code;
	free(ss_msgs[i].arg);
	ss_msgs[i].arg = arg ? strdup(arg) : NULL;
	ss_msg_count++;
}

static bool SystemService_IsExistSystemMessage(void)
{
	return ss_msg_count > 0;
}

/* `bool PopSystemMessage(int *message)`：取最早一条的码，队列空则返回 false。 */
static bool SystemService_PopSystemMessage(int *message)
{
	if (ss_msg_count <= 0)
		return false;
	if (message)
		*message = ss_msgs[ss_msg_head].code;
	free(ss_msgs[ss_msg_head].arg);
	ss_msgs[ss_msg_head].arg = NULL;
	ss_msg_head = (ss_msg_head + 1) % SS_SYSMSG_MAX;
	ss_msg_count--;
	return true;
}

/* `PopSystemMessageString()`：取最早一条的附加串（队列空返回空串）。
 * 签名按「无参、返回 string」推断 —— 表里原来连项都没有，没有可对照的注释。 */
static struct string *SystemService_PopSystemMessageString(void)
{
	if (ss_msg_count <= 0)
		return string_dup(&EMPTY_STRING);
	const char *arg = ss_msgs[ss_msg_head].arg;
	struct string *r = arg ? cstr_to_string(arg) : string_dup(&EMPTY_STRING);
	free(ss_msgs[ss_msg_head].arg);
	ss_msgs[ss_msg_head].arg = NULL;
	ss_msg_head = (ss_msg_head + 1) % SS_SYSMSG_MAX;
	ss_msg_count--;
	return r;
}

static void SystemService_RestrainScreensaver(void) { }

//static int SystemService_Debug_GetUseVideoMemorySize(void);

// 兰斯01
static void SystemService_Rance0123456789(struct string **text)
{
	*text = cstr_to_string("-RANCE010ECNAR-");
}

// 兰斯01 体验版
static void SystemService_XXXXX01XXXXXXXX(struct string **text)
{
	*text = cstr_to_string("RANCE01RANCEKAKKOII");
}

// Drapeko
static void SystemService_Test(struct string **text)
{
	*text = cstr_to_string("DELETE ALL 758490275489207548093");
}

// 《Drapeko》体验版
static void SystemService_DRPKT(struct string **text)
{
	*text = cstr_to_string("DRPKT QWERTY NUFUAUEO 75849027582754829");
}

// 兰斯9
static void SystemService_Rance96161988(struct string **text) {
	*text = cstr_to_string("=Rance99/RANCE99=");
}

// 《Pascha3 Plus Contents》
static void SystemService_XXX(struct string **text) {
	*text = cstr_to_string("FORMAT HDD ERASE 578205024758284076520478254092784789752384758204687293");
}

// 《武装少女 Blade Briders》
static void SystemService_GetSystemBufferName(struct string **text) {
	*text = cstr_to_string("xxx4wxx:xxxx-xxxxxfx5x3");
}

static void SystemService_PreLink(void);

static void SystemService_ModuleInit(void)
{
	load_window_settings();
}

/* AIN f[11]/f[12]：游戏视图缩放率。xsystem4 只有一个固定的
 * 游戏视图，所以保留 SetGameViewScaleRate 写进来的值。 */
static float game_view_scale_rate = 1.0f;

static void SystemService_SetGameViewScaleRate(float rate)
{
	game_view_scale_rate = rate;
}

static float SystemService_GetGameViewScaleRate(void)
{
	return game_view_scale_rate;
}

/* AIN f[34]：string GetGameVersionByText()。这个移植里没有装载
 * 版本表；返回一个非空占位串，好让调用方能格式化它。 */
static struct string *SystemService_GetGameVersionByText(void)
{
	return cstr_to_string("00sdf00000f29fo0000000sdhf1");
}

/* AIN SystemService.GameVariable_*：第一阶段。移植里没有持久化的
 * 游戏变量，所以 IsExist 恒为 false，Get* 返回默认值（D80）。 */
/* AIN f[50]：void ShowWaitMessage(bool)。这个移植不画等待遮罩。 */
static void SystemService_ShowWaitMessage(int show)
{
	(void)show;
}
/* AIN SystemService f[7] GetDefaultViewWidth。[HACK-POC D91] Android/SystemService 占位桩。 */
/* [D394] 原来恒 0。`GetDefaultViewWidth` 是「基准视图宽度」，AIN 用它算
 * 缩放比；返回 0 会让整屏按 0 尺寸排版。用 config 的视图尺寸（启动时由
 * `--view-width` / ini 定下），语义上就是「默认值」。 */
static int SS_Stub_GetDefaultViewWidth(void)
{
	return config.view_width;
}

/* AIN SystemService f[8] GetDefaultViewHeight。[HACK-POC D91] Android/SystemService 占位桩。 */
static int SS_Stub_GetDefaultViewHeight(void)
{
	return config.view_height;
}

/* AIN SystemService f[13] SetViewSize。[HACK-POC D91] Android/SystemService 占位桩。 */
static void SS_Stub_SetViewSize(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SystemService f[14] SetWindowPos。[HACK-POC D91] Android/SystemService 占位桩。 */
static void SS_Stub_SetWindowPos(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SystemService f[15] GetWindowPosX。[HACK-POC D91] Android/SystemService 占位桩。 */
static int SS_Stub_GetWindowPosX(void)
{

	return 0;
}

/* AIN SystemService f[16] GetWindowPosY。[HACK-POC D91] Android/SystemService 占位桩。 */
static int SS_Stub_GetWindowPosY(void)
{

	return 0;
}

/* AIN SystemService f[19] SetViewResizableMode。[HACK-POC D91] Android/SystemService 占位桩。 */
static void SS_Stub_SetViewResizableMode(int a0)
{
	(void)a0;
}

/* AIN SystemService f[20] IsViewResizableMode。[HACK-POC D91] Android/SystemService 占位桩。 */
static int SS_Stub_IsViewResizableMode(void)
{

	return 0;
}

/* AIN SystemService f[21] SaveScreenshot。[HACK-POC D91] Android/SystemService 占位桩。 */
static int SS_Stub_SaveScreenshot(struct string * a0, int a1, int a2, int a3, int a4)
{
	(void)a0;
	(void)a1;
	(void)a2;
	(void)a3;
	(void)a4;
	return 0;
}

/* AIN SystemService f[24] SetWaitVSyncMode。[HACK-POC D91] Android/SystemService 占位桩。 */
static int SS_Stub_SetWaitVSyncMode(int a0)
{
	(void)a0;
	return 0;
}

/* AIN SystemService f[25] GetWaitVSyncMode。[HACK-POC D91] Android/SystemService 占位桩。 */
static int SS_Stub_GetWaitVSyncMode(void)
{

	return 0;
}

/* [D394] 成对的 set/get：原来 `SetUsePower2Texture` 是 `HLL_WARN_UNIMPLEMENTED(false,…)`
 * —— **返回 false 告诉 AIN「设置失败」**，于是 AIN 会走降级分支（换纹理规格）；
 * `SetAntiAliasingMode` 则返回 true 但什么都不存，`Get` 还是未注册。
 *
 * 这类「假成功 / 假失败」比空桩更坏：它让 AIN 的判定**看起来成立**，
 * 但状态是假的。这里改成真的存一个值，让 Get 能原样取回。
 *
 * 两个开关对我们没有实际渲染效果（引擎不做 MSAA、纹理规格由我们自己管），
 * 所以「接受设置、记住值、返回成功」就是正确语义 —— 不是 hack。 */
static bool ss_use_power2 = false;
static int ss_aa_mode = 0;

static bool SystemService_SetUsePower2Texture(bool use)
{
	ss_use_power2 = use;
	return true;
}

static bool SystemService_GetUsePower2Texture(void)
{
	return ss_use_power2;
}

static bool SystemService_SetAntiAliasingMode(int mode)
{
	ss_aa_mode = mode;
	return true;
}

static int SystemService_GetAntiAliasingMode(void)
{
	return ss_aa_mode;
}

/* [D393] SystemService 的变量系统（GameVariable / SystemVariable）。
 *
 * 现状：D91 时代这两个族全是空桩（`Set` 丢弃写入、`Get`/`GetKey` 干脆没注册），
 * 于是「写进去再读出来」永远是空 —— 任何依赖它们的 AIN 逻辑都会**静默走错**
 * （不报错，只是拿不到值），这类缺口比崩溃难查得多。
 *
 * 语义（六个函数是一组标准字典 API，从成对调用点推出）：
 *   Set(key, value)  存
 *   Get(key)         取；不存在时返回**空串**（AIN 侧直接当字符串用，不能给 NULL）
 *   GetKey(index)    按**插入序**取第 index 个键（配 NumofKey 做遍历）
 *   NumofKey()       键数
 *   Erase(key)       删
 *   IsExist(key)     判断
 *
 * 两套独立：GameVariable 属于当前游戏（将来要随存档走），SystemVariable 属于引擎
 * （跨存档）。第一版都放内存；GameVariable 的持久化等 gsave 侧接入。
 * 键值都是 AIN 的 GBK 字节串，按字节比较即可。
 */
struct ss_var { char *key; char *value; };
struct ss_var_table { struct ss_var *e; int n, cap; };

static struct ss_var_table ss_sysvar, ss_gamevar;

static int ss_var_find(struct ss_var_table *t, const char *key)
{
	for (int i = 0; i < t->n; i++)
		if (!strcmp(t->e[i].key, key))
			return i;
	return -1;
}

static void ss_var_set(struct ss_var_table *t, const char *key, const char *value)
{
	if (!key || !*key)
		return;
	int i = ss_var_find(t, key);
	if (i < 0) {
		if (t->n == t->cap) {
			t->cap = t->cap ? t->cap * 2 : 16;
			t->e = xrealloc(t->e, t->cap * sizeof(*t->e));
		}
		i = t->n++;
		t->e[i].key = strdup(key);
		t->e[i].value = NULL;
	}
	free(t->e[i].value);
	t->e[i].value = strdup(value ? value : "");
}

static const char *ss_var_get(struct ss_var_table *t, const char *key)
{
	if (!key || !*key)
		return NULL;
	int i = ss_var_find(t, key);
	return i < 0 ? NULL : t->e[i].value;
}

static const char *ss_var_get_key(struct ss_var_table *t, int index)
{
	if (index < 0 || index >= t->n)
		return NULL;
	return t->e[index].key;
}

static void ss_var_erase(struct ss_var_table *t, const char *key)
{
	int i = (key && *key) ? ss_var_find(t, key) : -1;
	if (i < 0)
		return;
	free(t->e[i].key);
	free(t->e[i].value);
	memmove(&t->e[i], &t->e[i + 1], (t->n - i - 1) * sizeof(*t->e));
	t->n--;
}

/* --- SystemVariable：引擎级 --- */
static bool SS_SystemVariable_IsExist(struct string *k)
{
	return ss_var_get(&ss_sysvar, k ? k->text : NULL) != NULL;
}

static void SS_SystemVariable_Set(struct string *k, struct string *v)
{
	ss_var_set(&ss_sysvar, k ? k->text : NULL, v ? v->text : "");
}

static struct string *SS_SystemVariable_Get(struct string *k)
{
	const char *v = ss_var_get(&ss_sysvar, k ? k->text : NULL);
	return v ? cstr_to_string(v) : string_dup(&EMPTY_STRING);
}

static struct string *SS_SystemVariable_GetKey(int index)
{
	const char *k = ss_var_get_key(&ss_sysvar, index);
	return k ? cstr_to_string(k) : string_dup(&EMPTY_STRING);
}

static int SS_SystemVariable_NumofKey(void)
{
	return ss_sysvar.n;
}

static void SS_SystemVariable_Erase(struct string *k)
{
	ss_var_erase(&ss_sysvar, k ? k->text : NULL);
}

/* --- GameVariable：当前游戏（将来随存档） --- */
static bool SS_GameVariable_IsExist(struct string *k)
{
	return ss_var_get(&ss_gamevar, k ? k->text : NULL) != NULL;
}

static void SS_GameVariable_Set(struct string *k, struct string *v)
{
	ss_var_set(&ss_gamevar, k ? k->text : NULL, v ? v->text : "");
}

static struct string *SS_GameVariable_Get(struct string *k)
{
	const char *v = ss_var_get(&ss_gamevar, k ? k->text : NULL);
	return v ? cstr_to_string(v) : string_dup(&EMPTY_STRING);
}

static struct string *SS_GameVariable_GetKey(int index)
{
	const char *k = ss_var_get_key(&ss_gamevar, index);
	return k ? cstr_to_string(k) : string_dup(&EMPTY_STRING);
}

static int SS_GameVariable_NumofKey(void)
{
	return ss_gamevar.n;
}

static void SS_GameVariable_Erase(struct string *k)
{
	ss_var_erase(&ss_gamevar, k ? k->text : NULL);
}

/* AIN SystemService f[35] SystemVariable_IsExist。[HACK-POC D91] Android/SystemService 占位桩。 */

/* AIN SystemService f[36] SystemVariable_Set。[HACK-POC D91] Android/SystemService 占位桩。 */

/* AIN SystemService f[38] SystemVariable_NumofKey。[HACK-POC D91] Android/SystemService 占位桩。 */

/* AIN SystemService f[40] SystemVariable_Erase。[HACK-POC D91] Android/SystemService 占位桩。 */

/* AIN SystemService f[42] GameVariable_Set。[HACK-POC D91] Android/SystemService 占位桩。 */

/* AIN SystemService f[44] GameVariable_NumofKey。[HACK-POC D91] Android/SystemService 占位桩。 */

/* AIN SystemService f[46] GameVariable_Erase。[HACK-POC D91] Android/SystemService 占位桩。 */

/* AIN SystemService f[47] GetPlatformType。[HACK-POC D91] Android/SystemService 占位桩。 */
static int SS_Stub_GetPlatformType(void)
{

	return 0;
}

/* AIN SystemService f[48] SetAndroidViewOrientation。[HACK-POC D91] Android/SystemService 占位桩。 */
static void SS_Stub_SetAndroidViewOrientation(int a0)
{
	(void)a0;
}

/* AIN SystemService f[49] SetAndroidViewKeepScreen。[HACK-POC D91] Android/SystemService 占位桩。 */
static void SS_Stub_SetAndroidViewKeepScreen(int a0)
{
	(void)a0;
}

/* AIN SystemService f[56] SetHookCloseApp。[HACK-POC D91] Android/SystemService 占位桩。 */
static void SS_Stub_SetHookCloseApp(int a0)
{
	(void)a0;
}

/* AIN SystemService f[57] IsHookCloseApp。[HACK-POC D91] Android/SystemService 占位桩。 */
static int SS_Stub_IsHookCloseApp(void)
{

	return 0;
}

/* AIN SystemService f[59] `void GetMemoryInfo(ref int total, ref int free)`。
 * [D91] 当年是 `(float,float)` 错形桩 —— dump-hll 类型表 `(18,18)` 实证
 * 两个都是 ref int（D202：ref int 出参 = C 普通指针）。同一"系统信息"页
 * 显示行（包装 16915 返回第 1 个出参，单位 MB），照 /proc/meminfo 回填。 */
static void SystemService_GetMemoryInfo(int *total_mb, int *free_mb)
{
	*total_mb = 0;
	*free_mb = 0;
	FILE *fp = fopen("/proc/meminfo", "r");
	if (!fp)
		return;
	char line[256];
	long kb[2] = { 0, 0 };
	int got = 0;
	while (got < 2 && fgets(line, sizeof(line), fp)) {
		long v;
		if (sscanf(line, "MemTotal: %ld kB", &v) == 1) {
			kb[0] = v;
			got++;
		} else if (sscanf(line, "MemAvailable: %ld kB", &v) == 1) {
			kb[1] = v;
			got++;
		}
	}
	fclose(fp);
	*total_mb = (int)(kb[0] / 1024);
	*free_mb = (int)(kb[1] / 1024);
}

/* AIN SystemService f[64] `int Debug_GetUseVideoMemorySize()`。
 * debug::detail::CDebugVideoMemorySize@Update 的调试面板读数；本引擎不统计
 * 显存占用，按 Debug_GetUseMemorySize 同族口径返回 0。 */
static int SystemService_Debug_GetUseVideoMemorySize(void)
{
	return 0;
}

/* AIN SystemService f[63] Debug_GetUseMemorySize。[HACK-POC D91] Android/SystemService 占位桩。 */
static int SS_Stub_Debug_GetUseMemorySize(void)
{

	return 0;
}

/* AIN SystemService f[65] Debug_SetShowMipmapLevel。[HACK-POC D91] Android/SystemService 占位桩。 */
static void SS_Stub_Debug_SetShowMipmapLevel(int a0)
{
	(void)a0;
}

/* AIN SystemService f[66] Debug_IsShowMipmapLevel。[HACK-POC D91] Android/SystemService 占位桩。 */
static int SS_Stub_Debug_IsShowMipmapLevel(void)
{

	return 0;
}

HLL_LIBRARY(SystemService,
	    HLL_EXPORT(SetGameViewScaleRate, SystemService_SetGameViewScaleRate),
	    HLL_EXPORT(GetGameViewScaleRate, SystemService_GetGameViewScaleRate),
	    HLL_EXPORT(GetGameVersionByText, SystemService_GetGameVersionByText),
	    HLL_EXPORT(ShowWaitMessage, SystemService_ShowWaitMessage),
	    HLL_EXPORT(GameVariable_IsExist, SS_GameVariable_IsExist),
	    HLL_EXPORT(_PreLink, SystemService_PreLink),
	    HLL_EXPORT(_ModuleInit, SystemService_ModuleInit),
	    HLL_EXPORT(GetOSInfo, SystemService_GetOSInfo),
	    HLL_EXPORT(GetCPUInfo, SystemService_GetCPUInfo),
	    HLL_EXPORT(GetScreenInfo, SystemService_GetScreenInfo),
	    HLL_EXPORT(GetMixerNumof, mixer_get_numof),
	    HLL_EXPORT(GetMixerName, SystemService_GetMixerName),
	    HLL_EXPORT(GetMixerVolume, mixer_get_volume),
	    HLL_EXPORT(GetMixerDefaultVolume, SystemService_GetMixerDefaultVolume),
	    HLL_EXPORT(GetMixerMute, mixer_get_mute),
	    HLL_EXPORT(SetMixerName, SystemService_SetMixerName),
	    HLL_EXPORT(SetMixerVolume, mixer_set_volume),
	    HLL_EXPORT(SetMixerMute, mixer_set_mute),
	    HLL_EXPORT(GetGameVersion, SystemService_GetGameVersion),
	    HLL_EXPORT(GetGameName, SystemService_GetGameName),
	    HLL_EXPORT(AddURLMenu, SystemService_AddURLMenu),
	    HLL_EXPORT(IsFullScreen, SystemService_IsFullScreen),
	    HLL_EXPORT(ChangeNormalScreen, SystemService_ChangeNormalScreen),
	    HLL_EXPORT(ChangeFullScreen, SystemService_ChangeFullScreen),
	    HLL_EXPORT(InitMainWindowPosAndSize, SystemService_InitMainWindowPosAndSize),
	    HLL_EXPORT(UpdateView, SystemService_UpdateView),
	    HLL_EXPORT(GetViewWidth, SystemService_GetViewWidth),
	    HLL_EXPORT(GetViewHeight, SystemService_GetViewHeight),
	    HLL_EXPORT(MoveMouseCursorPosImmediately, SystemService_MoveMouseCursorPosImmediately),
	    HLL_EXPORT(SetHideMouseCursorByGame, SystemService_SetHideMouseCursorByGame),
	    HLL_TODO_EXPORT(GetHideMouseCursorByGame, SystemService_GetHideMouseCursorByGame),
	    HLL_EXPORT(SetUsePower2Texture, SystemService_SetUsePower2Texture),
	    HLL_EXPORT(GetUsePower2Texture, SystemService_GetUsePower2Texture),
	    HLL_EXPORT(SetAntiAliasingMode, SystemService_SetAntiAliasingMode),
	    HLL_EXPORT(GetAntiAliasingMode, SystemService_GetAntiAliasingMode),
	    HLL_EXPORT(SetWindowSetting, SystemService_SetWindowSetting),
	    HLL_EXPORT(GetWindowSetting, SystemService_GetWindowSetting),
	    HLL_EXPORT(SetMouseCursorConfig, SystemService_SetMouseCursorConfig),
	    HLL_EXPORT(GetMouseCursorConfig, SystemService_GetMouseCursorConfig),
	    HLL_TODO_EXPORT(RunProgram, SystemService_RunProgram),
	    HLL_TODO_EXPORT(IsOpenedMutex, SystemService_IsOpenedMutex),
	    HLL_EXPORT(GetGameFolderPath, SystemService_GetGameFolderPath),
	    HLL_EXPORT(GetDate, get_date),
	    HLL_EXPORT(GetTime, SystemService_GetTime),
	    HLL_EXPORT(IsResetOnce, SystemService_IsResetOnce),
	    HLL_EXPORT(OpenPlayingManual, SystemService_OpenPlayingManual),
	    HLL_EXPORT(IsExistPlayingManual, SystemService_IsExistPlayingManual),
	    HLL_EXPORT(IsExistSystemMessage, SystemService_IsExistSystemMessage),
	    HLL_EXPORT(PopSystemMessage, SystemService_PopSystemMessage),
	    HLL_EXPORT(PopSystemMessageString, SystemService_PopSystemMessageString),
	    HLL_EXPORT(RestrainScreensaver, SystemService_RestrainScreensaver),
	    HLL_EXPORT(Debug_GetUseVideoMemorySize, SystemService_Debug_GetUseVideoMemorySize),
	    HLL_EXPORT(Rance0123456789, SystemService_Rance0123456789),
	    HLL_EXPORT(XXXXX01XXXXXXXX, SystemService_XXXXX01XXXXXXXX),
	    HLL_EXPORT(GetSystemBufferName, SystemService_GetSystemBufferName),
	    HLL_EXPORT(XXX, SystemService_XXX),
	    HLL_EXPORT(Test, SystemService_Test),
	    HLL_EXPORT(DRPKT, SystemService_DRPKT),
	    HLL_EXPORT(Rance96161988, SystemService_Rance96161988)
	,
	    HLL_EXPORT(GetDefaultViewWidth, SS_Stub_GetDefaultViewWidth),
	    HLL_EXPORT(GetDefaultViewHeight, SS_Stub_GetDefaultViewHeight),
	    HLL_EXPORT(SetViewSize, SS_Stub_SetViewSize),
	    HLL_EXPORT(SetWindowPos, SS_Stub_SetWindowPos),
	    HLL_EXPORT(GetWindowPosX, SS_Stub_GetWindowPosX),
	    HLL_EXPORT(GetWindowPosY, SS_Stub_GetWindowPosY),
	    HLL_EXPORT(SetViewResizableMode, SS_Stub_SetViewResizableMode),
	    HLL_EXPORT(IsViewResizableMode, SS_Stub_IsViewResizableMode),
	    HLL_EXPORT(SaveScreenshot, SS_Stub_SaveScreenshot),
	    HLL_EXPORT(SetWaitVSyncMode, SS_Stub_SetWaitVSyncMode),
	    HLL_EXPORT(GetWaitVSyncMode, SS_Stub_GetWaitVSyncMode),
	    /* [D393] 变量系统：一套字典 API，两套独立命名空间。 */
	    HLL_EXPORT(SystemVariable_IsExist, SS_SystemVariable_IsExist),
	    HLL_EXPORT(SystemVariable_Set, SS_SystemVariable_Set),
	    HLL_EXPORT(SystemVariable_Get, SS_SystemVariable_Get),
	    HLL_EXPORT(SystemVariable_GetKey, SS_SystemVariable_GetKey),
	    HLL_EXPORT(SystemVariable_NumofKey, SS_SystemVariable_NumofKey),
	    HLL_EXPORT(SystemVariable_Erase, SS_SystemVariable_Erase),
	    HLL_EXPORT(GameVariable_Set, SS_GameVariable_Set),
	    HLL_EXPORT(GameVariable_Get, SS_GameVariable_Get),
	    HLL_EXPORT(GameVariable_GetKey, SS_GameVariable_GetKey),
	    HLL_EXPORT(GameVariable_NumofKey, SS_GameVariable_NumofKey),
	    HLL_EXPORT(GameVariable_Erase, SS_GameVariable_Erase),
	    HLL_EXPORT(GetPlatformType, SS_Stub_GetPlatformType),
	    HLL_EXPORT(SetAndroidViewOrientation, SS_Stub_SetAndroidViewOrientation),
	    HLL_EXPORT(SetAndroidViewKeepScreen, SS_Stub_SetAndroidViewKeepScreen),
	    HLL_EXPORT(SetHookCloseApp, SS_Stub_SetHookCloseApp),
	    HLL_EXPORT(IsHookCloseApp, SS_Stub_IsHookCloseApp),
	    HLL_EXPORT(GetMemoryInfo, SystemService_GetMemoryInfo),
	    HLL_EXPORT(Debug_GetUseMemorySize, SS_Stub_Debug_GetUseMemorySize),
	    HLL_EXPORT(Debug_SetShowMipmapLevel, SS_Stub_Debug_SetShowMipmapLevel),
	    HLL_EXPORT(Debug_IsShowMipmapLevel, SS_Stub_Debug_IsShowMipmapLevel));

static struct ain_hll_function *get_fun(int libno, const char *name)
{
	int fno = ain_get_library_function(ain, libno, name);
	return fno >= 0 ? &ain->libraries[libno].functions[fno] : NULL;
}

static void SystemService_PreLink(void)
{
	struct ain_hll_function *fun;
	int libno = ain_get_library(ain, "SystemService");
	assert(libno >= 0);

	fun = get_fun(libno, "IsResetOnce");
	if (fun && fun->nr_arguments == 1) {
		static_library_replace(&lib_SystemService, "IsResetOnce",
				SystemService_IsResetOnce_Drapeko);
	}
}
