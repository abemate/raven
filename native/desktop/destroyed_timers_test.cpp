#include "nt/providers.h"
#include <fstream>
#include <iostream>
#include <stdexcept>

static void check(bool value,const char *message)
{
	if (!value) throw std::runtime_error(message);
}

// Windows destroyed with a timer running free its slot, and a timer set again replaces itself even with the table full (the fixture's exit
// code says what failed).
int main(int argc,char **argv)
{
	try {
		check(argc==3,"bootstrap and destroyed timers fixture required");
		std::ifstream file(argv[2],std::ios::binary | std::ios::ate);
		check(bool(file) && file.tellg()>0,"cannot read destroyed timers fixture");
		std::vector<uint8_t> bytes(size_t(file.tellg())); file.seekg(0);
		check(bool(file.read(reinterpret_cast<char *>(bytes.data()),std::streamsize(bytes.size()))),"short destroyed timers fixture");
		raven::ModuleSet modules; auto &entry=modules.add("destroyed-timers.exe",bytes);
		raven::DesktopPresentation presentation;
		presentation.present=[](uint64_t,std::string_view,uint32_t,uint32_t,std::span<const uint8_t>) {};
		presentation.remove=[](uint64_t) {};
		presentation.poll=[](std::chrono::milliseconds) { return std::vector<raven::DesktopEvent>{}; };
		const auto result=raven::run_native_process(modules,entry,argv[1],std::chrono::seconds(20),{},{},presentation);
		if (result.stop!=raven::NativeStop::returned || result.value) std::cerr<<"fixture returned "<<result.value<<'\n';
		check(result.stop==raven::NativeStop::returned,"destroyed timers fixture did not return");
		check(result.value!=3,"a destroyed window's timer kept its slot, so SetTimer failed");
		check(result.value!=8,"setting an existing timer again failed with the table full");
		check(!result.value,"destroyed timers fixture failed");
	} catch (const std::exception &error) { std::cerr<<error.what()<<'\n'; return 1; }
	return 0;
}
