// The real Ctrl trigger: a brand new object in the editor (own id).
// Uses the open-source "game objects factory" (lil2kki/geode-game-objects-factory),
// downloaded at build time and compiled into this mod.
#include <Geode/Geode.hpp>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <map>
#include <functional>
#include <string>
#include <vector>

// Geode v6 removed gd::vector / gd::string, but the library still uses these names.
namespace gd {
	template <class T>
	using vector = std::vector<T>;
	using string = std::string;
}

#include <main.hpp>
#include <impl.hpp>
#include "ctrl.hpp"

using namespace geode::prelude;

int ctrlObjectId() {
	static int id = static_cast<int>(UNIQ_ID("ctrl-trigger"));
	return id;
}

int physObjectId() {
	static int id = static_cast<int>(UNIQ_ID("physics-trigger"));
	return id;
}

namespace {
	std::map<GameObject*, PhysData> g_phys;
}

PhysData& physData(GameObject* obj) {
	return g_phys[obj];
}

namespace {
	std::map<GameObject*, int> g_modes;
	std::map<GameObject*, bool> g_locks;
	std::map<GameObject*, bool> g_afters;
	std::map<GameObject*, int> g_lefts;
	std::map<GameObject*, int> g_rights;
}

int ctrlLeftGet(GameObject* obj) {
	auto it = g_lefts.find(obj);
	return it == g_lefts.end() ? 0 : it->second;
}

void ctrlLeftSet(GameObject* obj, int group) {
	g_lefts[obj] = group;
}

int ctrlRightGet(GameObject* obj) {
	auto it = g_rights.find(obj);
	return it == g_rights.end() ? 0 : it->second;
}

void ctrlRightSet(GameObject* obj, int group) {
	g_rights[obj] = group;
}

bool ctrlAfterGet(GameObject* obj) {
	auto it = g_afters.find(obj);
	return it != g_afters.end() && it->second;
}

void ctrlAfterSet(GameObject* obj, bool after) {
	g_afters[obj] = after;
}

bool ctrlLockGet(GameObject* obj) {
	auto it = g_locks.find(obj);
	return it != g_locks.end() && it->second;
}

void ctrlLockSet(GameObject* obj, bool lock) {
	g_locks[obj] = lock;
}

int ctrlModeGet(GameObject* obj) {
	auto it = g_modes.find(obj);
	return it == g_modes.end() ? 0 : it->second;
}

void ctrlModeSet(GameObject* obj, int mode) {
	g_modes[obj] = mode;
}

// Hides the vanilla "Touch Trigger" / "Spawn Trigger" texts (they do nothing for Ctrl).
static void hideVanillaTexts(CCNode* n) {
	if (!n) return;
	auto kids = n->getChildren();
	if (!kids) return;
	for (auto c : CCArrayExt<CCNode*>(kids)) {
		if (auto l = typeinfo_cast<CCLabelBMFont*>(c)) {
			std::string t = l->getString();
			std::transform(t.begin(), t.end(), t.begin(), [](unsigned char ch) { return std::tolower(ch); });
			if (t.find("touch") != std::string::npos || t.find("spawn") != std::string::npos) c->setVisible(false);
		}
		hideVanillaTexts(c);
	}
}

// Controls of the Ctrl trigger window: a mode selector (Button / Drag), the groups of custom
// Left / Right movement buttons, and "Lock Move" / "After Drop" checkboxes.
class CTModeUI : public CCNode {
public:
	std::vector<EffectGameObject*> m_targets;
	CCMenuItemToggler* m_togLock = nullptr;
	CCMenuItemToggler* m_togAfter = nullptr;
	CCLabelBMFont* m_modeLabel = nullptr;
	int m_mode = 0;

	static char const* modeName(int m) {
		switch (m) {
			case 1: return "Drag";
			default: return "Button";
		}
	}

	static CTModeUI* create(std::vector<EffectGameObject*> targets, int mode, bool lock, bool after, CCMenu* menu, FLAlertLayer* popup) {
		auto r = new CTModeUI();
		if (r && r->init()) {
			r->autorelease();
			menu->addChild(r);
			r->setup(std::move(targets), mode, lock, after, menu, popup);
			return r;
		}
		CC_SAFE_DELETE(r);
		return nullptr;
	}

