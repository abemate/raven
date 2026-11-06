/* PageSetupDlgW: the paper, orientation and margins of a page, for the default printer or the one the request names. The printer's
 * answers come from winspool (DocumentPropertiesW, DeviceCapabilitiesW), loaded when the dialog is first asked for, so the component
 * does not depend on the print spooler to load. The dialog is a frame template whose controls are made in dialog units once it exists. */
#include <windows.h>
#include <commdlg.h>
#include <winspool.h>

typedef BOOL (WINAPI *GetDefaultPrinterWFn)(LPWSTR, LPDWORD);
typedef BOOL (WINAPI *OpenPrinterWFn)(LPWSTR, LPHANDLE, LPPRINTER_DEFAULTSW);
typedef BOOL (WINAPI *ClosePrinterFn)(HANDLE);
typedef LONG (WINAPI *DocumentPropertiesWFn)(HWND, HANDLE, LPWSTR, PDEVMODEW, PDEVMODEW, DWORD);
typedef DWORD (WINAPI *DeviceCapabilitiesWFn)(LPCWSTR, LPCWSTR, WORD, LPWSTR, const DEVMODEW *);

static struct {
	BOOL loaded;
	GetDefaultPrinterWFn default_printer;
	OpenPrinterWFn open;
	ClosePrinterFn close;
	DocumentPropertiesWFn properties;
	DeviceCapabilitiesWFn capabilities;
} spool;

static BOOL load_spool(void)
{
	HMODULE module;
	if (spool.loaded)
		return spool.properties != NULL;
	spool.loaded = TRUE;
	if (!(module = LoadLibraryW(L"winspool.drv")))
		return FALSE;
	spool.default_printer = (GetDefaultPrinterWFn)GetProcAddress(module, "GetDefaultPrinterW");
	spool.open = (OpenPrinterWFn)GetProcAddress(module, "OpenPrinterW");
	spool.close = (ClosePrinterFn)GetProcAddress(module, "ClosePrinter");
	spool.properties = (DocumentPropertiesWFn)GetProcAddress(module, "DocumentPropertiesW");
	spool.capabilities = (DeviceCapabilitiesWFn)GetProcAddress(module, "DeviceCapabilitiesW");
	if (!spool.default_printer || !spool.open || !spool.close || !spool.properties || !spool.capabilities)
		spool.properties = NULL;
	return spool.properties != NULL;
}

#define MAX_PAPERS 64
#define ID_PAPER 100
#define ID_PORTRAIT 101
#define ID_LANDSCAPE 102
#define ID_LEFT 103
#define ID_RIGHT 104
#define ID_TOP 105
#define ID_BOTTOM 106

typedef struct {
	PAGESETUPDLGW *request;
	WCHAR printer[256];
	DEVMODEW *mode;
	int papers;
	WORD paper_ids[MAX_PAPERS];
	WCHAR paper_names[MAX_PAPERS][64];
	POINT paper_sizes[MAX_PAPERS]; /* tenths of a millimetre, portrait */
	BOOL inches;                   /* the request's units: thousandths of an inch, else hundredths of a millimetre */
} PageSetup;

/* A length in the request's units from tenths of a millimetre. */
static LONG from_tenth_mm(const PageSetup *setup, LONG value)
{
	return setup->inches ? MulDiv(value, 1000, 254) : value * 10;
}

/* Margins are shown in whole millimetres, or inches with two decimals. */
static void show_margin(HWND dialog, int id, const PageSetup *setup, LONG value)
{
	WCHAR text[32];
	if (setup->inches) {
		LONG hundredths = (value + 5) / 10;
		wsprintfW(text, L"%ld.%02ld", hundredths / 100, hundredths % 100);
	} else {
		wsprintfW(text, L"%ld", (value + 50) / 100);
	}
	SetDlgItemTextW(dialog, id, text);
}

