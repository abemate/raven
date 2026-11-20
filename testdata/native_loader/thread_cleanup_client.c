#define WIN32_LEAN_AND_MEAN
#define PSAPI_VERSION 2
#include <windows.h>
#include <psapi.h>

/* What a program counts on when its threads end and when it asks how much memory it holds: fiber-local callbacks run on a thread that
   leaves through ExitThread, thread pool callbacks reuse a thread, and the memory figures include what the program itself touched. */
static DWORD fls_slot;
static volatile LONG fls_calls;
static volatile ULONG_PTR fls_value;
static volatile DWORD pool_thread[2];
static volatile LONG pool_runs;

static VOID WINAPI fls_callback(PVOID value)
{
	fls_value=(ULONG_PTR)value;
	InterlockedIncrement(&fls_calls);
}

static DWORD WINAPI exits_itself(LPVOID parameter)
{
	FlsSetValue(fls_slot,(PVOID)0x1234);
	ExitThread(7);
	return 0;
}

static VOID CALLBACK pool_work(PTP_CALLBACK_INSTANCE instance,PVOID context,PTP_WORK work)
{
	LONG run=InterlockedIncrement(&pool_runs)-1;
	if (run<2) pool_thread[run]=GetCurrentThreadId();
}

int raven_entry(void)
{
	fls_slot=FlsAlloc(fls_callback);
	if (fls_slot==FLS_OUT_OF_INDEXES) return 1;
	HANDLE thread=CreateThread(NULL,0,exits_itself,NULL,0,NULL);
	if (!thread) return 2;
	if (WaitForSingleObject(thread,5000)!=WAIT_OBJECT_0) return 3;
	DWORD code=0;
	if (!GetExitCodeThread(thread,&code) || code!=7) return 4;
	CloseHandle(thread);
	if (fls_calls!=1 || fls_value!=0x1234) return 5;

	PTP_WORK work=CreateThreadpoolWork(pool_work,NULL,NULL);
	if (!work) return 6;
	SubmitThreadpoolWork(work);
	WaitForThreadpoolWorkCallbacks(work,FALSE);
	SubmitThreadpoolWork(work);
	WaitForThreadpoolWorkCallbacks(work,FALSE);
	CloseThreadpoolWork(work);
	if (pool_runs!=2) return 7;
	if (!pool_thread[0] || pool_thread[0]!=pool_thread[1]) return 8;

	const SIZE_T size=64*1024*1024;
	PROCESS_MEMORY_COUNTERS before={0},after={0};
	before.cb=after.cb=sizeof before;
	if (!GetProcessMemoryInfo(GetCurrentProcess(),&before,sizeof before)) return 9;
	volatile BYTE *block=VirtualAlloc(NULL,size,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
	if (!block) return 10;
	for (SIZE_T at=0; at<size; at+=4096) block[at]=1;
	if (!GetProcessMemoryInfo(GetCurrentProcess(),&after,sizeof after)) return 11;
	if (after.WorkingSetSize<before.WorkingSetSize+size/2) return 12;
	if (after.PagefileUsage<before.PagefileUsage+size/2) return 13;
	MEMORYSTATUSEX status={0};
	status.dwLength=sizeof status;
	if (!GlobalMemoryStatusEx(&status) || !status.ullTotalPhys || status.ullAvailPhys>status.ullTotalPhys) return 14;
	return 0;
}