	void setup(std::vector<EffectGameObject*> targets, int mode, bool lock, bool after, CCMenu* menu, FLAlertLayer* popup) {
		m_targets = std::move(targets);
		m_mode = mode;

		// the existing Touch / Spawn checkboxes: we use their rows and then hide them
		std::vector<CCMenuItemToggler*> existing;
		if (auto kids = menu->getChildren()) {
			for (auto child : CCArrayExt<CCNode*>(kids)) {
				if (auto tg = typeinfo_cast<CCMenuItemToggler*>(child)) existing.push_back(tg);
			}
		}
		std::sort(existing.begin(), existing.end(), [](CCMenuItemToggler* a, CCMenuItemToggler* b) {
			return a->getPositionY() > b->getPositionY();
		});
		float x = 78.f, yTop = 37.f, yBottom = 8.f;
		float lx = -144.f, ly = 37.f, ly2 = 8.f;
		if (existing.size() >= 2) {
			x = existing[0]->getPositionX() + 222.f;
			yTop = existing[0]->getPositionY();
			yBottom = existing[1]->getPositionY();
			lx = existing[0]->getPositionX();
			ly = existing[0]->getPositionY();
			ly2 = existing[1]->getPositionY();
			for (auto tg : existing) {
				tg->setVisible(false);
				tg->setEnabled(false);
			}
			hideVanillaTexts(menu);
			if (popup) hideVanillaTexts(popup);
		}

		// mode selector:  <  Button  >
		auto arrow = [&](char const* t, SEL_MenuHandler cb, float px) {
			auto l = CCLabelBMFont::create(t, "bigFont.fnt");
			l->setScale(0.8f);
			auto it = CCMenuItemLabel::create(l, this, cb);
			it->setPosition({px, yTop});
			menu->addChild(it);
		};
		arrow("<", menu_selector(CTModeUI::onPrev), x);
		arrow(">", menu_selector(CTModeUI::onNext), x + 72.f);
		m_modeLabel = CCLabelBMFont::create(modeName(mode), "bigFont.fnt");
		m_modeLabel->setScale(0.5f);
		m_modeLabel->setPosition({x + 36.f, yTop});
		menu->addChild(m_modeLabel);
		auto cap = CCLabelBMFont::create("Mode", "bigFont.fnt");
		cap->setScale(0.3f);
		cap->setColor({200, 200, 200});
		cap->setPosition({x + 36.f, yTop + 13.f});
		menu->addChild(cap);

		// custom movement buttons: group of the Left button and group of the Right button
		auto field = [&](char const* caption, float px, int value, std::function<void(std::string const&)> cb) {
			auto in = TextInput::create(110.f, caption, "bigFont.fnt");
			in->setScale(0.4f);
			in->setPosition({px, yBottom - 3.f});
			in->setFilter("0123456789");
			in->setString(std::to_string(value));
			in->setCallback(cb);
			menu->addChild(in);
			auto l = CCLabelBMFont::create(caption, "bigFont.fnt");
			l->setScale(0.26f);
			l->setColor({200, 200, 200});
			l->setPosition({px, yBottom + 9.f});
			menu->addChild(l);
		};
		auto targets2 = m_targets;
		field("Left grp", x + 14.f, ctrlLeftGet(m_targets.front()), [targets2](std::string const& v) {
			int g = v.empty() ? 0 : std::atoi(v.c_str());
			for (auto t : targets2) ctrlLeftSet(t, g);
		});
		field("Right grp", x + 62.f, ctrlRightGet(m_targets.front()), [targets2](std::string const& v) {
			int g = v.empty() ? 0 : std::atoi(v.c_str());
			for (auto t : targets2) ctrlRightSet(t, g);
		});

		m_togLock = CCMenuItemToggler::createWithStandardSprites(this, menu_selector(CTModeUI::onLock), 0.55f);
		m_togAfter = CCMenuItemToggler::createWithStandardSprites(this, menu_selector(CTModeUI::onAfter), 0.55f);
		m_togLock->setPosition({lx, ly});
		m_togAfter->setPosition({lx, ly2});
		m_togLock->toggle(lock);
		m_togAfter->toggle(after);
		menu->addChild(m_togLock);
		menu->addChild(m_togAfter);

		auto addLabel = [&](char const* text, float px, float py) {
			auto l = CCLabelBMFont::create(text, "bigFont.fnt");
			l->setScale(0.4f);
			l->setAnchorPoint({0.f, 0.5f});
			l->setPosition({px + 14.f, py});
			menu->addChild(l);
		};
		addLabel("Lock\nMove", lx, ly);
		addLabel("After\nDrop", lx, ly2);
	}

