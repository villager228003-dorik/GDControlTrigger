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
	// true if the object belongs to a group that a Toggle trigger switched off
	// (every field name is only used if it exists in this version of the game)
	template <class O>
	bool objDisabled(O* o) {
		if constexpr (requires { o->m_groupDisabled; }) {
			if (o->m_groupDisabled) return true;
		}
		if constexpr (requires { o->m_isGroupDisabled; }) {
			if (o->m_isGroupDisabled) return true;
		}
		return false;
	}

	struct Cfg {
		int group = 0;
		int second = 0;
		float hold = 0.3f;
		bool afterDrop = false;
		int moveBtn = 0; // 2 = custom Left button, 3 = custom Right button
	};

	std::vector<EffectGameObject*> ctrlTriggers(GJBaseGameLayer* layer) {
		std::vector<EffectGameObject*> out;
		if (!layer || !layer->m_objects) return out;
		for (auto obj : CCArrayExt<GameObject*>(layer->m_objects)) {
			if (obj && obj->m_objectID == ctrlObjectId() && !objDisabled(obj)) {
				out.push_back(static_cast<EffectGameObject*>(obj));
			}
		}
		return out;
	}

	std::vector<Cfg> getCfgs(GJBaseGameLayer* layer) {
		std::vector<Cfg> v;
		for (auto t : ctrlTriggers(layer)) {
			// the main behavior: Button or Drag on the Button group
			if (t->m_targetGroupID > 0) {
				int mode = ctrlModeGet(t) == 1 ? 1 : 0;
				Cfg c;
				c.group = t->m_targetGroupID;
				c.hold = t->m_duration;
				c.afterDrop = ctrlAfterGet(t);
				c.second = mode == 0 ? t->m_centerGroupID : 0;
				if (!(mode == 0 && c.second <= 0)) v.push_back(c);
			}
			// custom Left / Right movement buttons (independent of the mode)
			if (int lg = ctrlLeftGet(t); lg > 0) {
				Cfg c;
				c.group = lg;
				c.moveBtn = 2;
				v.push_back(c);
			}
			if (int rg = ctrlRightGet(t); rg > 0) {
				Cfg c;
				c.group = rg;
				c.moveBtn = 3;
				v.push_back(c);
			}
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
			if (!obj || !obj->getParent() || objDisabled(obj)) continue;
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
			if (auto trig = typeinfo_cast<EffectGameObject*>(obj); trig && !objDisabled(obj)) {
				trig->triggerObject(layer, 0, nullptr);
			}
		}
	}



	// ---- Physics trigger: a body (group of blocks) falls under gravity and lands on solid blocks ----
	std::vector<EffectGameObject*> physTriggers(GJBaseGameLayer* layer) {
		std::vector<EffectGameObject*> out;
		if (!layer || !layer->m_objects) return out;
		for (auto obj : CCArrayExt<GameObject*>(layer->m_objects)) {
			if (obj && obj->m_objectID == physObjectId() && !objDisabled(obj)) out.push_back(static_cast<EffectGameObject*>(obj));
		}
		return out;
	}

	struct PhysBody {
		std::vector<std::pair<GameObject*, CCPoint>> blocks; // block and its start position
		float vy = 0.f;
		bool init = false;
	};
	std::map<EffectGameObject*, PhysBody> g_bodies;
	std::vector<CCRect> g_statics;
	bool g_staticsReady = false;
	std::vector<EffectGameObject*> g_physCache;
	GJBaseGameLayer* g_physLayer = nullptr;
	int g_physTick = 0;

	void resetPhysics(GJBaseGameLayer* layer) {
		for (auto& [t, body] : g_bodies) {
			for (auto& [o, start] : body.blocks) {
				if (o && o->getParent()) moveSolid(layer, o, start);
			}
			body.vy = 0.f;
		}
	}

	void buildStatics(GJBaseGameLayer* layer, std::vector<EffectGameObject*> const& trigs) {
		std::set<GameObject*> moving;
		for (auto t : trigs) {
			if (auto arr = layer->getGroup(t->m_targetGroupID)) {
				for (auto o : CCArrayExt<GameObject*>(arr)) moving.insert(o);
			}
		}
		g_statics.clear();
		if (layer->m_objects) {
			for (auto o : CCArrayExt<GameObject*>(layer->m_objects)) {
				if (!o || !o->getParent() || moving.count(o)) continue;
				if (o->m_objectType == GameObjectType::Solid) g_statics.push_back(o->boundingBox());
			}
		}
		g_staticsReady = true;
	}

	void stepPhysics(GJBaseGameLayer* layer, float dt) {
		if (!layer->m_player1) return;
		if (g_physLayer != layer || g_physTick++ % 60 == 0) {
			g_physLayer = layer;
			g_physCache = physTriggers(layer);
		}
		if (g_physCache.empty()) return;
		if (!g_staticsReady) buildStatics(layer, g_physCache);
		float px = layer->m_player1->getPositionX();

		for (auto t : g_physCache) {
			auto& d = physData(t);
			if (!d.physical || t->m_targetGroupID <= 0 || px < t->getPositionX()) continue;
			auto& body = g_bodies[t];
			if (!body.init) {
				body.init = true;
				if (auto arr = layer->getGroup(t->m_targetGroupID)) {
					for (auto o : CCArrayExt<GameObject*>(arr)) {
						if (o && o->getParent()) body.blocks.push_back({o, o->getPosition()});
					}
				}
			}
			if (body.blocks.empty()) continue;

			// 1 block = 30 units = 1 meter
			body.vy -= d.gravity * 30.f * dt;
			float dy = body.vy * dt;
			if (dy < 0.f) {
				std::vector<CCRect> rects;
				if (d.fillGaps) {
					float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
					for (auto& [o, s] : body.blocks) {
						auto r = o->boundingBox();
						x0 = std::min(x0, r.getMinX());
						y0 = std::min(y0, r.getMinY());
						x1 = std::max(x1, r.getMaxX());
						y1 = std::max(y1, r.getMaxY());
					}
					rects.push_back(CCRect(x0, y0, x1 - x0, y1 - y0));
				} else {
					for (auto& [o, s] : body.blocks) rects.push_back(o->boundingBox());
				}
				float allowed = -dy;
				for (auto const& r : rects) {
					for (auto const& st : g_statics) {
						if (r.getMaxX() > st.getMinX() + 0.5f && r.getMinX() < st.getMaxX() - 0.5f && st.getMaxY() <= r.getMinY() + 0.5f) {
							allowed = std::min(allowed, std::max(r.getMinY() - st.getMaxY(), 0.f));
						}
					}
				}
				if (allowed < -dy) {
					dy = -allowed;
					body.vy = 0.f;
				}
			}
			if (std::fabs(dy) > 0.001f) {
				for (auto& [o, s] : body.blocks) {
					if (o && o->getParent()) {
						auto p = o->getPosition();
						moveSolid(layer, o, ccp(p.x, p.y + dy));
					}
				}
			}
		}
	}

	// ---- "Lock Move": hide / disable the left-right buttons and keys ----
	bool g_lockMove = false;
	std::vector<Ref<CCNode>> g_hiddenUI;
	int g_uiTick = 0;

	void hideMoveButtons(CCNode* node, CCSize ws) {
		auto kids = node->getChildren();
		if (!kids) return;
		for (auto c : CCArrayExt<CCNode*>(kids)) {
			if (!c || !c->isVisible()) continue;
			auto bb = c->boundingBox();
			auto a = node->convertToWorldSpace(ccp(bb.getMinX(), bb.getMinY()));
			auto b = node->convertToWorldSpace(ccp(bb.getMaxX(), bb.getMaxY()));
			bool inside = bb.size.width > 1.f && b.x < ws.width * 0.35f && b.y < ws.height * 0.4f && a.x >= 0.f && a.y >= 0.f;
			if (inside) {
				c->setVisible(false);
				g_hiddenUI.push_back(c);
			} else {
				hideMoveButtons(c, ws);
			}
		}
	}

	void applyLockUI() {
		if (g_lockMove) {
			if (g_uiTick++ % 20 == 0) {
				if (auto ui = UILayer::get()) hideMoveButtons(ui, CCDirector::sharedDirector()->getWinSize());
			}
		} else if (!g_hiddenUI.empty()) {
			for (auto& n : g_hiddenUI) {
				if (n) n->setVisible(true);
			}
			g_hiddenUI.clear();
			g_uiTick = 0;
		}
	}

	// the Ctrl triggers of the level, refreshed once a second
	std::vector<EffectGameObject*> g_trigCache;
	GJBaseGameLayer* g_trigLayer = nullptr;
	int g_trigTick = 0;

	std::vector<EffectGameObject*> const& cachedTriggers(GJBaseGameLayer* layer) {
		if (layer != g_trigLayer || g_trigTick++ % 60 == 0) {
			g_trigLayer = layer;
			g_trigCache = ctrlTriggers(layer);
		}
		return g_trigCache;
	}

	CCTouch* g_buttonTouch = nullptr;

	// custom Left / Right buttons made of level objects: touch -> game button 2 / 3
	bool g_inject = false;
	std::map<CCTouch*, int> g_moveTouches;

	void pressMove(GJBaseGameLayer* layer, bool down, int button) {
		g_inject = true;
		layer->handleButton(down, button, true);
		g_inject = false;
	}

	struct DragState {
		CCTouch* touch = nullptr;
		GameObject* obj = nullptr;
		CCPoint cur;
		int moves = 0;
		bool moved = false;
		float hold = 0.3f;
		int group = 0;
		bool afterDrop = false;
		bool started = false;
		CCPoint grabStart;
		std::vector<std::pair<GameObject*, CCPoint>> others; // other objects of the drag group and their start positions
		std::chrono::steady_clock::time_point start;
	};
	DragState g_drag;

	std::map<GameObject*, CCPoint> g_dropped;
	float g_lastPlayerX = 0.f;

	void finishDrag() {
		if (g_drag.obj && g_drag.moved) {
			auto layer = GJBaseGameLayer::get();
			if (g_drag.afterDrop && layer) {
				// the movement was only visual: apply it for real now, so triggers and collision see it
				auto fin = g_drag.obj->getPosition();
				g_drag.obj->setPosition(g_drag.grabStart);
				moveSolid(layer, g_drag.obj, fin);
				for (auto& entry : g_drag.others) {
					if (entry.first && entry.first->getParent()) {
						auto f = entry.first->getPosition();
						entry.first->setPosition(entry.second);
						moveSolid(layer, entry.first, f);
					}
				}
			}
			g_dropped[g_drag.obj] = g_drag.obj->getPosition();
			for (auto& entry : g_drag.others) {
				if (entry.first && entry.first->getParent()) g_dropped[entry.first] = entry.first->getPosition();
			}
		}
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
	std::string m_captionText = "CTRL";

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

	void setCaption(char const* c) {
		if (m_captionText != c) {
			m_captionText = c;
			m_caption->setString(c);
		}
	}

	void setGroup(int g) {
		if (g == m_last) return;
		m_last = g;
		m_label->setString(std::to_string(g).c_str());
		m_label->setScale(g < 100 ? 0.5f : (g < 1000 ? 0.38f : 0.3f));
	}
};

