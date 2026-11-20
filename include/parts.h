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

#ifndef SYSTEM4_PARTS_H
#define SYSTEM4_PARTS_H

#include <stdbool.h>

struct page;
struct string;

// parts.c
void PE_enable_multi_controller(void);
bool PE_Init(void);
void PE_Reset(void);
void PE_Update(int passed_time, bool message_window_show);
void parts_advance_animation(int passed_time);
void PE_UpdateComponent(int passed_time);
void PE_UpdateParts(int passed_time, bool is_skip, bool message_window_show);
void PE_SetDelegateIndex(int parts_no, int delegate_index);
int PE_GetDelegateIndex(int parts_no);
/* [D337] child_no 是否为 ancestor_no 或其后代（点击落在容器上时用）。 */
bool PE_PartsIsUnder(int child_no, int ancestor_no);
/* [D338] 覆盖 (x,y) 的最上层局部 clickable 件（0 = 没有）。 */
int PE_FindClickableAt(int x, int y);
/* [D338 诊断] 打印单个部件的输入相关字段。 */
void PE_DebugPartsInfo(int parts_no);
/* [D341] 部件命中盒是否覆盖 (x,y)（补父偏移）。 */
bool PE_PartsCovers(int parts_no, int x, int y);
/* [D347 诊断] 打印所有覆盖 (x,y) 的有尺寸部件。 */
void PE_DebugCovers(int x, int y);
/* [D341] root_no 的子树里有没有部件覆盖 (x,y)。 */
bool PE_SubtreeCovers(int root_no, int x, int y);
bool PE_SetPartsCG(int parts_no, struct string *cg_name, int sprite_deform, int state);
bool PE_SetPartsCGThread(int parts_no, struct string *cg_name, int state);
bool PE_IsThreadLoading(int parts_no, int state);
bool PE_SetPartsCG_by_index(int parts_no, int cg_no, int sprite_deform, int state);
bool PE_SetPartsCG_by_string_index(int parts_no, struct string *cg_no,
		int sprite_deform, int state);
void PE_GetPartsCGName(int parts_no, struct string **cg_name, int state);
bool PE_SetPartsCGSurfaceArea(int parts_no, int x, int y, int w, int h, int state);
void PE_GetPartsCGSurfaceArea(int parts_no, int *x, int *y, int *w, int *h, int state);
int PE_GetPartsCGNumber(int parts_no, int state);
bool PE_SetLoopCG_by_index(int parts_no, int cg_no, int nr_frames, int frame_time, int state);
bool PE_SetLoopCG(int parts_no, struct string *cg_name, int start_no, int nr_frames,
		int frame_time, int state);
bool PE_SetLoopCGSurfaceArea(int parts_no, int x, int y, int w, int h, int state);
bool PE_SetHGaugeCG(int parts_no, struct string *cg_name, int state);
bool PE_SetHGaugeCG_by_index(int parts_no, int cg_no, int state);
bool PE_SetHGaugeRate(int parts_no, float numerator, float denominator, int state);
bool PE_SetHGaugeRate_int(int parts_no, int numerator, int denominator, int state);
bool PE_SetHGaugeSurfaceArea(int parts_no, int x, int y, int w, int h, int state);
bool PE_SetVGaugeCG(int parts_no, struct string *cg_name, int state);
bool PE_SetVGaugeCG_by_index(int parts_no, int cg_no, int state);
bool PE_SetVGaugeRate(int parts_no, float numerator, float denominator, int state);
bool PE_SetVGaugeRate_int(int parts_no, int numerator, int denominator, int state);
bool PE_SetVGaugeSurfaceArea(int parts_no, int x, int y, int w, int h, int state);
bool PE_SetNumeralCG(int parts_no, struct string *cg_name, int state);
bool PE_SetNumeralCG_by_index(int parts_no, int cg_no, int state);
bool PE_SetNumeralLinkedCGNumberWidthWidthList_by_index(int parts_no, int cg_no,
		int w0, int w1, int w2, int w3, int w4, int w5, int w6, int w7, int w8,
		int w9, int w_minus, int w_comma, int state);
