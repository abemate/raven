#include "nt/providers.h"
#include <xkbcommon/xkbcommon.h>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdexcept>

static void check(bool value,const char *message)
{
	if (!value) throw std::runtime_error(message);
}

// Ctrl+Alt+key is an ordinary key press on Windows, Alt+key a system key (the fixture's exit code names what differed).
int main(int argc,char **argv)
{
	try {
		check(argc==3,"bootstrap and key chord fixture required");
		std::ifstream file(argv[2],std::ios::binary | std::ios::ate);
		check(bool(file) && file.tellg()>0,"cannot read key chord fixture");
		std::vector<uint8_t> bytes(size_t(file.tellg())); file.seekg(0);
		check(bool(file.read(reinterpret_cast<char *>(bytes.data()),std::streamsize(bytes.size()))),"short key chord fixture");
		auto *context=xkb_context_new(XKB_CONTEXT_NO_FLAGS); check(context,"cannot create reference keyboard context");
		xkb_rule_names names{}; names.layout="us";
		auto *map=xkb_keymap_new_from_names(context,&names,XKB_KEYMAP_COMPILE_NO_FLAGS); check(map,"cannot create reference keyboard map");
		auto *text=xkb_keymap_get_as_string(map,XKB_KEYMAP_FORMAT_TEXT_V1); check(text,"cannot serialize keyboard map");
		const std::string keymap(text); std::free(text);
		const auto mask=[&](std::initializer_list<const char *> names) {
			uint32_t value=0; for (const auto *name : names) value|=1U<<xkb_keymap_mod_get_index(map,name); return value;
		};
		const uint32_t control=mask({XKB_MOD_NAME_CTRL}),control_alt=mask({XKB_MOD_NAME_CTRL,XKB_MOD_NAME_ALT}),alt=mask({XKB_MOD_NAME_ALT});
		xkb_keymap_unref(map); xkb_context_unref(context);
		raven::ModuleSet modules; auto &entry=modules.add("key-chord.exe",bytes);
		uint64_t window=0;
		unsigned step=0;
		raven::DesktopPresentation presentation;
		presentation.present=[&](uint64_t handle,std::string_view title,uint32_t,uint32_t,std::span<const uint8_t>) { if (title=="chord") window=handle; };
		presentation.remove=[&](uint64_t) {};
		presentation.poll=[&](std::chrono::milliseconds) {
			std::vector<raven::DesktopEvent> events;
			if (!window) return events;
			const auto modifiers=[&](uint32_t depressed) { raven::DesktopEvent event{0,depressed,0,0,raven::DesktopEventKind::modifiers}; return event; };
			const auto key=[&](uint32_t code,bool down) { return raven::DesktopEvent{window,code,down ? 1U : 0U,0,raven::DesktopEventKind::key}; };
			switch (step++) {
			case 0: {
				raven::DesktopEvent layout{0,0}; layout.kind=raven::DesktopEventKind::keymap; layout.keymap=keymap;
				events.push_back(std::move(layout));
				events.push_back({window,0,0,0,raven::DesktopEventKind::keyboard_focus});
				break;
			}
			case 1:
				// Ctrl (29), Alt (56), 7 (8), as a compositor reports them: each modifier key, then the modifier state.
				events.push_back(key(29,true)); events.push_back(modifiers(control));
				events.push_back(key(56,true)); events.push_back(modifiers(control_alt));
				events.push_back(key(8,true)); events.push_back(key(8,false));
				events.push_back(key(56,false)); events.push_back(modifiers(control));
				events.push_back(key(29,false)); events.push_back(modifiers(0));
				break;
			case 2:
				// Alt+F (33).
				events.push_back(key(56,true)); events.push_back(modifiers(alt));
				events.push_back(key(33,true)); events.push_back(key(33,false));
				events.push_back(key(56,false)); events.push_back(modifiers(0));
				break;
			case 3: events.push_back({window,0x10}); break;
			default: break;
			}
			return events;
		};
		const auto result=raven::run_native_process(modules,entry,argv[1],std::chrono::seconds(10),{},{},presentation);
		if (result.stop!=raven::NativeStop::returned || result.value) std::cerr<<"fixture returned "<<result.value<<'\n';
		check(result.stop==raven::NativeStop::returned,"key chord fixture did not return");
		check(result.value!=10,"Ctrl+Alt+7 came as a system key");
		check(result.value!=12,"Ctrl+Alt+7 typed a character");
		check(result.value!=16,"left and right Ctrl/Alt states differ from Windows (right Alt seen as down)");
		check(!result.value,"key chord workflow failed");
	} catch (const std::exception &error) { std::cerr<<error.what()<<'\n'; return 1; }
	return 0;
}
