/* Delegate —— 基于真实 DELEGATE_PAGE 表示的 HLL 绑定。
 *
 * 在 AIN v12 里，委托就是一张 VM 页：`values[]` 存 [obj, fun, seq]
 * 三元组，DG_CALL 用 delegate_get() 遍历它。早先有个占位实现
 * （`struct xs4_delegate`）把实参当成普通的 {funcs,nr,cap} 结构体，
 * 这就是为什么 Delegate.Set 必须重写（D69）—— 但 Add/Numof/Empty/
 * Exist/Erase/Clear 还留在占位实现上。把页指针当结构体读只会得到垃圾：
 * Delegate.Empty() 于是对那些确实有内容的委托报「空」。
 *
 *
 * 这就静默地打断了整条逐帧更新事件链：
 *   SceneStack@WaitFrame -> AFL_Parts_AddBeginUpdateEvent(delegate CheckWaitExit)
 *   parts::detail::AddPartsUpdateEvent  -> Array.PushBack(g_dgPartsBeginUpdateEvent)
 *   parts::detail::CallPartsUpdateEvent -> Delegate.Empty(entry) -> skip
 * 于是等待循环一直自旋，场景永远不前进（D112）。
 *
 * ffi 把 AIN_REF_DELEGATE 当 `struct page **` 传（指向槽的页字段），
 * 调用后再把页写回去，所以重新分配是安全的。
 */

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "hll.h"
#include "vm.h"
#include "vm/page.h"

static struct page *as_delegate_page(struct page **delegate)
{
	struct page *p = delegate ? *delegate : NULL;
	if (p && p->type == DELEGATE_PAGE)
		return p;
	return NULL;
}

/* D69：Delegate.Set 必须写进 DG_CALL 真正读的那张 DELEGATE_PAGE。
 * 旧的 xs4_delegate 占位实现写的是栈上的地址，
 * 那次注册对虚拟机完全不可见。 */
static void Delegate_Set(struct page **delegate, int func)
{
	struct page *p = as_delegate_page(delegate);
	if (!p)
		return;
	p = delegate_clear(p);
	if (func > 0)
		p = delegate_append(p, -1, func);
	*delegate = p;
}

static void Delegate_Add(struct page **delegate, int func)
{
	struct page *p = as_delegate_page(delegate);
	if (!p || func <= 0)
		return;
	if (delegate_contains(p, -1, func))
		return;
	*delegate = delegate_append(p, -1, func);
}

static int Delegate_Numof(struct page **delegate)
{
	struct page *p = as_delegate_page(delegate);
	int n = p ? delegate_numof(p) : 0;
	if (getenv("XSYS4_DG_DBG"))
		NOTICE("[dg] Numof page=%p type=%d n=%d", (void*)p, p ? p->type : -1, n);
	return n;
}

static bool Delegate_Empty(struct page **delegate)
{
	/* [D114] Do NOT route through Delegate_Numof(): delegate_numof() runs a
	 * 那个会**删掉** obj 槽已经丢了引用的条目的垃圾回收。
	 * parts::detail::CallPartsUpdateEvent 把 Delegate.Empty() 当作
	 * 「这条注册还活着吗」的无害查询；而这个 GC 副作用
	 * 把刚注册的东西清空了，于是 EndUpdateEvent 列表在第一帧
	 * 就丢了唯一那条，EndEventCallback@Update 再也没被派发过。
	 * 这一下打断了整条 FadeIn -> onFinish 链。
	 * Empty() 是个谓词，绝不能改动页。 */
	struct page *p = as_delegate_page(delegate);
	return !p || p->nr_vars == 0;
}

static bool Delegate_Exist(struct page **delegate, int func)
{
	struct page *p = as_delegate_page(delegate);
	return p ? delegate_contains(p, -1, func) : false;
}

static void Delegate_Erase(struct page **delegate, int func)
{
	struct page *p = as_delegate_page(delegate);
	if (!p)
		return;
	delegate_erase(p, -1, func);
}

static void Delegate_Clear(struct page **delegate)
{
	struct page *p = as_delegate_page(delegate);
	if (!p)
		return;
	*delegate = delegate_clear(p);
}

HLL_LIBRARY(Delegate,
	HLL_EXPORT(Set, Delegate_Set),
	HLL_EXPORT(Add, Delegate_Add),
	HLL_EXPORT(Numof, Delegate_Numof),
	HLL_EXPORT(Empty, Delegate_Empty),
	HLL_EXPORT(Exist, Delegate_Exist),
	HLL_EXPORT(Erase, Delegate_Erase),
	HLL_EXPORT(Clear, Delegate_Clear));
