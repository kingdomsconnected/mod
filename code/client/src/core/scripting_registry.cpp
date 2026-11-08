#include "scripting_registry.h"

#include "core/input/controls_scripting.h"

#include "core/streamed_assets/streamed_asset_scripting.h"
#include "features/audio/audio_scripting.h"
#include "features/book/book_scripting.h"
#include "features/camera/camera_scripting.h"
#include "features/camera/noclip_scripting.h"
#include "features/character_creator/character_creator_scripting.h"
#include "features/door/door_scripting.h"
#include "features/follow/follow_scripting.h"
#include "features/gizmos/gizmos_scripting.h"
#include "features/items/inventory_use_scripting.h"
#include "features/light/light_scripting.h"
#include "features/map/map_blip_scripting.h"
#include "features/map/map_scripting.h"
#include "features/player/player_progression_scripting.h"
#include "features/player/player_scripting.h"
#include "features/prop/prop_editor_scripting.h"
#include "features/prop/prop_placer_scripting.h"
#include "features/quest/quest_scripting.h"
#include "features/social/social_emote_wheel_scripting.h"
#include "features/social/social_voice_scripting.h"
#include "features/ui/native_ui_scripting.h"
#include "features/ui/ui_action_hints_scripting.h"
#include "features/ui/ui_hud_scripting.h"
#include "features/vendor/vendor_scripting.h"
#include "features/vfx/vfx_scripting.h"
#include "features/world/world_query_scripting.h"
#include "features/world/world_scripting.h"

#include "shared/scripting/catalog.h"

#include <logging/logger.h>
#include <scripting/builtins/entity.h>

namespace KCDC::Core {
    void RegisterScripting(v8::Isolate *isolate, v8::Local<v8::Object> global) {
        if (isolate == nullptr || global.IsEmpty()) {
            return;
        }

        // Nothing here is dropped and rebuilt between sessions. A reconnect resets the V8 context but keeps the isolate, and a v8pp class_ is registered against the isolate: building a second one throws "already exist in isolate" out of v8pp::detail::classes::add, which escapes
        // as an unhandled C++ exception. Each GetClass below caches its class_ for the life of the process, and the Register calls only republish it into the new global.

        // Creates EventMap before any binding below appends to it.
        Scripting::EnsureEventMap(Scripting::ClientCatalog());

        Framework::Scripting::Builtins::Entity::Register(isolate, global);
        Scripting::Player::Register(isolate, global);
        Scripting::LocalPlayer::RegisterGlobal(isolate, global);
        Scripting::Progression::Register(isolate, global);
        Scripting::Hud::Register(isolate, global);
        Scripting::Audio::Register(isolate, global);
        Scripting::Assets::Register(isolate, global);
        Scripting::Vfx::Register(isolate, global);
        Scripting::Light::Register(isolate, global);
        Scripting::Blip::Register(isolate, global);
        Scripting::RegisterQuestMetadata();
        Scripting::RegisterDoorMetadata();
        Scripting::RegisterVendorMetadata();
        Scripting::RegisterWorldMetadata();
        Scripting::World::Register(isolate, global);
        Scripting::WorldMap::Register(isolate, global);
        Scripting::NoClip::Register(isolate, global);
        Scripting::RegisterCamera(isolate, global);
        Scripting::RegisterGizmos(isolate, global);
        Scripting::PropGhost::Register(isolate, global);
        Scripting::RegisterPropPlacer(isolate, global);
        Scripting::RegisterMapEditor(isolate, global);
        Scripting::RegisterNativeUI(isolate, global);
        Scripting::RegisterControls(isolate, global);
        Scripting::RegisterEmoteWheel(isolate, global);
        Scripting::Voice::Register(isolate, global);
        Scripting::RegisterBook(isolate, global);
        Scripting::RegisterFollow(isolate, global);
        Scripting::RegisterCharacterCreator(isolate, global);
        Scripting::RegisterActionHints(isolate, global);
        Scripting::RegisterInventoryUse(isolate, global);

#if !defined(NDEBUG) && defined(KCDC_SCRIPTING_API_METADATA_FILE)
        // The export belongs to the list rather than to any one binding: it can only run once every feature above has put its metadata in. Debug builds refresh the committed file whenever client scripting initialises, which is on connect rather than on boot; see scripting-api/.
        if (!Scripting::ExportCatalog(Scripting::ClientCatalog(), "framework-client", KCDC_SCRIPTING_API_METADATA_FILE)) {
            Framework::Logging::GetLogger("Scripting")->error("Failed to export the scripting API metadata to {}", KCDC_SCRIPTING_API_METADATA_FILE);
        }
        else {
            Framework::Logging::GetLogger("Scripting")->debug("Exported the scripting API metadata to {}", KCDC_SCRIPTING_API_METADATA_FILE);
        }
#endif
    }
} // namespace KCDC::Core