	void setMode(int mode) {
		m_mode = (mode + 2) % 2;
		for (auto t : m_targets) ctrlModeSet(t, m_mode);
		m_modeLabel->setString(modeName(m_mode));
	}

	void onPrev(CCObject*) { setMode(m_mode - 1); }
	void onNext(CCObject*) { setMode(m_mode + 1); }

	// A toggler flips right AFTER its callback, so !isToggled() is the new state.
	void onAfter(CCObject* sender) {
		bool on = !static_cast<CCMenuItemToggler*>(sender)->isToggled();
		for (auto trg : m_targets) ctrlAfterSet(trg, on);
	}

	void onLock(CCObject* sender) {
		bool on = !static_cast<CCMenuItemToggler*>(sender)->isToggled();
		for (auto trg : m_targets) ctrlLockSet(trg, on);
	}
};

// Window of the Physics trigger: Physical / Collision / Fill Gaps checkboxes.
class CTPhysUI : public CCNode {
public:
	std::vector<EffectGameObject*> m_targets;

	static CTPhysUI* create(std::vector<EffectGameObject*> targets, CCMenu* menu, FLAlertLayer* popup) {
		auto r = new CTPhysUI();
		if (r && r->init()) {
			r->autorelease();
			menu->addChild(r);
			r->setup(std::move(targets), menu, popup);
			return r;
		}
		CC_SAFE_DELETE(r);
		return nullptr;
	}

	void setup(std::vector<EffectGameObject*> targets, CCMenu* menu, FLAlertLayer* popup) {
		m_targets = std::move(targets);
		auto first = m_targets.front();

		std::vector<CCMenuItemToggler*> existing;
		if (auto kids = menu->getChildren()) {
			for (auto child : CCArrayExt<CCNode*>(kids)) {
				if (auto tg = typeinfo_cast<CCMenuItemToggler*>(child)) existing.push_back(tg);
			}
		}
		std::sort(existing.begin(), existing.end(), [](CCMenuItemToggler* a, CCMenuItemToggler* b) {
			return a->getPositionY() > b->getPositionY();
		});
		float x = 78.f, yTop = 37.f, lx = -144.f, ly = 37.f, ly2 = 8.f;
		if (existing.size() >= 2) {
			x = existing[0]->getPositionX() + 222.f;
			yTop = existing[0]->getPositionY();
			lx = existing[0]->getPositionX();
			ly = existing[0]->getPositionY();
			ly2 = existing[1]->getPositionY();
			for (auto tg : existing) {
				tg->setVisible(false);
				tg->setEnabled(false);
			}
			hideVanillaTexts(menu);
			if (popup) hideVanillaTexts(popup);
		}

		auto& d = physData(first);
		auto add = [&](SEL_MenuHandler cb, bool on, float px, float py, char const* text) {
			auto t = CCMenuItemToggler::createWithStandardSprites(this, cb, 0.55f);
			t->setPosition({px, py});
			t->toggle(on);
			menu->addChild(t);
			auto l = CCLabelBMFont::create(text, "bigFont.fnt");
			l->setScale(0.4f);
			l->setAnchorPoint({0.f, 0.5f});
			l->setPosition({px + 14.f, py});
			menu->addChild(l);
		};
		add(menu_selector(CTPhysUI::onPhysical), d.physical, lx, ly, "Physical\nObject");
		add(menu_selector(CTPhysUI::onCollision), d.collision, lx, ly2, "Collision");
		add(menu_selector(CTPhysUI::onFill), d.fillGaps, x, yTop, "Fill\nGaps");
	}

	void onPhysical(CCObject* s) {
		bool on = !static_cast<CCMenuItemToggler*>(s)->isToggled();
		for (auto t : m_targets) physData(t).physical = on;
	}
	void onCollision(CCObject* s) {
		bool on = !static_cast<CCMenuItemToggler*>(s)->isToggled();
		for (auto t : m_targets) physData(t).collision = on;
	}
	void onFill(CCObject* s) {
		bool on = !static_cast<CCMenuItemToggler*>(s)->isToggled();
		for (auto t : m_targets) physData(t).fillGaps = on;
	}
};

namespace {
	// keys used to save the trigger settings inside the level string
	constexpr int KEY_BUTTON = 9001; // button group
	constexpr int KEY_FIRE = 9002;   // group of triggers to fire (0 = drag mode)
	constexpr int KEY_HOLD = 9003;   // hold time in seconds
	constexpr int KEY_MODE = 9004;   // 0 = button mode, 1 = drag mode
	constexpr int KEY_LOCK = 9005;   // 1 = lock the left/right buttons and keys
	constexpr int KEY_AFTER = 9006;  // 1 = dragged objects become active only after release
	constexpr int KEY_LEFT = 9007;   // group of the custom Left button
	constexpr int KEY_RIGHT = 9008;  // group of the custom Right button
}

