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
			auto size = obj->getContentSize();
			float r = std::max(std::max(size.width, size.height), 30.f) * 0.5f * obj->getScale() + 12.f;
			auto p = obj->getParent()->convertToWorldSpace(obj->getPosition());
			float dx = p.x - world.x;
			float dy = p.y - world.y;
			if (dx * dx + dy * dy <= r * r) return obj;
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
			auto parent = g_drag.obj->getParent();
			auto a = parent->convertToNodeSpace(g_drag.last);
			auto b = parent->convertToNodeSpace(cur);
			g_drag.obj->setPosition(ccpAdd(g_drag.obj->getPosition(), ccpSub(b, a)));
		}
		g_drag.last = cur;
	}
};