bool PE_SetNumeralLinkedCGNumberWidthWidthList(int parts_no, struct string *cg_name,
		int w0, int w1, int w2, int w3, int w4, int w5, int w6, int w7, int w8,
		int w9, int w_minus, int w_comma, int state);
bool PE_SetNumeralNumber(int parts_no, int n, int state);
int PE_GetNumeralNumber(int parts_no, int state);
bool PE_SetNumeralShowComma(int parts_no, bool show_comma, int state);
bool PE_SetNumeralSpace(int parts_no, int space, int state);
bool PE_SetNumeralLength(int parts_no, int length, int state);
bool PE_SetNumeralSurfaceArea(int parts_no, int x, int y, int w, int h, int state);
void PE_ReleaseParts(int parts_no);
void PE_ReleaseAllParts(void);
void PE_ReleaseAllPartsWithoutSystem(void);
void PE_ReleaseAllWithoutSystem(struct page **erase_number_list);
void PE_SetPos(int parts_no, int x, int y);
int PE_GetPartsAbsoluteX(int parts_no);
int PE_GetPartsAbsoluteY(int parts_no);
int PE_GetPartsAbsoluteZ(int parts_no);
int PE_GetPartsX(int parts_no);
/* 视图绝对坐标（含父链）。排障用；渲染与命中都以 global.pos 为准。 */
int PE_GetPartsGlobalX(int parts_no);
int PE_GetPartsGlobalY(int parts_no);
int PE_GetPartsY(int parts_no);
void PE_SetZ(int parts_no, int z);
int PE_GetPartsZ(int parts_no);
void PE_SetShow(int parts_no, bool show);
bool PE_GetPartsShow(int parts_no);
void PE_SetAlpha(int parts_no, int alpha);
int PE_GetPartsAlpha(int parts_no);
void PE_SetPartsDrawFilter(int parts_no, int draw_filter);
int PE_GetPartsDrawFilter(int parts_no);
void PE_SetAddColor(int parts_no, int r, int g, int b);
void PE_GetAddColor(int parts_no, int *r, int *g, int *b);
void PE_SetMultiplyColor(int parts_no, int r, int g, int b);
void PE_GetMultiplyColor(int parts_no, int *r, int *g, int *b);
int PE_GetPartsWidth(int parts_no, int state);
int PE_GetPartsHeight(int parts_no, int state);
int PE_GetPartsUpperLeftPosX(int parts_no, int state);
int PE_GetPartsUpperLeftPosY(int parts_no, int state);
void PE_SetPartsOriginPosMode(int parts_no, int origin_pos_mode);
int PE_GetPartsOriginPosMode(int parts_no);
void PE_SetParentPartsNumber(int parts_no, int parent_parts_no);
/* [D312] 父部件号查询（0 = 没有父）。AIN 的 Parts_GetParentPartsNumber 用它算层号。 */
int PE_parts_get_parent_parts_number(int parts_no);
/* [D312] 真正设置活动层（决定新部件落在哪一层）。 */
void PE_SetActiveController(int no);
void PE_AddChild(int parts_no, int child_no);
void PE_InsertChild(int parts_no, int child_no, int index);
void PE_RemoveChild(int parts_no, int child_no);
void PE_ClearChild(int parts_no);
int PE_NumofChild(int parts_no);
int PE_GetChild(int parts_no, int index);
int PE_GetChildIndex(int parts_no, int child_no);
int PE_IsExistChild(int parts_no, int child_no);
int PE_GetParentPartsNumber(int parts_no);
bool PE_SetPartsGroupNumber(int parts_no, int group_no);
void PE_SetPartsMessageWindowShowLink(int parts_no, bool message_window_show_link);
bool PE_GetPartsMessageWindowShowLink(int parts_no);
void PE_SetSpeedupRateByMessageSkip(int parts_no, int rate);
int PE_GetSpeedupRateByMessageSkip(int parts_no);
void PE_SetPartsMagX(int parts_no, float scale_x);
float PE_GetPartsMagX(int parts_no);
void PE_SetPartsMagY(int parts_no, float scale_y);
float PE_GetPartsMagY(int parts_no);
void PE_SetPartsRotateX(int parts_no, float rot_x);
void PE_SetPartsRotateY(int parts_no, float rot_y);
void PE_SetPartsRotateZ(int parts_no, float rot_z);
float PE_GetPartsRotateZ(int parts_no);
void PE_SetPartsAlphaClipperPartsNumber(int parts_no, int alpha_clipper_parts_no);
void PE_SetPartsPixelDecide(int PartsNumber, bool pixel_decide);
bool PE_SetThumbnailReductionSize(int reduction_size);
bool PE_SetThumbnailMode(bool Mode);
void PE_SetComponentType(int parts_no, int type, int state);
int PE_GetComponentType(int parts_no, int state);
void PE_SetInputState(int parts_no, int state);
int PE_GetInputState(int parts_no);
bool PE_SetPartsRectangleDetectionSize(int parts_no, int w, int h, int state);
bool PE_SetPartsSize(int parts_no, int w, int h, int state);
/* 用户组件 (component type 15)：登记控件种类与显示文字（游戏编码字节串）。
 * 目前只绘制 TextButton。 */
