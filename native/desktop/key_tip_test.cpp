#include "nt/providers.h"
#include <fstream>
#include <iostream>
#include <stdexcept>

static void check(bool value,const char *message)
{
	if (!value) throw std::runtime_error(message);
}

// An unowned captionless tool popup (PXC's Alt key tips) shows on the active window where the program placed it, not as a host
// window of its own that the compositor places.
int main(int argc,char **argv)
{
	try {
		check(argc==3,"bootstrap and key tip fixture required");
		std::ifstream file(argv[2],std::ios::binary | std::ios::ate);
		check(bool(file) && file.tellg()>0,"cannot read key tip fixture");
		std::vector<uint8_t> bytes(size_t(file.tellg())); file.seekg(0);
		check(bool(file.read(reinterpret_cast<char *>(bytes.data()),std::streamsize(bytes.size()))),"short key tip fixture");
		raven::ModuleSet modules; auto &entry=modules.add("key-tip.exe",bytes);
		uint64_t main_window=0,tip=0,tip_owner=0;
		int32_t tip_x=-1,tip_y=-1;
		std::vector<uint8_t> tip_pixels;
		unsigned own_surfaces=0;
		raven::DesktopPresentation presentation;
		presentation.present=[&](uint64_t handle,std::string_view title,uint32_t,uint32_t,std::span<const uint8_t>) {
			if (title=="main") main_window=handle;
			else ++own_surfaces;
		};
		presentation.owned_popup=[&](uint64_t handle,uint64_t owner,int32_t x,int32_t y,uint32_t,uint32_t,std::span<const uint8_t> pixels) {
			tip_pixels.assign(pixels.begin(),pixels.end());
			tip=handle; tip_owner=owner; tip_x=x; tip_y=y;
		};
		presentation.remove=[&](uint64_t) {};
		presentation.poll=[&](std::chrono::milliseconds) { return std::vector<raven::DesktopEvent>{}; };
		const auto result=raven::run_native_process(modules,entry,argv[1],std::chrono::seconds(10),{},{},presentation);
		if (result.stop!=raven::NativeStop::returned || result.value) std::cerr<<"fixture returned "<<result.value<<'\n';
		check(result.stop==raven::NativeStop::returned && !result.value,"key tip fixture failed");
		check(main_window,"the main window never showed");
		check(!own_surfaces,"the key tip became a host window of its own");
		check(tip && tip_owner==main_window,"the key tip did not show on the active window");
		check(tip_x==30 && tip_y==40,"the key tip did not show where the program placed it");
		// The white window at 150 of 255 opacity, premultiplied.
		check(tip_pixels.size()>=4 && tip_pixels[3]==150 && tip_pixels[0]==150 && tip_pixels[2]==150,"the layered window's opacity was not applied");
	} catch (const std::exception &error) { std::cerr<<error.what()<<'\n'; return 1; }
	return 0;
}
