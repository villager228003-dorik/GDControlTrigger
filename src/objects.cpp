// The real Ctrl trigger: a brand new object in the editor (own id).
// Uses the open-source "game objects factory" (lil2kki/geode-game-objects-factory),
// downloaded at build time and compiled into this mod.
#include <Geode/Geode.hpp>
#include <algorithm>
#include <cctype>
#include <cmath>
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
	std::map<GameObject*, float> g_repeats;
}

float ctrlRepeatGet(GameObject* obj) {
	auto it = g_repeats.find(obj);
	return it == g_repeats.end() ? 0.f : it->second;
}

void ctrlRepeatSet(GameObject* obj, float seconds) {
	g_repeats[obj] = seconds;
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

// Makes the "Edit Triggers" window bigger (wider and taller) and moves its title, the info
// button and the OK button, so there is room for our own controls. Returns false if the
// window background was not found (then the old compact layout is used).
static bool expandPopup(FLAlertLayer* popup, CCMenu* menu) {
	if (!popup || !menu) return false;
	constexpr float addW = 40.f, addH = 70.f, shift = addH / 2.f;

	// the biggest Scale9 sprite of the window is its brown background
	CCScale9Sprite* bg = nullptr;
	float best = 0.f;
	std::function<void(CCNode*)> find = [&](CCNode* n) {
		auto kids = n->getChildren();
		if (!kids) return;
		for (auto c : CCArrayExt<CCNode*>(kids)) {
			if (auto s = typeinfo_cast<CCScale9Sprite*>(c)) {
				auto sz = s->getContentSize();
				if (sz.width * sz.height > best) {
					best = sz.width * sz.height;
					bg = s;
				}
			}
			find(c);
		}
	};
	find(popup);
	if (!bg) return false;
	auto sz = bg->getContentSize();
	bg->setContentSize({sz.width + addW, sz.height + addH});

	// the title moves up
	std::function<void(CCNode*)> moveTitle = [&](CCNode* n) {
		auto kids = n->getChildren();
		if (!kids) return;
		for (auto c : CCArrayExt<CCNode*>(kids)) {
			if (auto l = typeinfo_cast<CCLabelBMFont*>(c)) {
				std::string t = l->getString();
				std::transform(t.begin(), t.end(), t.begin(), [](unsigned char ch) { return std::tolower(ch); });
				if (t.find("edit trigger") != std::string::npos) l->setPositionY(l->getPositionY() + shift);
			}
			moveTitle(c);
		}
	};
	moveTitle(popup);

	// info button (top left) moves up, OK button (bottom centre) moves down
	if (auto kids = menu->getChildren()) {
		for (auto c : CCArrayExt<CCNode*>(kids)) {
			if (!typeinfo_cast<CCMenuItemSpriteExtra*>(c)) continue;
			float px = c->getPositionX(), py = c->getPositionY();
			if (py > 55.f && px < -100.f) c->setPositionY(py + shift);
			else if (std::fabs(px) < 10.f && std::fabs(py) < 12.f) c->setPositionY(py - shift);
		}
	}
	return true;
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

	static CTModeUI* create(std::vector<EffectGameObject*> targets, int mode, bool lock, bool after, CCMenu* menu, FLAlertLayer* popup, bool big) {
		auto r = new CTModeUI();
		if (r && r->init()) {
			r->autorelease();
			menu->addChild(r);
			r->setup(std::move(targets), mode, lock, after, menu, popup, big);
			return r;
		}
		CC_SAFE_DELETE(r);
		return nullptr;
	}

	void setup(std::vector<EffectGameObject*> targets, int mode, bool lock, bool after, CCMenu* menu, FLAlertLayer* popup, bool big) {
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

		// positions of our controls: compact (old) layout or the expanded one
		float selX = x, selY = yTop, f1x = x + 14.f, f2x = x + 62.f, fy = yBottom - 3.f, fcap = yBottom + 9.f, ax = lx;
		if (big) {
			selX = -136.f; selY = 52.f;
			f1x = 0.f; f2x = 100.f; fy = 49.f; fcap = 64.f;
			lx = -150.f; ly = 18.f; ax = -30.f; ly2 = 18.f;
		}

		// mode selector:  <  Button  >
		auto arrow = [&](char const* t, SEL_MenuHandler cb, float px) {
			auto l = CCLabelBMFont::create(t, "bigFont.fnt");
			l->setScale(0.8f);
			auto it = CCMenuItemLabel::create(l, this, cb);
			it->setPosition({px, selY});
			menu->addChild(it);
		};
		arrow("<", menu_selector(CTModeUI::onPrev), selX);
		arrow(">", menu_selector(CTModeUI::onNext), selX + 72.f);
		m_modeLabel = CCLabelBMFont::create(modeName(mode), "bigFont.fnt");
		m_modeLabel->setScale(0.4f);
		m_modeLabel->setPosition({selX + 36.f, selY});
		menu->addChild(m_modeLabel);
		auto cap = CCLabelBMFont::create("Mode", "bigFont.fnt");
		cap->setScale(0.26f);
		cap->setColor({200, 200, 200});
		cap->setPosition({selX + 36.f, selY + 11.f});
		menu->addChild(cap);

		// custom movement buttons: group of the Left button and group of the Right button
		auto field = [&](char const* caption, float px, int value, std::function<void(std::string const&)> cb) {
			auto in = TextInput::create(110.f, caption, "bigFont.fnt");
			in->setScale(0.4f);
			in->setPosition({px, fy});
			in->setFilter("0123456789");
			in->setString(std::to_string(value));
			in->setCallback(cb);
			menu->addChild(in);
			auto l = CCLabelBMFont::create(caption, "bigFont.fnt");
			l->setScale(0.26f);
			l->setColor({200, 200, 200});
			l->setPosition({px, fcap});
			menu->addChild(l);
		};
		auto targets2 = m_targets;
		field("Left grp", f1x, ctrlLeftGet(m_targets.front()), [targets2](std::string const& v) {
			int g = v.empty() ? 0 : std::atoi(v.c_str());
			for (auto t : targets2) ctrlLeftSet(t, g);
		});
		field("Right grp", f2x, ctrlRightGet(m_targets.front()), [targets2](std::string const& v) {
			int g = v.empty() ? 0 : std::atoi(v.c_str());
			for (auto t : targets2) ctrlRightSet(t, g);
		});

		if (big) {
			auto in = TextInput::create(110.f, "Repeat", "bigFont.fnt");
			in->setScale(0.4f);
			in->setPosition({110.f, 15.f});
			in->setFilter("0123456789.");
			in->setString(fmt::format("{}", ctrlRepeatGet(m_targets.front())));
			in->setCallback([targets2](std::string const& v) {
				float r = v.empty() ? 0.f : static_cast<float>(std::atof(v.c_str()));
				for (auto t : targets2) ctrlRepeatSet(t, r);
			});
			menu->addChild(in);
			auto cap2 = CCLabelBMFont::create("Repeat sec (0 = off)", "bigFont.fnt");
			cap2->setScale(0.26f);
			cap2->setColor({200, 200, 200});
			cap2->setPosition({110.f, 29.f});
			menu->addChild(cap2);
		}

		m_togLock = CCMenuItemToggler::createWithStandardSprites(this, menu_selector(CTModeUI::onLock), 0.55f);
		m_togAfter = CCMenuItemToggler::createWithStandardSprites(this, menu_selector(CTModeUI::onAfter), 0.55f);
		m_togLock->setPosition({lx, ly});
		m_togAfter->setPosition({ax, ly2});
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
		addLabel("After\nDrop", ax, ly2);
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

	static CTPhysUI* create(std::vector<EffectGameObject*> targets, CCMenu* menu, FLAlertLayer* popup, bool big) {
		auto r = new CTPhysUI();
		if (r && r->init()) {
			r->autorelease();
			menu->addChild(r);
			r->setup(std::move(targets), menu, popup, big);
			return r;
		}
		CC_SAFE_DELETE(r);
		return nullptr;
	}

	void setup(std::vector<EffectGameObject*> targets, CCMenu* menu, FLAlertLayer* popup, bool big) {
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

		float cx = lx;
		if (big) {
			lx = -150.f; ly = 52.f; cx = -40.f; ly2 = 52.f;
			x = 70.f; yTop = 52.f;
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
		add(menu_selector(CTPhysUI::onCollision), d.collision, cx, ly2, "Collision");
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


// GD keeps only its own property keys while a level is loaded (a table indexed by key),
// so our values travel inside ONE existing text key: p0[31], as numbers joined with '_'.
namespace {
	constexpr size_t KEY_PAYLOAD = 31;

	std::vector<std::string> splitPayload(std::string const& s) {
		std::vector<std::string> out;
		std::string cur;
		for (char ch : s) {
			if (ch == '_') {
				out.push_back(cur);
				cur.clear();
			} else {
				cur += ch;
			}
		}
		out.push_back(cur);
		return out;
	}

	int toInt(std::vector<std::string> const& v, size_t i) {
		return i < v.size() && !v[i].empty() ? std::atoi(v[i].c_str()) : 0;
	}

	float toFloat(std::vector<std::string> const& v, size_t i, float def) {
		return i < v.size() && !v[i].empty() ? static_cast<float>(std::atof(v[i].c_str())) : def;
	}

	std::string appendPayload(std::string str, std::string const& payload) {
		bool semi = !str.empty() && str.back() == ';';
		if (semi) str.pop_back();
		if (!str.empty() && str.back() != ',') str += ",";
		str += fmt::format("{},{}", KEY_PAYLOAD, payload);
		if (semi) str += ";";
		return str;
	}
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
			bool big = expandPopup(popup, menu);

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
				CTModeUI::create(targets, ctrlModeGet(trigger), ctrlLockGet(trigger), ctrlAfterGet(trigger), menu, popup, big);
			}

			auto add = [&](char const* hint, float x, std::string value, std::function<void(std::string const&)> cb) {
				auto in = TextInput::create(110.f, hint, "bigFont.fnt");
				in->setScale(0.55f);
				in->setPosition({x, big ? 84.f : 47.f});
				in->setFilter("0123456789.");
				in->setString(value);
				in->setCallback(cb);
				menu->addChild(in);
				if (big) {
					auto l = CCLabelBMFont::create(hint, "bigFont.fnt");
					l->setScale(0.3f);
					l->setColor({200, 200, 200});
					l->setPosition({x, 97.f});
					menu->addChild(l);
				}
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
		// button group, fire group, hold, mode, lock, after drop, left group, right group
		auto payload = fmt::format("{}_{}_{}_{}_{}_{}_{}_{}_{}",
			trig->m_targetGroupID, trig->m_centerGroupID, trig->m_duration, ctrlModeGet(obj),
			ctrlLockGet(obj) ? 1 : 0, ctrlAfterGet(obj) ? 1 : 0, ctrlLeftGet(obj), ctrlRightGet(obj), ctrlRepeatGet(obj));
		str = appendPayload(std::move(str), payload);
		log::info("Ctrl save: {}", str);
		return str;
	});

	cfg->objectFromVector([](GameObject* obj, auto& p0, auto&, auto, auto) -> GameObject* {
		auto trig = static_cast<EffectGameObject*>(obj);
		if (p0.size() > KEY_PAYLOAD) {
			std::string raw = std::string(p0[KEY_PAYLOAD]);
			log::info("Ctrl load: payload '{}'", raw);
			if (!raw.empty()) {
				auto v = splitPayload(raw);
				trig->m_targetGroupID = toInt(v, 0);
				trig->m_centerGroupID = toInt(v, 1);
				trig->m_duration = toFloat(v, 2, 0.5f);
				ctrlModeSet(obj, toInt(v, 3));
				ctrlLockSet(obj, toInt(v, 4) != 0);
				ctrlAfterSet(obj, toInt(v, 5) != 0);
				ctrlLeftSet(obj, toInt(v, 6));
				ctrlRightSet(obj, toInt(v, 7));
				ctrlRepeatSet(obj, toFloat(v, 8, 0.f));
			}
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
			bool big = expandPopup(popup, menu);

			std::vector<EffectGameObject*> targets{trigger};
			if (objects) {
				for (auto obj : CCArrayExt<GameObject*>(objects)) {
					if (obj && obj != trigger && obj->m_objectID == physObjectId()) {
						targets.push_back(static_cast<EffectGameObject*>(obj));
					}
				}
			}
			CTPhysUI::create(targets, menu, popup, big);

			auto add = [&](char const* hint, float x, std::string value, std::function<void(std::string const&)> cb) {
				auto in = TextInput::create(110.f, hint, "bigFont.fnt");
				in->setScale(0.55f);
				in->setPosition({x, big ? 84.f : 47.f});
				in->setFilter("0123456789.");
				in->setString(value);
				in->setCallback(cb);
				menu->addChild(in);
				if (big) {
					auto l = CCLabelBMFont::create(hint, "bigFont.fnt");
					l->setScale(0.3f);
					l->setColor({200, 200, 200});
					l->setPosition({x, 97.f});
					menu->addChild(l);
				}
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
		// group, mass in grams, gravity, flags
		auto payload = fmt::format("{}_{}_{}_{}", trig->m_targetGroupID, d.massGrams, d.gravity, flags);
		return appendPayload(std::move(str), payload);
	});

	cfg->objectFromVector([](GameObject* obj, auto& p0, auto&, auto, auto) -> GameObject* {
		auto trig = static_cast<EffectGameObject*>(obj);
		auto& d = physData(obj);
		if (p0.size() > KEY_PAYLOAD) {
			std::string raw = std::string(p0[KEY_PAYLOAD]);
			if (!raw.empty()) {
				auto v = splitPayload(raw);
				trig->m_targetGroupID = toInt(v, 0);
				d.massGrams = toInt(v, 1);
				d.gravity = toFloat(v, 2, 9.8f);
				int f = toInt(v, 3);
				d.physical = (f & 1) != 0;
				d.collision = (f & 2) != 0;
				d.fillGaps = (f & 4) != 0;
			}
		}
		return obj;
	});

	GameObjectsFactory::registerGameObject(cfg);
}