void PE_SetUserComponent(int parts_no, const char *object, const char *label);
/* 用户组件的名字与键值数据（PartsEngine.Set/GetUserComponent* 的底层）。
 * 入参/返回值都是游戏编码字节串。 */
void PE_SetUserComponentName(int parts_no, const char *name);
const char *PE_GetUserComponentName(int parts_no);
void PE_SetUserComponentData(int parts_no, const char *key, const char *value);
const char *PE_GetUserComponentData(int parts_no, const char *key);
/* 从根参数重算整棵部件树的 global 参数（pos/z/alpha/scale/颜色）。
 * 建树完成或布局变动后调用；幂等。详见 parts.c 里的说明。 */
void PE_RefreshGlobalTree(void);
bool PE_SetPartsCGDetectionSize(int parts_no, struct string *cg_name, int state);
bool PE_Save(struct page **buffer);
bool PE_SaveWithoutHideParts(struct page **buffer);
bool PE_Load(struct page **buffer);
/* [D202] 背景画面快照（消息窗口弹出/收起时 AIN 会 Save/Load）。
 * 复用部件引擎的完整序列化；Save 必须返回 true，否则 AIN 打
 * "[画面保管においてパーツライブラリの保存失败]" 并中断剧情。 */
bool PE_SaveBackScene(struct page **buffer);
bool PE_LoadBackScene(struct page **buffer);
int PE_AddController(int index);
/* [D289] AIN 的顺序是 `Activity@Load`（建部件）**先于** `SceneStack@AddLayer`
 * （建层）。部件创建时按 `ctrl_stack.active` 归属，于是 Load 出的部件全部
 * 落在**上一层**上；之后 AIN 按新层的 `LayerPartsNumber` 去 EraseLayer，
 * 那个层是空的 —— 实测 `released=0`（`d315`），旧场景部件永久残留。
 *
 * AIN 无法改，所以在 Load 前后包一层：`PE_ControllerPendingBegin()` 记下
 * 「接下来 Load 出来的部件属于尚未创建的层」，`PE_AddController()` 建层时
 * 把这些部件迁到新层。XSYS4_CTRL_PENDING=0 可整体回退到旧行为（A/B）。 */
void PE_ControllerPendingBegin(void);
void PE_ControllerPendingEnd(void);
void PE_RemoveController(struct page **erase_number_list, int index);
/* [D129] 图层 ID ↔ 栈下标的边界转换。AIN 侧保存的是 ID，内部渲染/命中
 * 用的是下标；只有跨 HLL 边界（AddLayer/GetActiveLayer/SetActiveLayer/
 * EraseLayer/SetComponentShow/GetLayerID/GetLayerIndex）才需要转换。 */
