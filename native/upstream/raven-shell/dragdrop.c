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
 * program goes on painting.
 *
 * Files another program of the desktop drags over a window come from the host: it sends each step of that drag to a hidden broker
 * window of the thread that owns the window under the pointer (made by RegisterDragDrop and DragAcceptFiles), and the broker plays it
 * to the drop target there as DoDragDrop would, with a data object that holds the files as CF_HDROP, or hands the files to a window that
 * takes them the old way (WM_DROPFILES). */
#define CINTERFACE
#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <ole2.h>
#include <shellapi.h>
#include <shlobj.h>
#include <string.h>

static const WCHAR drop_target_property[] = L"RavenOleDropTarget";

static void ensure_broker(void);

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
	ensure_broker();
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

/* --- A drag carried out to the desktop. --- */

/* What a data object handed out goes back as the medium says: through the object that keeps it, or freed here. */
static void release_medium(STGMEDIUM *medium)
{
	if (medium->pUnkForRelease) {
		IUnknown_Release(medium->pUnkForRelease);
		return;
	}
	if (medium->tymed == TYMED_HGLOBAL && medium->hGlobal)
		GlobalFree(medium->hGlobal);
	else if (medium->tymed == TYMED_ISTREAM && medium->pstm)
		IStream_Release(medium->pstm);
}

/* Appends a name and its null to a list of names; the list keeps a second null after the last. */
static BOOL append_name(WCHAR **list, SIZE_T *used, SIZE_T *capacity, const WCHAR *name)
{
	const SIZE_T length = lstrlenW(name) + 1;
	if (*used + length + 1 > *capacity) {
		const SIZE_T wanted = (*used + length + 1) * 2 + 256;
		WCHAR *grown = *list ? HeapReAlloc(GetProcessHeap(), 0, *list, wanted * sizeof(WCHAR)) : HeapAlloc(GetProcessHeap(), 0, wanted * sizeof(WCHAR));
		if (!grown)
			return FALSE;
		*list = grown;
		*capacity = wanted;
	}
	memcpy(*list + *used, name, length * sizeof(WCHAR));
	*used += length;
	(*list)[*used] = 0;
	return TRUE;
}

/* Writes one file of a virtual file group (FileContents at index) to path. */
static BOOL write_contents(IDataObject *data, CLIPFORMAT contents, LONG index, const FILEDESCRIPTORW *descriptor, const WCHAR *path)
{
	FORMATETC format = {contents, NULL, DVASPECT_CONTENT, index, TYMED_ISTREAM | TYMED_HGLOBAL};
	STGMEDIUM medium;
	HANDLE file;
	BOOL ok = FALSE;
	DWORD written;
	memset(&medium, 0, sizeof(medium));
	if (FAILED(IDataObject_GetData(data, &format, &medium)))
		return FALSE;
	file = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (file != INVALID_HANDLE_VALUE) {
		if (medium.tymed == TYMED_ISTREAM && medium.pstm) {
			static BYTE buffer[65536];
			ULONG count = 0;
			LARGE_INTEGER start;
			start.QuadPart = 0;
			IStream_Seek(medium.pstm, start, STREAM_SEEK_SET, NULL);
			ok = TRUE;
			while (SUCCEEDED(IStream_Read(medium.pstm, buffer, sizeof(buffer), &count)) && count) {
				if (!WriteFile(file, buffer, count, &written, NULL) || written != count) {
					ok = FALSE;
					break;
				}
				count = 0;
			}
		} else if (medium.tymed == TYMED_HGLOBAL && medium.hGlobal) {
			SIZE_T size = GlobalSize(medium.hGlobal);
			const void *bytes = GlobalLock(medium.hGlobal);
			if (descriptor && (descriptor->dwFlags & FD_FILESIZE) && !descriptor->nFileSizeHigh && descriptor->nFileSizeLow < size)
				size = descriptor->nFileSizeLow;
			ok = bytes && WriteFile(file, bytes, (DWORD)size, &written, NULL) && written == size;
			if (bytes)
				GlobalUnlock(medium.hGlobal);
		}
		CloseHandle(file);
		if (!ok)
			DeleteFileW(path);
	}
	release_medium(&medium);
	return ok;
}

