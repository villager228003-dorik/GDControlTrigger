#include <Geode/Geode.hpp>
#include <Geode/modify/UILayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/CCTouchDispatcher.hpp>
#include <vector>
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
}

// ---------- Button mode (works through UILayer touches) ----------
class $modify(CTUILayer, UILayer) {
	bool ccTouchBegan(CCTouch* t, CCEvent* e) {
		auto layer = GJBaseGameLayer::get();
		auto mod = Mod::get();
		if (layer && mod->getSettingValue<std::string>("mode") == "button") {
			int group = static_cast<int>(mod->getSettingValue<int64_t>("button-group"));
			if (hitObject(layer, group, t->getLocation())) {
				g_buttonTouch = t;
				fireGroup(layer, static_cast<int>(mod->getSettingValue<int64_t>("target-group")));
				return true;
			}
		}
		return UILayer::ccTouchBegan(t, e);
	}

	void ccTouchEnded(CCTouch* t, CCEvent* e) {
		if (g_buttonTouch && t == g_buttonTouch) {
			g_buttonTouch = nullptr;
			return;
		}
		UILayer::ccTouchEnded(t, e);
	}

	void ccTouchCancelled(CCTouch* t, CCEvent* e) {
		if (g_buttonTouch && t == g_buttonTouch) {
			g_buttonTouch = nullptr;
			return;
		}
		UILayer::ccTouchCancelled(t, e);
	}
};

// ---------- Drag mode (works at the touch dispatcher level) ----------
namespace {
	struct DragState {
		CCTouch* touch = nullptr;
		GameObject* obj = nullptr;
		CCPoint last;
		std::chrono::steady_clock::time_point start;
	};
	DragState g_drag;
}

class $modify(CTDispatcher, CCTouchDispatcher) {
	void touches(CCSet* set, CCEvent* ev, unsigned int type) {
		auto pl = PlayLayer::get();
		if (!pl) g_drag = DragState{};
		auto mod = Mod::get();
		if (!pl || !set || mod->getSettingValue<std::string>("mode") != "drag") {
			CCTouchDispatcher::touches(set, ev, type);
			return;
		}

		std::vector<CCTouch*> list;
		for (auto it = set->begin(); it != set->end(); ++it) {
			list.push_back(static_cast<CCTouch*>(*it));
		}

		for (auto t : list) {
			if (type == 0) {
				if (g_drag.touch) continue;
				int group = static_cast<int>(mod->getSettingValue<int64_t>("button-group"));
				if (auto obj = hitObject(pl, group, t->getLocation())) {
					g_drag.touch = t;
					g_drag.obj = obj;
					g_drag.last = t->getLocation();
					g_drag.start = std::chrono::steady_clock::now();
					set->removeObject(t);
				}
			} else if (t == g_drag.touch) {
				if (type == 1) {
					if (g_drag.obj && g_drag.obj->getParent()) {
						float hold = static_cast<float>(mod->getSettingValue<double>("hold-time"));
						float elapsed = std::chrono::duration<float>(std::chrono::steady_clock::now() - g_drag.start).count();
						auto cur = t->getLocation();
						if (elapsed >= hold) {
							auto parent = g_drag.obj->getParent();
							auto a = parent->convertToNodeSpace(g_drag.last);
							auto b = parent->convertToNodeSpace(cur);
							g_drag.obj->setPosition(ccpAdd(g_drag.obj->getPosition(), ccpSub(b, a)));
						}
						g_drag.last = cur;
					}
				} else {
					g_drag = DragState{};
				}
				set->removeObject(t);
			}
		}

		if (set->count() == 0) return;
		CCTouchDispatcher::touches(set, ev, type);
	}
};