int PE_get_controller_id(int index);
int PE_get_controller_index(int id);
bool PE_CreateParts3DLayerPluginID(int parts_no, int state);
int PE_GetParts3DLayerPluginID(int parts_no, int state);
bool PE_ReleaseParts3DLayerPluginID(int parts_no, int state);
// GUIEngine
int PE_GetFreeNumber(void);
bool PE_IsExist(int parts_no);
// PartsFunc 接口
void PE_set_active_controller(int controller_no);
int PE_get_active_controller(void);
int PE_get_system_controller(void);
int PE_get_nr_controllers(void);
void PE_parts_set_want_save(int parts_no, bool want_save);
void PE_parts_set_want_save_back_scene(int parts_no, bool want);
/* [D310] 读取端：该部件是否属于「背景场景」（SaveBackScene 保存/收起的范围）。 */
bool PE_parts_get_want_save_back_scene(int parts_no);
bool PE_save_thumbnail(struct string *filename, int reduction_factor);
bool PE_init_parts_movie(int parts_no, int width, int height, int bg_r, int bg_g, int bg_b, int state);
int PE_get_movie_sprite(int parts_no, int state);
float PE_parts_get_absolute_x(int parts_no);
float PE_parts_get_absolute_y(int parts_no);
int PE_parts_get_absolute_z(int parts_no);
void PE_parts_set_lock_input_state(int parts_no, bool lock);

// construction.c
bool PE_AddCreateToPartsConstructionProcess(int parts_no, int w, int h, int state);
bool PE_AddCreatePixelOnlyToPartsConstructionProcess(int parts_no, int w, int h, int state);
bool PE_AddCreateCGToProcess(int parts_no, struct string *cg_name, int state);
bool PE_AddFillToPartsConstructionProcess(int parts_no, int x, int y, int w, int h,
		int r, int g, int b, int state);
bool PE_AddFillAlphaColorToPartsConstructionProcess(int parts_no, int x, int y, int w, int h,
		int r, int g, int b, int a, int state);
bool PE_AddFillAMapToPartsConstructionProcess(int parts_no, int x, int y, int w, int h, int a, int state);
bool PE_AddFillWithAlphaToPartsConstructionProcess(int parts_no, int x, int y, int w, int h,
		int r, int g, int b, int a, int state);
bool PE_AddDrawRectToPartsConstructionProcess(int parts_no, int x, int y, int w, int h,
		int r, int g, int b, int state);
bool PE_AddDrawCutCGToPartsConstructionProcess(int parts_no, struct string *cg_name,
		int dx, int dy, int dw, int dh, int sx, int sy, int sw, int sh, int interp_type, int state);
bool PE_AddCopyCutCGToPartsConstructionProcess(int parts_no, struct string *cg_name,
		int dx, int dy, int dw, int dh, int sx, int sy, int sw, int sh, int interp_type, int state);
bool PE_AddGrayFilterToPartsConstructionProcess(int parts_no, int x, int y, int w, int h,
		bool full_size, int state);
bool PE_AddCopyTextToPartsConstructionProcess(int parts_no, int x, int y, struct string *text,
		int type, int size, int r, int g, int b, float bold_weight,
		int edge_r, int edge_g, int edge_b, float edge_weight,
		int char_space, int line_space, int state);
bool PE_AddDrawTextToPartsConstructionProcess(int parts_no, int x, int y, struct string *text,
		int type, int size, int r, int g, int b, float bold_weight,
		int edge_r, int edge_g, int edge_b, float edge_weight,
		int char_space, int line_space, int state);
bool PE_BuildPartsConstructionProcess(int parts_no, int state);
/* [D146] 线程版按同步实现；count 用来让调用方的"等合成完成"轮询第一轮就退出 */
bool PE_BuildPartsConstructionProcessThread(int parts_no, int state);
int PE_GetPartsConstructionProcessCount(int parts_no, int state);
bool PE_ClearPartsConstructionProcess(int parts_no, int state);
bool PE_SetPartsConstructionSurfaceArea(int parts_no, int x, int y, int w, int h, int state);

// input.c
void PE_UpdateInputState(int passed_time);
/* 帧心跳的输入阶段：handle_events() + PE_UpdateInputState()。
 * sact_Update 与 vm_frame_pump 都必须调用它（见 input.c 注释）。 */