namespace {
	std::map<GameObject*, Ref<CTBadge>> g_badges;

	void syncBadges(LevelEditorLayer* layer) {
		if (!layer->m_objectLayer) return;
		std::set<GameObject*> seen;
		auto all = ctrlTriggers(layer);
		for (auto t : physTriggers(layer)) all.push_back(t);
		for (auto t : all) {
			if (!t->getParent()) continue;
			seen.insert(t);
			t->setOpacity(0);
			// hide the leftover text / decorations of the borrowed trigger sprite (e.g. the word MOVE)
			if (auto kids = t->getChildren()) {
				for (auto c : CCArrayExt<CCNode*>(kids)) c->setVisible(false);
			}
			auto& b = g_badges[t];
			if (!b) {
				b = CTBadge::create();
				if (!b) continue;
				layer->m_objectLayer->addChild(b, 5000);
			}
			auto world = t->getParent()->convertToWorldSpace(t->getPosition());
			b->setPosition(layer->m_objectLayer->convertToNodeSpace(world));
			b->setCaption(t->m_objectID == physObjectId() ? "PHYS" : "CTRL");
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

	LevelEditorLayer* m_layer = nullptr;

	bool init() override {
		if (!CCNode::init()) return false;
		// badges of an earlier editor session are gone together with that editor
		g_badges.clear();
		this->scheduleUpdate();
		return true;
	}

	~CTSyncNode() override {
		g_badges.clear();
	}

	void update(float) override {
		auto layer = LevelEditorLayer::get();
		if (layer && layer == m_layer) syncBadges(layer);
	}
};

// The palette button of our triggers shows our own pictures instead of the sprites of Move / Follow.
static void reskinPaletteIcon(CCNode* n) {
	auto kids = n->getChildren();
	if (!kids) return;
	for (auto c : CCArrayExt<CCNode*>(kids)) {
		if (auto go = typeinfo_cast<GameObject*>(c)) {
			std::string name;
			if (go->m_objectID == ctrlObjectId()) name = "ctrl_btn.png"_spr;
			else if (go->m_objectID == physObjectId()) name = "phys_btn.png"_spr;
			if (!name.empty()) {
				if (auto f = CCSpriteFrameCache::sharedSpriteFrameCache()->spriteFrameByName(name.c_str())) {
					go->setDisplayFrame(f);
				}
			}
		}
		reskinPaletteIcon(c);
	}
}

class $modify(CTEditorUI, EditorUI) {
	CreateMenuItem* getCreateBtn(int id, int bg) {
		auto btn = EditorUI::getCreateBtn(id, bg);
		if (btn && (id == ctrlObjectId() || id == physObjectId())) reskinPaletteIcon(btn);
		return btn;
	}

	bool init(LevelEditorLayer* editor) {
		if (!EditorUI::init(editor)) return false;
		if (auto n = CTSyncNode::create()) {
			n->m_layer = editor;
			this->addChild(n);
		}
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
				if (c.moveBtn) {
					g_moveTouches[t] = c.moveBtn;
					pressMove(layer, true, c.moveBtn);
					return true;
				}
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
				g_drag.group = c.group;
				g_drag.afterDrop = c.afterDrop;
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
		if (auto it = g_moveTouches.find(t); it != g_moveTouches.end()) {
			if (auto layer = GJBaseGameLayer::get()) pressMove(layer, false, it->second);
			g_moveTouches.erase(it);
			return;
		}
		if (g_buttonTouch && t == g_buttonTouch) { g_buttonTouch = nullptr; return; }
		if (g_drag.touch && t == g_drag.touch) { finishDrag(); return; }
		UILayer::ccTouchEnded(t, e);
	}

	void ccTouchCancelled(CCTouch* t, CCEvent* e) {
		if (auto it = g_moveTouches.find(t); it != g_moveTouches.end()) {
			if (auto layer = GJBaseGameLayer::get()) pressMove(layer, false, it->second);
			g_moveTouches.erase(it);
			return;
		}
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
		g_lockMove = false;
		g_moveTouches.clear();
		g_hiddenUI.clear();
		g_trigCache.clear();
		g_trigLayer = nullptr;
		g_bodies.clear();
		g_statics.clear();
		g_staticsReady = false;
		g_physCache.clear();
		g_physLayer = nullptr;
		PlayLayer::setupHasCompleted();
	}
};

// Runs in the real game and in the editor playtest.
class $modify(CTBaseLayer, GJBaseGameLayer) {
	// block the left (2) and right (3) buttons / keys while "Lock Move" is active
	void handleButton(bool down, int button, bool isPlayer1) {
		if (down && g_lockMove && !g_inject && (button == 2 || button == 3)) return;
		GJBaseGameLayer::handleButton(down, button, isPlayer1);
	}

	void update(float dt) {
		// Lock Move is active from the very start of the level if any Ctrl trigger has it on
		bool lock = false;
		for (auto t : cachedTriggers(this)) {
			if (t && ctrlLockGet(t)) {
				lock = true;
				break;
			}
		}
		g_lockMove = lock;
		applyLockUI();

		GJBaseGameLayer::update(dt);
		stepPhysics(static_cast<GJBaseGameLayer*>(this), dt);

		if (m_player1) {
			float x = m_player1->getPositionX();
			if (x < g_lastPlayerX - 5.f) {
				g_dropped.clear();
				resetPhysics(static_cast<GJBaseGameLayer*>(this));
			}
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
				auto target = g_drag.obj->getParent()->convertToNodeSpace(loc);
				if (!g_drag.started) {
					// remember where every object of the drag group started
					g_drag.started = true;
					g_drag.grabStart = g_drag.obj->getPosition();
					g_drag.others.clear();
					if (auto arr = this->getGroup(g_drag.group)) {
						for (auto o : CCArrayExt<GameObject*>(arr)) {
							if (o && o != g_drag.obj && o->getParent() && !objDisabled(o)) g_drag.others.push_back({o, o->getPosition()});
						}
					}
				}
				auto delta = ccpSub(target, g_drag.grabStart);
				auto mv = [&](GameObject* o, CCPoint p) {
					if (g_drag.afterDrop) o->setPosition(p);
					else moveSolid(static_cast<GJBaseGameLayer*>(this), o, p);
				};
				mv(g_drag.obj, target);
				// the other objects follow the same path, each from its own start
				for (auto& entry : g_drag.others) {
					if (entry.first && entry.first->getParent()) {
						mv(entry.first, ccpAdd(entry.second, delta));
					}
				}
				g_drag.moved = true;
			}
		}
	}
};
