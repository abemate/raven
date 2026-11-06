// Printing to the host's printers through CUPS.
//
// The spooler (winspool.drv) lists the CUPS destinations as printers, with the papers, copies, two-sided printing and colour a
// program asks of them, and describes them in DEVMODE. A printer device context is a page at 300 dots per inch: the program draws each
// page with GDI as on any other context, EndPage keeps the page, and EndDoc makes a PDF of the pages at the paper's size and gives it
// to CUPS (cupsPrintFile) with the copies, sides, paper and colour chosen, or writes it to the file StartDoc named. libcups and zlib
// are the host's, loaded when first needed; without libcups there are no printers, as on a Windows with none installed.

#include "api.h"
#include "gdi.h"
#include "nt/text.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unistd.h>
#include <vector>

namespace raven {

namespace {

struct CupsOption { char *name, *value; };
struct CupsDest { char *name, *instance; int is_default, num_options; CupsOption *options; };

struct Cups {
	int (*get_dests)(CupsDest **);
	void (*free_dests)(int, CupsDest *);
	const char *(*get_option)(const char *, int, CupsOption *);
	int (*add_option)(const char *, const char *, int, CupsOption **);
	void (*free_options)(int, CupsOption *);
	int (*print_file)(const char *, const char *, const char *, int, CupsOption *);
	const char *(*last_error)();
	bool ready = false;

