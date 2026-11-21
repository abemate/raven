/* SPDX-License-Identifier: MIT
 * See LICENSES/MIT.txt.
 */
/* Raven shell component: OLE drag and drop inside a process.
 *
 * A window takes drops once RegisterDragDrop gave it an IDropTarget, which the window keeps as a property. DoDragDrop runs the
 * drag as Windows documents it: the mouse is captured, and on each input the source is asked whether the drag goes on
 * (QueryContinueDrag), the window under the pointer that takes drops, or the nearest parent that does, hears DragEnter, DragOver or
 * DragLeave and answers the effect it would take, the source shows that effect (GiveFeedback), and when the source says to drop,
 * the target under the pointer receives Drop. Escape cancels; messages other than input keep being dispatched meanwhile, so the
 * program goes on painting. */
#define CINTERFACE
#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <ole2.h>
#include <string.h>

static const WCHAR drop_target_property[] = L"RavenOleDropTarget";

HRESULT WINAPI RegisterDragDrop(HWND window, IDropTarget *target)
{
	if (!target)
		return E_INVALIDARG;
	if (!window || !IsWindow(window))
		return DRAGDROP_E_INVALIDHWND;
	if (GetPropW(window, drop_target_property))
		return DRAGDROP_E_ALREADYREGISTERED;
	IDropTarget_AddRef(target);
	if (!SetPropW(window, drop_target_property, (HANDLE)target)) {
		IDropTarget_Release(target);
		return E_OUTOFMEMORY;
	}
	return S_OK;
}

HRESULT WINAPI RevokeDragDrop(HWND window)
{
	IDropTarget *target;
	if (!window || !IsWindow(window))
		return DRAGDROP_E_INVALIDHWND;
	target = (IDropTarget *)RemovePropW(window, drop_target_property);
	if (!target)
		return DRAGDROP_E_NOTREGISTERED;
	IDropTarget_Release(target);
	return S_OK;
}

/* The buttons and modifier keys held now, as MK_ flags. */
static DWORD key_state(void)
{
	DWORD keys = 0;
	if (GetAsyncKeyState(VK_LBUTTON) & 0x8000)
		keys |= MK_LBUTTON;
	if (GetAsyncKeyState(VK_RBUTTON) & 0x8000)
		keys |= MK_RBUTTON;
	if (GetAsyncKeyState(VK_MBUTTON) & 0x8000)
		keys |= MK_MBUTTON;
	if (GetKeyState(VK_SHIFT) & 0x8000)
		keys |= MK_SHIFT;
	if (GetKeyState(VK_CONTROL) & 0x8000)
		keys |= MK_CONTROL;
	if (GetKeyState(VK_MENU) & 0x8000)
		keys |= MK_ALT;
	return keys;
}

/* The drop target of the window under the point, or of the nearest window containing it that has one. */
static IDropTarget *target_at(POINT point)
{
	HWND window = WindowFromPoint(point);
	while (window) {
		IDropTarget *target = (IDropTarget *)GetPropW(window, drop_target_property);
		if (target)
			return target;
		if (!(GetWindowLongW(window, GWL_STYLE) & WS_CHILD))
			break;
		window = GetParent(window);
	}
	return NULL;
}