/* Removes the folders of earlier drags out that are older than a day: the file manager has long copied what it took from them. */
static void sweep_drag_folders(const WCHAR *root)
{
	WIN32_FIND_DATAW found;
	WCHAR pattern[MAX_PATH + 8];
	ULARGE_INTEGER now, limit;
	FILETIME current;
	HANDLE search;
	if (lstrlenW(root) > MAX_PATH - 30)
		return;
	GetSystemTimeAsFileTime(&current);
	now.LowPart = current.dwLowDateTime;
	now.HighPart = current.dwHighDateTime;
	limit.QuadPart = now.QuadPart - 864000000000ULL;
	wsprintfW(pattern, L"%s\\*", root);
	search = FindFirstFileW(pattern, &found);
	if (search == INVALID_HANDLE_VALUE)
		return;
	do {
		ULARGE_INTEGER written;
		WCHAR folder[MAX_PATH * 2], inner[MAX_PATH * 2];
		WIN32_FIND_DATAW file;
		HANDLE files;
		written.LowPart = found.ftLastWriteTime.dwLowDateTime;
		written.HighPart = found.ftLastWriteTime.dwHighDateTime;
		if (!(found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || found.cFileName[0] == L'.' || written.QuadPart > limit.QuadPart)
			continue;
		wsprintfW(folder, L"%s\\%s", root, found.cFileName);
		wsprintfW(inner, L"%s\\*", folder);
		files = FindFirstFileW(inner, &file);
		if (files != INVALID_HANDLE_VALUE) {
			do {
				if (!(file.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
					wsprintfW(inner, L"%s\\%s", folder, file.cFileName);
					DeleteFileW(inner);
				}
			} while (FindNextFileW(files, &file));
			FindClose(files);
		}
		RemoveDirectoryW(folder);
	} while (FindNextFileW(search, &found));
	FindClose(search);
}

/* The files the dragged data stands for, as names each ended by a null with a second null after the last: the files themselves for
 * CF_HDROP, or, for a group of virtual files (FileGroupDescriptorW with FileContents, as a program offers pages it makes on demand),
 * copies written to a folder of their own under the temporary folder, which the desktop's file manager copies from. NULL when there
 * are none. */
static WCHAR *dragged_files(IDataObject *data)
{
	FORMATETC format = {CF_HDROP, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
	STGMEDIUM medium;
	WCHAR *list = NULL;
	SIZE_T used = 0, capacity = 0;
	memset(&medium, 0, sizeof(medium));
	if (SUCCEEDED(IDataObject_GetData(data, &format, &medium))) {
		const UINT count = DragQueryFileW((HDROP)medium.hGlobal, 0xFFFFFFFF, NULL, 0);
		UINT index;
		for (index = 0; index < count; ++index) {
			WCHAR name[MAX_PATH * 4];
			if (DragQueryFileW((HDROP)medium.hGlobal, index, name, ARRAYSIZE(name)))
				append_name(&list, &used, &capacity, name);
		}
		release_medium(&medium);
		return list;
	}
	format.cfFormat = (CLIPFORMAT)RegisterClipboardFormatW(L"FileGroupDescriptorW");
	memset(&medium, 0, sizeof(medium));
	if (SUCCEEDED(IDataObject_GetData(data, &format, &medium)) && medium.tymed == TYMED_HGLOBAL) {
		const FILEGROUPDESCRIPTORW *group = GlobalLock(medium.hGlobal);
		const CLIPFORMAT contents = (CLIPFORMAT)RegisterClipboardFormatW(L"FileContents");
		WCHAR folder[MAX_PATH];
		const DWORD length = group ? GetTempPathW(MAX_PATH - 40, folder) : 0;
		if (length && length < MAX_PATH - 40) {
			UINT index;
			lstrcatW(folder, L"RavenDrag");
			CreateDirectoryW(folder, NULL);
			sweep_drag_folders(folder);
			wsprintfW(folder + lstrlenW(folder), L"\\%08lx%04lx", GetTickCount(), GetCurrentProcessId() & 0xffff);
			CreateDirectoryW(folder, NULL);
			for (index = 0; index < group->cItems && index < 1024; ++index) {
				const FILEDESCRIPTORW *descriptor = &group->fgd[index];
				WCHAR path[MAX_PATH * 2], *at;
				if ((descriptor->dwFlags & FD_ATTRIBUTES) && (descriptor->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
					continue;
				lstrcpyW(path, folder);
				lstrcatW(path, L"\\");
				at = path + lstrlenW(path);
				lstrcpynW(at, descriptor->cFileName[0] ? descriptor->cFileName : L"file", MAX_PATH);
				/* A name with folders in it keeps only its own part. */
				for (; *at; ++at)
					if (*at == L'\\' || *at == L'/' || *at == L':')
						*at = L'_';
				if (write_contents(data, contents, (LONG)index, descriptor, path))
					append_name(&list, &used, &capacity, path);
			}
		}
		if (group)
			GlobalUnlock(medium.hGlobal);
	}
	if (medium.tymed)
		release_medium(&medium);
	return list;
}

/* The host side of a drag out to the desktop (ravenhost.dll): absent, a drag stays inside the program. */
static BOOL (WINAPI *host_drag_start)(const WCHAR *);
static DWORD (WINAPI *host_drag_poll)(void);

static BOOL host_drag_available(void)
{
	static BOOL looked;
	if (!looked) {
		HMODULE host = LoadLibraryW(L"ravenhost.dll");
		if (host) {
			host_drag_start = (void *)GetProcAddress(host, "RavenDragStart");
			host_drag_poll = (void *)GetProcAddress(host, "RavenDragPoll");
		}
		looked = TRUE;
	}
	return host_drag_start && host_drag_poll;
}

/* Waits for the desktop to end a drag carried out to it, dispatching the program's other messages meanwhile. */
static HRESULT finish_outside(DWORD allowed, DWORD *effect)
{
	MSG message;
	for (;;) {
		const DWORD state = host_drag_poll();
		if (state == 1) {
			*effect = DROPEFFECT_COPY & allowed;
			return DRAGDROP_S_DROP;
		}
		if (state != 0)
			return DRAGDROP_S_CANCEL;
		MsgWaitForMultipleObjects(0, NULL, FALSE, 30, QS_ALLINPUT);
		while (PeekMessageW(&message, NULL, 0, 0, PM_REMOVE)) {
			if (message.message == WM_QUIT) {
				PostQuitMessage((int)message.wParam);
				return DRAGDROP_S_CANCEL;
			}
			/* The pointer belongs to the desktop's drag now; what input arrives here is not the program's. */
			if ((message.message >= WM_MOUSEFIRST && message.message <= WM_MOUSELAST) ||
				(message.message >= WM_KEYFIRST && message.message <= WM_KEYLAST))
				continue;
			DispatchMessageW(&message);
		}
	}
}

HRESULT WINAPI DoDragDrop(IDataObject *data, IDropSource *source, DWORD allowed, DWORD *effect)
{
	BOOL outside_tried = FALSE;
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
		/* Leaving every window of the program with the button held carries files out to the desktop, which copies them. */
		if (!outside_tried && !WindowFromPoint(point) && (keys & (MK_LBUTTON | MK_RBUTTON)) && (allowed & DROPEFFECT_COPY) &&
			host_drag_available()) {
			WCHAR *files = dragged_files(data);
			outside_tried = TRUE;
			if (files && host_drag_start(files)) {
				HeapFree(GetProcessHeap(), 0, files);
				if (target)
					IDropTarget_DragLeave(target);
				target = NULL;
				hr = finish_outside(allowed, effect);
				break;
			}
			if (files)
				HeapFree(GetProcessHeap(), 0, files);
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

/* --- Files dragged in from the desktop. --- */

/* The host's message to the broker: wParam is the step (1 the drag arrives, carrying the files as an HDROP in lParam that the broker
 * then owns; 2 it moves; 3 it leaves; 4 it is dropped), and the answer is the effect the window under the pointer takes. The pointer
 * position is the cursor's. */
#define RAVEN_DROP_MESSAGE (WM_APP + 0x3d0)
static const WCHAR broker_class[] = L"RavenDropBroker";
extern IMAGE_DOS_HEADER __ImageBase;

IEnumFORMATETC *raven_format_enumerator(const FORMATETC *formats, ULONG count);

typedef struct {
	IDataObject iface;
	LONG refs;
	HGLOBAL files;
} file_data;

static BOOL same_guid(REFIID left, REFIID right)
{
	return !memcmp(left, right, sizeof(IID));
}

static HRESULT WINAPI files_QueryInterface(IDataObject *iface, REFIID iid, void **out)
{
	if (!out)
		return E_POINTER;
	if (same_guid(iid, &IID_IUnknown) || same_guid(iid, &IID_IDataObject)) {
		*out = iface;
		IDataObject_AddRef(iface);
		return S_OK;
	}
	*out = NULL;
	return E_NOINTERFACE;
}

static ULONG WINAPI files_AddRef(IDataObject *iface)
{
	return (ULONG)InterlockedIncrement(&((file_data *)iface)->refs);
}

static ULONG WINAPI files_Release(IDataObject *iface)
{
	file_data *self = (file_data *)iface;
	const LONG refs = InterlockedDecrement(&self->refs);
	if (!refs) {
		GlobalFree(self->files);
		HeapFree(GetProcessHeap(), 0, self);
	}
	return (ULONG)refs;
}

static HRESULT files_check(const FORMATETC *format)
{
	if (!format)
		return E_INVALIDARG;
	if (format->cfFormat != CF_HDROP)
		return DV_E_FORMATETC;
	if (!(format->tymed & TYMED_HGLOBAL))
		return DV_E_TYMED;
	if (format->dwAspect != DVASPECT_CONTENT)
		return DV_E_DVASPECT;
	return S_OK;
}

static HRESULT WINAPI files_GetData(IDataObject *iface, FORMATETC *format, STGMEDIUM *medium)
{
	HRESULT result;
	if (!medium)
		return E_INVALIDARG;
	memset(medium, 0, sizeof(*medium));
	result = files_check(format);
	if (FAILED(result))
		return result;
	medium->hGlobal = OleDuplicateData(((file_data *)iface)->files, CF_HDROP, GMEM_MOVEABLE);
	if (!medium->hGlobal)
		return E_OUTOFMEMORY;
	medium->tymed = TYMED_HGLOBAL;
	return S_OK;
}

static HRESULT WINAPI files_GetDataHere(IDataObject *iface, FORMATETC *format, STGMEDIUM *medium)
{
	return E_NOTIMPL;
}

static HRESULT WINAPI files_QueryGetData(IDataObject *iface, FORMATETC *format)
{
	return files_check(format);
}

static HRESULT WINAPI files_GetCanonicalFormatEtc(IDataObject *iface, FORMATETC *format, FORMATETC *out)
{
	if (!format || !out)
		return E_INVALIDARG;
	*out = *format;
	out->ptd = NULL;
	return DATA_S_SAMEFORMATETC;
}

static HRESULT WINAPI files_SetData(IDataObject *iface, FORMATETC *format, STGMEDIUM *medium, BOOL release)
{
	return E_NOTIMPL;
}

static HRESULT WINAPI files_EnumFormatEtc(IDataObject *iface, DWORD direction, IEnumFORMATETC **out)
{
	static const FORMATETC formats[] = {{CF_HDROP, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL}};
	if (!out)
		return E_INVALIDARG;
	*out = NULL;
	if (direction != DATADIR_GET)
		return E_NOTIMPL;
	*out = raven_format_enumerator(formats, 1);
	return *out ? S_OK : E_OUTOFMEMORY;
}

static HRESULT WINAPI files_DAdvise(IDataObject *iface, FORMATETC *format, DWORD flags, IAdviseSink *sink, DWORD *connection)
{
	return OLE_E_ADVISENOTSUPPORTED;
}

static HRESULT WINAPI files_DUnadvise(IDataObject *iface, DWORD connection)
{
	return OLE_E_ADVISENOTSUPPORTED;
}

static HRESULT WINAPI files_EnumDAdvise(IDataObject *iface, IEnumSTATDATA **out)
{
	return OLE_E_ADVISENOTSUPPORTED;
}

static const IDataObjectVtbl files_vtbl = {files_QueryInterface, files_AddRef, files_Release, files_GetData, files_GetDataHere,
	files_QueryGetData, files_GetCanonicalFormatEtc, files_SetData, files_EnumFormatEtc, files_DAdvise, files_DUnadvise, files_EnumDAdvise};

/* What the broker of a thread knows of the drag in progress: the files, the drop target under the pointer, or the window that takes
 * files by WM_DROPFILES when there is no target. */
typedef struct {
	file_data *data;
	IDropTarget *target;
	HWND accepter;
} drop_state;

static void drop_end(drop_state *state)
{
	if (state->target)
		IDropTarget_Release(state->target);
	if (state->data)
		IDataObject_Release(&state->data->iface);
	state->target = NULL;
	state->data = NULL;
	state->accepter = NULL;
}

/* The window under the point that takes dropped files the old way, or its nearest parent that does. */
static HWND accepter_at(POINT point)
{
	HWND window = WindowFromPoint(point);
	while (window) {
		if (GetWindowLongW(window, GWL_EXSTYLE) & WS_EX_ACCEPTFILES)
			return window;
		if (!(GetWindowLongW(window, GWL_STYLE) & WS_CHILD))
			break;
		window = GetParent(window);
	}
	return NULL;
}

/* A dragged file is copied, as from another drive: the desktop's file manager never deletes the original. */
static DWORD drop_step(drop_state *state, UINT stage, HGLOBAL files)
{
	const DWORD offered = DROPEFFECT_COPY | DROPEFFECT_MOVE | DROPEFFECT_LINK;
	DWORD keys = MK_LBUTTON, effect = DROPEFFECT_NONE;
	IDropTarget *next;
	POINT point;
	POINTL at;

	if (stage == 1) {
		drop_end(state);
		if (!files)
			return DROPEFFECT_NONE;
		state->data = HeapAlloc(GetProcessHeap(), 0, sizeof(*state->data));
		if (!state->data) {
			GlobalFree(files);
			return DROPEFFECT_NONE;
		}
		state->data->iface.lpVtbl = (IDataObjectVtbl *)&files_vtbl;
		state->data->refs = 1;
		state->data->files = files;
	}
	if (!state->data)
		return DROPEFFECT_NONE;
	if (stage == 3) {
		if (state->target)
			IDropTarget_DragLeave(state->target);
		drop_end(state);
		return DROPEFFECT_NONE;
	}
	if (GetKeyState(VK_SHIFT) & 0x8000)
		keys |= MK_SHIFT;
	if (GetKeyState(VK_CONTROL) & 0x8000)
		keys |= MK_CONTROL;
	GetCursorPos(&point);
	at.x = point.x;
	at.y = point.y;
	next = target_at(point);
	if (next != state->target) {
		if (state->target) {
			IDropTarget_DragLeave(state->target);
			IDropTarget_Release(state->target);
		}
		state->target = next;
		if (next) {
			IDropTarget_AddRef(next);
			effect = offered;
			if (FAILED(IDropTarget_DragEnter(next, &state->data->iface, keys, at, &effect))) {
				IDropTarget_Release(next);
				state->target = NULL;
				effect = DROPEFFECT_NONE;
			}
		}
	} else if (state->target) {
		effect = offered;
		if (FAILED(IDropTarget_DragOver(state->target, keys, at, &effect)))
			effect = DROPEFFECT_NONE;
	}
	effect &= offered;
	state->accepter = state->target ? NULL : accepter_at(point);
	if (state->accepter)
		effect = DROPEFFECT_COPY;
	if (stage == 4) {
		if (state->target && effect) {
			DWORD result = effect;
			if (FAILED(IDropTarget_Drop(state->target, &state->data->iface, keys, at, &result)))
				result = DROPEFFECT_NONE;
			effect = result & offered;
		} else if (state->target) {
			IDropTarget_DragLeave(state->target);
		} else if (state->accepter) {
			HGLOBAL copy = OleDuplicateData(state->data->files, CF_HDROP, GMEM_MOVEABLE);
			DROPFILES *header = copy ? GlobalLock(copy) : NULL;
			effect = DROPEFFECT_NONE;
			if (header) {
				POINT client = point;
				ScreenToClient(state->accepter, &client);
				header->pt = client;
				header->fNC = FALSE;
				GlobalUnlock(copy);
				if (PostMessageW(state->accepter, WM_DROPFILES, (WPARAM)copy, 0))
					effect = DROPEFFECT_COPY;
				else
					GlobalFree(copy);
			} else if (copy) {
				GlobalFree(copy);
			}
		}
		drop_end(state);
	}
	return effect ? DROPEFFECT_COPY : DROPEFFECT_NONE;
}

static LRESULT CALLBACK broker_procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
	drop_state *state;
	if (message != RAVEN_DROP_MESSAGE)
		return DefWindowProcW(window, message, wparam, lparam);
	state = (drop_state *)GetWindowLongPtrW(window, GWLP_USERDATA);
	if (!state) {
		state = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*state));
		if (!state) {
			if (wparam == 1 && lparam)
				GlobalFree((HGLOBAL)lparam);
			return DROPEFFECT_NONE;
		}
		SetWindowLongPtrW(window, GWLP_USERDATA, (LONG_PTR)state);
	}
	return drop_step(state, (UINT)wparam, (HGLOBAL)lparam);
}

/* Threads that have a broker, so that each makes one. */
static SRWLOCK broker_lock = SRWLOCK_INIT;
static DWORD broker_threads[256];
static UINT broker_count;

static void ensure_broker(void)
{
	const DWORD thread = GetCurrentThreadId();
	WNDCLASSW definition;
	BOOL present = FALSE;
	UINT index;
	AcquireSRWLockExclusive(&broker_lock);
	for (index = 0; index < broker_count; ++index)
		if (broker_threads[index] == thread)
			present = TRUE;
	if (!present && broker_count < ARRAYSIZE(broker_threads))
		broker_threads[broker_count++] = thread;
	else
		present = TRUE;
	ReleaseSRWLockExclusive(&broker_lock);
	if (present)
		return;
	memset(&definition, 0, sizeof(definition));
	definition.lpfnWndProc = broker_procedure;
	definition.hInstance = (HINSTANCE)&__ImageBase;
	definition.lpszClassName = broker_class;
	RegisterClassW(&definition);
	CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, broker_class, L"", WS_POPUP, 0, 0, 0, 0, NULL, NULL, (HINSTANCE)&__ImageBase, NULL);
}

/* --- The files of a drop, as WM_DROPFILES and CF_HDROP hand them over: a DROPFILES header and the names after it, each ended by a
 * null, with a second null after the last. --- */

/* The name at index, written into the buffer when there is one; the count of names for index 0xFFFFFFFF. Answers the length of the
 * name without its null, or what was copied of it. */
static UINT query_file(HDROP drop, UINT index, void *buffer, UINT capacity, BOOL wide)
{
	const DROPFILES *header = drop ? GlobalLock((HGLOBAL)drop) : NULL;
	const SIZE_T size = drop ? GlobalSize((HGLOBAL)drop) : 0;
	UINT count = 0, result = 0;
	SIZE_T offset;
	if (!header)
		return 0;
	offset = header->pFiles;
	while (offset < size) {
		const WCHAR *name = (const WCHAR *)((const BYTE *)header + offset);
		const char *narrow = (const char *)header + offset;
		SIZE_T length = 0;
		if (header->fWide) {
			while (offset + (length + 1) * 2 <= size && name[length])
				++length;
		} else {
			while (offset + length < size && narrow[length])
				++length;
		}
		if (!length)
			break;
		if (count == index) {
			if (header->fWide && wide) {
				result = (UINT)length;
				if (buffer && capacity) {
					result = (UINT)(length < capacity - 1 ? length : capacity - 1);
					memcpy(buffer, name, result * sizeof(WCHAR));
					((WCHAR *)buffer)[result] = 0;
				}
			} else if (header->fWide) {
				const int needed = WideCharToMultiByte(CP_ACP, 0, name, (int)length, NULL, 0, NULL, NULL);
				result = (UINT)needed;
				if (buffer && capacity) {
					result = (UINT)WideCharToMultiByte(CP_ACP, 0, name, (int)length, buffer, (int)capacity - 1, NULL, NULL);
					((char *)buffer)[result] = 0;
				}
			} else if (wide) {
				const int needed = MultiByteToWideChar(CP_ACP, 0, narrow, (int)length, NULL, 0);
				result = (UINT)needed;
				if (buffer && capacity) {
					result = (UINT)MultiByteToWideChar(CP_ACP, 0, narrow, (int)length, buffer, (int)capacity - 1);
					((WCHAR *)buffer)[result] = 0;
				}
			} else {
				result = (UINT)length;
				if (buffer && capacity) {
					result = (UINT)(length < capacity - 1 ? length : capacity - 1);
					memcpy(buffer, narrow, result);
					((char *)buffer)[result] = 0;
				}
			}
			GlobalUnlock((HGLOBAL)drop);
			return result;
		}
		++count;
		offset += (length + 1) * (header->fWide ? 2 : 1);
	}
	GlobalUnlock((HGLOBAL)drop);
	return index == 0xFFFFFFFF ? count : 0;
}

UINT WINAPI DragQueryFileW(HDROP drop, UINT index, LPWSTR buffer, UINT capacity)
{
	return query_file(drop, index, buffer, capacity, TRUE);
}

UINT WINAPI DragQueryFileA(HDROP drop, UINT index, LPSTR buffer, UINT capacity)
{
	return query_file(drop, index, buffer, capacity, FALSE);
}

BOOL WINAPI DragQueryPoint(HDROP drop, POINT *point)
{
	const DROPFILES *header = drop ? GlobalLock((HGLOBAL)drop) : NULL;
	BOOL client;
	if (!header || !point) {
		if (header)
			GlobalUnlock((HGLOBAL)drop);
		return FALSE;
	}
	*point = header->pt;
	client = !header->fNC;
	GlobalUnlock((HGLOBAL)drop);
	return client;
}

void WINAPI DragFinish(HDROP drop)
{
	if (drop)
		GlobalFree((HGLOBAL)drop);
}

void WINAPI DragAcceptFiles(HWND window, BOOL accept)
{
	LONG style;
	if (!window || !IsWindow(window))
		return;
	style = GetWindowLongW(window, GWL_EXSTYLE);
	SetWindowLongW(window, GWL_EXSTYLE, accept ? (style | WS_EX_ACCEPTFILES) : (style & ~WS_EX_ACCEPTFILES));
	if (accept)
		ensure_broker();
}
