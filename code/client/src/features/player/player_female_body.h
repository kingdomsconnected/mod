#pragma once

#include "sdk/cryengine/3dengine/c_skinning_info.h"
#include "sdk/cryengine/3dengine/i_chunk_file.h"
#include "sdk/cryengine/animation/c_default_skeleton.h"
#include "sdk/cryengine/animation/c_internal_database_info.h"
#include "sdk/cryengine/animation/global_animation_header_caf.h"
#include "sdk/cryengine/entitysystem/i_entity.h"
#include "sdk/cryengine/system/xml_node_ref.h"

#include <function2/function2.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace KCDC::Features::Player {
    /**
     * A woman's body on the man's animation set.
     *
     * Every human body, whatever its gender, runs the man's Mannequin
     * vocabulary and his clips, and a woman's skeleton and clips are
     * converted to his hand frames as the game loads them. Nothing here
     * belongs to one body: it edits game assets once, from the loading
     * hooks in player_service.cpp.
     */
    class FemaleBody final {
      public:
        // --- One Mannequin vocabulary for every human -------------------------

        /**
         * Extends one of the game's Mannequin files as it loads, so that a
         * man's and a woman's body run the same controller definition and
         * database: her fragment ids, tags and fragment definitions join the
         * man's, her database joins his behind the `female` tag, and every
         * database written against her vocabulary is pointed at his.
         * `load` reads another game file without coming back here.
         */
        static void ExtendMannequinFile(const char *file, SDK::XmlNodeRef &document, const fu2::function<SDK::XmlNodeRef(const char *) const> &load);

        /**
         * Before `CAnimatedCharacter::InitializeMannequin` reads the entity's
         * class script: a body standing on her skeleton is given the man's
         * controller definition and database where its class names hers.
         * Whether the body is a woman's.
         */
        static bool AdoptHumanMannequin(SDK::IEntity *entity);

        // --- A woman's hands on the man's joint frames ---------------------------

        /**
         * `male.chr` and `female.chr` name their hand joints alike but turn
         * them differently, and a clip stores absolute local rotations, so a
         * male clip bent a woman's hands about the wrong axes. Her hand
         * joints are put on his frames once, as each thing that carries them
         * loads, and what was authored against her own frames is converted
         * with them; male clips then play on her as they are. The frame
         * changes are `Shared::Game::g_HandFrames`, mined from both `.chr`
         * files by tools/generate_hand_frames.py.
         */
        static bool IsFemaleSkeleton(const char *path);

        /** Her bones and hand colliders, before `InitializeBones` derives anything from them. */
        static void AdoptMaleHandFrames(SDK::CSkinningInfo &skinning);

        /** Her aim and look poses, baked in `DirectionalBlends.img`, once her IK lists are read. */
        static void AdoptMaleHandFramesInDirectionalPoses(const SDK::CDefaultSkeleton &skeleton);

        /** Brackets `ReadController905`: scans one of her databases' controller chunk. */
        static void BeginFemaleDatabase(const SDK::CInternalDatabaseInfo &database, const SDK::IChunkFile::ChunkDesc &chunk);
        static void EndFemaleDatabase();

        /** One of her clips, before `InitControllerLookup` makes it playable. */
        static void AdoptMaleHandFramesInClip(SDK::GlobalAnimationHeaderCAF &clip, std::uint32_t controllerCount);

        /** Frees the controller copies a database's clips were given. */
        static void ReleaseFemaleDatabase(const SDK::CInternalDatabaseInfo &database);

      private:
        struct FemaleDatabaseLoad;

        // The database ReadController905 is building on this thread, while it does.
        static thread_local FemaleDatabaseLoad *_femaleDatabaseLoad;

        // Controller copies, owned by the database whose clips point at them.
        static std::mutex _femaleDatabaseClonesLock;
        static std::unordered_map<const SDK::CInternalDatabaseInfo *, std::vector<std::unique_ptr<std::byte[]>>> _femaleDatabaseClones;

        // The aim/look headers already converted, by index; the img loads once.
        static std::mutex _directionalPosesLock;
        static std::vector<bool> _directionalPosesAdopted;
    };
} // namespace KCDC::Features::Player
