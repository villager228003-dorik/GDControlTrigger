// The real Ctrl trigger: a brand new object in the editor (own id).
// Uses the open-source "game objects factory" (lil2kki/geode-game-objects-factory),
// downloaded at build time and compiled into this mod.
#include <Geode/Geode.hpp>
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

// Two buttons in the trigger window: Button mode / Drag mode (only one can be active).
class CTModeUI : public CCNode {
public:
	std::vector<EffectGameObject*> m_targets;
	CCLabelBMFont* m_btn = nullptr;
	CCLabelBMFont* m_drag = nullptr;

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
		m_btn = CCLabelBMFont::create("Button mode", "bigFont.fnt");
		m_drag = CCLabelBMFont::create("Drag mode", "bigFont.fnt");
		m_btn->setScale(0.35f);
		m_drag->setScale(0.35f);
		auto b = CCMenuItemLabel::create(m_btn, this, menu_selector(CTModeUI::onButton));
		auto d = CCMenuItemLabel::create(m_drag, this, menu_selector(CTModeUI::onDrag));
		b->setPosition({96.f, 25.f});
		d->setPosition({96.f, 7.f});
		menu->addChild(b);
		menu->addChild(d);
		refresh(mode);
	}

	void refresh(int mode) {
		m_btn->setColor(mode == 0 ? ccColor3B{80, 255, 80} : ccColor3B{130, 130, 130});
		m_drag->setColor(mode == 1 ? ccColor3B{80, 255, 80} : ccColor3B{130, 130, 130});
	}

	void apply(int mode) {
		for (auto t : m_targets) ctrlModeSet(t, mode);
		refresh(mode);
	}

	void onButton(CCObject*) { apply(0); }
	void onDrag(CCObject*) { apply(1); }
};

namespace {
	// keys used to save the trigger settings inside the level string
	constexpr int KEY_BUTTON = 9001; // button group
	constexpr int KEY_FIRE = 9002;   // group of triggers to fire (0 = drag mode)
	constexpr int KEY_HOLD = 9003;   // hold time in seconds
	constexpr int KEY_MODE = 9004;   // 0 = button mode, 1 = drag mode
}

$execute {
	auto cfg = GameObjectsFactory::createTriggerConfig(
		UNIQ_ID("ctrl-trigger"),
		"d_ball_06_001.png",
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
