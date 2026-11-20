#define WIN32_LEAN_AND_MEAN
#include <windows.h>

/* A cursor the program draws itself, as PDF-XChange's Pencil does, goes to the desktop as a picture; a system cursor set afterwards replaces it. */
int raven_entry(void)
{
	/* BGRA, top-down: opaque red, half-transparent red; fully transparent, opaque white. */
	static const DWORD bits[4]={0xffff0000,0x80ff0000,0x00000000,0xffffffff};
	static const BYTE mask_bits[8]={0};
	BITMAPINFO described={0};
	described.bmiHeader.biSize=sizeof described.bmiHeader;
	described.bmiHeader.biWidth=2;
	described.bmiHeader.biHeight=-2;
	described.bmiHeader.biPlanes=1;
	described.bmiHeader.biBitCount=32;
	DWORD *pixels=NULL;
	HBITMAP color=CreateDIBSection(NULL,&described,DIB_RGB_COLORS,(void **)&pixels,NULL,0),mask=CreateBitmap(2,2,1,1,mask_bits);
	if (!color || !mask || !pixels) return 1;
	for (int at=0;at<4;++at) pixels[at]=bits[at];
	ICONINFO info={0};
	info.fIcon=FALSE;
	info.xHotspot=1;
	info.yHotspot=0;
	info.hbmMask=mask;
	info.hbmColor=color;
	HCURSOR cursor=CreateIconIndirect(&info);
	if (!cursor) return 2;
	SetCursor(cursor);
	SetCursor(cursor);
	SetCursor(LoadCursorW(NULL,IDC_CROSS));
	return 0;
}