	static const Cups *get()
	{
		static Cups cups;
		static std::once_flag once;
		std::call_once(once, [] {
			void *library = ::dlopen("libcups.so.2", RTLD_NOW | RTLD_LOCAL);
			if (!library)
				return;
			bool all = true;
			const auto find = [&](auto &slot, const char *name) {
				slot = reinterpret_cast<std::remove_reference_t<decltype(slot)>>(::dlsym(library, name));
				all = all && slot;
			};
			find(cups.get_dests, "cupsGetDests");
			find(cups.free_dests, "cupsFreeDests");
			find(cups.get_option, "cupsGetOption");
			find(cups.add_option, "cupsAddOption");
			find(cups.free_options, "cupsFreeOptions");
			find(cups.print_file, "cupsPrintFile");
			find(cups.last_error, "cupsLastErrorString");
			cups.ready = all;
		});
		return cups.ready ? &cups : nullptr;
	}
};

using Compress = int (*)(uint8_t *, unsigned long *, const uint8_t *, unsigned long, int);
using Bound = unsigned long (*)(unsigned long);

std::optional<std::vector<uint8_t>> deflate(const std::vector<uint8_t> &data)
{
	static Compress compress = nullptr;
	static Bound bound = nullptr;
	static std::once_flag once;
	std::call_once(once, [] {
		if (void *library = ::dlopen("libz.so.1", RTLD_NOW | RTLD_LOCAL)) {
			compress = reinterpret_cast<Compress>(::dlsym(library, "compress2"));
			bound = reinterpret_cast<Bound>(::dlsym(library, "compressBound"));
		}
	});
	if (!compress || !bound)
		return std::nullopt;
	unsigned long size = bound(data.size());
	std::vector<uint8_t> out(size);
	if (compress(out.data(), &size, data.data(), data.size(), 6) != 0)
		return std::nullopt;
	out.resize(size);
	return out;
}

// The papers offered: the DMPAPER number, the name, the size in tenths of a millimetre and the CUPS (PWG) media name.
struct Paper { uint16_t id; const char16_t *name; int32_t width, height; const char *media; };
constexpr Paper papers[] = {
	{1, u"Letter", 2159, 2794, "na_letter_8.5x11in"}, {5, u"Legal", 2159, 3556, "na_legal_8.5x14in"},
	{9, u"A4", 2100, 2970, "iso_a4_210x297mm"}, {8, u"A3", 2970, 4200, "iso_a3_297x420mm"},
	{11, u"A5", 1480, 2100, "iso_a5_148x210mm"}, {7, u"Executive", 1842, 2667, "na_executive_7.25x10.5in"},
	{3, u"Tabloid", 2794, 4318, "na_ledger_11x17in"}, {13, u"B5 (JIS)", 1820, 2570, "jis_b5_182x257mm"},
};
// Pages are drawn at 300 dots per inch unless RAVEN_PRINT_DPI asks for another resolution (72 to 1200).
int32_t print_dpi()
{
	static const int32_t value = [] {
		const char *text = std::getenv("RAVEN_PRINT_DPI");
		const int parsed = text ? std::atoi(text) : 0;
		return parsed >= 72 && parsed <= 1200 ? parsed : 300;
	}();
	return value;
}

const Paper &paper_of(uint16_t id)
{
	for (const auto &paper : papers)
		if (paper.id == id)
			return paper;
	return papers[2];
}

// The settings of a printer or a job, what DEVMODE carries.
struct Settings {
	uint16_t paper = 9, copies = 1, orientation = 1, duplex = 1, color = 2, collate = 1;
};

struct PrinterInfo {
	std::string name, info, location, model;
	bool is_default = false, duplex = false, color = true;
	Settings defaults;
};

std::vector<PrinterInfo> host_printers()
{
	std::vector<PrinterInfo> result;
	const auto *cups = Cups::get();
	if (!cups)
		return result;
	CupsDest *dests = nullptr;
	const int count = cups->get_dests(&dests);
	for (int index = 0; index < count; ++index) {
		const auto &dest = dests[index];
		if (!dest.name || dest.instance)
			continue;
		PrinterInfo printer;
		printer.name = dest.name;
		printer.is_default = dest.is_default != 0;
		const auto option = [&](const char *name) {
			const char *value = cups->get_option(name, dest.num_options, dest.options);
			return std::string(value ? value : "");
		};
		printer.info = option("printer-info");
		printer.location = option("printer-location");
		printer.model = option("printer-make-and-model");
		const auto sides = option("sides-supported");
		printer.duplex = sides.find("two-sided") != std::string::npos;
		const auto color = option("print-color-mode-supported");
		printer.color = color.empty() || color.find("color") != std::string::npos;
		const auto media = option("media");
		const char *lang = std::getenv("LANG");
		printer.defaults.paper = lang && (std::strncmp(lang, "en_US", 5) == 0 || std::strncmp(lang, "en_CA", 5) == 0) ? 1 : 9;
		for (const auto &paper : papers)
			if (!media.empty() && (media == paper.media || media == to_utf8(paper.name) ||
				(media.size() == std::strlen(paper.media) && std::equal(media.begin(), media.end(), paper.media))))
				printer.defaults.paper = paper.id;
		if (media == "Letter" || media == "na_letter_8.5x11in") printer.defaults.paper = 1;
		if (media == "A4" || media == "iso_a4_210x297mm") printer.defaults.paper = 9;
		if (!printer.color)
			printer.defaults.color = 1;
		result.push_back(std::move(printer));
	}
	cups->free_dests(count, dests);
	return result;
}

std::optional<PrinterInfo> printer_named(std::u16string_view name)
{
	for (auto &printer : host_printers())
		if (equal_fold(from_utf8(printer.name), name))
			return printer;
	return std::nullopt;
}

// DEVMODEW is 220 bytes, DEVMODEA 156: the names are 32 characters of the width the form uses.
std::vector<uint8_t> devmode(const std::string &device, const Settings &settings, bool wide)
{
	const size_t name = wide ? 64 : 32, size = wide ? 220 : 156;
	std::vector<uint8_t> bytes(size, 0);
	const auto put16 = [&](size_t at, uint16_t value) { std::memcpy(bytes.data() + at, &value, 2); };
	const auto put32 = [&](size_t at, uint32_t value) { std::memcpy(bytes.data() + at, &value, 4); };
	const auto text = [&](size_t at, const std::u16string &value) {
		for (size_t index = 0; index < std::min<size_t>(value.size(), 31); ++index) {
			if (wide) put16(at + index * 2, value[index]);
			else bytes[at + index] = uint8_t(value[index] < 128 ? value[index] : '?');
		}
	};
	text(0, from_utf8(device));
	const size_t after = name;
	put16(after, 0x401);
	put16(after + 4, uint16_t(size));
	// dmFields: orientation, paper size, copies, colour, duplex, collate, print quality, resolution, form name.
	put32(after + 8, 0x1 | 0x2 | 0x100 | 0x400 | 0x800 | 0x1000 | 0x2000 | 0x8000 | 0x10000);
	const auto &paper = paper_of(settings.paper);
	put16(after + 12, settings.orientation);
	put16(after + 14, paper.id);
	put16(after + 16, uint16_t(paper.height));
	put16(after + 18, uint16_t(paper.width));
	put16(after + 20, 100);
	put16(after + 22, settings.copies);
	put16(after + 24, 7);
	put16(after + 26, print_dpi());
	put16(after + 28, settings.color);
	put16(after + 30, settings.duplex);
	put16(after + 32, print_dpi());
	put16(after + 34, 3);
	put16(after + 36, settings.collate);
	text(after + 38, paper.name);
	return bytes;
}

// Settings a DEVMODE asks for, over the ones given; only the fields it names.
Settings merged(Settings settings, std::span<const uint8_t> bytes, bool wide)
{
	const size_t after = wide ? 64 : 32;
	if (bytes.size() < after + 38)
		return settings;
	const auto get16 = [&](size_t at) { uint16_t value; std::memcpy(&value, bytes.data() + at, 2); return value; };
	uint32_t fields;
	std::memcpy(&fields, bytes.data() + after + 8, 4);
	if ((fields & 0x1) && (get16(after + 12) == 1 || get16(after + 12) == 2)) settings.orientation = get16(after + 12);
	if (fields & 0x2) for (const auto &paper : papers) if (paper.id == get16(after + 14)) settings.paper = paper.id;
	if ((fields & 0x100) && get16(after + 22) >= 1) settings.copies = std::min<uint16_t>(get16(after + 22), 999);
	if ((fields & 0x800) && (get16(after + 28) == 1 || get16(after + 28) == 2)) settings.color = get16(after + 28);
	if ((fields & 0x1000) && get16(after + 30) >= 1 && get16(after + 30) <= 3) settings.duplex = get16(after + 30);
	if (fields & 0x8000) settings.collate = get16(after + 36) ? 1 : 0;
	return settings;
}

struct Job {
	std::string printer;
	Settings settings;
	uint64_t bitmap = 0;
	int32_t width = 0, height = 0;
	bool started = false, in_page = false, info_only = false;
	std::string title, output;
	struct Page { int32_t width, height; std::vector<uint8_t> data; };
	std::vector<Page> pages;
};

std::pair<int32_t, int32_t> page_pixels(const Settings &settings)
{
	const auto &paper = paper_of(settings.paper);
	int32_t width = int32_t(std::lround(paper.width * double(print_dpi()) / 254.0)), height = int32_t(std::lround(paper.height * double(print_dpi()) / 254.0));
	if (settings.orientation == 2)
		std::swap(width, height);
	return {width, height};
}

// A PDF of the pages, each an image covering the paper.
bool write_pdf(const std::string &path, const std::vector<Job::Page> &pages)
{
	FILE *file = std::fopen(path.c_str(), "wb");
	if (!file)
		return false;
	std::vector<long> offsets;
	const auto start = [&] { offsets.push_back(std::ftell(file)); std::fprintf(file, "%zu 0 obj\n", offsets.size()); };
	std::fprintf(file, "%%PDF-1.4\n%%\xe2\xe3\xcf\xd3\n");
	start();
	std::fprintf(file, "<< /Type /Catalog /Pages 2 0 R >>\nendobj\n");
	start();
	std::fprintf(file, "<< /Type /Pages /Count %zu /Kids [", pages.size());
	for (size_t index = 0; index < pages.size(); ++index)
		std::fprintf(file, " %zu 0 R", 3 + index * 3);
	std::fprintf(file, " ] >>\nendobj\n");
	for (const auto &page : pages) {
		const double width = page.width * 72.0 / print_dpi(), height = page.height * 72.0 / print_dpi();
		const size_t number = offsets.size() + 1;
		start();
		std::fprintf(file, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 %.2f %.2f] /Resources << /XObject << /Page %zu 0 R >> >> /Contents %zu 0 R >>\nendobj\n",
			width, height, number + 2, number + 1);
		const std::string content = "q " + std::to_string(width) + " 0 0 " + std::to_string(height) + " 0 0 cm /Page Do Q";
		start();
		std::fprintf(file, "<< /Length %zu >>\nstream\n%s\nendstream\nendobj\n", content.size(), content.c_str());
		start();
		std::fprintf(file, "<< /Type /XObject /Subtype /Image /Width %d /Height %d /ColorSpace /DeviceRGB /BitsPerComponent 8 /Filter /FlateDecode /Length %zu >>\nstream\n",
			page.width, page.height, page.data.size());
		std::fwrite(page.data.data(), 1, page.data.size(), file);
		std::fprintf(file, "\nendstream\nendobj\n");
	}
	const long xref = std::ftell(file);
	std::fprintf(file, "xref\n0 %zu\n0000000000 65535 f \n", offsets.size() + 1);
	for (const auto offset : offsets)
		std::fprintf(file, "%010ld 00000 n \n", offset);
	std::fprintf(file, "trailer\n<< /Size %zu /Root 1 0 R >>\nstartxref\n%ld\n%%%%EOF\n", offsets.size() + 1, xref);
	const bool written = !std::ferror(file);
	return std::fclose(file) == 0 && written;
}

}

