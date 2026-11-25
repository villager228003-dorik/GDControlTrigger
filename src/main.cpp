#include <Geode/Geode.hpp>
#include <Geode/modify/UILayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <chrono>
#include <map>

using namespace geode::prelude;

namespace {
	GameObject* hitObject(GJBaseGameLayer* layer, int group, CCPoint world) {
		auto arr = layer->getGroup(group);
		if (!arr) return nullptr;
		for (auto obj : CCArrayExt<GameObject*>(arr)) {
			if (!obj || !obj->getParent()) continue;
			auto parent = obj->getParent();
			auto local = parent->convertToNodeSpace(world);
			if (obj->boundingBox().containsPoint(local)) return obj;
		}
		return nullptr;
	}

	void fireGroup(GJBaseGameLayer* layer, int group) {
		auto arr = layer->getGroup(group);
		if (!arr) return;
		for (auto obj : CCArrayExt<GameObject*>(arr)) {
			if (auto trig = typeinfo_cast<EffectGameObject*>(obj)) {
				trig->triggerObject(layer, 0, nullptr);
			}
		}
	}

	CCTouch* g_buttonTouch = nullptr;

	struct DragState {
		CCTouch* touch = nullptr;
		GameObject* obj = nullptr;
		CCPoint cur;
		int moves = 0;
		bool moved = false;
		std::chrono::steady_clock::time_point start;
	};
	DragState g_drag;

	// objects that were dropped by the finger: they are kept at the dropped position
	std::map<GameObject*, CCPoint> g_dropped;
	float g_lastPlayerX = 0.f;

	void finishDrag() {
		if (g_drag.obj && g_drag.moved) {
			g_dropped[g_drag.obj] = g_drag.obj->getPosition();
		}
		g_drag = DragState{};
	}
}

// Touch start/move/end go through UILayer.
class $modify(CTUILayer, UILayer) {
	bool ccTouchBegan(CCTouch* t, CCEvent* e) {
		auto layer = GJBaseGameLayer::get();
		auto mod = Mod::get();
		if (layer) {
			auto mode = mod->getSettingValue<std::string>("mode");
			int group = static_cast<int>(mod->getSettingValue<int64_t>("button-group"));
			if (auto obj = hitObject(layer, group, t->getLocation())) {
				if (mode == "button") {
					g_buttonTouch = t;
					fireGroup(layer, static_cast<int>(mod->getSettingValue<int64_t>("target-group")));
					return true;
				}
				if (mode == "drag") {
					g_drag = DragState{};
					g_drag.touch = t;
					g_drag.obj = obj;
					g_drag.cur = t->getLocation();
					g_drag.start = std::chrono::steady_clock::now();
					return true;
				}
			}
		}
		return UILayer::ccTouchBegan(t, e);
	}

	void ccTouchMoved(CCTouch* t, CCEvent* e) {
		if (g_drag.touch && t == g_drag.touch) {
			g_drag.cur = t->getLocation();
			g_drag.moves++;
			return;
		}
		UILayer::ccTouchMoved(t, e);
	}

	void ccTouchEnded(CCTouch* t, CCEvent* e) {
		if (g_buttonTouch && t == g_buttonTouch) { g_buttonTouch = nullptr; return; }
		if (g_drag.touch && t == g_drag.touch) { finishDrag(); return; }
		UILayer::ccTouchEnded(t, e);
	}

	void ccTouchCancelled(CCTouch* t, CCEvent* e) {
		if (g_buttonTouch && t == g_buttonTouch) { g_buttonTouch = nullptr; return; }
		if (g_drag.touch && t == g_drag.touch) { finishDrag(); return; }
		UILayer::ccTouchCancelled(t, e);
	}
};

// New level: forget everything.
class $modify(CTPlayLayer, PlayLayer) {
	void setupHasCompleted() {
		g_drag = DragState{};
		g_dropped.clear();
		g_buttonTouch = nullptr;
		g_lastPlayerX = 0.f;
		PlayLayer::setupHasCompleted();
	}
};

// Runs in the real game and in the editor playtest.
class $modify(CTBaseLayer, GJBaseGameLayer) {
	void update(float dt) {
		GJBaseGameLayer::update(dt);

		// restart detected (player went back): dropped objects return to the start
		if (m_player1) {
			float x = m_player1->getPositionX();
			if (x < g_lastPlayerX - 5.f) g_dropped.clear();
			g_lastPlayerX = x;
		}

		// keep dropped objects where the finger left them
		for (auto& [obj, pos] : g_dropped) {
			if (obj && obj->getParent()) obj->setPosition(pos);
		}

		// object under the finger
		if (g_drag.touch && g_drag.obj && g_drag.obj->getParent()) {
			float hold = static_cast<float>(Mod::get()->getSettingValue<double>("hold-time"));
			float elapsed = std::chrono::duration<float>(std::chrono::steady_clock::now() - g_drag.start).count();
			auto loc = g_drag.moves > 0 ? g_drag.cur : g_drag.touch->getLocation();
			if (elapsed >= hold) {
				g_drag.obj->setPosition(g_drag.obj->getParent()->convertToNodeSpace(loc));
				g_drag.moved = true;
			}
		}
	}
};
