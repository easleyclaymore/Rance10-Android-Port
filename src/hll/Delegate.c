/* Delegate - HLL bindings over the real DELEGATE_PAGE representation.
 *
 * In AIN v12 a delegate is a VM page: `values[]` holds [obj, fun, seq] triplets
 * and DG_CALL walks it with delegate_get(). An earlier placeholder
 * (`struct xs4_delegate`) treated the argument as a plain {funcs,nr,cap} struct,
 * which is why Delegate.Set had to be rewritten (D69) -- but Add/Numof/Empty/
 * Exist/Erase/Clear were left on the placeholder. Reading a page pointer as a
 * struct yields garbage: Delegate.Empty() then reported "empty" for delegates
 * that did contain entries.
 *
 * That silently broke the whole per-frame update event chain:
 *   SceneStack@WaitFrame -> AFL_Parts_AddBeginUpdateEvent(delegate CheckWaitExit)
 *   parts::detail::AddPartsUpdateEvent  -> Array.PushBack(g_dgPartsBeginUpdateEvent)
 *   parts::detail::CallPartsUpdateEvent -> Delegate.Empty(entry) -> skip
 * so the wait loop spun forever and the scene never advanced (D112).
 *
 * ffi passes AIN_REF_DELEGATE as `struct page **` (pointer to the slot's page
 * field) and writes the page back after the call, so reallocation is safe.
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

/* D69: Delegate.Set must write into the real DELEGATE_PAGE that DG_CALL
 * reads. The old xs4_delegate placeholder wrote into a stack address and
 * the registration was invisible to the VM. */
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
	return Delegate_Numof(delegate) == 0;
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
