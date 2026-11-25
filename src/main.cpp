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

// ---------- Drag mode (own touch layer with high priority) ----------
class CTDragLayer : public CCLayer {
public:
	CCTouch* m_touch = nullptr;
	GameObject* m_obj = nullptr;
	CCPoint m_last;
	std::chrono::steady_clock::time_point m_start;

	static CTDragLayer* create() {
		auto r = new CTDragLayer();
		if (r && r->init()) {
			r->autorelease();
			return r;
		}
		CC_SAFE_DELETE(r);
		return nullptr;
	}

	bool init() override {
		if (!CCLayer::init()) return false;
		this->setTouchEnabled(true);
		return true;
	}

	void registerWithTouchDispatcher() override {
		CCTouchDispatcher::get()->addTargetedDelegate(this, -700, true);
	}

	bool ccTouchBegan(CCTouch* t, CCEvent*) override {
		auto pl = PlayLayer::get();
		auto mod = Mod::get();
		if (!pl || mod->getSettingValue<std::string>("mode") != "drag") return false;
		int group = static_cast<int>(mod->getSettingValue<int64_t>("button-group"));
		auto obj = hitObject(pl, group, t->getLocation());
		if (!obj) return false;
		m_touch = t;
		m_obj = obj;
		m_last = t->getLocation();
		m_start = std::chrono::steady_clock::now();
		return true;
	}

	void ccTouchMoved(CCTouch* t, CCEvent*) override {
		if (t != m_touch || !m_obj || !m_obj->getParent()) return;
		float hold = static_cast<float>(Mod::get()->getSettingValue<double>("hold-time"));
		float elapsed = std::chrono::duration<float>(std::chrono::steady_clock::now() - m_start).count();
		auto cur = t->getLocation();
		if (elapsed >= hold) {
			auto parent = m_obj->getParent();
			auto a = parent->convertToNodeSpace(m_last);
			auto b = parent->convertToNodeSpace(cur);
			m_obj->setPosition(ccpAdd(m_obj->getPosition(), ccpSub(b, a)));
		}
		m_last = cur;
	}

	void ccTouchEnded(CCTouch* t, CCEvent*) override {
		if (t == m_touch) { m_touch = nullptr; m_obj = nullptr; }
	}

	void ccTouchCancelled(CCTouch* t, CCEvent*) override {
		if (t == m_touch) { m_touch = nullptr; m_obj = nullptr; }
	}
};

class $modify(CTPlayLayer, PlayLayer) {
	void setupHasCompleted() {
		PlayLayer::setupHasCompleted();
		if (auto l = CTDragLayer::create()) this->addChild(l, 10000);
	}
};
