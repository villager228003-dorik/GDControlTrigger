// Step 1 of the real Ctrl trigger: a brand new object in the editor (own id, own sprite).
// Uses the open-source "game objects factory" (lil2kki/geode-game-objects-factory),
// downloaded at build time and compiled into this mod.
#include <Geode/Geode.hpp>
#include <main.hpp>
#include <impl.hpp>

using namespace geode::prelude;

$execute {
	GameObjectsFactory::registerGameObject(GameObjectsFactory::createTriggerConfig(
		UNIQ_ID("ctrl-trigger"),
		"ctrl_circle.png"_spr,
		[](auto, auto, auto, auto) {
			// trigger logic comes in the next step
		}
	));
}
