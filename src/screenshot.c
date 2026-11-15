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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <time.h>
#include <SDL.h>

#include "system4.h"
#include "system4/cg.h"
#include "system4/file.h"
#include "xsystem4.h"

// XXX: can't include gfx/gfx.h because windows.h redefines Rectangle
typedef struct texture Texture;
Texture *gfx_main_surface(void);
int gfx_save_texture(Texture *t, const char *path, enum cg_type);

/* [D223] 联合取证：XSYS4_SHOT_PARTS_DUMP=1 时，在引擎自截图那一刻打**一次**
 * 部件快照（layer_show / cover / 每部件 vis），与 PNG 严格同帧。只在调用点
 * 声明，避免把 parts_internal.h 的依赖链拖进来；全程探针会拖慢帧率、改变
 * 剧情快进时序（D222 教训），这里按截图时刻触发就不会。 */
void PE_DebugDumpParts(const char *tag);
/* [D224] 同帧 CG 绘制日志（src/parts/render.c），与部件快照一起打印。 */
void PE_DebugCGRenderLog(const char *tag);

#ifdef _WIN32
#include <windows.h>

static char *default_filename(void)
{
	time_t t = time(NULL);
	struct tm *tm = localtime(&t);
	char datetime[128];
	strftime(datetime, sizeof(datetime), "%Y%m%d_%H%M%S", tm);

	char *name = xmalloc(PATH_MAX);
	snprintf(name, PATH_MAX, "%s_%s.png", config.game_name, datetime);
	return name;
}

static char *default_path(void)
{
	// FIXME: Earlier games use "[gamedir]/Snapshot/", but later games save under
	//        "Documents/AliceSoft/[gamename]/Screenshot/"
	char *path = gamedir_path("Snapshot");
	if (mkdir_p(path)) {
		WARNING("mkdir_p: %s", strerror(errno));
		free(path);
		return NULL;
	}

	for (int i = 0; path[i]; i++) {
		if (path[i] == '/')
			path[i] = '\\';
	}

	return path;
}

void screenshot_save(void)
{
	char *name = default_filename();
	char *path = default_path();

	OPENFILENAME ofn;
	ZeroMemory(&ofn, sizeof(ofn));
	ofn.lStructSize = sizeof(ofn);
	ofn.lpstrFilter = "PNG Files (*.png)\0*.png\0";
	ofn.lpstrFile = name;
	ofn.lpstrInitialDir = path;
	ofn.nMaxFile = MAX_PATH;
	ofn.Flags = OFN_EXPLORER | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY | OFN_OVERWRITEPROMPT;
	ofn.lpstrDefExt = "png";

	if (GetSaveFileName(&ofn))
	{
		for (int i = 0; name[i]; i++) {
			if (name[i] == '\\')
				name[i] = '/';
		}
		gfx_save_texture(gfx_main_surface(), name, ALCG_PNG);
		char msg[PATH_MAX + 64];
		snprintf(msg, sizeof(msg), "Screenshot saved to %s", name);
		SDL_ShowSimpleMessageBox(0, "xsystem4", msg, NULL);
	}

	free(name);
	free(path);
}

#else
#ifdef __ANDROID__

void screenshot_save(void)
{

}

#else

static char *default_filename(void)
{
	time_t t = time(NULL);
	struct tm *tm = localtime(&t);
	char datetime[128];
	strftime(datetime, sizeof(datetime), "%Y%m%d_%H%M%S", tm);

	// FIXME: Earlier games use "[gamedir]/Snapshot/", but later games save under
	//        "Documents/AliceSoft/[gamename]/Screenshot/"
	char name[PATH_MAX];
	snprintf(name, sizeof(name), "Snapshot/%s_%s.png", config.game_name, datetime);
	return gamedir_path(name);
}

static bool have_zenity(void)
{
	FILE *fp = popen("zenity --version", "r");
	if (!fp)
		return false;

	char line[256];
	while (fgets(line, sizeof(line), fp))
		printf("%s", line);
	if (pclose(fp) != 0)
		return false;
	return true;
}

static char *zenity_get_filename(const char *dflt)
{
	char buf[PATH_MAX + 64];
	snprintf(buf, sizeof(buf), "zenity --file-selection --filename=\"%s\" --save", dflt);

	FILE *fp = popen(buf, "r");
	if (!fp)
		return NULL;

	// read filename
	char *filename = fgets(buf, sizeof(buf), fp);
	if (filename && *filename) {
		filename = xstrdup(filename);

		// delete trailing newline
		size_t len = strlen(filename);
		if (filename[len-1] == '\n')
			filename[len-1] = '\0';
	} else {
		filename = NULL;
	}

	// empty pipe and close
	while (fgets(buf, sizeof(buf), fp))
		/* nothing */;
	pclose(fp);

	return filename;
}

void screenshot_save(void)
{
	/* [D222] 无头跑测快路径：XSYS4_SHOT_DIR 指定目录时直接 dump GL main
	 * surface，绕过 zenity 文件框（无人确认会挂住引擎）和 X11 合成 ——
	 * GL 直绘下 x11grab / import -window 只能拿到全黑 framebuffer，
	 * 见 GUIDE 1.13。默认关闭；每按一次 Alt+S 产一个递增编号文件。 */
	const char *shot_dir = getenv("XSYS4_SHOT_DIR");
	if (shot_dir && *shot_dir) {
		static int seq;
		char path[PATH_MAX];
		if (!mkdir_p(shot_dir)) {
			snprintf(path, sizeof(path), "%s/shot_%03d.png", shot_dir, ++seq);
			if (getenv("XSYS4_SHOT_PARTS_DUMP")) {
				PE_DebugDumpParts("shot-engine");
				PE_DebugCGRenderLog("shot-engine");
			}
			gfx_save_texture(gfx_main_surface(), path, ALCG_PNG);
			NOTICE("[shot-engine] %s", path);
		} else {
			WARNING("mkdir_p: %s", strerror(errno));
		}
		return;
	}
	// get filename
	char *name = default_filename();
	if (have_zenity()) {
		char *tmp = zenity_get_filename(name);
		free(name);
		name = tmp;
		if (!name)
			return;
	}

	// create directory
	char *dir = path_dirname(name);
	if (mkdir_p(dir)) {
		WARNING("mkdir_p: %s", strerror(errno));
		free(name);
		return;
	}

	// save screenshot
	gfx_save_texture(gfx_main_surface(), name, ALCG_PNG);

	// XXX: this uses zenity on wayland, so there is no confirmation
	//      that anything happened if zenity is not installed
	char msg[PATH_MAX + 64];
	snprintf(msg, sizeof(msg), "Screenshot saved to %s", name);
	SDL_ShowSimpleMessageBox(0, "xsystem4", msg, NULL);
	NOTICE("%s", msg);

	free(name);
}

#endif // __ANDROID__
#endif // _WIN32
