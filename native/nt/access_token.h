#pragma once

#include "objects.h"

#include <cstdint>
#include <vector>

namespace raven {

// Token metadata has shared storage so handles transferred to another process keep the same identity and mutations.
class AccessToken : public KernelObject {
public:
	struct Privilege {
		uint64_t luid;
		uint32_t attributes;
	};
	struct Data {
		std::vector<uint8_t> default_dacl;
		uint32_t integrity = 8192;
		uint32_t type = 1;
		uint32_t impersonation_level = 2;
		std::vector<Privilege> privileges;
	};
	AccessToken();
	explicit AccessToken(int descriptor);
	AccessToken(const AccessToken &other);
	AccessToken &operator=(const AccessToken &) = delete;
	~AccessToken() override;
	std::u16string_view type_name() const override { return u"Token"; }
	uint32_t default_access() const override { return 0xf01ff; }
	bool describe(InheritedHandle &record) const override;
	Data data() const;
	void set_default_dacl(std::span<const uint8_t> acl);
	void set_integrity(uint32_t value);
	void set_type(uint32_t value);
	void set_impersonation_level(uint32_t value);
	void restrict_privileges(bool maximum, std::span<const uint64_t> removed);
	// Output validation runs before any privilege changes become visible through another handle.
	uint32_t adjust_privileges(bool disable, std::span<const Privilege> changes,
		const std::function<uint32_t(std::span<const Privilege>)> &prepare);

private:
	struct Shared;
	int descriptor = -1;
	Shared *shared = nullptr;
	void map();
};

std::optional<uint32_t> token_access(uint32_t requested);

// A SID with the given identifier authority and sub-authorities.
inline std::vector<uint8_t> make_sid(uint8_t authority, std::initializer_list<uint32_t> parts)
{
	std::vector<uint8_t> sid{1, uint8_t(parts.size()), 0, 0, 0, 0, 0, authority};
	for (const auto part : parts)
		for (int shift = 0; shift < 32; shift += 8)
			sid.push_back(uint8_t(part >> shift));
	return sid;
}

// The account the program runs as: the same identity the registry path of the current user names.
inline std::vector<uint8_t> user_sid() { return make_sid(5, {21, 1000, 1000, 1000, 1000}); }

// The groups of the interactive desktop logon every program runs in, in the order TokenGroups lists them: Users, Administrators
// (held for denying access only unless elevated), Everyone, the logon session, INTERACTIVE, CONSOLE LOGON, Authenticated Users,
// This Organization and LOCAL. Programs look for INTERACTIVE to tell a user's session from a service.
inline std::vector<std::vector<uint8_t>> interactive_group_sids()
{
	return {make_sid(5, {32, 545}), make_sid(5, {32, 544}), make_sid(1, {0}), make_sid(5, {5, 0, 1}), make_sid(5, {4}),
		make_sid(2, {1}), make_sid(5, {11}), make_sid(5, {15}), make_sid(2, {0})};
}

}
