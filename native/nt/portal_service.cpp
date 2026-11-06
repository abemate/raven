// The desktop's own file dialog for a program's open and save dialogs, through the XDG desktop portal (org.freedesktop.portal.FileChooser):
// on KDE that is KDE's dialog, on GNOME GNOME's.
//
// The shell component's dialog asks ravenhost.dll's RavenFileChooserStart with the dialog's state (title, folder, name, filters, the
// check boxes a program added) and polls RavenFileChooserPoll while it goes on dispatching the program's messages, so its windows keep
// answering the desktop while the dialog is open. The portal is reached on a thread of its own over the session bus, with the libdbus of
// the host loaded when first needed; without a session bus or a portal the poll says so, and the component shows its own dialog.
// RAVEN_NATIVE_DIALOGS=0 asks for that dialog always.

#include "api.h"
#include "kernel_context.h"
#include "text.h"

#include <atomic>
#include <chrono>
#include <cstring>
#include <dlfcn.h>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <cstdio>
#include <vector>

namespace raven {

namespace {

// libdbus, declared here as its public ABI has it, so building Raven needs no development files of it.
struct DBusError {
	const char *name, *message;
	unsigned int dummy;
	void *padding;
};
struct DBusMessageIter {
	void *dummy1, *dummy2;
	uint32_t dummy3;
	int dummy4, dummy5, dummy6, dummy7, dummy8, dummy9, dummy10, dummy11, pad1;
	void *pad2, *pad3;
};
struct DBusConnection;
struct DBusMessage;

struct DBus {
	void (*error_init)(DBusError *);
	void (*error_free)(DBusError *);
	unsigned (*error_is_set)(const DBusError *);
	DBusConnection *(*bus_get_private)(int, DBusError *);
	void (*set_exit_on_disconnect)(DBusConnection *, unsigned);
	void (*add_match)(DBusConnection *, const char *, DBusError *);
	DBusMessage *(*new_method_call)(const char *, const char *, const char *, const char *);
	void (*iter_init_append)(DBusMessage *, DBusMessageIter *);
	unsigned (*append_basic)(DBusMessageIter *, int, const void *);
	unsigned (*open_container)(DBusMessageIter *, int, const char *, DBusMessageIter *);
	unsigned (*close_container)(DBusMessageIter *, DBusMessageIter *);
	DBusMessage *(*send_with_reply_and_block)(DBusConnection *, DBusMessage *, int, DBusError *);
	unsigned (*read_write)(DBusConnection *, int);
	DBusMessage *(*pop_message)(DBusConnection *);
	unsigned (*is_signal)(DBusMessage *, const char *, const char *);
	const char *(*get_path)(DBusMessage *);
	unsigned (*iter_init)(DBusMessage *, DBusMessageIter *);
	int (*arg_type)(DBusMessageIter *);
	void (*get_basic)(DBusMessageIter *, void *);
	void (*recurse)(DBusMessageIter *, DBusMessageIter *);
	unsigned (*next)(DBusMessageIter *);
	void (*unref)(DBusMessage *);
	void (*close)(DBusConnection *);
	void (*connection_unref)(DBusConnection *);
	bool ready = false;

