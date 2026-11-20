#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <ole2.h>

// OleRun leaves an object without IRunnableObject running, and asks a runnable one to run.

#define CHECK(code, condition) do { if (!(condition)) return code; } while (0)

static int same_guid(REFIID left, REFIID right)
{
	const BYTE *a = (const BYTE *)left, *b = (const BYTE *)right;
	for (unsigned i = 0; i < sizeof(GUID); ++i) if (a[i] != b[i]) return 0;
	return 1;
}

static LONG runs;

// An object with nothing but IUnknown, and one that can also run.
static HRESULT STDMETHODCALLTYPE plain_query(IUnknown *self, REFIID iid, void **out)
{
	if (same_guid(iid, &IID_IUnknown)) { *out = self; return S_OK; }
	*out = 0;
	return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE add_ref(void *self) { (void)self; return 2; }
static ULONG STDMETHODCALLTYPE release(void *self) { (void)self; return 1; }
static IUnknownVtbl plain_table = {plain_query, (void *)add_ref, (void *)release};

static HRESULT STDMETHODCALLTYPE runnable_query(IRunnableObject *self, REFIID iid, void **out)
{
	if (same_guid(iid, &IID_IUnknown) || same_guid(iid, &IID_IRunnableObject)) { *out = self; return S_OK; }
	*out = 0;
	return E_NOINTERFACE;
}
static HRESULT STDMETHODCALLTYPE runnable_class(IRunnableObject *self, CLSID *id) { (void)self; (void)id; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE runnable_run(IRunnableObject *self, IBindCtx *context) { (void)self; return context ? E_UNEXPECTED : (++runs, S_OK); }
static BOOL STDMETHODCALLTYPE runnable_running(IRunnableObject *self) { (void)self; return runs != 0; }
static HRESULT STDMETHODCALLTYPE runnable_lock(IRunnableObject *self, BOOL lock, BOOL close) { (void)self; (void)lock; (void)close; return S_OK; }
static HRESULT STDMETHODCALLTYPE runnable_contained(IRunnableObject *self, BOOL contained) { (void)self; (void)contained; return S_OK; }
static IRunnableObjectVtbl runnable_table = {runnable_query, (void *)add_ref, (void *)release, runnable_class, runnable_run, runnable_running,
	runnable_lock, runnable_contained};

int raven_entry(void)
{
	IUnknown plain = {&plain_table};
	IRunnableObject runnable = {&runnable_table};
	CHECK(1, OleRun(&plain) == S_OK);
	CHECK(2, OleRun((IUnknown *)&runnable) == S_OK && runs == 1);
	CHECK(3, OleRun(0) == E_INVALIDARG);
	return 0;
}
