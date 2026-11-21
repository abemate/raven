#define CINTERFACE
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <ole2.h>

// OLE drag and drop inside a program: a window registered with RegisterDragDrop hears DragEnter and Drop when DoDragDrop's source
// drops over it, and DragLeave when the source cancels; WindowFromPoint passes over a disabled window lying on top, as a drop does.
// PDF-XChange Editor moves pages by dragging their thumbnails; DoDragDrop cancelled at once, so nothing moved.

#define CHECK(code, condition) do { if (!(condition)) return code; } while (0)

static int same_guid(REFIID left, REFIID right)
{
	const BYTE *a = (const BYTE *)left, *b = (const BYTE *)right;
	for (unsigned i = 0; i < sizeof(GUID); ++i) if (a[i] != b[i]) return 0;
	return 1;
}

static ULONG STDMETHODCALLTYPE add_ref(void *self) { (void)self; return 2; }
static ULONG STDMETHODCALLTYPE release(void *self) { (void)self; return 1; }

static LONG entered, dropped, left, checks;
static HRESULT finish;

static HRESULT STDMETHODCALLTYPE target_query(IDropTarget *self, REFIID iid, void **out)
{
	if (same_guid(iid, &IID_IUnknown) || same_guid(iid, &IID_IDropTarget)) { *out = self; return S_OK; }
	*out = 0;
	return E_NOINTERFACE;
}
static HRESULT STDMETHODCALLTYPE target_enter(IDropTarget *self, IDataObject *data, DWORD keys, POINTL at, DWORD *effect)
{ (void)self; (void)data; (void)keys; (void)at; ++entered; *effect = DROPEFFECT_MOVE; return S_OK; }
static HRESULT STDMETHODCALLTYPE target_over(IDropTarget *self, DWORD keys, POINTL at, DWORD *effect)
{ (void)self; (void)keys; (void)at; *effect = DROPEFFECT_MOVE; return S_OK; }
static HRESULT STDMETHODCALLTYPE target_leave(IDropTarget *self) { (void)self; ++left; return S_OK; }
static HRESULT STDMETHODCALLTYPE target_drop(IDropTarget *self, IDataObject *data, DWORD keys, POINTL at, DWORD *effect)
{ (void)self; (void)data; (void)keys; (void)at; ++dropped; *effect = DROPEFFECT_MOVE; return S_OK; }
static IDropTargetVtbl target_table = {target_query, (void *)add_ref, (void *)release, target_enter, target_over, target_leave, target_drop};

// The source goes on at its first check and then ends the drag as the test says: a drop, or a cancel.
static HRESULT STDMETHODCALLTYPE source_query(IDropSource *self, REFIID iid, void **out)
{
	if (same_guid(iid, &IID_IUnknown) || same_guid(iid, &IID_IDropSource)) { *out = self; return S_OK; }
	*out = 0;
	return E_NOINTERFACE;
}
static HRESULT STDMETHODCALLTYPE source_continue(IDropSource *self, BOOL escape, DWORD keys)
{ (void)self; (void)escape; (void)keys; return checks++ ? finish : S_OK; }
static HRESULT STDMETHODCALLTYPE source_feedback(IDropSource *self, DWORD effect) { (void)self; (void)effect; return DRAGDROP_S_USEDEFAULTCURSORS; }
static IDropSourceVtbl source_table = {source_query, (void *)add_ref, (void *)release, source_continue, source_feedback};

// The data is never looked at by these targets.
static HRESULT STDMETHODCALLTYPE data_query(IDataObject *self, REFIID iid, void **out)
{
	if (same_guid(iid, &IID_IUnknown) || same_guid(iid, &IID_IDataObject)) { *out = self; return S_OK; }
	*out = 0;
	return E_NOINTERFACE;
}
static HRESULT STDMETHODCALLTYPE data_none(void) { return E_NOTIMPL; }
static IDataObjectVtbl data_table = {data_query, (void *)add_ref, (void *)release, (void *)data_none, (void *)data_none, (void *)data_none,
	(void *)data_none, (void *)data_none, (void *)data_none, (void *)data_none, (void *)data_none, (void *)data_none};

static HRESULT drag(HWND window, DWORD *effect)
{
	IDataObject data = {&data_table};
	IDropSource source = {&source_table};
	checks = 0;
	// One message for the drag to take from the queue between its checks.
	PostMessageW(window, WM_NULL, 0, 0);
	return DoDragDrop(&data, &source, DROPEFFECT_MOVE | DROPEFFECT_COPY, effect);
}

int raven_entry(void)
{
	WNDCLASSW type = {0};
	type.lpfnWndProc = DefWindowProcW;
	type.hInstance = GetModuleHandleW(0);
	type.lpszClassName = L"RavenDragDrop";
	CHECK(90, RegisterClassW(&type));
	HWND window = CreateWindowExW(0, L"RavenDragDrop", L"target", WS_POPUP | WS_VISIBLE, 100, 100, 200, 200, 0, 0, type.hInstance, 0);
	CHECK(91, window != 0);
	CHECK(92, SetCursorPos(200, 200));
	IDropTarget target = {&target_table};
	CHECK(1, RegisterDragDrop(window, &target) == S_OK);
	CHECK(2, RegisterDragDrop(window, &target) == DRAGDROP_E_ALREADYREGISTERED);
	DWORD effect = 0;
	finish = DRAGDROP_S_DROP;
	CHECK(3, drag(window, &effect) == DRAGDROP_S_DROP);
	CHECK(4, entered == 1 && dropped == 1 && effect == DROPEFFECT_MOVE);
	finish = DRAGDROP_S_CANCEL;
	CHECK(5, drag(window, &effect) == DRAGDROP_S_CANCEL && effect == DROPEFFECT_NONE);
	CHECK(6, entered == 2 && left == 1 && dropped == 1);
	CHECK(7, RevokeDragDrop(window) == S_OK && RevokeDragDrop(window) == DRAGDROP_E_NOTREGISTERED);
	// A disabled window on top is passed over: the point names the window under it.
	HWND cover = CreateWindowExW(0, L"RavenDragDrop", L"cover", WS_POPUP | WS_VISIBLE | WS_DISABLED, 150, 150, 100, 100, 0, 0, type.hInstance, 0);
	CHECK(93, cover != 0);
	POINT point = {200, 200};
	CHECK(8, WindowFromPoint(point) == window);
	DestroyWindow(cover);
	DestroyWindow(window);
	return 0;
}