	static const DBus *get()
	{
		static DBus bus;
		static std::once_flag once;
		std::call_once(once, [] {
			void *library = ::dlopen("libdbus-1.so.3", RTLD_NOW | RTLD_LOCAL);
			if (!library)
				return;
			bool all = true;
			const auto find = [&](auto &slot, const char *name) {
				slot = reinterpret_cast<std::remove_reference_t<decltype(slot)>>(::dlsym(library, name));
				all = all && slot;
			};
			find(bus.error_init, "dbus_error_init");
			find(bus.error_free, "dbus_error_free");
			find(bus.error_is_set, "dbus_error_is_set");
			find(bus.bus_get_private, "dbus_bus_get_private");
			find(bus.set_exit_on_disconnect, "dbus_connection_set_exit_on_disconnect");
			find(bus.add_match, "dbus_bus_add_match");
			find(bus.new_method_call, "dbus_message_new_method_call");
			find(bus.iter_init_append, "dbus_message_iter_init_append");
			find(bus.append_basic, "dbus_message_iter_append_basic");
			find(bus.open_container, "dbus_message_iter_open_container");
			find(bus.close_container, "dbus_message_iter_close_container");
			find(bus.send_with_reply_and_block, "dbus_connection_send_with_reply_and_block");
			find(bus.read_write, "dbus_connection_read_write");
			find(bus.pop_message, "dbus_connection_pop_message");
			find(bus.is_signal, "dbus_message_is_signal");
			find(bus.get_path, "dbus_message_get_path");
			find(bus.iter_init, "dbus_message_iter_init");
			find(bus.arg_type, "dbus_message_iter_get_arg_type");
			find(bus.get_basic, "dbus_message_iter_get_basic");
			find(bus.recurse, "dbus_message_iter_recurse");
			find(bus.next, "dbus_message_iter_next");
			find(bus.unref, "dbus_message_unref");
			find(bus.close, "dbus_connection_close");
			find(bus.connection_unref, "dbus_connection_unref");
			bus.ready = all;
		});
		return bus.ready ? &bus : nullptr;
	}
};

constexpr int type_string = 's', type_path = 'o', type_boolean = 'b', type_uint32 = 'u', type_array = 'a', type_variant = 'v',
	type_dict_entry = 'e', type_struct = 'r', type_byte = 'y';

// What a dialog asks of the portal, in host terms, and what it answered.
struct Request {
	bool save = false, multiple = false, directory = false;
	std::string title, accept, name, folder;
	struct Filter { std::string name; std::vector<std::string> patterns; };
	std::vector<Filter> filters;
	int current_filter = -1;
	struct Check { std::string id, label; bool checked; };
	std::vector<Check> checks;
};

struct Outcome {
	// 0 while the dialog is open, 1 accepted, 2 cancelled, 3 no portal to ask.
	std::atomic<uint32_t> status{0};
	std::vector<std::string> paths;
	std::string filter;
	std::map<std::string, std::string> choices;
};

// The layout of the shell component's DialogState (shell.h): only 32-bit integers and UTF-16 arrays, the same for both widths.
namespace layout {
constexpr size_t save = 0, options = 4, title = 8, ok_label = 264, filters = 392, filter_size = 640, filter_spec = 128, filter_count = 20872,
	filter_index = 20876, dir = 20880, name = 21400, defext = 21920, result = 21952, results = 22472, result_count = 55752,
	has_result = 55756, checks = 55760, check_size = 136, check_count = 56304, size = 56308;
constexpr size_t max_filters = 32, max_results = 64, max_checks = 4, path = 260;
}

std::u16string wide_at(const std::vector<uint8_t> &bytes, size_t at, size_t characters)
{
	std::u16string text;
	for (size_t index = 0; index < characters && at + index * 2 + 1 < bytes.size(); ++index) {
		const char16_t c = char16_t(bytes[at + index * 2] | bytes[at + index * 2 + 1] << 8);
		if (!c)
			break;
		text.push_back(c);
	}
	return text;
}

uint32_t u32_at(const std::vector<uint8_t> &bytes, size_t at)
{
	uint32_t value;
	std::memcpy(&value, bytes.data() + at, 4);
	return value;
}

std::string uri_path(const std::string &uri)
{
	if (uri.rfind("file://", 0) != 0)
		return {};
	std::string path;
	for (size_t at = 7; at < uri.size(); ++at) {
		if (uri[at] == '%' && at + 2 < uri.size()) {
			path.push_back(char(std::stoi(uri.substr(at + 1, 2), nullptr, 16)));
			at += 2;
		} else {
			path.push_back(uri[at]);
		}
	}
	return path;
}

// Asks the portal and waits for the user's answer; on any failure to reach the portal the outcome says there is none.
void ask(const Request request, std::shared_ptr<Outcome> outcome)
{
	const auto *bus = DBus::get();
	if (!bus) {
		outcome->status = 3;
		return;
	}
	DBusError error;
	bus->error_init(&error);
	DBusConnection *connection = bus->bus_get_private(0, &error);
	if (!connection || bus->error_is_set(&error)) {
		bus->error_free(&error);
		outcome->status = 3;
		return;
	}
	bus->set_exit_on_disconnect(connection, 0);
	bus->add_match(connection, "type='signal',interface='org.freedesktop.portal.Request',member='Response'", &error);
	bus->error_free(&error);
	DBusMessage *call = bus->new_method_call("org.freedesktop.portal.Desktop", "/org/freedesktop/portal/desktop",
		"org.freedesktop.portal.FileChooser", request.save ? "SaveFile" : "OpenFile");
	DBusMessageIter top, options, entry, variant, array, item, inner, patterns, pattern;
	bus->iter_init_append(call, &top);
	const char *parent = "", *title = request.title.c_str();
	bus->append_basic(&top, type_string, &parent);
	bus->append_basic(&top, type_string, &title);
	bus->open_container(&top, type_array, "{sv}", &options);
	const auto begin = [&](const char *key, const char *signature) {
		bus->open_container(&options, type_dict_entry, nullptr, &entry);
		bus->append_basic(&entry, type_string, &key);
		bus->open_container(&entry, type_variant, signature, &variant);
	};
	const auto end = [&] {
		bus->close_container(&entry, &variant);
		bus->close_container(&options, &entry);
	};
	const auto text = [&](const char *key, const std::string &value) {
		begin(key, "s");
		const char *data = value.c_str();
		bus->append_basic(&variant, type_string, &data);
		end();
	};
	const auto flag = [&](const char *key, bool value) {
		begin(key, "b");
		const uint32_t data = value;
		bus->append_basic(&variant, type_boolean, &data);
		end();
	};
	static std::atomic<uint32_t> counter{0};
	text("handle_token", "raven" + std::to_string(++counter));
	flag("modal", true);
	if (!request.accept.empty())
		text("accept_label", request.accept);
	if (request.save) {
		if (!request.name.empty())
			text("current_name", request.name);
	} else {
		flag("multiple", request.multiple);
		flag("directory", request.directory);
	}
	if (!request.folder.empty()) {
		begin("current_folder", "ay");
		bus->open_container(&variant, type_array, "y", &array);
		for (const char c : request.folder + std::string(1, '\0')) {
			const uint8_t byte = uint8_t(c);
			bus->append_basic(&array, type_byte, &byte);
		}
		bus->close_container(&variant, &array);
		end();
	}
	const auto write_filter = [&](DBusMessageIter *into, const Request::Filter &filter) {
		const char *name = filter.name.c_str();
		bus->append_basic(into, type_string, &name);
		bus->open_container(into, type_array, "(us)", &patterns);
		for (const auto &glob : filter.patterns) {
			bus->open_container(&patterns, type_struct, nullptr, &pattern);
			const uint32_t kind = 0;
			const char *value = glob.c_str();
			bus->append_basic(&pattern, type_uint32, &kind);
			bus->append_basic(&pattern, type_string, &value);
			bus->close_container(&patterns, &pattern);
		}
		bus->close_container(into, &patterns);
	};
	if (!request.filters.empty()) {
		begin("filters", "a(sa(us))");
		bus->open_container(&variant, type_array, "(sa(us))", &array);
		for (const auto &filter : request.filters) {
			bus->open_container(&array, type_struct, nullptr, &item);
			write_filter(&item, filter);
			bus->close_container(&array, &item);
		}
		bus->close_container(&variant, &array);
		end();
		if (request.current_filter >= 0 && size_t(request.current_filter) < request.filters.size()) {
			begin("current_filter", "(sa(us))");
			bus->open_container(&variant, type_struct, nullptr, &item);
			write_filter(&item, request.filters[size_t(request.current_filter)]);
			bus->close_container(&variant, &item);
			end();
		}
	}
	if (!request.checks.empty()) {
		begin("choices", "a(ssa(ss)s)");
		bus->open_container(&variant, type_array, "(ssa(ss)s)", &array);
		for (const auto &check : request.checks) {
			bus->open_container(&array, type_struct, nullptr, &item);
			const char *id = check.id.c_str(), *label = check.label.c_str(), *initial = check.checked ? "true" : "false";
			bus->append_basic(&item, type_string, &id);
			bus->append_basic(&item, type_string, &label);
			bus->open_container(&item, type_array, "(ss)", &inner);
			bus->close_container(&item, &inner);
			bus->append_basic(&item, type_string, &initial);
			bus->close_container(&array, &item);
		}
		bus->close_container(&variant, &array);
		end();
	}
	bus->close_container(&top, &options);
	static const bool trace = std::getenv("RAVEN_TRACE_PORTAL") != nullptr;
	if (trace)
		std::fprintf(stderr, "portal: %s \"%s\" folder \"%s\" name \"%s\" filters %zu checks %zu\n", request.save ? "SaveFile" : "OpenFile",
			request.title.c_str(), request.folder.c_str(), request.name.c_str(), request.filters.size(), request.checks.size());
	DBusMessage *reply = bus->send_with_reply_and_block(connection, call, 10000, &error);
	bus->unref(call);
	std::string handle;
	if (reply) {
		DBusMessageIter answer;
		if (bus->iter_init(reply, &answer) && bus->arg_type(&answer) == type_path) {
			const char *path = nullptr;
			bus->get_basic(&answer, &path);
			if (path)
				handle = path;
		}
		bus->unref(reply);
	}
	if (trace)
		std::fprintf(stderr, "portal: request %s%s%s\n", handle.c_str(), bus->error_is_set(&error) ? " error " : "",
			bus->error_is_set(&error) && error.message ? error.message : "");
	if (handle.empty() || bus->error_is_set(&error)) {
		bus->error_free(&error);
		bus->close(connection);
		bus->connection_unref(connection);
		outcome->status = 3;
		return;
	}
	// The answer is the Response signal of the request object; a dialog stays open as long as the user wants it.
	uint32_t status = 0;
	while (!status) {
		if (!bus->read_write(connection, 200)) {
			status = 2;
			break;
		}
		while (DBusMessage *message = bus->pop_message(connection)) {
			const char *path = bus->get_path(message);
			if (!status && bus->is_signal(message, "org.freedesktop.portal.Request", "Response") && path && handle == path) {
				DBusMessageIter arguments, results, pair, value, list, element;
				uint32_t response = 2;
				if (bus->iter_init(message, &arguments) && bus->arg_type(&arguments) == type_uint32) {
					bus->get_basic(&arguments, &response);
					bus->next(&arguments);
					if (bus->arg_type(&arguments) == type_array) {
						bus->recurse(&arguments, &results);
						for (; bus->arg_type(&results) == type_dict_entry; bus->next(&results)) {
							bus->recurse(&results, &pair);
							const char *key = nullptr;
							bus->get_basic(&pair, &key);
							bus->next(&pair);
							bus->recurse(&pair, &value);
							const std::string name = key ? key : "";
							if (name == "uris" && bus->arg_type(&value) == type_array) {
								bus->recurse(&value, &list);
								for (; bus->arg_type(&list) == type_string; bus->next(&list)) {
									const char *uri = nullptr;
									bus->get_basic(&list, &uri);
									if (uri)
										outcome->paths.push_back(uri_path(uri));
								}
							} else if (name == "current_filter" && bus->arg_type(&value) == type_struct) {
								bus->recurse(&value, &element);
								const char *filter = nullptr;
								if (bus->arg_type(&element) == type_string)
									bus->get_basic(&element, &filter);
								if (filter)
									outcome->filter = filter;
							} else if (name == "choices" && bus->arg_type(&value) == type_array) {
								bus->recurse(&value, &list);
								for (; bus->arg_type(&list) == type_struct; bus->next(&list)) {
									bus->recurse(&list, &element);
									const char *id = nullptr, *chosen = nullptr;
									bus->get_basic(&element, &id);
									bus->next(&element);
									bus->get_basic(&element, &chosen);
									if (id && chosen)
										outcome->choices[id] = chosen;
								}
							}
						}
					}
				}
				status = response == 0 && !outcome->paths.empty() ? 1 : 2;
			}
			bus->unref(message);
		}
	}
	if (trace)
		std::fprintf(stderr, "portal: answer %u with %zu paths, filter \"%s\"%s%s\n", status, outcome->paths.size(), outcome->filter.c_str(),
			outcome->paths.empty() ? "" : ", first ", outcome->paths.empty() ? "" : outcome->paths.front().c_str());
	bus->close(connection);
	bus->connection_unref(connection);
	outcome->status = status;
}

}

void register_portal_service(ApiRegistry &registry, KernelContext &context)
{
	auto tickets = std::make_shared<std::map<uint64_t, std::shared_ptr<Outcome>>>();
	auto lock = std::make_shared<std::mutex>();
	// RavenFileChooserStart(state, size, owner): a ticket for the dialog the portal shows, or 0 when the program's own one is wanted.
	registry.add("RavenFileChooserStart", 3, [tickets, lock, &context](ApiCall &call) {
		if (const char *wanted = std::getenv("RAVEN_NATIVE_DIALOGS"); wanted && std::string(wanted) == "0") {
			call.ret(0);
			return;
		}
		if (!call[0] || call.u32(1) != layout::size || !DBus::get()) {
			call.ret(0);
			return;
		}
		const auto state = call.read(call[0], layout::size);
		Request request;
		request.save = u32_at(state, layout::save) != 0;
		const uint32_t options = u32_at(state, layout::options);
		request.multiple = (options & 0x200) != 0;
		request.directory = (options & 0x20) != 0;
		request.title = to_utf8(wide_at(state, layout::title, 128));
		if (request.title.empty())
			request.title = request.directory ? "Select Folder" : request.save ? "Save As" : "Open";
		request.accept = to_utf8(wide_at(state, layout::ok_label, 32));
		const auto name = wide_at(state, layout::name, layout::path);
		const auto slash = name.find_last_of(u"\\/");
		request.name = to_utf8(slash == name.npos ? name : name.substr(slash + 1));
		std::u16string folder = wide_at(state, layout::dir, layout::path);
		if (slash != name.npos && (name.size() > 1 && name[1] == u':'))
			folder = name.substr(0, slash);
		if (!folder.empty())
			if (const auto resolved = context.paths.resolve(folder); resolved && resolved->exists)
				request.folder = resolved->host.string();
		const uint32_t filters = std::min<uint32_t>(u32_at(state, layout::filter_count), layout::max_filters);
		for (uint32_t index = 0; index < filters; ++index) {
			const size_t at = layout::filters + index * layout::filter_size;
			Request::Filter filter{to_utf8(wide_at(state, at, 64)), {}};
			const auto spec = to_utf8(wide_at(state, at + layout::filter_spec, 256));
			for (size_t start = 0; start <= spec.size();) {
				const auto stop = spec.find(';', start);
				auto glob = spec.substr(start, stop == spec.npos ? spec.npos : stop - start);
				while (!glob.empty() && glob.front() == ' ') glob.erase(glob.begin());
				if (!glob.empty())
					filter.patterns.push_back(glob == "*.*" ? "*" : glob);
				if (stop == spec.npos)
					break;
				start = stop + 1;
			}
			if (filter.patterns.empty())
				filter.patterns.push_back("*");
			request.filters.push_back(std::move(filter));
		}
		const uint32_t index = u32_at(state, layout::filter_index);
		request.current_filter = index >= 1 && index <= filters ? int(index - 1) : -1;
		const uint32_t checks = std::min<uint32_t>(u32_at(state, layout::check_count), layout::max_checks);
		for (uint32_t check = 0; check < checks; ++check) {
			const size_t at = layout::checks + check * layout::check_size;
			request.checks.push_back({std::to_string(u32_at(state, at)), to_utf8(wide_at(state, at + 8, 64)), u32_at(state, at + 4) != 0});
		}
		auto outcome = std::make_shared<Outcome>();
		static std::atomic<uint64_t> next{1};
		const uint64_t ticket = next++;
		{
			std::lock_guard guard(*lock);
			(*tickets)[ticket] = outcome;
		}
		std::thread(ask, std::move(request), outcome).detach();
		call.ret(ticket);
	});
	// RavenFileChooserPoll(ticket, state): 0 while the dialog is open; then 1 with the chosen paths, the filter and the check boxes in
	// the state, 2 when the user cancelled, 3 when there was no portal to ask after all.
	registry.add("RavenFileChooserPoll", 2, [tickets, lock, &context](ApiCall &call) {
		std::shared_ptr<Outcome> outcome;
		{
			std::lock_guard guard(*lock);
			const auto found = tickets->find(call[0]);
			if (found == tickets->end()) {
				call.ret(3);
				return;
			}
			outcome = found->second;
		}
		const uint32_t status = outcome->status;
		if (!status) {
			call.ret(0);
			return;
		}
		{
			std::lock_guard guard(*lock);
			tickets->erase(call[0]);
		}
		if (status != 1 || !call[1]) {
			call.ret(status);
			return;
		}
		auto state = call.read(call[1], layout::size);
		const auto put_wide = [&](size_t at, const std::u16string &text, size_t characters) {
			const size_t count = std::min(text.size(), characters - 1);
			for (size_t index = 0; index < characters; ++index) {
				const char16_t c = index < count ? text[index] : u'\0';
				state[at + index * 2] = uint8_t(c);
				state[at + index * 2 + 1] = uint8_t(c >> 8);
			}
		};
		const auto put_u32 = [&](size_t at, uint32_t value) { std::memcpy(state.data() + at, &value, 4); };
		const bool save = u32_at(state, layout::save) != 0;
		const auto defext = wide_at(state, layout::defext, 16);
		uint32_t count = 0;
		for (const auto &path : outcome->paths) {
			if (count >= layout::max_results || path.empty())
				continue;
			auto windows = context.paths.dos(std::filesystem::path(path));
			if (windows.empty())
				continue;
			// A name saved without an extension takes the default one, as the dialog adds it on Windows.
			if (save && !defext.empty()) {
				const auto part = windows.substr(windows.find_last_of(u'\\') + 1);
				if (part.find(u'.') == part.npos)
					windows += u"." + defext;
			}
			put_wide(layout::results + count * layout::path * 2, windows, layout::path);
			if (!count)
				put_wide(layout::result, windows, layout::path);
			++count;
		}
		if (!count) {
			call.ret(2);
			return;
		}
		put_u32(layout::result_count, count);
		put_u32(layout::has_result, 1);
		const uint32_t filters = std::min<uint32_t>(u32_at(state, layout::filter_count), layout::max_filters);
		for (uint32_t index = 0; index < filters; ++index)
			if (to_utf8(wide_at(state, layout::filters + index * layout::filter_size, 64)) == outcome->filter)
				put_u32(layout::filter_index, index + 1);
		const uint32_t checks = std::min<uint32_t>(u32_at(state, layout::check_count), layout::max_checks);
		for (uint32_t check = 0; check < checks; ++check) {
			const size_t at = layout::checks + check * layout::check_size;
			if (const auto found = outcome->choices.find(std::to_string(u32_at(state, at))); found != outcome->choices.end())
				put_u32(at + 4, found->second == "true");
		}
		call.write(call[1], state);
		call.ret(1);
	});
}

}
