#include "nt/providers.h"
#include <fstream>
#include <iostream>
#include <stdexcept>

static void check(bool value,const char *message)
{
	if (!value) throw std::runtime_error(message);
}

// Input for a window of a thread that is not pumping messages waits for that thread: the pointer moving over the main window while
// its thread waits must not stop the worker thread from getting the click on its own window (the fixture's exit code says how far
// it came).
int main(int argc,char **argv)
{
	try {
		check(argc==3,"bootstrap and thread input fixture required");
		std::ifstream file(argv[2],std::ios::binary | std::ios::ate);
		check(bool(file) && file.tellg()>0,"cannot read thread input fixture");
		std::vector<uint8_t> bytes(size_t(file.tellg())); file.seekg(0);
		check(bool(file.read(reinterpret_cast<char *>(bytes.data()),std::streamsize(bytes.size()))),"short thread input fixture");
		raven::ModuleSet modules; auto &entry=modules.add("thread-input.exe",bytes);
		uint64_t main_window=0,worker_window=0;
		unsigned step=0;
		raven::DesktopPresentation presentation;
		presentation.present=[&](uint64_t handle,std::string_view title,uint32_t,uint32_t,std::span<const uint8_t>) {
			if (title=="main") main_window=handle;
			if (title=="worker") worker_window=handle;
		};
		presentation.remove=[&](uint64_t) {};
		presentation.poll=[&](std::chrono::milliseconds) {
			std::vector<raven::DesktopEvent> events;
			if (!main_window || !worker_window) return events;
			switch (step++) {
			// Over the main window's caption, then its client area: both ask its thread where the pointer is.
			case 0: events.push_back({main_window,0,0,0,raven::DesktopEventKind::pointer_motion,60,12}); break;
			case 1: events.push_back({main_window,0,0,0,raven::DesktopEventKind::pointer_motion,100,100}); break;
			case 2:
				events.push_back({worker_window,0,0,0,raven::DesktopEventKind::pointer_motion,100,80});
				events.push_back({worker_window,0x110,1,0,raven::DesktopEventKind::pointer_button,100,80});
				events.push_back({worker_window,0x110,0,0,raven::DesktopEventKind::pointer_button,100,80});
				break;
			default: break;
			}
			return events;
		};
		const auto result=raven::run_native_process(modules,entry,argv[1],std::chrono::seconds(20),{},{},presentation);
		if (result.stop!=raven::NativeStop::returned || result.value) std::cerr<<"fixture returned "<<result.value<<'\n';
		check(result.stop==raven::NativeStop::returned,"thread input fixture did not return");
		check(result.value!=10,"the worker thread never got its click: it waited for the main thread to answer");
		check(!result.value,"thread input workflow failed");
		check(step>=3,"the pointer did not reach the worker window");
	} catch (const std::exception &error) { std::cerr<<error.what()<<'\n'; return 1; }
	return 0;
}