HRESULT WINAPI DoDragDrop(IDataObject *data, IDropSource *source, DWORD allowed, DWORD *effect)
{
	IDropTarget *target = NULL;
	DWORD current = DROPEFFECT_NONE;
	BOOL escape = FALSE;
	HRESULT hr;
	HWND tracker;
	MSG message;

	if (!data || !source || !effect)
		return E_INVALIDARG;
	*effect = DROPEFFECT_NONE;
	/* A hidden window of the thread holds the capture, so the pointer's input comes here wherever it goes. */
	tracker = CreateWindowExW(0, L"Static", L"", WS_POPUP, 0, 0, 0, 0, NULL, NULL, GetModuleHandleW(NULL), NULL);
	if (tracker)
		SetCapture(tracker);
	for (;;) {
		const DWORD keys = key_state();
		POINT point;
		POINTL at;

		hr = IDropSource_QueryContinueDrag(source, escape, keys);
		GetCursorPos(&point);
		at.x = point.x;
		at.y = point.y;
		if (hr != S_OK) {
			if (hr == DRAGDROP_S_DROP && target && current != DROPEFFECT_NONE) {
				DWORD result = current;
				if (FAILED(IDropTarget_Drop(target, data, keys, at, &result)))
					result = DROPEFFECT_NONE;
				*effect = result & allowed;
				if (*effect == DROPEFFECT_NONE)
					hr = DRAGDROP_S_CANCEL;
			} else {
				if (target)
					IDropTarget_DragLeave(target);
				/* Released where nothing takes the data, the drag ends as cancelled. */
				if (hr == DRAGDROP_S_DROP)
					hr = DRAGDROP_S_CANCEL;
			}
			break;
		}
		{
			IDropTarget *next = target_at(point);
			if (next != target) {
				if (target)
					IDropTarget_DragLeave(target);
				target = next;
				current = allowed;
				if (target && FAILED(IDropTarget_DragEnter(target, data, keys, at, &current)))
					target = NULL;
			} else if (target) {
				current = allowed;
				if (FAILED(IDropTarget_DragOver(target, keys, at, &current)))
					current = DROPEFFECT_NONE;
			}
			if (!target)
				current = DROPEFFECT_NONE;
			current &= allowed | DROPEFFECT_SCROLL;
			if (IDropSource_GiveFeedback(source, current & ~DROPEFFECT_SCROLL) == DRAGDROP_S_USEDEFAULTCURSORS)
				SetCursor(LoadCursorW(NULL, (current & ~DROPEFFECT_SCROLL) ? (LPCWSTR)IDC_ARROW : (LPCWSTR)IDC_NO));
			current &= ~DROPEFFECT_SCROLL;
		}
		if (!GetMessageW(&message, NULL, 0, 0)) {
			/* WM_QUIT ends the drag; it goes back on the queue for the program's own loop. */
			PostQuitMessage((int)message.wParam);
			if (target)
				IDropTarget_DragLeave(target);
			hr = DRAGDROP_S_CANCEL;
			break;
		}
		if (message.message == WM_KEYDOWN && message.wParam == VK_ESCAPE) {
			escape = TRUE;
			continue;
		}
		/* Pointer and keyboard input drives the drag; anything else, painting and timers among it, goes on as usual. */
		if ((message.message >= WM_MOUSEFIRST && message.message <= WM_MOUSELAST) ||
			(message.message >= WM_KEYFIRST && message.message <= WM_KEYLAST))
			continue;
		DispatchMessageW(&message);
	}
	if (tracker) {
		ReleaseCapture();
		DestroyWindow(tracker);
	}
	return hr;
}

/* OleDuplicateData: a copy of the data a handle holds, as the format keeps it. A bitmap is copied as a bitmap; the metafile formats
 * cannot be, as there are no metafiles here, and answer NULL; any other format is global memory, copied into a new block made with
 * the flags given (GMEM_MOVEABLE when none are). */
HANDLE WINAPI OleDuplicateData(HANDLE source, CLIPFORMAT format, UINT flags)
{
	SIZE_T size;
	HGLOBAL copy;
	void *from, *to;
	if (!source)
		return NULL;
	switch (format) {
	case CF_BITMAP:
		return CopyImage(source, IMAGE_BITMAP, 0, 0, 0);
	case CF_METAFILEPICT:
	case CF_ENHMETAFILE:
	case CF_PALETTE:
		return NULL;
	}
	size = GlobalSize(source);
	copy = GlobalAlloc(flags ? flags : GMEM_MOVEABLE, size ? size : 1);
	if (!copy)
		return NULL;
	from = GlobalLock(source);
	to = GlobalLock(copy);
	if (!from || !to) {
		if (from)
			GlobalUnlock(source);
		if (to)
			GlobalUnlock(copy);
		GlobalFree(copy);
		return NULL;
	}
	memcpy(to, from, size);
	GlobalUnlock(copy);
	GlobalUnlock(source);
	return copy;
}
