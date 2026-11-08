#pragma once

#include "features/npc/npc.h"
#include "features/player/player.h"
#include "sdk/cryengine/animation/i_character_instance.h"
#include "sdk/cryengine/entitysystem/i_entity.h"
#include "sdk/cryengine/entitysystem/i_entity_system.h"
#include "sdk/cryengine/physics/c_physical_world.h"
#include "sdk/cryengine/renderer/i_renderer.h"

#include <external/imgui/widgets/nametag.h>

#include <cstdint>
#include <string>
#include <vector>

struct ImDrawList;

namespace KCDC::Features::Player {
    /**
     * The game-thread ImGui pass that draws every named body's tag.
     *
     * It outlives a frame because the smoothed ambient reading and the
     * candidate buffer are carried between frames; per-body state lives on
     * the body, in `Human::nametagVisibility`.
     *
     * Two things bound the cost, and neither grows with the session: the
     * light-driven draw distance rejects a body before any engine call is
     * made about it, and the occlusion rays come out of a fixed per-frame
     * budget spent nearest body first.
     */
    class NametagPass {
      public:
        /**
         * The ImGui font the names are drawn in, and the stem of its file under
         * `files/kcdc/fonts/`.
         */
        static constexpr const char *kFontName = "NotoSerif-Bold";

        /** Game thread, inside the ImGui callback. */
        void Render();

      private:
        /** Where a body's tag goes, once every gate before the occlusion ray has passed. */
        struct Anchor {
            SDK::IEntity *entity;
            SDK::ICharacterInstance *character;
            SDK::Vec3 neck;
            ImVec2 screen;
            float distance;
        };

        /**
         * One body to name, whoever owns it.
         *
         * The pass wants six things and no more, which is why it can serve a
         * remote player and a server-owned NPC out of the same buffer: where
         * the body is, which entity it is, the replicated tag state, the label
         * to fall back on when that state carries no text of its own, how much
         * health to draw, and somewhere to keep the fade.
         */
        struct Subject {
            SDK::EntityId entityId                                          = 0;
            /** The replicated id scripts know the body by (`player.id`); keys its Nametags.setLabel note. */
            std::uint64_t networkId                                         = 0;
            glm::vec3 position                                              = glm::vec3(0.0F);
            const Framework::Networking::Replication::NametagState *nametag = nullptr;
            const std::string *fallbackLabel                                = nullptr;
            float healthPercent                                             = 0.0F;
            NametagVisibility *visibility                                   = nullptr;

            /** The voice speaker to show talking: the owning player's peer GUID, 0 for an NPC. */
            std::uint64_t voiceSpeaker = 0;

            /**
             * Whether the body is sneaking. Crouching is the game's stealth,
             * and a name floating over a hedge gives away exactly what it
             * hides, so a concealed body's tag fades out and stays out.
             */
            bool concealed = false;
        };

        /** The pre-cull sort key; `Anchor::distance` is the exact one. */
        struct Candidate {
            Subject subject;
            float distance;
        };

        /** Resolves this frame's engine pointers and light-driven layout. */
        bool Begin();

        float LightDrawDistance(float deltaTime);

        /** Adds one body to this frame's buffer, or clears its fade if it is out of range. */
        void Consider(const Subject &subject, float cullDistanceSquared);

        bool ResolveAnchor(const Subject &subject, Anchor &anchor) const;
        void Draw(const Subject &subject, float deltaTime, int &rayBudget);

        bool HasLineOfSight(const Anchor &anchor, const SDK::Vec3 &target) const;

        SDK::CPhysicalWorld *_physics                = nullptr;
        SDK::IPhysicalEntity *_localPhysics          = nullptr;
        SDK::IPhysicalEntity *_localCharacterPhysics = nullptr;
        SDK::IRenderer *_renderer                    = nullptr;
        SDK::IEntitySystem *_entitySystem            = nullptr;
        ImDrawList *_drawList                        = nullptr;
        ImFont *_font                                = nullptr;
        float _displayWidth                          = 0.0F;
        float _displayHeight                         = 0.0F;
        SDK::Vec3 _cameraPosition {};
        Framework::External::ImGUI::Widgets::NameTagLayout _layout;

        /** Smoothed draw distance; negative until the first frame sets it. */
        float _lightDistance = -1.0F;

        /** Reused across frames; holds nothing once Render returns. */
        std::vector<Candidate> _candidates;
    };
} // namespace KCDC::Features::Player