/* The margin typed into a field, in the request's units; FALSE when it is not a number. */
static BOOL read_margin(HWND dialog, int id, const PageSetup *setup, LONG *value)
{
	WCHAR text[32], *at = text;
	LONG whole = 0, fraction = 0, places = 0;
	BOOL digits = FALSE;
	GetDlgItemTextW(dialog, id, text, 32);
	while (*at == L' ')
		++at;
	while (*at >= L'0' && *at <= L'9' && whole < 100000) {
		whole = whole * 10 + (*at++ - L'0');
		digits = TRUE;
	}
	if (*at == L'.' || *at == L',') {
		++at;
		while (*at >= L'0' && *at <= L'9') {
			if (places < 3) {
				fraction = fraction * 10 + (*at - L'0');
				++places;
			}
			++at;
			digits = TRUE;
		}
	}
	while (*at == L' ')
		++at;
	if (!digits || *at)
		return FALSE;
	while (places < 3) {
		fraction *= 10;
		++places;
	}
	/* fraction now counts thousandths of the unit shown. */
	*value = setup->inches ? whole * 1000 + fraction : whole * 100 + fraction / 10;
	return TRUE;
}

static HWND make(HWND dialog, const WCHAR *type, const WCHAR *text, DWORD style, int x, int y, int width, int height, int id)
{
	RECT place = {x, y, x + width, y + height};
	HWND control;
	MapDialogRect(dialog, &place);
	control = CreateWindowExW(!lstrcmpiW(type, L"Edit") ? WS_EX_CLIENTEDGE : 0, type, text, WS_CHILD | WS_VISIBLE | style,
		place.left, place.top, place.right - place.left, place.bottom - place.top, dialog, (HMENU)(INT_PTR)id, GetModuleHandleW(NULL), NULL);
	SendMessageW(control, WM_SETFONT, SendMessageW(dialog, WM_GETFONT, 0, 0), FALSE);
	return control;
}

