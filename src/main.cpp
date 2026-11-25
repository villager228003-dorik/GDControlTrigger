#include <Geode/Geode.hpp>
#include <Geode/modify/UILayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <chrono>
#include <algorithm>
#include <cstdio>

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
		CCPoint cur;
		int moves = 0;
		std::chrono::steady_clock::time_point start;
	};
	DragState g_drag;
	CCLabelBMFont* g_dbg = nullptr;
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
					g_drag.cur = g_drag.last;
					g_drag.moves = 0;
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
		g_dbg = CCLabelBMFont::create("Ctrl debug", "bigFont.fnt");
		if (g_dbg) {
			g_dbg->setScale(0.35f);
			g_dbg->setAnchorPoint({0.f, 1.f});
			auto ws = CCDirector::sharedDirector()->getWinSize();
			g_dbg->setPosition({6.f, ws.height - 6.f});
			this->addChild(g_dbg, 10000);
		}
	}

	void postUpdate(float dt) {
		PlayLayer::postUpdate(dt);
		if (!g_drag.touch || !g_drag.obj || !g_drag.obj->getParent()) return;
		float hold = static_cast<float>(Mod::get()->getSettingValue<double>("hold-time"));
		float elapsed = std::chrono::duration<float>(std::chrono::steady_clock::now() - g_drag.start).count();
		auto poll = g_drag.touch->getLocation();
		auto loc = g_drag.moves > 0 ? g_drag.cur : poll;
		if (elapsed >= hold) {
			g_drag.obj->setPosition(g_drag.obj->getParent()->convertToNodeSpace(loc));
		}
		g_drag.last = loc;
		if (g_dbg) {
			auto op = g_drag.obj->getPosition();
			char buf[200];
			std::snprintf(buf, sizeof(buf), "e=%.2f mv=%d poll=%.0f,%.0f use=%.0f,%.0f obj=%.0f,%.0f",
				elapsed, g_drag.moves, poll.x, poll.y, loc.x, loc.y, op.x, op.y);
			g_dbg->setString(buf);
		}
	}
};
