#include <Geode/Geode.hpp>
#include <Geode/modify/UILayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/EditorUI.hpp>
#include <chrono>
#include <cmath>
#include <map>
#include <set>
#include <vector>
#include <string>
#include "ctrl.hpp"

using namespace geode::prelude;

namespace {
	struct Cfg {
		int group = 0;
		int second = 0;
		float hold = 0.3f;
	};

	std::vector<EffectGameObject*> ctrlTriggers(GJBaseGameLayer* layer) {
		std::vector<EffectGameObject*> out;
		if (!layer || !layer->m_objects) return out;
		for (auto obj : CCArrayExt<GameObject*>(layer->m_objects)) {
			if (obj && obj->m_objectID == ctrlObjectId()) {
				out.push_back(static_cast<EffectGameObject*>(obj));
			}
		}
		return out;
	}

	std::vector<Cfg> getCfgs(GJBaseGameLayer* layer) {
		std::vector<Cfg> v;
		for (auto t : ctrlTriggers(layer)) {
			if (t->m_targetGroupID <= 0) continue;
			int mode = ctrlModeGet(t);
			Cfg c;
			c.group = t->m_targetGroupID;
			c.hold = t->m_duration;
			c.second = mode == 0 ? t->m_centerGroupID : 0;
			if (mode == 0 && c.second <= 0) continue;
			v.push_back(c);
		}
		if (v.empty()) {
			// no Ctrl triggers in the level: use the mod settings
			auto mod = Mod::get();
			Cfg c;
			c.group = static_cast<int>(mod->getSettingValue<int64_t>("button-group"));
			bool button = mod->getSettingValue<std::string>("mode") == "button";
			c.second = button ? static_cast<int>(mod->getSettingValue<int64_t>("target-group")) : 0;
			c.hold = static_cast<float>(mod->getSettingValue<double>("hold-time"));
			v.push_back(c);
		}
		return v;
	}

	GameObject* hitObject(GJBaseGameLayer* layer, int group, CCPoint world) {
		auto arr = layer->getGroup(group);
		if (!arr) return nullptr;
		for (auto obj : CCArrayExt<GameObject*>(arr)) {
			if (!obj || !obj->getParent()) continue;
			auto local = obj->getParent()->convertToNodeSpace(world);
			if (obj->boundingBox().containsPoint(local)) return obj;
		}
		return nullptr;
	}


	// Moves an object the way the game does, so collision follows it (best effort:
	// every call is only used if it exists in the bindings of this game version).
	template <class L, class O>
	void moveSolid(L* layer, O* obj, CCPoint newPos) {
		auto old = obj->getPosition();
		double dx = newPos.x - old.x;
		double dy = newPos.y - old.y;
		bool moved = false;
		if constexpr (requires { layer->moveObject(obj, dx, dy, false); }) {
			layer->moveObject(obj, dx, dy, false);
			moved = true;
		}
		if (!moved) obj->setPosition(newPos);
		if constexpr (requires { obj->m_isObjectRectDirty = true; }) obj->m_isObjectRectDirty = true;
		if constexpr (requires { obj->m_isOrientedRectDirty = true; }) obj->m_isOrientedRectDirty = true;
		if constexpr (requires { obj->updateOrientedBox(); }) obj->updateOrientedBox();
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
		float hold = 0.3f;
		std::chrono::steady_clock::time_point start;
	};
	DragState g_drag;

	std::map<GameObject*, CCPoint> g_dropped;
	float g_lastPlayerX = 0.f;

	void finishDrag() {
		if (g_drag.obj && g_drag.moved) g_dropped[g_drag.obj] = g_drag.obj->getPosition();
		g_drag = DragState{};
	}
}

// ---------- White circle with a black outline and the group number (editor only) ----------
class CTBadge : public CCNode {
public:
	CCDrawNode* m_circle = nullptr;
	CCLabelBMFont* m_label = nullptr;
	CCLabelBMFont* m_caption = nullptr;
	int m_last = -999;
	int m_sel = -1;

	static CTBadge* create() {
		auto r = new CTBadge();
		if (r && r->init()) {
			r->autorelease();
			return r;
		}
		CC_SAFE_DELETE(r);
		return nullptr;
	}

	bool init() override {
		if (!CCNode::init()) return false;
		m_circle = CCDrawNode::create();
		this->addChild(m_circle);
		m_label = CCLabelBMFont::create("0", "bigFont.fnt");
		this->addChild(m_label);
		m_caption = CCLabelBMFont::create("CTRL", "bigFont.fnt");
		m_caption->setScale(0.5f);
		m_caption->setPosition({0.f, 26.f});
		this->addChild(m_caption);
		setSelected(false);
		return true;
	}

