#define WIN32_LEAN_AND_MEAN
#include <windows.h>

/* PDF-XChange sizes a clipboard block for its text and a terminator, then overwrites the terminator with whatever followed its own string.
   Neither that unit nor what lies past the block may reach the desktop. The block reuses a larger one left full of letters, so the bytes
   after it are not zero. */
int raven_entry(void)
{
	HGLOBAL old=GlobalAlloc(GMEM_MOVEABLE,32);
	if (!old) return 1;
	volatile WCHAR *fill=GlobalLock(old);
	for (int at=0;at<16;++at) fill[at]=L'Z';
	GlobalUnlock(old);
	GlobalFree(old);
	HGLOBAL block=GlobalAlloc(GMEM_MOVEABLE,28);
	if (!block) return 2;
	volatile WCHAR *text=GlobalLock(block);
	/* Without letters past the block this test proves nothing. */
	if (text[14]!=L'Z') return 5;
	static const WCHAR claim[13]={'A','B','C','D','E','F','G','H','I','J','K','L','M'};
	for (int at=0;at<13;++at) text[at]=claim[at];
	text[13]=0xffff;
	GlobalUnlock(block);
	if (!OpenClipboard(NULL)) return 3;
	EmptyClipboard();
	if (!SetClipboardData(CF_UNICODETEXT,block)) return 4;
	volatile WCHAR *held=GlobalLock(GetClipboardData(CF_UNICODETEXT));
	if (!held || held[13]) return 6;
	GlobalUnlock(block);
	CloseClipboard();
	return 0;
}
