#include "nt/providers.h"
#include <fstream>
#include <iostream>
#include <stdexcept>

static void check(bool value,const char *message)
{
	if (!value) throw std::runtime_error(message);
}

// A window shown with SW_SHOWNOACTIVATE beneath the active dialog must not cover the dialog on the host; it shows when the dialog goes.
int main(int argc,char **argv)
{
	try {
		check(argc==3,"bootstrap and fixture required");
		std::ifstream file(argv[2],std::ios::binary | std::ios::ate);
		check(bool(file) && file.tellg()>0,"cannot read fixture");
		std::vector<uint8_t> bytes(size_t(file.tellg())); file.seekg(0);
		check(bool(file.read(reinterpret_cast<char *>(bytes.data()),std::streamsize(bytes.size()))),"short fixture");
		raven::ModuleSet modules; auto &entry=modules.add("covered.exe",bytes);
		bool dialog_shown=false,dialog_gone=false,progress_while_dialog=false,progress_after=false;
		uint64_t dialog=0;
		raven::DesktopPresentation presentation;
		presentation.present=[&](uint64_t handle,std::string_view title,uint32_t,uint32_t,std::span<const uint8_t>) {
			if (title=="dialog") { dialog_shown=true; dialog=handle; }
			if (title=="progress") { if (dialog_gone) progress_after=true; else progress_while_dialog=true; }
		};
		presentation.remove=[&](uint64_t handle) { if (handle==dialog) dialog_gone=true; };
		presentation.poll=[&](std::chrono::milliseconds) { return std::vector<raven::DesktopEvent>{}; };
		const auto result=raven::run_native_process(modules,entry,argv[1],std::chrono::seconds(10),{},{},presentation);
		if (result.stop!=raven::NativeStop::returned || result.value) std::cerr<<"fixture returned "<<result.value<<'\n';
		check(result.stop==raven::NativeStop::returned && !result.value,"fixture failed");
		check(dialog_shown,"the dialog never showed");
		check(!progress_while_dialog,"the window beneath the active dialog came up over it");
		check(progress_after,"the window did not show after the dialog closed");
	} catch (const std::exception &error) { std::cerr<<error.what()<<'\n'; return 1; }
	return 0;
}
