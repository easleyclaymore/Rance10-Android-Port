/* Delegate - stub implementation for Rance X bring-up.
 *
 * NOTE: In AIN v12 a delegate is a first-class type (ain->delegates[]), and the
 * HLL argument types are AIN_REF_DELEGATE (67) / AIN_HLL_FUNC (95). The exact
 * runtime representation is not documented; this is a placeholder so the
 * library links, with a naive function-reference list behind it.
 */
#include <stdlib.h>
#include <string.h>

#include "hll.h"
#include "vm.h"
#include "vm/page.h"

struct xs4_delegate {
	int *funcs;
	int nr;
	int cap;
};

static struct xs4_delegate *as_delegate(void *p)
{
	return (struct xs4_delegate *)p;
}

static void ensure(void *p)
{
	struct xs4_delegate *d = as_delegate(p);
	if (d && !d->funcs) {
		d->cap = 4;
		d->funcs = calloc(d->cap, sizeof(int));
		d->nr = 0;
	}
}

/* D69: Delegate.Set must write into the real DELEGATE_PAGE that DG_CALL
 * reads. The old xs4_delegate placeholder wrote into a stack address and
 * the registration was invisible to the VM. */
static void Delegate_Set(struct page **delegate, int func)
{
	struct page *p = delegate ? *delegate : NULL;
	if (!p || p->type != DELEGATE_PAGE)
		return;
	p = delegate_clear(p);
	if (func > 0)
		p = delegate_append(p, -1, func);
	*delegate = p;
}
static void Delegate_Add(void *delegate, int func)
{
	WARNING("DBG_DELEGATE Add(delegate=%p, func=%d)", delegate, func);
	struct xs4_delegate *d = as_delegate(delegate);
	if (!d)
		return;
	ensure(delegate);
	if (d->nr >= d->cap) {
		d->cap *= 2;
		d->funcs = realloc(d->funcs, d->cap * sizeof(int));
	}
	d->funcs[d->nr++] = func;
}

static int Delegate_Numof(void *delegate)
{
	WARNING("DBG_DELEGATE Numof(delegate=%p)", delegate);
	struct xs4_delegate *d = as_delegate(delegate);
	return d ? d->nr : 0;
}

static bool Delegate_Empty(void *delegate)
{
	return Delegate_Numof(delegate) == 0;
}

static bool Delegate_Exist(void *delegate, int func)
{
	struct xs4_delegate *d = as_delegate(delegate);
	if (!d)
		return false;
	for (int i = 0; i < d->nr; i++)
		if (d->funcs[i] == func)
			return true;
	return false;
}

static void Delegate_Erase(void *delegate, int func)
{
	struct xs4_delegate *d = as_delegate(delegate);
	if (!d)
		return;
	for (int i = 0; i < d->nr; i++) {
		if (d->funcs[i] == func) {
			memmove(&d->funcs[i], &d->funcs[i + 1], (d->nr - i - 1) * sizeof(int));
			d->nr--;
			return;
		}
	}
}

static void Delegate_Clear(void *delegate)
{
	struct xs4_delegate *d = as_delegate(delegate);
	if (d)
		d->nr = 0;
}

HLL_LIBRARY(Delegate,
	HLL_EXPORT(Set, Delegate_Set),
	HLL_EXPORT(Add, Delegate_Add),
	HLL_EXPORT(Numof, Delegate_Numof),
	HLL_EXPORT(Empty, Delegate_Empty),
	HLL_EXPORT(Exist, Delegate_Exist),
	HLL_EXPORT(Erase, Delegate_Erase),
	HLL_EXPORT(Clear, Delegate_Clear));
