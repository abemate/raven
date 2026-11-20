#include "nt/providers.h"
#include <array>
#include <fstream>
#include <iostream>
#include <stdexcept>

static void check(bool value,const char *message)
{
	if (!value) throw std::runtime_error(message);
}

// A cursor drawn by the program reaches the host as premultiplied BGRA with its hot spot, once while it stays the same.
int main(int argc,char **argv)
{
	try {
		check(argc==3,"bootstrap and cursor fixture required");
		std::ifstream file(argv[2],std::ios::binary | std::ios::ate);
		check(bool(file) && file.tellg()>0,"cannot read cursor fixture");
		std::vector<uint8_t> bytes(size_t(file.tellg())); file.seekg(0);
		check(bool(file.read(reinterpret_cast<char *>(bytes.data()),std::streamsize(bytes.size()))),"short cursor fixture");
		raven::ModuleSet modules; auto &entry=modules.add("cursor-image.exe",bytes);
		unsigned images=0;
		uint32_t hot_x=99,hot_y=99,width=0,height=0,last_shape=0;
		bool image_was_last=false;
		std::vector<uint8_t> shown;
		raven::DesktopPresentation presentation;
		presentation.present=[&](uint64_t,std::string_view,uint32_t,uint32_t,std::span<const uint8_t>) {};
		presentation.cursor=[&](uint32_t shape) { last_shape=shape; image_was_last=false; };
		presentation.cursor_image=[&](uint32_t x,uint32_t y,uint32_t w,uint32_t h,std::span<const uint8_t> pixels) {
			++images; hot_x=x; hot_y=y; width=w; height=h; shown.assign(pixels.begin(),pixels.end()); image_was_last=true;
		};
		presentation.poll=[&](std::chrono::milliseconds) { return std::vector<raven::DesktopEvent>{}; };
		const auto result=raven::run_native_process(modules,entry,argv[1],std::chrono::seconds(10),{},{},presentation);
		if (result.stop!=raven::NativeStop::returned || result.value) std::cerr<<"fixture returned "<<result.value<<'\n';
		check(result.stop==raven::NativeStop::returned && !result.value,"cursor fixture failed");
		check(images==1,"the program's cursor did not reach the host exactly once");
		check(width==2 && height==2 && hot_x==1 && hot_y==0,"the cursor's size or hot spot differs");
		const std::array<uint8_t,16> wanted{0,0,255,255, 0,0,128,128, 0,0,0,0, 255,255,255,255};
		check(shown.size()==16 && std::equal(wanted.begin(),wanted.end(),shown.begin()),"the cursor's pixels differ");
		check(!image_was_last && last_shape==32515,"a system cursor did not replace the program's");
	} catch (const std::exception &error) { std::cerr<<error.what()<<'\n'; return 1; }
	return 0;
}
