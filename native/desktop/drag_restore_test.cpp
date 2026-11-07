#include "nt/providers.h"
#include <fstream>
#include <iostream>
#include <stdexcept>

static void check(bool value,const char *message)
{
	if (!value) throw std::runtime_error(message);
}

// The desktop restores a maximized window without giving a size (KWin on a caption drag): a window whose normal size fills the screen
// comes back smaller (the fixture's exit code says what differed).
int main(int argc,char **argv)
{
	try {
		check(argc==3,"bootstrap and drag restore fixture required");
		std::ifstream file(argv[2],std::ios::binary | std::ios::ate);
		check(bool(file) && file.tellg()>0,"cannot read drag restore fixture");
		std::vector<uint8_t> bytes(size_t(file.tellg())); file.seekg(0);
		check(bool(file.read(reinterpret_cast<char *>(bytes.data()),std::streamsize(bytes.size()))),"short drag restore fixture");
		raven::ModuleSet modules; auto &entry=modules.add("drag-restore.exe",bytes);
		uint64_t window=0;
		unsigned step=0;
		raven::DesktopPresentation presentation;
		presentation.present=[&](uint64_t handle,std::string_view title,uint32_t,uint32_t,std::span<const uint8_t>) { if (title=="restore") window=handle; };
		presentation.remove=[&](uint64_t) {};
		presentation.poll=[&](std::chrono::milliseconds) {
			std::vector<raven::DesktopEvent> events;
			if (!window) return events;
			switch (step++) {
			case 0: events.push_back({window,0,0,0,raven::DesktopEventKind::host_maximized,1,1}); break;
			case 2: events.push_back({window,0,0,0,raven::DesktopEventKind::host_maximized,0,0}); break;
			case 4: events.push_back({window,0x10}); break;
			default: break;
			}
			return events;
		};
		const auto result=raven::run_native_process(modules,entry,argv[1],std::chrono::seconds(10),{},{},presentation);
		if (result.stop!=raven::NativeStop::returned || result.value) std::cerr<<"fixture returned "<<result.value<<'\n';
		check(result.stop==raven::NativeStop::returned,"drag restore fixture did not return");
		check(result.value!=100,"the window was never restored");
		check(result.value!=11,"the restored window still fills the screen");
		check(!result.value,"drag restore workflow failed");
	} catch (const std::exception &error) { std::cerr<<error.what()<<'\n'; return 1; }
	return 0;
}