	// normal: white circle, black outline, black number. selected: inverted, green caption.
	void setSelected(bool sel) {
		int s = sel ? 1 : 0;
		if (s == m_sel) return;
		m_sel = s;
		CCPoint pts[32];
		for (int i = 0; i < 32; i++) {
			float a = static_cast<float>(i) * (2.f * 3.14159265f / 32.f);
			pts[i] = ccp(std::cos(a) * 15.f, std::sin(a) * 15.f);
		}
		m_circle->clear();
		if (sel) {
			m_circle->drawPolygon(pts, 32, ccc4f(0.f, 0.f, 0.f, 1.f), 2.f, ccc4f(1.f, 1.f, 1.f, 1.f));
		} else {
			m_circle->drawPolygon(pts, 32, ccc4f(1.f, 1.f, 1.f, 1.f), 2.f, ccc4f(0.f, 0.f, 0.f, 1.f));
		}
		m_label->setColor(sel ? ccColor3B{255, 255, 255} : ccColor3B{0, 0, 0});
		m_caption->setColor(sel ? ccColor3B{80, 255, 80} : ccColor3B{255, 255, 255});
	}

	void setGroup(int g) {
		if (g == m_last) return;
		m_last = g;
		m_label->setString(std::to_string(g).c_str());
		m_label->setScale(g < 100 ? 0.5f : (g < 1000 ? 0.38f : 0.3f));
	}
};

namespace {
	std::map<GameObject*, CTBadge*> g_badges;

	void syncBadges(LevelEditorLayer* layer) {
		if (!layer->m_objectLayer) return;
		std::set<GameObject*> seen;
		for (auto t : ctrlTriggers(layer)) {
			if (!t->getParent()) continue;
			seen.insert(t);
			t->setOpacity(0);
			auto& b = g_badges[t];
			if (!b) {
				b = CTBadge::create();
				if (!b) continue;
				layer->m_objectLayer->addChild(b, 5000);
			}
			auto world = t->getParent()->convertToWorldSpace(t->getPosition());
			b->setPosition(layer->m_objectLayer->convertToNodeSpace(world));
			b->setGroup(t->m_targetGroupID);
			b->setSelected(t->m_isSelected);
		}
		for (auto it = g_badges.begin(); it != g_badges.end();) {
			if (!seen.count(it->first)) {
				if (it->second) it->second->removeFromParent();
				it = g_badges.erase(it);
			} else {
				++it;
			}
		}
	}
}

// A tiny node that runs every frame while the editor is open and keeps the circles in sync.
class CTSyncNode : public CCNode {
public:
	static CTSyncNode* create() {
		auto r = new CTSyncNode();
		if (r && r->init()) {
			r->autorelease();
			return r;
		}
		CC_SAFE_DELETE(r);
		return nullptr;
	}

	bool init() override {
		if (!CCNode::init()) return false;
		this->scheduleUpdate();
		return true;
	}

	void update(float) override {
		if (auto layer = LevelEditorLayer::get()) syncBadges(layer);
	}
};

class $modify(CTEditorUI, EditorUI) {
	bool init(LevelEditorLayer* editor) {
		if (!EditorUI::init(editor)) return false;
		if (auto n = CTSyncNode::create()) this->addChild(n);
		return true;
	}
};

// ---------- Touch start/move/end ----------
class $modify(CTUILayer, UILayer) {
	bool ccTouchBegan(CCTouch* t, CCEvent* e) {
		auto layer = GJBaseGameLayer::get();
		if (layer) {
			for (auto const& c : getCfgs(layer)) {
				auto obj = hitObject(layer, c.group, t->getLocation());
				if (!obj) continue;
				if (c.second > 0) {
					g_buttonTouch = t;
					fireGroup(layer, c.second);
					return true;
				}
				g_drag = DragState{};
				g_drag.touch = t;
				g_drag.obj = obj;
				g_drag.cur = t->getLocation();
				g_drag.hold = c.hold;
				g_drag.start = std::chrono::steady_clock::now();
				return true;
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

		if (m_player1) {
			float x = m_player1->getPositionX();
			if (x < g_lastPlayerX - 5.f) g_dropped.clear();
			g_lastPlayerX = x;
		}

		for (auto& [obj, pos] : g_dropped) {
			if (obj && obj->getParent()) {
				auto cur = obj->getPosition();
				if (std::fabs(cur.x - pos.x) > 0.01f || std::fabs(cur.y - pos.y) > 0.01f) {
					moveSolid(static_cast<GJBaseGameLayer*>(this), obj, pos);
				}
			}
		}

		if (g_drag.touch && g_drag.obj && g_drag.obj->getParent()) {
			float elapsed = std::chrono::duration<float>(std::chrono::steady_clock::now() - g_drag.start).count();
			auto loc = g_drag.moves > 0 ? g_drag.cur : g_drag.touch->getLocation();
			if (elapsed >= g_drag.hold) {
				moveSolid(static_cast<GJBaseGameLayer*>(this), g_drag.obj, g_drag.obj->getParent()->convertToNodeSpace(loc));
				g_drag.moved = true;
			}
		}
	}
};
