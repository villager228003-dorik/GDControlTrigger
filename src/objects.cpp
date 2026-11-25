// The real Ctrl trigger: a brand new object in the editor (own id).
// Uses the open-source "game objects factory" (lil2kki/geode-game-objects-factory),
// downloaded at build time and compiled into this mod.
#include <Geode/Geode.hpp>
#include <cstdlib>
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
	// keys used to save the trigger settings inside the level string
	constexpr int KEY_BUTTON = 9001; // button group
	constexpr int KEY_FIRE = 9002;   // group of triggers to fire (0 = drag mode)
	constexpr int KEY_HOLD = 9003;   // hold time in seconds
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
		str += fmt::format("{},{},{},{},{},{}", KEY_BUTTON, trig->m_targetGroupID, KEY_FIRE, trig->m_centerGroupID, KEY_HOLD, trig->m_duration);
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
		}
		return obj;
	});

	GameObjectsFactory::registerGameObject(cfg);
}