$execute {
	// use the real sprite frame of an existing trigger (Move) so the texture matches;
	// the white circle with the number is drawn over it by main.cpp
	char const* frame = ObjectToolbox::sharedState()->intKeyToFrame(901);
	auto cfg = GameObjectsFactory::createTriggerConfig(
		UNIQ_ID("ctrl-trigger"),
		frame,
		[](auto, auto, auto, auto) {
			// the trigger itself does nothing: the touch logic lives in main.cpp
		},
		[](EditTriggersPopup* popup, EffectGameObject* trigger, CCArray* objects) {
			auto menu = popup->m_buttonMenu;
			if (!menu) return;

			auto apply = [trigger, objects](auto fn) {
				fn(trigger);
				if (objects) {
					for (auto obj : CCArrayExt<GameObject*>(objects)) {
						if (obj && obj->m_objectID == ctrlObjectId()) {
							fn(static_cast<EffectGameObject*>(obj));
						}
					}
				}
			};

			{
				std::vector<EffectGameObject*> targets{trigger};
				if (objects) {
					for (auto obj : CCArrayExt<GameObject*>(objects)) {
						if (obj && obj != trigger && obj->m_objectID == ctrlObjectId()) {
							targets.push_back(static_cast<EffectGameObject*>(obj));
						}
					}
				}
				CTModeUI::create(targets, ctrlModeGet(trigger), ctrlLockGet(trigger), ctrlAfterGet(trigger), menu, popup);
			}

			auto add = [&](char const* hint, float x, std::string value, std::function<void(std::string const&)> cb) {
				auto in = TextInput::create(110.f, hint, "bigFont.fnt");
				in->setScale(0.55f);
				in->setPosition({x, 47.f});
				in->setFilter("0123456789.");
				in->setString(value);
				in->setCallback(cb);
				menu->addChild(in);
			};

			add("Button group", -85.f, std::to_string(trigger->m_targetGroupID), [apply](std::string const& s) {
				int v = s.empty() ? 0 : std::atoi(s.c_str());
				apply([v](EffectGameObject* t) { t->m_targetGroupID = v; });
			});
			add("Fire group", 0.f, std::to_string(trigger->m_centerGroupID), [apply](std::string const& s) {
				int v = s.empty() ? 0 : std::atoi(s.c_str());
				apply([v](EffectGameObject* t) { t->m_centerGroupID = v; });
			});
			add("Hold sec", 85.f, std::to_string(trigger->m_duration), [apply](std::string const& s) {
				float v = s.empty() ? 0.f : static_cast<float>(std::atof(s.c_str()));
				apply([v](EffectGameObject* t) { t->m_duration = v; });
			});
		}
	);

	cfg->saveString([](std::string str, GameObject* obj, auto) {
		auto trig = static_cast<EffectGameObject*>(obj);
		bool semi = !str.empty() && str.back() == ';';
		if (semi) str.pop_back();
		if (!str.empty() && str.back() != ',') str += ",";
		str += fmt::format("{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{}", KEY_BUTTON, trig->m_targetGroupID, KEY_FIRE, trig->m_centerGroupID, KEY_HOLD, trig->m_duration, KEY_MODE, ctrlModeGet(obj), KEY_LOCK, ctrlLockGet(obj) ? 1 : 0, KEY_AFTER, ctrlAfterGet(obj) ? 1 : 0, KEY_LEFT, ctrlLeftGet(obj), KEY_RIGHT, ctrlRightGet(obj));
		if (semi) str += ";";
		return str;
	});

	cfg->objectFromVector([](GameObject* obj, auto& p0, auto&, auto, auto) -> GameObject* {
		auto trig = static_cast<EffectGameObject*>(obj);
		for (size_t i = 0; i + 1 < p0.size(); i += 2) {
			int key = std::atoi(std::string(p0[i]).c_str());
			std::string val = std::string(p0[i + 1]);
			if (key == KEY_BUTTON) trig->m_targetGroupID = std::atoi(val.c_str());
			else if (key == KEY_FIRE) trig->m_centerGroupID = std::atoi(val.c_str());
			else if (key == KEY_HOLD) trig->m_duration = static_cast<float>(std::atof(val.c_str()));
			else if (key == KEY_MODE) ctrlModeSet(obj, std::atoi(val.c_str()));
			else if (key == KEY_LOCK) ctrlLockSet(obj, std::atoi(val.c_str()) != 0);
			else if (key == KEY_AFTER) ctrlAfterSet(obj, std::atoi(val.c_str()) != 0);
			else if (key == KEY_LEFT) ctrlLeftSet(obj, std::atoi(val.c_str()));
			else if (key == KEY_RIGHT) ctrlRightSet(obj, std::atoi(val.c_str()));
		}
		return obj;
	});

	GameObjectsFactory::registerGameObject(cfg);
}

