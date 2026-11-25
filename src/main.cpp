#include <Geode/Geode.hpp>
#include <Geode/modify/UILayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <chrono>
#include <algorithm>

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
		CCPoint last;
		std::chrono::steady_clock::time_point start;
	};
	DragState g_drag;
}

// Touch start/end go through UILayer (this part is known to work).
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
					g_drag.touch = t;
					g_drag.obj = obj;
					g_drag.last = t->getLocation();
					g_drag.start = std::chrono::steady_clock::now();
					return true;
				}
			}
		}
		return UILayer::ccTouchBegan(t, e);
	}

	void ccTouchEnded(CCTouch* t, CCEvent* e) {
		if (g_buttonTouch && t == g_buttonTouch) { g_buttonTouch = nullptr; return; }
		if (g_drag.touch && t == g_drag.touch) { g_drag = DragState{}; return; }
		UILayer::ccTouchEnded(t, e);
	}

	void ccTouchCancelled(CCTouch* t, CCEvent* e) {
		if (g_buttonTouch && t == g_buttonTouch) { g_buttonTouch = nullptr; return; }
		if (g_drag.touch && t == g_drag.touch) { g_drag = DragState{}; return; }
		UILayer::ccTouchCancelled(t, e);
	}
};

// Dragging: the finger position is polled every frame instead of relying on touch-move events.
class $modify(CTPlayLayer, PlayLayer) {
	void setupHasCompleted() {
		g_drag = DragState{};
		g_buttonTouch = nullptr;
		PlayLayer::setupHasCompleted();
	}

	void postUpdate(float dt) {
		PlayLayer::postUpdate(dt);
		if (!g_drag.touch || !g_drag.obj || !g_drag.obj->getParent()) return;
		float hold = static_cast<float>(Mod::get()->getSettingValue<double>("hold-time"));
		float elapsed = std::chrono::duration<float>(std::chrono::steady_clock::now() - g_drag.start).count();
		auto cur = g_drag.touch->getLocation();
		if (elapsed >= hold) {
			// teleport the object to the finger and keep it there
			g_drag.obj->setPosition(g_drag.obj->getParent()->convertToNodeSpace(cur));
		}
		g_drag.last = cur;
	}
};
