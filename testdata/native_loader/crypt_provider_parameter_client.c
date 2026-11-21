#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wincrypt.h>

// CryptSetProvParam accepts the parameters a software provider can take, and refuses the rest.

#define CHECK(code, condition) do { if (!(condition)) return code; } while (0)

int raven_entry(void)
{
	HCRYPTPROV provider = 0;
	CHECK(90, CryptAcquireContextW(&provider, 0, 0, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT));
	HWND owner = 0;
	CHECK(1, CryptSetProvParam(0, PP_CLIENT_HWND, (BYTE *)&owner, 0));
	SetLastError(0);
	CHECK(2, !CryptSetProvParam(0, PP_CLIENT_HWND, 0, 0) && GetLastError() == ERROR_INVALID_PARAMETER);
	char prompt[] = "test";
	CHECK(3, CryptSetProvParam(provider, PP_UI_PROMPT, (BYTE *)prompt, 0));
	SetLastError(0);
	CHECK(4, !CryptSetProvParam(provider, 999, (BYTE *)prompt, 0) && GetLastError() == (DWORD)NTE_BAD_TYPE);
	SetLastError(0);
	CHECK(5, !CryptSetProvParam(provider + 0x1000, PP_UI_PROMPT, (BYTE *)prompt, 0) && GetLastError() == (DWORD)NTE_BAD_UID);
	CHECK(6, CryptReleaseContext(provider, 0));
	return 0;
}
