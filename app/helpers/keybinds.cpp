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
#include "keybinds.h"

namespace helpers {
namespace {
// like "a+b".split("+") in JS: empty pieces stay, and there is always one
std::vector<std::string> split(const std::string& text, char separator) {
	std::vector<std::string> pieces;
	std::string current;
	for (char c : text) {
		if (c == separator) {
			pieces.push_back(current);
			current.clear();
		} else {
			current += c;
		}
	}
	pieces.push_back(current);
	return pieces;
}

// the name in ModifierKeys that a trigger token means, or nullptr. The name
// wins, so "Control" is still Control and not something else. After that
// "Ctrl" is looked up by what it is saved as.
const std::string* resolveModifier(const std::string& token) {
	for (const auto& entry : ModifierKeys) {
		if (entry.first == token) {
			return &entry.first;
		}
	}
	for (const auto& entry : ModifierKeys) {
		if (entry.second.savesAs == token) {
			return &entry.first;
		}
	}
	return nullptr;
}
} // namespace

// port of app/helpers/settings.ts ModifierKeys (L378-415)
// the mac symbol is U+2318 written out as UTF-8 bytes
const ModifierKeyMap ModifierKeys = {
    {"CommandOrControl",
     ModifierKey{"Ctrl", true, std::nullopt,
                 OsVariants{{"darwin", "\xE2\x8C\x98"}, {"default", "Ctrl"}}}},
    {"Control",
     ModifierKey{"Control", false, std::string("Ctrl"), std::nullopt}},
    {"Meta", ModifierKey{"Meta", true, std::nullopt,
                         OsVariants{{"darwin", "\xE2\x8C\x98"},
                                    {"win32", "Win"},
                                    {"default", "Meta"}}}},
    {"Alt", ModifierKey{"Alt", false, std::nullopt, std::nullopt}},
    {"AltGraph",
     ModifierKey{"AltGraph", false, std::string("AltGr"), std::nullopt}},
    {"Shift", ModifierKey{"Shift", false, std::nullopt, std::nullopt}},
};

// port of app/helpers/settings.ts modifierToString() (L417-428)
std::string modifierToString(const std::string& name,
                             const std::string& platform) {
	const ModifierKey* modifier = ModifierKeys.find(name);
	if (modifier == nullptr) {
		return name;
	}
	std::optional<std::string> displayAs;
	if (!modifier->osDependent || !modifier->osVariants) {
		displayAs = modifier->displayAs ? *modifier->displayAs : name;
	} else if (const std::string* variant =
	               modifier->osVariants->find(platform)) {
		displayAs = *variant;
	} else if (const std::string* fallback =
	               modifier->osVariants->find("default")) {
		displayAs = *fallback;
	}
	return displayAs.value_or(name);
}

// port of app/helpers/settings.ts KeybindHelper (L40-133)
KeybindHelper::KeybindHelper(Keybind keybind, std::string platform)
    : m_keybind(std::move(keybind)), m_platform(std::move(platform)) {}

// port of app/helpers/settings.ts KeybindHelper.fromKeybind() (L48-50)
KeybindHelper KeybindHelper::fromKeybind(const Keybind& keybind,
                                         const std::string& platform) {
	return KeybindHelper(keybind, platform);
}

// port of app/helpers/settings.ts KeybindHelper.fromTrigger() (L52-64)
KeybindHelper KeybindHelper::fromTrigger(const std::string& trigger,
                                         const std::string& platform) {
	Keybind keybind;
	std::vector<std::string> keys = split(trigger, '+');
	// last key is the non-modifier key
	keybind.key = keys.back();
	keys.pop_back();
	for (const std::string& token : keys) {
		const std::string* name = resolveModifier(token);
		if (name != nullptr) {
			keybind.modifiers.set(*name, ModifierKeys.at(*name));
		}
	}
	return fromKeybind(keybind, platform);
}

// port of app/helpers/settings.ts KeybindHelper.keybindFromTriggerArray()
// (L66-78)
std::vector<Keybind> KeybindHelper::keybindFromTriggerArray(
    const std::vector<std::string>& triggers,
    const std::optional<std::string>& platform) {
	// if we are using this to create initial config we don't care.
	const std::string used = platform ? *platform : "none";
	std::vector<Keybind> keybinds;
	for (const std::string& trigger : triggers) {
		keybinds.push_back(fromTrigger(trigger, used).getKeybind());
	}
	return keybinds;
}

// port of app/helpers/settings.ts KeybindHelper.fromKeybindArray() (L80-87)
std::vector<KeybindHelper>
KeybindHelper::fromKeybindArray(const std::vector<Keybind>& keybinds,
                                const std::string& platform) {
	std::vector<KeybindHelper> helpers;
	for (const Keybind& keybind : keybinds) {
		helpers.push_back(fromKeybind(keybind, platform));
	}
	return helpers;
}

// port of app/helpers/settings.ts KeybindHelper.getKeybind() (L89-91)
const Keybind& KeybindHelper::getKeybind() const {
	return m_keybind;
}

const std::string& KeybindHelper::getPlatform() const {
	return m_platform;
}

// port of app/helpers/settings.ts KeybindHelper.toTrigger() (L93-106)
// A binding with no key at all gives "Ctrl+" here, upstream wrote "Ctrl+null".
std::string KeybindHelper::toTrigger() const {
	std::string trigger;
	for (const auto& entry : m_keybind.modifiers) {
		// a modifier we don't know by name still has its own savesAs
		const ModifierKey* known = ModifierKeys.find(entry.first);
		trigger += (known != nullptr ? known->savesAs : entry.second.savesAs);
		trigger += '+';
	}
	trigger += m_keybind.key.value_or("");
	return trigger;
}

// port of app/helpers/settings.ts KeybindHelper.toString() (L108-110)
std::string KeybindHelper::toString() const {
	std::string text;
	for (const std::string& piece : toStringArray()) {
		if (!text.empty()) {
			text += " + ";
		}
		text += piece;
	}
	return text;
}

// port of app/helpers/settings.ts KeybindHelper.toStringArray() (L112-124)
std::vector<std::string> KeybindHelper::toStringArray() const {
	std::vector<std::string> keybindStrings;
	if (!m_keybind.key) {
		return keybindStrings;
	}
	for (const auto& entry : m_keybind.modifiers) {
		keybindStrings.push_back(modifierToString(entry.first, m_platform));
	}
	keybindStrings.push_back(*m_keybind.key);
	return keybindStrings;
}

// port of app/helpers/settings.ts KeybindHelper.getModifierKeys() (L126-128)
std::vector<ModifierKey> KeybindHelper::getModifierKeys() const {
	return m_keybind.modifiers.values();
}

// port of app/helpers/settings.ts KeybindHelper.getKey() (L130-132)
std::optional<std::string> KeybindHelper::getKey() const {
	return m_keybind.key;
}

// port of app/helpers/settings.ts KeybindsHelper (L135-198)
KeybindsHelper::KeybindsHelper(KeybindsMap config, std::string platform)
    : config(std::move(config)), platform(std::move(platform)) {
	actions = this->config.keys();
}

// port of app/helpers/settings.ts KeybindsHelper.getActionKeybinds()
// (L147-152)
std::vector<KeybindHelper>
KeybindsHelper::getActionKeybinds(const std::string& action) const {
	return KeybindHelper::fromKeybindArray(config.at(action).keybind, platform);
}

// port of app/helpers/settings.ts KeybindsHelper.getActionKeybind() (L154-159)
KeybindHelper KeybindsHelper::getActionKeybind(const std::string& action,
                                               std::size_t index) const {
	return KeybindHelper::fromKeybind(config.at(action).keybind.at(index),
	                                  platform);
}

// port of app/helpers/settings.ts KeybindsHelper.getActionKeybindsString()
// (L161-165)
std::vector<std::string>
KeybindsHelper::getActionKeybindsString(const std::string& action) const {
	std::vector<std::string> result;
	for (const KeybindHelper& keybind : getActionKeybinds(action)) {
		result.push_back(keybind.toString());
	}
	return result;
}

// port of app/helpers/settings.ts KeybindsHelper.getActionKeybindsTrigger()
// (L167-171)
std::vector<std::string>
KeybindsHelper::getActionKeybindsTrigger(const std::string& action) const {
	std::vector<std::string> result;
	for (const KeybindHelper& keybind : getActionKeybinds(action)) {
		result.push_back(keybind.toTrigger());
	}
	return result;
}

// port of app/helpers/settings.ts KeybindsHelper.getActionDisplayName()
// (L173-175)
std::string
KeybindsHelper::getActionDisplayName(const std::string& action) const {
	return config.at(action).displayName.value_or(action);
}

// port of app/helpers/settings.ts KeybindsHelper.getActionData() (L177-179)
std::optional<std::string>
KeybindsHelper::getActionData(const std::string& action) const {
	return config.at(action).data;
}

// port of app/helpers/settings.ts KeybindsHelper.getAction() (L181-183)
std::string KeybindsHelper::getAction(const std::string& action) const {
	return config.at(action).action;
}

// port of app/helpers/settings.ts KeybindsHelper.updateActionKeybind()
// (L185-192)
const Keybinds& KeybindsHelper::updateActionKeybind(const std::string& action,
                                                    std::size_t index,
                                                    const Keybind& keybind) {
	Keybinds& entry = config.at(action);
	if (index > entry.keybind.size()) {
		throw std::out_of_range("keybind index is past the end");
	}
	if (index == entry.keybind.size()) {
		entry.keybind.push_back(keybind);
	} else {
		entry.keybind[index] = keybind;
	}
	return entry;
}

// port of app/helpers/settings.ts KeybindsHelper.removeActionKeybind()
// (L194-197)
const Keybinds& KeybindsHelper::removeActionKeybind(const std::string& action,
                                                    std::size_t index) {
	Keybinds& entry = config.at(action);
	if (index < entry.keybind.size()) {
		entry.keybind.erase(entry.keybind.begin() +
		                    static_cast<std::ptrdiff_t>(index));
	}
	return entry;
}
} // namespace helpers
