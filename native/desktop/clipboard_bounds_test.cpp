#include "nt/providers.h"
#include <fstream>
#include <iostream>
#include <stdexcept>

static void check(bool value,const char *message)
{
	if (!value) throw std::runtime_error(message);
}

// Text that fills its clipboard block with no terminator reaches the desktop as it is, without the memory after the block.
int main(int argc,char **argv)
{
	try {
		check(argc==3,"bootstrap and clipboard fixture required");
		std::ifstream file(argv[2],std::ios::binary | std::ios::ate);
		check(bool(file) && file.tellg()>0,"cannot read clipboard fixture");
		std::vector<uint8_t> bytes(size_t(file.tellg())); file.seekg(0);
		check(bool(file.read(reinterpret_cast<char *>(bytes.data()),std::streamsize(bytes.size()))),"short clipboard fixture");
		raven::ModuleSet modules; auto &entry=modules.add("clipboard-bounds.exe",bytes);
		std::string published;
		raven::DesktopPresentation presentation;
		presentation.present=[&](uint64_t,std::string_view,uint32_t,uint32_t,std::span<const uint8_t>) {};
		presentation.clipboard_publish=[&](std::string_view text) { published=text; };
		presentation.poll=[&](std::chrono::milliseconds) { return std::vector<raven::DesktopEvent>{}; };
		const auto result=raven::run_native_process(modules,entry,argv[1],std::chrono::seconds(10),{},{},presentation);
		if (result.stop!=raven::NativeStop::returned || result.value) std::cerr<<"fixture returned "<<result.value<<'\n';
		check(result.stop==raven::NativeStop::returned && !result.value,"clipboard fixture failed");
		if (published!="ABCDEFGHIJKLM") std::cerr<<"published "<<published.size()<<" bytes: "<<published.substr(0,40)<<'\n';
		check(published=="ABCDEFGHIJKLM","the text on the clipboard was not bounded by its block");
	} catch (const std::exception &error) { std::cerr<<error.what()<<'\n'; return 1; }
	return 0;
}