static INT_PTR CALLBACK page_setup_proc(HWND dialog, UINT message, WPARAM wparam, LPARAM lparam)
{
	PageSetup *setup = (PageSetup *)GetWindowLongPtrW(dialog, DWLP_USER);
	if (message == WM_INITDIALOG) {
		HWND list;
		int index, chosen = 0;
		const WCHAR *unit;
		setup = (PageSetup *)lparam;
		SetWindowLongPtrW(dialog, DWLP_USER, (LONG_PTR)setup);
		unit = setup->inches ? L"Margins (inches)" : L"Margins (millimetres)";
		make(dialog, L"Button", L"Paper", BS_GROUPBOX, 7, 7, 216, 34, -1);
		make(dialog, L"Static", L"&Size:", SS_LEFT, 15, 22, 30, 10, -1);
		list = make(dialog, L"ComboBox", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 48, 20, 166, 140, ID_PAPER);
		for (index = 0; index < setup->papers; ++index) {
			SendMessageW(list, CB_ADDSTRING, 0, (LPARAM)setup->paper_names[index]);
			if (setup->paper_ids[index] == setup->mode->dmPaperSize)
				chosen = index;
		}
		SendMessageW(list, CB_SETCURSEL, chosen, 0);
		make(dialog, L"Button", L"Orientation", BS_GROUPBOX, 7, 46, 80, 46, -1);
		make(dialog, L"Button", L"P&ortrait", BS_AUTORADIOBUTTON | WS_GROUP | WS_TABSTOP, 15, 60, 66, 10, ID_PORTRAIT);
		make(dialog, L"Button", L"L&andscape", BS_AUTORADIOBUTTON, 15, 75, 66, 10, ID_LANDSCAPE);
		CheckRadioButton(dialog, ID_PORTRAIT, ID_LANDSCAPE, setup->mode->dmOrientation == DMORIENT_LANDSCAPE ? ID_LANDSCAPE : ID_PORTRAIT);
		make(dialog, L"Button", unit, BS_GROUPBOX, 93, 46, 130, 46, -1);
		make(dialog, L"Static", L"&Left:", SS_LEFT, 100, 60, 22, 10, -1);
		make(dialog, L"Edit", L"", ES_AUTOHSCROLL | WS_TABSTOP | WS_GROUP, 124, 58, 30, 12, ID_LEFT);
		make(dialog, L"Static", L"&Right:", SS_LEFT, 160, 60, 24, 10, -1);
		make(dialog, L"Edit", L"", ES_AUTOHSCROLL | WS_TABSTOP, 186, 58, 30, 12, ID_RIGHT);
		make(dialog, L"Static", L"&Top:", SS_LEFT, 100, 75, 22, 10, -1);
		make(dialog, L"Edit", L"", ES_AUTOHSCROLL | WS_TABSTOP, 124, 73, 30, 12, ID_TOP);
		make(dialog, L"Static", L"&Bottom:", SS_LEFT, 160, 75, 26, 10, -1);
		make(dialog, L"Edit", L"", ES_AUTOHSCROLL | WS_TABSTOP, 186, 73, 30, 12, ID_BOTTOM);
		show_margin(dialog, ID_LEFT, setup, setup->request->rtMargin.left);
		show_margin(dialog, ID_RIGHT, setup, setup->request->rtMargin.right);
		show_margin(dialog, ID_TOP, setup, setup->request->rtMargin.top);
		show_margin(dialog, ID_BOTTOM, setup, setup->request->rtMargin.bottom);
		make(dialog, L"Button", L"OK", BS_DEFPUSHBUTTON | WS_TABSTOP | WS_GROUP, 119, 100, 50, 14, IDOK);
		make(dialog, L"Button", L"Cancel", BS_PUSHBUTTON | WS_TABSTOP, 173, 100, 50, 14, IDCANCEL);
		if (setup->request->Flags & PSD_DISABLEPAPER)
			EnableWindow(list, FALSE);
		if (setup->request->Flags & PSD_DISABLEORIENTATION) {
			EnableWindow(GetDlgItem(dialog, ID_PORTRAIT), FALSE);
			EnableWindow(GetDlgItem(dialog, ID_LANDSCAPE), FALSE);
		}
		if (setup->request->Flags & PSD_DISABLEMARGINS)
			for (index = ID_LEFT; index <= ID_BOTTOM; ++index)
				EnableWindow(GetDlgItem(dialog, index), FALSE);
		SetFocus(list);
		return FALSE;
	}
	if (message == WM_COMMAND && LOWORD(wparam) == IDOK && setup) {
		LONG margins[4];
		int index, id;
		for (id = ID_LEFT; id <= ID_BOTTOM; ++id)
			if (!read_margin(dialog, id, setup, &margins[id - ID_LEFT])) {
				MessageBoxW(dialog, L"Enter a number for each margin.", L"Page Setup", MB_OK | MB_ICONWARNING);
				SetFocus(GetDlgItem(dialog, id));
				SendDlgItemMessageW(dialog, id, EM_SETSEL, 0, -1);
				return TRUE;
			}
		index = (int)SendDlgItemMessageW(dialog, ID_PAPER, CB_GETCURSEL, 0, 0);
		if (index >= 0 && index < setup->papers) {
			setup->mode->dmPaperSize = setup->paper_ids[index];
			setup->mode->dmPaperWidth = (short)setup->paper_sizes[index].x;
			setup->mode->dmPaperLength = (short)setup->paper_sizes[index].y;
			setup->mode->dmFields |= DM_PAPERSIZE;
		}
		setup->mode->dmOrientation = IsDlgButtonChecked(dialog, ID_LANDSCAPE) == BST_CHECKED ? DMORIENT_LANDSCAPE : DMORIENT_PORTRAIT;
		setup->mode->dmFields |= DM_ORIENTATION;
		setup->request->rtMargin.left = margins[0];
		setup->request->rtMargin.right = margins[1];
		setup->request->rtMargin.top = margins[2];
		setup->request->rtMargin.bottom = margins[3];
		EndDialog(dialog, IDOK);
		return TRUE;
	}
	if (message == WM_COMMAND && LOWORD(wparam) == IDCANCEL) {
		EndDialog(dialog, IDCANCEL);
		return TRUE;
	}
	return FALSE;
}

