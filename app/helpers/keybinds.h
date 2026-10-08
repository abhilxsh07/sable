/*
Sable, a native port of NightPDF
Copyright (C) 2021  Advaith Madhukar
Copyright (C) 2026  Abhilash Kar

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; version 2
of the License.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/
// see:
// https://github.com/Lunarequest/NightPDF/blob/mistress/app/helpers/settings.ts
#pragma once

#include <cstddef>
#include <initializer_list>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace helpers {
// JS objects keep their keys in the order they were added, and the settings
// window lists things in that order. std::map would sort them, so this is a
// tiny stand-in that keeps the order. Not meant for big maps.
template <typename V> class OrderedMap {
  public:
	using Entry = std::pair<std::string, V>;
	using Storage = std::vector<Entry>;
	using iterator = typename Storage::iterator;
	using const_iterator = typename Storage::const_iterator;

	OrderedMap() = default;
	OrderedMap(std::initializer_list<Entry> init) {
		for (const Entry& entry : init) {
			set(entry.first, entry.second);
		}
	}

	bool contains(const std::string& name) const {
		return find(name) != nullptr;
	}

	const V* find(const std::string& name) const {
		for (const Entry& entry : m_entries) {
			if (entry.first == name) {
				return &entry.second;
			}
		}
		return nullptr;
	}

	V* find(const std::string& name) {
		for (Entry& entry : m_entries) {
			if (entry.first == name) {
				return &entry.second;
			}
		}
		return nullptr;
	}

	const V& at(const std::string& name) const {
		const V* value = find(name);
		if (value == nullptr) {
			throw std::out_of_range("no entry named " + name);
		}
		return *value;
	}

	V& at(const std::string& name) {
		V* value = find(name);
		if (value == nullptr) {
			throw std::out_of_range("no entry named " + name);
		}
		return *value;
	}

	// same as obj[name] = value in JS: replaces in place or adds at the end
	V& set(const std::string& name, V value) {
		if (V* existing = find(name)) {
			*existing = std::move(value);
			return *existing;
		}
		m_entries.emplace_back(name, std::move(value));
		return m_entries.back().second;
	}

	bool erase(const std::string& name) {
		for (auto it = m_entries.begin(); it != m_entries.end(); ++it) {
			if (it->first == name) {
				m_entries.erase(it);
				return true;
			}
		}
		return false;
	}

	std::size_t size() const {
		return m_entries.size();
	}
	bool empty() const {
		return m_entries.empty();
	}

	// Object.keys()
	std::vector<std::string> keys() const {
		std::vector<std::string> result;
		for (const Entry& entry : m_entries) {
			result.push_back(entry.first);
		}
		return result;
	}

	// Object.values()
	std::vector<V> values() const {
		std::vector<V> result;
		for (const Entry& entry : m_entries) {
			result.push_back(entry.second);
		}
		return result;
	}

	iterator begin() {
		return m_entries.begin();
	}
	iterator end() {
		return m_entries.end();
	}
	const_iterator begin() const {
		return m_entries.begin();
	}
	const_iterator end() const {
		return m_entries.end();
	}

	bool operator==(const OrderedMap&) const = default;

  private:
	Storage m_entries;
};

// platform name -> text, "default" is the fallback
using OsVariants = OrderedMap<std::string>;

// Modifier key definition (for keybinds)
// port of app/helpers/settings.ts ModifierKey (L30-38)
struct ModifierKey {
	std::string savesAs;
	bool osDependent = false;
	std::optional<std::string> displayAs;
	std::optional<OsVariants> osVariants;

	bool operator==(const ModifierKey&) const = default;
};

// A collection of modifier keys
// port of app/helpers/settings.ts ModifierKeyMap (L25-27)
using ModifierKeyMap = OrderedMap<ModifierKey>;

// to use in settings save/display
// port of app/helpers/settings.ts Keybind (L19-22)
struct Keybind {
	ModifierKeyMap modifiers;
	// nullopt is a json null, meaning nothing is bound
	std::optional<std::string> key;

	bool operator==(const Keybind&) const = default;
};

// kebind config
// port of app/helpers/settings.ts Keybinds (L11-16)
struct Keybinds {
	std::vector<Keybind> keybind;
	std::string action;
	std::optional<std::string> data;
	std::optional<std::string> displayName;

	bool operator==(const Keybinds&) const = default;
};

// upstream's Record<string, Keybinds>: action id -> its keybinds
using KeybindsMap = OrderedMap<Keybinds>;

// The modifier keys allowed in NightPDF
// port of app/helpers/settings.ts ModifierKeys (L378-415)
extern const ModifierKeyMap ModifierKeys;

// port of app/helpers/settings.ts modifierToString() (L417-428)
std::string modifierToString(const std::string& name,
                             const std::string& platform);

// port of app/helpers/settings.ts KeybindHelper (L40-133)
class KeybindHelper {
  public:
	KeybindHelper(Keybind keybind, std::string platform);

	static KeybindHelper fromKeybind(const Keybind& keybind,
	                                 const std::string& platform);

	// Accepts a modifier by its name ("CommandOrControl") or by the text it
	// is saved as ("Ctrl"). Upstream only looked at the name, so every
	// default "Ctrl+..." trigger lost its Ctrl. Unknown tokens are dropped.
	static KeybindHelper fromTrigger(const std::string& trigger,
	                                 const std::string& platform);

	// no platform means "none", like upstream
	static std::vector<Keybind> keybindFromTriggerArray(
	    const std::vector<std::string>& triggers,
	    const std::optional<std::string>& platform = std::nullopt);

	static std::vector<KeybindHelper>
	fromKeybindArray(const std::vector<Keybind>& keybinds,
	                 const std::string& platform);

	const Keybind& getKeybind() const;
	const std::string& getPlatform() const;

	// This is to store the trigger keybinds in the settings file
	// the savesAs property is what it should be stored as
	std::string toTrigger() const;

	// "Ctrl + Shift + T"
	std::string toString() const;
	std::vector<std::string> toStringArray() const;

	std::vector<ModifierKey> getModifierKeys() const;
	std::optional<std::string> getKey() const;

  private:
	Keybind m_keybind;
	std::string m_platform;
};

// Helper class for keybinds
// port of app/helpers/settings.ts KeybindsHelper (L135-198)
// Unknown actions and bad indexes throw std::out_of_range, where upstream
// would have hit a TypeError.
class KeybindsHelper {
  public:
	KeybindsHelper(KeybindsMap config, std::string platform);

	KeybindsMap config;
	std::string platform;
	std::vector<std::string> actions;

	std::vector<KeybindHelper>
	getActionKeybinds(const std::string& action) const;
	KeybindHelper getActionKeybind(const std::string& action,
	                               std::size_t index) const;
	std::vector<std::string>
	getActionKeybindsString(const std::string& action) const;
	std::vector<std::string>
	getActionKeybindsTrigger(const std::string& action) const;
	std::string getActionDisplayName(const std::string& action) const;
	std::optional<std::string> getActionData(const std::string& action) const;
	std::string getAction(const std::string& action) const;

	// index can be one past the end to add a binding
	const Keybinds& updateActionKeybind(const std::string& action,
	                                    std::size_t index,
	                                    const Keybind& keybind);
	const Keybinds& removeActionKeybind(const std::string& action,
	                                    std::size_t index);
};
} // namespace helpers