void NativeDesktop::register_print_api(ApiRegistry &registry)
{
	struct State {
		std::map<uint64_t, std::string> handles;
		uint64_t next_handle = 0x52500000;
		std::map<uint64_t, Job> jobs;
		std::map<std::string, Settings> remembered;
		uint32_t next_job = 1;
	};
	auto state = std::make_shared<State>();
	const auto settings_of = [state](const PrinterInfo &printer) {
		const auto found = state->remembered.find(printer.name);
		return found != state->remembered.end() ? found->second : printer.defaults;
	};
	const auto text_of = [](ApiCall &call, uint64_t address, bool wide) -> std::u16string {
		if (!address)
			return {};
		return wide ? call.wide(address) : from_ansi(call.narrow(address), 1252);
	};

	// PRINTER_INFO_1, 2, 4, 5 and 8 of printers, laid out in the buffer the program gave: the records, the DEVMODEs, then the strings.
	const auto write_printers = [](ApiCall &call, const std::vector<PrinterInfo> &printers, const std::vector<Settings> &settings,
		uint32_t level, uint64_t buffer, uint32_t capacity, uint64_t needed_out, uint64_t count_out, bool wide) -> bool {
		const size_t pointer = call.pointer_width();
		size_t record = 0;
		switch (level) {
		case 1: record = pointer == 8 ? 32 : 16; break;
		case 2: record = pointer * 13 + 32; break;
		case 4: record = pointer == 8 ? 24 : 12; break;
		case 5: record = pointer == 8 ? 32 : 20; break;
		case 8: case 9: record = pointer; break;
		// PRINTER_INFO_6, the status (ready), and PRINTER_INFO_7, the directory publishing of the printer (none).
		case 6: record = 4; break;
		case 7: record = pointer == 8 ? 16 : 8; break;
		default: call.fail(124); return false;
		}
		const size_t mode_size = wide ? 220 : 156;
		const bool with_mode = level == 2 || level == 8 || level == 9;
		std::vector<std::vector<std::u16string>> strings(printers.size());
		for (size_t index = 0; index < printers.size(); ++index) {
			const auto &printer = printers[index];
			const auto name = from_utf8(printer.name), comment = from_utf8(printer.info), location = from_utf8(printer.location);
			const auto driver = from_utf8(printer.model.empty() ? "Raven CUPS Printer" : printer.model);
			if (level == 1) strings[index] = {name + u",CUPS," + location, name, comment};
			else if (level == 2) strings[index] = {name, u"", u"CUPS:", driver, comment, location, u"", u"winprint", u"RAW", u""};
			else if (level == 4) strings[index] = {name};
			else if (level == 5) strings[index] = {name, u"CUPS:"};
		}
		const size_t unit = wide ? 2 : 1;
		size_t text_size = 0;
		for (const auto &list : strings)
			for (const auto &value : list)
				text_size += (value.size() + 1) * unit;
		const size_t modes_at = (record * printers.size() + 7) & ~size_t(7);
		const size_t texts_at = modes_at + (with_mode ? mode_size * printers.size() : 0);
		const size_t needed = texts_at + text_size;
		if (needed_out)
			call.store<uint32_t>(needed_out, uint32_t(needed));
		if (count_out)
			call.store<uint32_t>(count_out, 0);
		if (capacity < needed || (!buffer && needed)) {
			call.fail(122);
			return false;
		}
		std::vector<uint8_t> bytes(needed, 0);
		size_t text_at = texts_at;
		const auto put = [&](size_t at, uint64_t value, size_t width) { std::memcpy(bytes.data() + at, &value, width); };
		const auto string = [&](const std::u16string &value) -> uint64_t {
			const uint64_t address = buffer + text_at;
			for (size_t index = 0; index < value.size(); ++index) {
				if (wide) put(text_at + index * 2, value[index], 2);
				else bytes[text_at + index] = uint8_t(value[index] < 128 ? value[index] : '?');
			}
			text_at += (value.size() + 1) * unit;
			return address;
		};
		for (size_t index = 0; index < printers.size(); ++index) {
			const auto &printer = printers[index];
			const auto &list = strings[index];
			const size_t at = index * record;
			uint64_t mode_address = 0;
			if (with_mode) {
				const auto mode = devmode(printer.name, settings[index], wide);
				std::memcpy(bytes.data() + modes_at + index * mode_size, mode.data(), mode.size());
				mode_address = buffer + modes_at + index * mode_size;
			}
			const uint32_t attributes = 0x40 | (printer.is_default ? 0x4 : 0);
			if (level == 1) {
				put(at, 0x00800000, 4);
				for (size_t field = 0; field < 3; ++field)
					put(at + pointer + field * pointer, string(list[field]), pointer);
			} else if (level == 2) {
				const size_t order[] = {0, 0, 1, 2, 3, 4, 5, 0, 6, 7, 8, 9, 0};
				for (size_t field = 0; field < 13; ++field) {
					if (field == 0 || field == 12) continue;
					if (field == 7) { put(at + field * pointer, mode_address, pointer); continue; }
					put(at + field * pointer, string(list[order[field]]), pointer);
				}
				const size_t numbers = at + 13 * pointer;
				put(numbers, attributes, 4);
				put(numbers + 4, 1, 4);
			} else if (level == 4) {
				put(at, string(list[0]), pointer);
				put(at + pointer * 2, attributes, 4);
			} else if (level == 6) {
				put(at, 0, 4);
			} else if (level == 7) {
				put(at + pointer, 4, 4);
			} else if (level == 5) {
				put(at, string(list[0]), pointer);
				put(at + pointer, string(list[1]), pointer);
				put(at + pointer * 2, attributes, 4);
				put(at + pointer * 2 + 4, 15000, 4);
				put(at + pointer * 2 + 8, 45000, 4);
			} else {
				put(at, mode_address, pointer);
			}
		}
		if (buffer && !bytes.empty())
			call.write(buffer, bytes);
		if (count_out)
			call.store<uint32_t>(count_out, uint32_t(printers.size()));
		call.ret(1);
		return true;
	};

	for (const bool wide : {true, false}) {
		// EnumPrinters(flags, name, level, buffer, size, needed, returned): the host's printers, local or not; a named server other
		// than this computer has none.
		registry.add(wide ? "EnumPrintersW" : "EnumPrintersA", 7, [=](ApiCall &call) {
			if (!call[5] || !call[6]) {
				call.fail(1780, 0);
				return;
			}
			std::vector<PrinterInfo> printers;
			if (!call[1] || !(call.u32(0) & 0x8))
				printers = host_printers();
			if (call.u32(0) & 0x1) {
				std::erase_if(printers, [](const PrinterInfo &printer) { return !printer.is_default; });
				if (printers.size() > 1) printers.resize(1);
			}
			std::vector<Settings> settings;
			for (const auto &printer : printers)
				settings.push_back(settings_of(printer));
			write_printers(call, printers, settings, call.u32(2), call[3], call.u32(4), call[5], call[6], wide);
		});
		// GetDefaultPrinter(buffer, size in characters with the terminator).
		registry.add(wide ? "GetDefaultPrinterW" : "GetDefaultPrinterA", 2, [=](ApiCall &call) {
			if (!call[1]) {
				call.fail(87);
				return;
			}
			std::optional<PrinterInfo> chosen;
			for (auto &printer : host_printers())
				if (printer.is_default)
					chosen = printer;
			if (!chosen) {
				call.fail(2);
				return;
			}
			const auto name = from_utf8(chosen->name);
			const uint32_t capacity = call.load<uint32_t>(call[1]);
			call.store<uint32_t>(call[1], uint32_t(name.size() + 1));
			if (!call[0] || capacity < name.size() + 1) {
				call.fail(122);
				return;
			}
			if (wide) call.put_wide(call[0], name, capacity);
			else call.put_narrow(call[0], to_ansi(name, 1252), capacity);
			call.ret(1);
		});
		// OpenPrinter(name, handle, defaults): a handle on a printer, or on the print server when no name is given.
		registry.add(wide ? "OpenPrinterW" : "OpenPrinterA", 3, [=](ApiCall &call) {
			if (!call[1]) {
				call.fail(87);
				return;
			}
			call.store_pointer(call[1], 0);
			std::string name;
			if (call[0]) {
				const auto requested = text_of(call, call[0], wide);
				const auto printer = printer_named(requested.substr(0, requested.find(u',')));
				if (!printer) {
					call.fail(1801);
					return;
				}
				name = printer->name;
			}
			const uint64_t handle = state->next_handle++;
			state->handles[handle] = name;
			call.store_pointer(call[1], handle);
			call.ret(1);
		});
		// GetPrinter(handle, level, buffer, size, needed).
		registry.add(wide ? "GetPrinterW" : "GetPrinterA", 5, [=](ApiCall &call) {
			const auto found = state->handles.find(call[0]);
			if (found == state->handles.end() || found->second.empty()) {
				if (call[4]) call.store<uint32_t>(call[4], 0);
				call.fail(6);
				return;
			}
			const auto printer = printer_named(from_utf8(found->second));
			if (!printer) {
				call.fail(1801);
				return;
			}
			write_printers(call, {*printer}, {settings_of(*printer)}, call.u32(1), call[2], call.u32(3), call[4], 0, wide);
		});
		// GetPrinterDriver(handle, environment, level, buffer, size, needed): DRIVER_INFO_1 to 3, the CUPS driver of the printer.
		registry.add(wide ? "GetPrinterDriverW" : "GetPrinterDriverA", 6, [=](ApiCall &call) {
			const auto found = state->handles.find(call[0]);
			const auto printer = found == state->handles.end() || found->second.empty() ? std::nullopt : printer_named(from_utf8(found->second));
			if (!printer) {
				if (call[5]) call.store<uint32_t>(call[5], 0);
				call.fail(6);
				return;
			}
			const uint32_t level = call.u32(2);
			if (level < 1 || level > 3) {
				call.fail(124);
				return;
			}
			const size_t pointer = call.pointer_width(), unit = wide ? 2 : 1;
			const auto driver = from_utf8(printer->model.empty() ? "Raven CUPS Printer" : printer->model);
			std::vector<std::u16string> strings{driver};
			if (level >= 2) strings.insert(strings.end(), {u"Windows x64", u"C:\\Windows\\System32\\spool\\drivers\\x64\\3\\ravencups.dll",
				u"C:\\Windows\\System32\\spool\\drivers\\x64\\3\\ravencups.ppd", u"C:\\Windows\\System32\\spool\\drivers\\x64\\3\\ravencupsui.dll"});
			if (level == 3) strings.insert(strings.end(), {u"", u"", u"", u""});
			const size_t record = level == 1 ? pointer : level == 2 ? (pointer == 8 ? 8 : 4) + pointer * 5 : (pointer == 8 ? 8 : 4) + pointer * 9;
			size_t needed = record;
			for (const auto &value : strings) needed += (value.size() + 1) * unit;
			if (call[5]) call.store<uint32_t>(call[5], uint32_t(needed));
			if (!call[3] || call.u32(4) < needed) {
				call.fail(122);
				return;
			}
			std::vector<uint8_t> bytes(needed, 0);
			size_t text_at = record;
			const auto put = [&](size_t at, uint64_t value, size_t width) { std::memcpy(bytes.data() + at, &value, width); };
			size_t field_at = 0;
			if (level >= 2) {
				put(0, 3, 4);
				field_at = pointer == 8 ? 8 : 4;
			}
			for (const auto &value : strings) {
				put(field_at, call[3] + text_at, pointer);
				field_at += pointer;
				for (size_t index = 0; index < value.size(); ++index) {
					if (wide) put(text_at + index * 2, value[index], 2);
					else bytes[text_at + index] = uint8_t(value[index] < 128 ? value[index] : '?');
				}
				text_at += (value.size() + 1) * unit;
			}
			call.write(call[3], bytes);
			call.ret(1);
		});
		// GetPrinterData(handle, value name, type, data, size, needed): no driver data is kept, so a value is not found.
		registry.add(wide ? "GetPrinterDataW" : "GetPrinterDataA", 6, [=](ApiCall &call) {
			if (call[5]) call.store<uint32_t>(call[5], 0);
			call.ret(state->handles.contains(call[0]) ? 2 : 6);
		});
		// DocumentProperties(window, handle, name, out, in, mode): the size of a DEVMODE without a mode, else the printer's settings
		// with what the input asks merged in. The settings a program makes are kept for the printer while it runs; no dialog is shown.
		registry.add(wide ? "DocumentPropertiesW" : "DocumentPropertiesA", 6, [=](ApiCall &call) {
			std::optional<PrinterInfo> printer;
			if (const auto found = state->handles.find(call[1]); found != state->handles.end() && !found->second.empty())
				printer = printer_named(from_utf8(found->second));
			else if (call[2])
				printer = printer_named(text_of(call, call[2], wide));
			if (!printer) {
				call.fail(1801, uint64_t(int64_t(-1)));
				return;
			}
			const size_t size = wide ? 220 : 156;
			const uint32_t mode = call.u32(5);
			if (!mode) {
				call.ret(size);
				return;
			}
			auto settings = settings_of(*printer);
			if ((mode & 8) && call[4])
				settings = merged(settings, call.read(call[4], size), wide);
			if ((mode & 2) && call[3]) {
				call.write(call[3], devmode(printer->name, settings, wide));
				state->remembered[printer->name] = settings;
			}
			call.ret(1);
		});
		// DeviceCapabilities(device, port, capability, output, devmode): what the printer offers, as counts and arrays.
		registry.add(wide ? "DeviceCapabilitiesW" : "DeviceCapabilitiesA", 5, [=](ApiCall &call) {
			const auto printer = call[0] ? printer_named(text_of(call, call[0], wide)) : std::nullopt;
			if (!printer) {
				call.fail(1801, uint64_t(uint32_t(-1)));
				return;
			}
			const uint64_t out = call[3];
			const auto words = [&](std::vector<uint16_t> values) {
				if (out) for (size_t index = 0; index < values.size(); ++index) call.store<uint16_t>(out + index * 2, values[index]);
				call.ret(values.size());
			};
			const auto longs = [&](std::vector<int32_t> values, size_t per) {
				if (out) for (size_t index = 0; index < values.size(); ++index) call.store<int32_t>(out + index * 4, values[index]);
				call.ret(values.size() / per);
			};
			const auto names = [&](std::vector<std::u16string> values, size_t width) {
				if (out)
					for (size_t index = 0; index < values.size(); ++index) {
						const uint64_t at = out + index * width * (wide ? 2 : 1);
						if (wide) call.put_wide(at, values[index], width);
						else call.put_narrow(at, to_ansi(values[index], 1252), width);
					}
				call.ret(values.size());
			};
			switch (call.u32(2)) {
			case 1: call.ret(0x1 | 0x2 | 0x100 | 0x400 | 0x800 | 0x1000 | 0x2000 | 0x8000 | 0x10000); break;
			case 2: { std::vector<uint16_t> ids; for (const auto &paper : papers) ids.push_back(paper.id); words(ids); break; }
			case 3: { std::vector<int32_t> sizes; for (const auto &paper : papers) { sizes.push_back(paper.width); sizes.push_back(paper.height); } longs(sizes, 2); break; }
			case 16: { std::vector<std::u16string> list; for (const auto &paper : papers) list.push_back(paper.name); names(list, 64); break; }
			case 4: longs({1000, 1000}, 2); break;
			case 5: longs({4318, 4318}, 2); break;
			case 6: words({7}); break;
			case 12: names({u"Automatically Select"}, 24); break;
			case 7: call.ret(printer->duplex ? 1 : 0); break;
			case 8: call.ret(wide ? 220 : 156); break;
			case 9: call.ret(0); break;
			case 10: call.ret(0x401); break;
			case 11: call.ret(1); break;
			case 13: longs({print_dpi(), print_dpi()}, 2); break;
			case 17: call.ret(90); break;
			case 18: call.ret(999); break;
			case 22: call.ret(1); break;
			case 32: call.ret(printer->color ? 1 : 0); break;
			case 33: words({1}); call.ret(1); break;
			default: call.fail(50, uint64_t(uint32_t(-1))); break;
			}
		});
	}
	registry.add("ClosePrinter", 1, [=](ApiCall &call) {
		if (!state->handles.erase(call[0])) {
			call.fail(6);
			return;
		}
		call.ret(1);
	});

	// A printer device context: CreateDC(driver, device, port, devmode) naming a printer, or CreateIC to ask about it. Any other
	// device is the display, as before.
	const auto make_printer_dc = [this, state](const PrinterInfo &printer, const Settings &settings, bool info_only) -> uint64_t {
		const auto [width, height] = page_pixels(settings);
		// An information context only answers questions about the device; it draws nothing, so its surface is a single pixel.
		const uint32_t surface_width = info_only ? 1 : uint32_t(width), surface_height = info_only ? 1 : uint32_t(height);
		DesktopBitmap page{surface_width, surface_height, 32, std::vector<uint8_t>(size_t(surface_width) * surface_height * 4, 0xff)};
		const uint64_t bitmap = objects.add(std::move(page));
		if (!bitmap)
			throw uint32_t(8);
		const uint64_t dc = create_dc(0);
		if (!dc) {
			objects.erase(bitmap);
			throw uint32_t(8);
		}
		select(dc, bitmap);
		objects.dc(dc)->printer = true;
		Job job;
		job.printer = printer.name;
		job.settings = settings;
		job.bitmap = bitmap;
		job.width = width;
		job.height = height;
		job.info_only = info_only;
		state->jobs[dc] = std::move(job);
		return dc;
	};
	for (const bool wide : {true, false})
		for (const bool info : {false, true})
			registry.add(info ? (wide ? "CreateICW" : "CreateICA") : (wide ? "CreateDCW" : "CreateDCA"), 4, [=, this](ApiCall &call) {
				const auto driver = text_of(call, call[0], wide), device = text_of(call, call[1], wide);
				std::optional<PrinterInfo> printer;
				if (!device.empty() && !equal_fold(device, u"DISPLAY") && !device.starts_with(u"\\\\.\\DISPLAY"))
					printer = printer_named(device);
				else if (equal_fold(driver, u"WINSPOOL") && !device.empty())
					printer = printer_named(device);
				if (!printer) {
					// The display, or a device that is no printer here: a context compatible with the screen, as before.
					const uint64_t dc = create_dc(0);
					if (!dc) call.fail(8);
					else call.ret(dc);
					return;
				}
				auto settings = settings_of(*printer);
				if (call[3])
					settings = merged(settings, call.read(call[3], wide ? 220 : 156), wide);
				try {
					call.ret(make_printer_dc(*printer, settings, info));
				} catch (uint32_t error) {
					call.fail(error);
				}
			});
	// ResetDC(dc, devmode): new settings for the pages to come.
	for (const bool wide : {true, false})
		registry.add(wide ? "ResetDCW" : "ResetDCA", 2, [=, this](ApiCall &call) {
			const auto found = state->jobs.find(call[0]);
			if (found == state->jobs.end() || found->second.in_page) {
				call.ret(found == state->jobs.end() ? call[0] : 0);
				return;
			}
			auto &job = found->second;
			if (call[1])
				job.settings = merged(job.settings, call.read(call[1], wide ? 220 : 156), wide);
			const auto [width, height] = page_pixels(job.settings);
			if (width != job.width || height != job.height) {
				DesktopBitmap page{uint32_t(width), uint32_t(height), 32, std::vector<uint8_t>(size_t(width) * height * 4, 0xff)};
				const uint64_t bitmap = objects.add(std::move(page));
				if (!bitmap) {
					call.ret(0);
					return;
				}
				select(call[0], bitmap);
				objects.erase(job.bitmap);
				job.bitmap = bitmap;
				job.width = width;
				job.height = height;
			}
			call.ret(call[0]);
		});
	// GetDeviceCaps of a printer context: a page of the paper at 300 dots per inch, all of it printable.
	registry.add("GetDeviceCaps", 2, [=, this](ApiCall &call) {
		auto *dc = objects.dc(call[0]);
		if (!dc) {
			call.fail(6);
			return;
		}
		const auto found = state->jobs.find(call[0]);
		if (found == state->jobs.end()) {
			try {
				call.ret(device_caps(objects, *dc, call.i32(1)));
			} catch (uint32_t error) {
				call.fail(error);
			}
			return;
		}
		const auto &job = found->second;
		const auto &paper = paper_of(job.settings.paper);
		const int32_t millimetres_x = (job.settings.orientation == 2 ? paper.height : paper.width) / 10;
		const int32_t millimetres_y = (job.settings.orientation == 2 ? paper.width : paper.height) / 10;
		switch (call.i32(1)) {
		case 2: call.ret(2); break;
		case 4: call.ret(uint32_t(millimetres_x)); break;
		case 6: call.ret(uint32_t(millimetres_y)); break;
		case 8: case 110: case 118: call.ret(uint32_t(job.width)); break;
		case 10: case 111: case 117: call.ret(uint32_t(job.height)); break;
		case 12: call.ret(24); break;
		case 14: call.ret(1); break;
		case 24: call.ret(uint32_t(-1)); break;
		case 28: call.ret(0xff); break;
		case 30: call.ret(0xfe); break;
		case 32: call.ret(0xff); break;
		case 34: call.ret(0x7807); break;
		case 36: call.ret(1); break;
		case 38: call.ret(0x2e89); break;
		case 40: case 42: call.ret(print_dpi()); break;
		case 44: call.ret(424); break;
		case 88: case 90: call.ret(print_dpi()); break;
		case 108: call.ret(24); break;
		default: call.ret(0); break;
		}
	});
	registry.add("DeleteDC", 1, [=, this](ApiCall &call) {
		const auto found = state->jobs.find(call[0]);
		const uint64_t bitmap = found != state->jobs.end() ? found->second.bitmap : 0;
		const bool deleted = delete_dc(call[0]);
		if (found != state->jobs.end()) {
			state->jobs.erase(found);
			if (bitmap) objects.erase(bitmap);
		}
		if (!deleted) call.fail(6);
		else call.ret(1);
	});
	// StartDoc(dc, DOCINFO): the job's title and, when it names one, the file it is printed to instead of the printer.
	for (const bool wide : {true, false})
		registry.add(wide ? "StartDocW" : "StartDocA", 2, [=](ApiCall &call) {
			const auto found = state->jobs.find(call[0]);
			if (found == state->jobs.end() || found->second.info_only) {
				call.fail(found == state->jobs.end() ? 6 : 87, uint64_t(int64_t(-1)));
				return;
			}
			auto &job = found->second;
			job.pages.clear();
			job.started = true;
			job.title.clear();
			job.output.clear();
			if (call[1]) {
				const size_t pointer = call.pointer_width();
				const uint64_t name = call.load_pointer(call[1] + pointer), output = call.load_pointer(call[1] + pointer * 2);
				job.title = to_utf8(text_of(call, name, wide));
				const auto file = text_of(call, output, wide);
				if (!file.empty() && !equal_fold(file, u"FILE:"))
					job.output = to_utf8(file);
			}
			if (job.title.empty())
				job.title = "Raven document";
			call.ret(state->next_job++);
		});
	const auto clear_page = [this](Job &job) {
		if (auto *bitmap = objects.bitmap(job.bitmap)) {
			auto pixels = bitmap->pixels();
			std::fill(pixels.begin(), pixels.end(), uint8_t(0xff));
		}
	};
	registry.add("StartPage", 1, [=](ApiCall &call) {
		const auto found = state->jobs.find(call[0]);
		if (found == state->jobs.end() || !found->second.started) {
			call.fail(found == state->jobs.end() ? 6 : 87, 0);
			return;
		}
		clear_page(found->second);
		found->second.in_page = true;
		call.ret(1);
	});
	registry.add("EndPage", 1, [=, this](ApiCall &call) {
		const auto found = state->jobs.find(call[0]);
		if (found == state->jobs.end() || !found->second.in_page) {
			call.fail(found == state->jobs.end() ? 6 : 87, 0);
			return;
		}
		auto &job = found->second;
		job.in_page = false;
		const auto *bitmap = objects.bitmap(job.bitmap);
		if (!bitmap) {
			call.fail(6, 0);
			return;
		}
		const auto pixels = bitmap->pixels();
		std::vector<uint8_t> rgb(size_t(job.width) * job.height * 3);
		for (size_t index = 0, count = size_t(job.width) * job.height; index < count; ++index) {
			rgb[index * 3] = pixels[index * 4 + 2];
			rgb[index * 3 + 1] = pixels[index * 4 + 1];
			rgb[index * 3 + 2] = pixels[index * 4];
		}
		auto data = deflate(rgb);
		if (!data) {
			call.fail(8, 0);
			return;
		}
		job.pages.push_back({job.width, job.height, std::move(*data)});
		call.ret(1);
	});
	registry.add("AbortDoc", 1, [=](ApiCall &call) {
		const auto found = state->jobs.find(call[0]);
		if (found == state->jobs.end()) {
			call.fail(6, 0);
			return;
		}
		found->second.pages.clear();
		found->second.started = found->second.in_page = false;
		call.ret(1);
	});
	registry.add("EndDoc", 1, [=, this](ApiCall &call) {
		const auto found = state->jobs.find(call[0]);
		if (found == state->jobs.end() || !found->second.started) {
			call.fail(found == state->jobs.end() ? 6 : 87, 0);
			return;
		}
		auto &job = found->second;
		job.started = false;
		if (job.in_page) {
			job.in_page = false;
		}
		if (job.pages.empty()) {
			call.ret(1);
			return;
		}
		static const bool trace = std::getenv("RAVEN_TRACE_PRINT") != nullptr;
		// A file the job is printed to: Z: is the host's root, the other drives the prefix's drive_ folders.
		std::string path;
		if (job.output.size() > 3 && job.output[1] == ':' && (job.output[2] == '\\' || job.output[2] == '/')) {
			std::string rest = job.output.substr(3);
			std::replace(rest.begin(), rest.end(), '\\', '/');
			const char drive = char(std::tolower(uint8_t(job.output[0])));
			if (drive == 'z')
				path = "/" + rest;
			else if (const char *prefix = std::getenv("RAVEN_PREFIX"))
				path = std::string(prefix) + "/drive_" + drive + "/" + rest;
		}
		const bool to_file = !path.empty();
		if (!to_file) {
			char name[] = "/tmp/raven-print-XXXXXX";
			const int descriptor = ::mkstemp(name);
			if (descriptor < 0) {
				call.fail(8, 0);
				return;
			}
			::close(descriptor);
			path = name;
		}
		if (!write_pdf(path, job.pages)) {
			if (!to_file) ::unlink(path.c_str());
			call.fail(82, 0);
			return;
		}
		if (trace)
			std::fprintf(stderr, "print: %zu pages of \"%s\" to %s\n", job.pages.size(), job.title.c_str(), to_file ? path.c_str() : job.printer.c_str());
		if (!to_file) {
			const auto *cups = Cups::get();
			int submitted = 0;
			if (cups) {
				CupsOption *options = nullptr;
				int count = 0;
				const auto &paper = paper_of(job.settings.paper);
				count = cups->add_option("media", paper.media, count, &options);
				if (job.settings.copies > 1)
					count = cups->add_option("copies", std::to_string(job.settings.copies).c_str(), count, &options);
				count = cups->add_option("Collate", job.settings.collate ? "True" : "False", count, &options);
				count = cups->add_option("sides", job.settings.duplex == 2 ? "two-sided-long-edge" : job.settings.duplex == 3 ? "two-sided-short-edge" : "one-sided", count, &options);
				if (job.settings.color == 1)
					count = cups->add_option("print-color-mode", "monochrome", count, &options);
				submitted = cups->print_file(job.printer.c_str(), path.c_str(), job.title.c_str(), count, options);
				cups->free_options(count, options);
				if (trace)
					std::fprintf(stderr, "print: CUPS job %d%s%s\n", submitted, submitted ? "" : ": ", submitted ? "" : cups->last_error());
			}
			::unlink(path.c_str());
			if (!submitted) {
				call.fail(1804, 0);
				return;
			}
		}
		job.pages.clear();
		call.ret(1);
	});
	// The abort procedure is kept by no one: a job here is never held up in a way the program would be asked about.
	registry.add("SetAbortProc", 2, [=](ApiCall &call) { call.ret(state->jobs.contains(call[0]) ? 1 : uint32_t(-1)); });
	// Printer escapes: none beyond asking which are supported (QUERYESCSUPPORT, which answers for itself only).
	for (const auto *name : {"Escape", "ExtEscape"})
		registry.add(name, 5, [](ApiCall &call) {
			const uint32_t escape = call.u32(1);
			if (escape == 8 && call[3] && call.u32(2) >= 4) {
				call.ret(call.load<uint32_t>(call[3]) == 8 ? 1 : 0);
				return;
			}
			call.ret(0);
		});
}

}