/* The paper the mode names, in tenths of a millimetre and portrait; the mode's own size when the printer does not list it. */
static POINT paper_of(const PageSetup *setup)
{
	POINT size = {setup->mode->dmPaperWidth, setup->mode->dmPaperLength};
	int index;
	for (index = 0; index < setup->papers; ++index)
		if (setup->paper_ids[index] == setup->mode->dmPaperSize)
			return setup->paper_sizes[index];
	if (size.x <= 0 || size.y <= 0) {
		size.x = 2100;
		size.y = 2970;
	}
	return size;
}

static HGLOBAL make_devnames(const WCHAR *printer)
{
	static const WCHAR driver[] = L"winspool", port[] = L"";
	const SIZE_T header = sizeof(DEVNAMES) / sizeof(WCHAR), name = lstrlenW(printer) + 1;
	HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, (header + 9 + name + 1) * sizeof(WCHAR));
	DEVNAMES *names;
	WCHAR *text;
	if (!memory || !(names = GlobalLock(memory)))
		return memory;
	text = (WCHAR *)names;
	names->wDriverOffset = (WORD)header;
	lstrcpyW(text + header, driver);
	names->wDeviceOffset = (WORD)(header + 9);
	lstrcpyW(text + header + 9, printer);
	names->wOutputOffset = (WORD)(header + 9 + name);
	lstrcpyW(text + header + 9 + name, port);
	names->wDefault = 1;
	GlobalUnlock(memory);
	return memory;
}