void PE_PumpInput(int passed_time);
void PE_SetPassCursor(int parts_no, bool pass);
bool PE_GetPartsPassCursor(int parts_no);
void PE_SetClickable(int parts_no, bool clickable);
/* [D174] 复选框状态（旁路表，不进 struct parts / 存档） */
void PE_CheckBoxChecked(int parts_no, bool check);
bool PE_IsCheckBoxChecked(int parts_no);
void PE_SetCheckBoxEnable(int parts_no, bool enable);
bool PE_IsCheckBoxEnable(int parts_no);
bool PE_GetPartsClickable(int parts_no);
void PE_SetDrag(int parts_no, bool enable);
void PE_SetPartsGroupDecideOnCursor(int group_no, bool decide_on_cursor);
void PE_SetPartsGroupDecideClick(int group_no, bool decide_click);
void PE_SetOnCursorShowLinkPartsNumber(int parts_no, int link_parts_no);
int PE_GetOnCursorShowLinkPartsNumber(int parts_no);
bool PE_SetPartsOnCursorSoundNumber(int parts_no, int sound_no);
bool PE_SetPartsClickSoundNumber(int parts_no, int sound_no);
bool PE_SetClickMissSoundNumber(int sound_no);
void PE_BeginInput(void);
void PE_EndInput(void);
bool PE_IsInputBegun(void);
int PE_GetClickPartsNumber(void);
bool PE_IsCursorIn(int parts_no, int mouse_x, int mouse_y, int state);
/* [D309] 光标当前指向的部件号（0 = 没有）。AIN 的 PartsEngine.GetActiveParts 用它。 */
int parts_input_active_parts(void);

// message.c
void PE_ReleaseMessage(void);
void PE_PopMessage(void);
int PE_GetMessageType(void);
int PE_GetMessagePartsNumber(void);
int PE_GetMessageDelegateIndex(void);
int PE_GetMessageVariableCount(void);
int PE_GetMessageVariableType(int index);
int PE_GetMessageVariableInt(int index);
float PE_GetMessageVariableFloat(int index);
bool PE_GetMessageVariableBool(int index);
void PE_GetMessageVariableString(int index, struct string **out);

// motion.c
void PE_AddMotionPos(int parts_no, int begin_x, int begin_y, int end_x, int end_y, int begin_t, int end_t);
void PE_AddMotionPos_curve(int parts_no, int begin_x, int begin_y, int end_x, int end_y,
		int begin_t, int end_t, struct string *curve_name);
void PE_AddMotionAlpha(int parts_no, int begin_a, int end_a, int begin_t, int end_t);
void PE_AddMotionAlpha_curve(int parts_no, int begin_a, int end_a, int begin_t, int end_t,
		struct string *curve_name);
void PE_AddMotionCG_by_index(int parts_no, int begin_cg_no, int nr_cg, int begin_t, int end_t);
void PE_AddMotionHGaugeRate(int parts_no, float begin_numerator, float begin_denominator,
			    float end_numerator, float end_denominator, int begin_t, int end_t);
void PE_AddMotionHGaugeRate_curve(int parts_no, float begin_numerator, float begin_denominator,
			    float end_numerator, float end_denominator, int begin_t, int end_t,
			    struct string *curve_name);
void PE_AddMotionVGaugeRate(int parts_no, float begin_numerator, float begin_denominator,
			    float end_numerator, float end_denominator, int begin_t, int end_t);
void PE_AddMotionVGaugeRate_curve(int parts_no, float begin_numerator, float begin_denominator,
			    float end_numerator, float end_denominator, int begin_t, int end_t,
			    struct string *curve_name);
void PE_AddMotionNumeralNumber(int parts_no, int begin_n, int end_n, int begin_t, int end_t);
void PE_AddMotionNumeralNumber_curve(int parts_no, int begin_n, int end_n, int begin_t,
		int end_t, struct string *curve_name);
void PE_AddMotionMagX(int parts_no, float begin, float end, int begin_t, int end_t);
void PE_AddMotionMagX_curve(int parts_no, float begin, float end, int begin_t, int end_t,
		struct string *curve_name);