$execute {
	char const* frame = ObjectToolbox::sharedState()->intKeyToFrame(1347);
	auto cfg = GameObjectsFactory::createTriggerConfig(
		UNIQ_ID("physics-trigger"),
		frame,
		[](auto, auto, auto, auto) {
			// the physics is simulated in main.cpp
		},
		[](EditTriggersPopup* popup, EffectGameObject* trigger, CCArray* objects) {
			auto menu = popup->m_buttonMenu;
			if (!menu) return;

			std::vector<EffectGameObject*> targets{trigger};
			if (objects) {
				for (auto obj : CCArrayExt<GameObject*>(objects)) {
					if (obj && obj != trigger && obj->m_objectID == physObjectId()) {
						targets.push_back(static_cast<EffectGameObject*>(obj));
					}
				}
			}
			CTPhysUI::create(targets, menu, popup);

			auto add = [&](char const* hint, float x, std::string value, std::function<void(std::string const&)> cb) {
				auto in = TextInput::create(110.f, hint, "bigFont.fnt");
				in->setScale(0.55f);
				in->setPosition({x, 47.f});
				in->setFilter("0123456789.");
				in->setString(value);
				in->setCallback(cb);
				menu->addChild(in);
			};
			auto all = [targets](auto fn) { for (auto t : targets) fn(t); };

			add("Group", -85.f, std::to_string(trigger->m_targetGroupID), [all](std::string const& s) {
				int v = s.empty() ? 0 : std::atoi(s.c_str());
				all([v](EffectGameObject* t) { t->m_targetGroupID = v; });
			});
			add("Mass (g)", 0.f, std::to_string(physData(trigger).massGrams), [all](std::string const& s) {
				int v = s.empty() ? 0 : std::atoi(s.c_str());
				all([v](EffectGameObject* t) { physData(t).massGrams = v; });
			});
			add("Gravity", 85.f, std::to_string(physData(trigger).gravity), [all](std::string const& s) {
				float v = s.empty() ? 0.f : static_cast<float>(std::atof(s.c_str()));
				all([v](EffectGameObject* t) { physData(t).gravity = v; });
			});
		}
	);

	cfg->saveString([](std::string str, GameObject* obj, auto) {
		auto trig = static_cast<EffectGameObject*>(obj);
		auto& d = physData(obj);
		int flags = (d.physical ? 1 : 0) | (d.collision ? 2 : 0) | (d.fillGaps ? 4 : 0);
		bool semi = !str.empty() && str.back() == ';';
		if (semi) str.pop_back();
		if (!str.empty() && str.back() != ',') str += ",";
		str += fmt::format("{},{},{},{},{},{},{},{}", 9001, trig->m_targetGroupID, 9011, d.massGrams, 9012, d.gravity, 9013, flags);
		if (semi) str += ";";
		return str;
	});

	cfg->objectFromVector([](GameObject* obj, auto& p0, auto&, auto, auto) -> GameObject* {
		auto trig = static_cast<EffectGameObject*>(obj);
		auto& d = physData(obj);
		for (size_t i = 0; i + 1 < p0.size(); i += 2) {
			int key = std::atoi(std::string(p0[i]).c_str());
			std::string val = std::string(p0[i + 1]);
			if (key == 9001) trig->m_targetGroupID = std::atoi(val.c_str());
			else if (key == 9011) d.massGrams = std::atoi(val.c_str());
			else if (key == 9012) d.gravity = static_cast<float>(std::atof(val.c_str()));
			else if (key == 9013) {
				int f = std::atoi(val.c_str());
				d.physical = (f & 1) != 0;
				d.collision = (f & 2) != 0;
				d.fillGaps = (f & 4) != 0;
			}
		}
		return obj;
	});

	GameObjectsFactory::registerGameObject(cfg);
}