BOOL WINAPI PageSetupDlgW(LPPAGESETUPDLGW request)
{
	PageSetup *setup;
	HANDLE printer = NULL;
	LONG size;
	DWORD length;
	BOOL accepted = TRUE;
	POINT paper;
	if (!request || request->lStructSize != sizeof(PAGESETUPDLGW) || !load_spool())
		return FALSE;
	if (!(setup = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof *setup)))
		return FALSE;
	setup->request = request;
	/* The printer the request names, else the default one. */
	if (request->hDevNames) {
		const DEVNAMES *names = GlobalLock(request->hDevNames);
		if (names) {
			lstrcpynW(setup->printer, (const WCHAR *)names + names->wDeviceOffset, 256);
			GlobalUnlock(request->hDevNames);
		}
	}
	length = 256;
	if (!setup->printer[0] && !spool.default_printer(setup->printer, &length))
		goto failed;
	if (!spool.open(setup->printer, &printer, NULL))
		goto failed;
	size = spool.properties(request->hwndOwner, printer, setup->printer, NULL, NULL, 0);
	if (size < (LONG)sizeof(DEVMODEW) || !(setup->mode = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size)))
		goto failed;
	/* The mode the request holds goes over the printer's defaults. */
	if (request->hDevMode) {
		DEVMODEW *given = GlobalLock(request->hDevMode);
		if (given) {
			spool.properties(request->hwndOwner, printer, setup->printer, setup->mode, given, DM_IN_BUFFER | DM_OUT_BUFFER);
			GlobalUnlock(request->hDevMode);
		}
	} else {
		spool.properties(request->hwndOwner, printer, setup->printer, setup->mode, NULL, DM_OUT_BUFFER);
	}
	spool.close(printer);
	printer = NULL;
	/* The papers the printer takes: their numbers, names and sizes. */
	{
		DWORD count = spool.capabilities(setup->printer, NULL, DC_PAPERS, NULL, setup->mode);
		if (count != (DWORD)-1 && count > 0) {
			WCHAR *names;
			if (count > MAX_PAPERS)
				count = MAX_PAPERS;
			if ((names = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, 64 * sizeof(WCHAR) * (count + 64)))) {
				POINT *sizes = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(POINT) * (count + 64));
				WORD *ids = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(WORD) * (count + 64));
				if (sizes && ids) {
					DWORD index;
					spool.capabilities(setup->printer, NULL, DC_PAPERS, (LPWSTR)ids, setup->mode);
					spool.capabilities(setup->printer, NULL, DC_PAPERNAMES, names, setup->mode);
					spool.capabilities(setup->printer, NULL, DC_PAPERSIZE, (LPWSTR)sizes, setup->mode);
					for (index = 0; index < count; ++index) {
						setup->paper_ids[index] = ids[index];
						lstrcpynW(setup->paper_names[index], names + index * 64, 64);
						setup->paper_sizes[index] = sizes[index];
					}
					setup->papers = (int)count;
				}
				if (sizes)
					HeapFree(GetProcessHeap(), 0, sizes);
				if (ids)
					HeapFree(GetProcessHeap(), 0, ids);
				HeapFree(GetProcessHeap(), 0, names);
			}
		}
	}
	/* Units: what the request asks for, else the locale's system of measurement. */
	if (request->Flags & PSD_INTHOUSANDTHSOFINCHES) {
		setup->inches = TRUE;
	} else if (!(request->Flags & PSD_INHUNDREDTHSOFMILLIMETERS)) {
		WCHAR measure[4] = L"0";
		GetLocaleInfoW(LOCALE_USER_DEFAULT, LOCALE_IMEASURE, measure, 4);
		setup->inches = measure[0] == L'1';
		request->Flags |= setup->inches ? PSD_INTHOUSANDTHSOFINCHES : PSD_INHUNDREDTHSOFMILLIMETERS;
	}
	/* Margins: the request's when it gives them, else an inch (or 25 mm) all round. */
	if (!(request->Flags & PSD_MARGINS)) {
		const LONG margin = setup->inches ? 1000 : 2500;
		SetRect(&request->rtMargin, margin, margin, margin, margin);
	}
	if (!(request->Flags & PSD_RETURNDEFAULT)) {
		WORD *buffer = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, 256), *at = buffer;
		static const WCHAR title[] = L"Page Setup", font[] = L"MS Shell Dlg";
		if (!buffer)
			goto failed;
		*(DWORD *)at = DS_SETFONT | DS_MODALFRAME | DS_CENTER | WS_POPUP | WS_CAPTION | WS_SYSMENU;
		at += 4; /* style, extended style */
		*at++ = 0; /* no controls in the template */
		*at++ = 0; *at++ = 0; *at++ = 230; *at++ = 121;
		*at++ = 0; *at++ = 0; /* no menu, the dialog class */
		lstrcpyW((WCHAR *)at, title);
		at += lstrlenW(title) + 1;
		*at++ = 8;
		lstrcpyW((WCHAR *)at, font);
		accepted = DialogBoxIndirectParamW(GetModuleHandleW(NULL), (LPCDLGTEMPLATEW)buffer, request->hwndOwner, page_setup_proc, (LPARAM)setup) == IDOK;
		HeapFree(GetProcessHeap(), 0, buffer);
	}
	if (accepted) {
		HGLOBAL mode;
		DEVMODEW *target;
		const SIZE_T bytes = setup->mode->dmSize + setup->mode->dmDriverExtra;
		paper = paper_of(setup);
		if (setup->mode->dmOrientation == DMORIENT_LANDSCAPE)
			request->ptPaperSize.x = from_tenth_mm(setup, paper.y), request->ptPaperSize.y = from_tenth_mm(setup, paper.x);
		else
			request->ptPaperSize.x = from_tenth_mm(setup, paper.x), request->ptPaperSize.y = from_tenth_mm(setup, paper.y);
		/* The whole page is printable. */
		SetRect(&request->rtMinMargin, 0, 0, 0, 0);
		mode = request->hDevMode ? GlobalReAlloc(request->hDevMode, bytes, GMEM_MOVEABLE) : GlobalAlloc(GMEM_MOVEABLE, bytes);
		if (mode && (target = GlobalLock(mode))) {
			CopyMemory(target, setup->mode, bytes);
			GlobalUnlock(mode);
			request->hDevMode = mode;
		}
		if (!request->hDevNames)
			request->hDevNames = make_devnames(setup->printer);
	}
	HeapFree(GetProcessHeap(), 0, setup->mode);
	HeapFree(GetProcessHeap(), 0, setup);
	return accepted;
failed:
	if (printer)
		spool.close(printer);
	if (setup->mode)
		HeapFree(GetProcessHeap(), 0, setup->mode);
	HeapFree(GetProcessHeap(), 0, setup);
	return FALSE;
}
