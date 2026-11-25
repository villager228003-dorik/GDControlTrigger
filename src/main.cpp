#include <Geode/Geode.hpp>
#include <Geode/modify/UILayer.hpp>
#include <chrono>
#include <algorithm>

using namespace geode::prelude;

namespace {
	struct TouchState {
		CCTouch* touch = nullptr;
		GameObject* dragObj = nullptr;
		bool buttonMode = false;
		int targetGroup = 0;
		CCPoint last;
		std::chrono::steady_clock::time_point start;
	};
	TouchState g;

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

	void releaseTouch() {
		g = TouchState{};
	}
}

class $modify(CTUILayer, UILayer) {
	bool ccTouchBegan(CCTouch* t, CCEvent* e) {
		auto layer = GJBaseGameLayer::get();
		if (layer) {
			auto mod = Mod::get();
			int group = static_cast<int>(mod->getSettingValue<int64_t>("button-group"));
			if (auto obj = hitObject(layer, group, t->getLocation())) {
				g = TouchState{};
				g.touch = t;
				g.last = t->getLocation();
				g.start = std::chrono::steady_clock::now();
				if (mod->getSettingValue<std::string>("mode") == "button") {
					g.buttonMode = true;
					g.targetGroup = static_cast<int>(mod->getSettingValue<int64_t>("target-group"));
					fireGroup(layer, g.targetGroup);
				} else {
					g.dragObj = obj;
				}
				return true;
			}
		}
		return UILayer::ccTouchBegan(t, e);
	}

	void ccTouchMoved(CCTouch* t, CCEvent* e) {
		if (g.touch && t == g.touch) {
			if (g.dragObj && g.dragObj->getParent()) {
				float hold = static_cast<float>(Mod::get()->getSettingValue<double>("hold-time"));
				float elapsed = std::chrono::duration<float>(std::chrono::steady_clock::now() - g.start).count();
				auto cur = t->getLocation();
				if (elapsed >= hold) {
					auto parent = g.dragObj->getParent();
					auto a = parent->convertToNodeSpace(g.last);
					auto b = parent->convertToNodeSpace(cur);
					g.dragObj->setPosition(ccpAdd(g.dragObj->getPosition(), ccpSub(b, a)));
				}
				g.last = cur;
			}
			return;
		}
		UILayer::ccTouchMoved(t, e);
	}

	void ccTouchEnded(CCTouch* t, CCEvent* e) {
		if (g.touch && t == g.touch) {
			releaseTouch();
			return;
		}
		UILayer::ccTouchEnded(t, e);
	}

	void ccTouchCancelled(CCTouch* t, CCEvent* e) {
		if (g.touch && t == g.touch) {
			releaseTouch();
			return;
		}
		UILayer::ccTouchCancelled(t, e);
	}
};
