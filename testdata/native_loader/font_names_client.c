#define WIN32_LEAN_AND_MEAN
#include <windows.h>

/* A usual Windows face that is drawn from a substitute still carries its Windows names in the file's name table: PDF-XChange finds the
   standard PDF fonts by them. */
static int has(const BYTE *table,DWORD size,const char *text)
{
	DWORD length=0;
	while (text[length]) ++length;
	for (DWORD at=0; at+length*2<=size; ++at) {
		DWORD i=0;
		while (i<length && table[at+i*2]==0 && table[at+i*2+1]==(BYTE)text[i]) ++i;
		if (i==length) return 1;
	}
	return 0;
}

static int check(HDC dc,const WCHAR *family,LONG weight,BYTE italic,const char *postscript)
{
	static BYTE table[8192];
	LOGFONTW font={0};
	font.lfHeight=-12; font.lfWeight=weight; font.lfItalic=italic; font.lfCharSet=DEFAULT_CHARSET;
	lstrcpyW(font.lfFaceName,family);
	HFONT handle=CreateFontIndirectW(&font);
	if (!handle) return 0;
	HGDIOBJ old=SelectObject(dc,handle);
	const DWORD tag='n'|'a'<<8|'m'<<16|'e'<<24;
	DWORD size=GetFontData(dc,tag,0,table,sizeof table);
	SelectObject(dc,old);
	DeleteObject(handle);
	return size!=GDI_ERROR && has(table,size,postscript);
}

int raven_entry(void)
{
	HDC dc=CreateCompatibleDC(NULL);
	if (!dc) return 1;
	if (!check(dc,L"Times New Roman",400,0,"TimesNewRomanPSMT")) return 2;
	if (!check(dc,L"Times New Roman",700,0,"TimesNewRomanPS-BoldMT")) return 3;
	if (!check(dc,L"Times New Roman",400,1,"TimesNewRomanPS-ItalicMT")) return 4;
	if (!check(dc,L"Arial",400,0,"ArialMT")) return 5;
	if (!check(dc,L"Courier New",700,1,"CourierNewPS-BoldItalicMT")) return 6;
	if (!check(dc,L"Liberation Serif",400,0,"LiberationSerif")) return 7;
	/* A font whose other tables were read is being embedded, and keeps the file's own name. */
	{
		static BYTE table[8192], head[64];
		LOGFONTW font={0};
		font.lfHeight=-12; font.lfWeight=400; font.lfCharSet=DEFAULT_CHARSET;
		lstrcpyW(font.lfFaceName,L"Times New Roman");
		HFONT handle=CreateFontIndirectW(&font);
		if (!handle) return 8;
		HGDIOBJ old=SelectObject(dc,handle);
		if (GetFontData(dc,'h'|'e'<<8|'a'<<16|'d'<<24,0,head,sizeof head)==GDI_ERROR) return 9;
		DWORD size=GetFontData(dc,'n'|'a'<<8|'m'<<16|'e'<<24,0,table,sizeof table);
		SelectObject(dc,old);
		DeleteObject(handle);
		if (size==GDI_ERROR || has(table,size,"TimesNewRomanPSMT") || !has(table,size,"LiberationSerif")) return 10;
	}
	DeleteDC(dc);
	return 0;
}
