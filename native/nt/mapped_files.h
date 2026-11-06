#pragma once

#include <cstdint>
#include <map>
#include <mutex>
#include <set>
#include <sys/stat.h>
#include <utility>

namespace raven {

// The files of this process that views share pages with, and where each view ends. Windows refuses to shorten a file below a view
// mapped from it (ERROR_USER_MAPPED_FILE), whichever handle asks; on the host the view would lose the pages past the new end and the
// program would fault reading them, so the same refusal keeps it whole.
class MappedFiles {
public:
	using Key = std::pair<uint64_t, uint64_t>;
	static MappedFiles &instance()
	{
		static MappedFiles files;
		return files;
	}
	static bool key_of(int descriptor, Key &key)
	{
		struct stat status{};
		if (::fstat(descriptor, &status))
			return false;
		key = {uint64_t(status.st_dev), uint64_t(status.st_ino)};
		return true;
	}
	void add(const Key &key, uint64_t end)
	{
		std::lock_guard lock(mutex);
		ends[key].insert(end);
	}
	void remove(const Key &key, uint64_t end)
	{
		std::lock_guard lock(mutex);
		const auto found = ends.find(key);
		if (found == ends.end())
			return;
		if (const auto entry = found->second.find(end); entry != found->second.end())
			found->second.erase(entry);
		if (found->second.empty())
			ends.erase(found);
	}
	// Whether the file may become this long: no view of it reaches past that length.
	bool may_shorten(int descriptor, uint64_t length)
	{
		Key key;
		if (!key_of(descriptor, key))
			return true;
		std::lock_guard lock(mutex);
		const auto found = ends.find(key);
		return found == ends.end() || found->second.empty() || *found->second.rbegin() <= length;
	}

private:
	std::mutex mutex;
	std::map<Key, std::multiset<uint64_t>> ends;
};

}