void PE_AddMotionMagY(int parts_no, float begin, float end, int begin_t, int end_t);
void PE_AddMotionMagY_curve(int parts_no, float begin, float end, int begin_t, int end_t,
		struct string *curve_name);
void PE_AddMotionRotateX(int parts_no, float begin, float end, int begin_t, int end_t);
void PE_AddMotionRotateX_curve(int parts_no, float begin, float end, int begin_t, int end_t,
		struct string *curve_name);
void PE_AddMotionRotateY(int parts_no, float begin, float end, int begin_t, int end_t);
void PE_AddMotionRotateY_curve(int parts_no, float begin, float end, int begin_t, int end_t,
		struct string *curve_name);
void PE_AddMotionRotateZ(int parts_no, float begin, float end, int begin_t, int end_t);
void PE_AddMotionRotateZ_curve(int parts_no, float begin, float end, int begin_t, int end_t,
		struct string *curve_name);
void PE_AddMotionVibrationSize(int parts_no, int begin_w, int begin_h, int begin_t, int end_t);
void PE_AddMotionSound(int sound_no, int begin_t);
void PE_AddWholeMotionVibrationSize(int begin_w, int begin_h, int begin_t, int end_t);
void PE_BeginMotion(void);
void PE_EndMotion(void);
void PE_SetMotionTime(int t);
void PE_SeekEndMotion(void);
void PE_UpdateMotionTime(int time, bool skip);
void PE_AdvanceMotionClock(int passed_time);
bool PE_IsMotion(void);
int PE_GetMotionEndTime(void);
void PE_PauseMotion(bool pause);

// text.c
bool PE_SetText(int parts_no, struct string *text, int state);
struct string *PE_GetText(int parts_no, int state);
bool PE_AddPartsText(int parts_no, struct string *text, int state);
bool PE_SetPartsTextSurfaceArea(int parts_no, int x, int y, int w, int h, int state);
bool PE_SetFont(int parts_no, int type, int size, int r, int g, int b, float bold_weight, int edge_r, int edge_g, int edge_b, float edge_weight, int state);
bool PE_SetPartsFontType(int parts_no, int type, int state);
bool PE_SetPartsFontSize(int parts_no, int size, int state);
bool PE_SetPartsFontColor(int parts_no, int r, int g, int b, int state);
bool PE_SetPartsFontBoldWeight(int parts_no, float bold_weight, int state);
bool PE_SetPartsFontEdgeColor(int parts_no, int r, int g, int b, int state);
bool PE_SetPartsFontEdgeWeight(int parts_no, float edge_weight, int state);
bool PE_SetTextCharSpace(int parts_no, int char_space, int state);
bool PE_SetTextLineSpace(int parts_no, int line_space, int state);
int PE_GetTextCharSpace(int parts_no, int state);
int PE_GetTextLineSpace(int parts_no, int state);

// flash.c
bool PE_ExistsFlashFile(struct string *flash_filename);
bool PE_SetPartsFlash(int parts_no, struct string *flash_filename, int state);
bool PE_IsPartsFlashEnd(int parts_no, int state);
int PE_GetPartsFlashCurrentFrameNumber(int parts_no, int state);
bool PE_BackPartsFlashBeginFrame(int parts_no, int state);
bool PE_StepPartsFlashFinalFrame(int parts_no, int state);
bool PE_SetPartsFlashAndStop(int parts_no, struct string *flash_filename, int state);
bool PE_StopPartsFlash(int parts_no, int state);
bool PE_StartPartsFlash(int parts_no, int state);
bool PE_GoFramePartsFlash(int parts_no, int frame_no, int state);
int PE_GetPartsFlashEndFrame(int parts_no, int state);

// flat.c
bool PE_ExistsFlatFile(struct string *filename);
bool PE_SetPartsFlat(int parts_no, struct string *filename, int state);
bool PE_SetPartsFlatThread(int parts_no, struct string *filename, int state);
bool PE_IsPartsFlatEnd(int parts_no, int state);
int PE_GetPartsFlatCurrentFrameNumber(int parts_no, int state);
bool PE_BackPartsFlatBeginFrame(int parts_no, int state);
bool PE_StepPartsFlatFinalFrame(int parts_no, int state);
bool PE_SetPartsFlatSurfaceArea(int parts_no, int x, int y, int w, int h, int state);
/* [D192] 通用「表示領域」设置（任意部件类型，不只 flat）。
 * AIN 用它给選択肢按钮之类的部件划定绘制/点击矩形；先前是 HLL_TODO_EXPORT，
 * 于是按钮的 hitbox 恒为空，回合流程推不动。 */
