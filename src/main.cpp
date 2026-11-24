#include <Geode/Geode.hpp>
#include <Geode/modify/MenuLayer.hpp>

using namespace geode::prelude;

// Step 1: pipeline test. Shows a popup once after the game starts
// to prove that the mod was built by GitHub Actions and loaded by Geode.
class $modify(CTMenuLayer, MenuLayer) {
	bool init() {
		if (!MenuLayer::init()) return false;

		static bool shown = false;
		if (!shown) {
			shown = true;
			log::info("Control Trigger: loaded");
			FLAlertLayer::create("Control Trigger", "Mod loaded. Build v1 works.", "OK")->show();
		}
		return true;
	}
};
