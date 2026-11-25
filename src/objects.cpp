// The real Ctrl trigger: a brand new object in the editor (own id).
// Uses the open-source "game objects factory" (lil2kki/geode-game-objects-factory),
// downloaded at build time and compiled into this mod.
#include <Geode/Geode.hpp>
#include <algorithm>
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

namespace {
	std::map<GameObject*, int> g_modes;
}

int ctrlModeGet(GameObject* obj) {
	auto it = g_modes.find(obj);
	return it == g_modes.end() ? 0 : it->second;
}

void ctrlModeSet(GameObject* obj, int mode) {
	g_modes[obj] = mode;
}

// Two checkboxes in the trigger window, styled like the ones in the Move trigger window:
// "Button Mode" and "Drag Mode" (only one can be on).
class CTModeUI : public CCNode {
public:
	std::vector<EffectGameObject*> m_targets;
	CCMenuItemToggler* m_togBtn = nullptr;
	CCMenuItemToggler* m_togDrag = nullptr;

	static CTModeUI* create(std::vector<EffectGameObject*> targets, int mode, CCMenu* menu) {
		auto r = new CTModeUI();
		if (r && r->init()) {
			r->autorelease();
			menu->addChild(r);
			r->setup(std::move(targets), mode, menu);
			return r;
		}
		CC_SAFE_DELETE(r);
		return nullptr;
	}

	void setup(std::vector<EffectGameObject*> targets, int mode, CCMenu* menu) {
		m_targets = std::move(targets);

		// place the checkboxes on the same rows as the existing Touch / Spawn trigger checkboxes
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
		if (existing.size() >= 2) {
			x = existing[0]->getPositionX() + 222.f;
			yTop = existing[0]->getPositionY();
			yBottom = existing[1]->getPositionY();
		}

		m_togBtn = CCMenuItemToggler::createWithStandardSprites(this, menu_selector(CTModeUI::onButton), 0.55f);
		m_togDrag = CCMenuItemToggler::createWithStandardSprites(this, menu_selector(CTModeUI::onDrag), 0.55f);
		m_togBtn->setPosition({x, yTop});
		m_togDrag->setPosition({x, yBottom});
		m_togBtn->toggle(mode == 0);
		m_togDrag->toggle(mode == 1);
		menu->addChild(m_togBtn);
		menu->addChild(m_togDrag);

		auto addLabel = [&](char const* text, float y) {
			auto l = CCLabelBMFont::create(text, "bigFont.fnt");
			l->setScale(0.4f);
			l->setAnchorPoint({0.f, 0.5f});
			l->setPosition({x + 14.f, y});
			menu->addChild(l);
		};
		addLabel("Button\nMode", yTop);
		addLabel("Drag\nMode", yBottom);
	}

	void apply(int mode) {
		for (auto t : m_targets) ctrlModeSet(t, mode);
	}

	// A toggler flips right AFTER its callback, so we set the opposite state here.
	void onButton(CCObject* sender) {
		auto t = static_cast<CCMenuItemToggler*>(sender);
		if (t->isToggled()) t->toggle(false);
		m_togDrag->toggle(false);
		apply(0);
	}

	void onDrag(CCObject* sender) {
		auto t = static_cast<CCMenuItemToggler*>(sender);
		if (t->isToggled()) t->toggle(false);
		m_togBtn->toggle(false);
		apply(1);
	}
};

namespace {
	// keys used to save the trigger settings inside the level string
	constexpr int KEY_BUTTON = 9001; // button group
	constexpr int KEY_FIRE = 9002;   // group of triggers to fire (0 = drag mode)
	constexpr int KEY_HOLD = 9003;   // hold time in seconds
	constexpr int KEY_MODE = 9004;   // 0 = button mode, 1 = drag mode
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
				CTModeUI::create(targets, ctrlModeGet(trigger), menu);
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
		str += fmt::format("{},{},{},{},{},{},{},{}", KEY_BUTTON, trig->m_targetGroupID, KEY_FIRE, trig->m_centerGroupID, KEY_HOLD, trig->m_duration, KEY_MODE, ctrlModeGet(obj));
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
		}
		return obj;
	});

	GameObjectsFactory::registerGameObject(cfg);
}