bool PE_SetPartsRectangleDetectionSurfaceArea(int parts_no, int x, int y, int w, int h, int state);
struct string *PE_GetPartsFlatName(int parts_no, int state);
bool PE_SetPartsFlatAndStop(int parts_no, struct string *filename, int state);
bool PE_StopPartsFlat(int parts_no, int state);
bool PE_StartPartsFlat(int parts_no, int state);
bool PE_GoFramePartsFlat(int parts_no, int frame_no, int state);
int PE_GetPartsFlatEndFrame(int parts_no, int state);
/* [D341] flat 的源帧率。 */
int PE_GetPartsFlatFPS(int parts_no, int state);
/* [D341] gauge 的分子/分母（分母固定 1000，按 rate 反推分子）。 */
int PE_GetPartsHGaugeNumerator(int parts_no, int state);
int PE_GetPartsHGaugeDenominator(int parts_no, int state);
int PE_GetPartsVGaugeNumerator(int parts_no, int state);
int PE_GetPartsVGaugeDenominator(int parts_no, int state);

// layoutbox.c
/* [D294] EX `文本エリア` 的文本排版原点（相对部件位置）。 */
void PE_SetTextArea(int parts_no, int x, int y);
void PE_SetLayoutBoxLayoutType(int parts_no, int type);
int PE_GetLayoutBoxLayoutType(int parts_no);
void PE_SetLayoutBoxReturn(int parts_no, bool return_flag, int return_size);
bool PE_IsLayoutBoxReturn(int parts_no);
int PE_GetLayoutBoxReturnSize(int parts_no);
void PE_SetLayoutBoxAlign(int parts_no, int align);
int PE_GetLayoutBoxAlign(int parts_no);
void PE_SetComponentMargin(int parts_no, int top, int bottom, int left, int right);
int PE_GetComponentMarginTop(int parts_no);
int PE_GetComponentMarginBottom(int parts_no);
int PE_GetComponentMarginLeft(int parts_no);
int PE_GetComponentMarginRight(int parts_no);
void PE_set_layoutbox_padding(int parts_no, int top, int bottom, int left, int right);
int PE_get_layoutbox_padding_top(int parts_no);
int PE_get_layoutbox_padding_bottom(int parts_no);
int PE_get_layoutbox_padding_left(int parts_no);
int PE_get_layoutbox_padding_right(int parts_no);

// activity.c（D89）
struct activity;
struct activity_part;
bool activity_exists(const char *name);
struct activity *activity_get(const char *name);
struct activity *activity_create(const char *name);
bool activity_read(struct string *name, struct string *file);
bool activity_release(const char *name);
/* [D270] 带 EraseNumberList 出参的版本：把被释放部件的委托下标写回
 * AIN，让它从消息分发表里销掉。`activity_release(name)` 等价于
 * `activity_release_ex(name, NULL)`。 */
bool activity_release_ex(const char *name, struct page **erase_number_list);
void activity_release_all(void);
void activity_free(struct activity *act);
bool activity_is_exist_file(const char *file);
int activity_nr_parts(struct activity *act);
struct activity_part *activity_find_parts(struct activity *act, const char *parts_name);
int activity_find_parts_no(struct activity *act, const char *parts_name);
int activity_part_no_at(struct activity *act, int index);
const char *activity_part_name_at(struct activity *act, int index);
int activity_part_cg_name_at(struct activity *act, int index, const char **out);
bool activity_add_parts(struct string *name, struct string *parts_name, int parts_no);
bool activity_remove_parts(struct string *name, struct string *parts_name);
void activity_remove_all_parts(struct string *name);
const char *activity_background(struct activity *act);

#endif /* SYSTEM4_PARTS_H */
