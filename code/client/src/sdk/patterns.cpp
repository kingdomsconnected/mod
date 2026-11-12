#include "patterns.h"

#include <core/obfuscation.h>
#include <utils/hooking/hooking.h>
#include <utils/hooking/hooking_patterns.h>

namespace SDK {
    Patterns gPatterns;

    void Patterns::Init() {
        gPatterns.C_Player__NotifyActorActionRequest                   = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 08 57 48 83 EC 20 48 8B FA 48 8B D9 E8 ? ? ? ? 48 8B 8B C8 0C 00 00 48 8B D7")));
        gPatterns.C_Actor__NotifyActorActionRequest                    = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 83 EC 28 83 7A 18 03 75 05 48 83 C4 28 C3 48 8B 89 80 02 00 00 E8 ? ? ? ? 45 33 C0 33 D2")));
        gPatterns.C_RiderPlayerControl__RegisterMountedHintCallbacks   = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE(
            "48 8B C4 48 89 58 08 48 89 70 10 48 89 78 18 55 48 8D 68 A1 48 81 EC A0 00 00 00 48 8B 05 ? ? ? ? 48 33 C4 48 89 45 47 48 8B F9 E8 ? ? ? ? 48 8D 4D EF 48 89 7D 0F 48 8B 90 28 01 00 00 48 8B 72 60 48 8D 15 ? ? ? ? 48 8B 06 48 8B 58 48 48 8D 05 ? ? ? ? 48 89 45 07 48 8D 45 07 48 89 45 3F E8 ? ? ? ? 48 8D 15 ? ? ? ? 48 8D 4D E7 E8 ? ? ? ? 4C 8D 4D 07 C6 44 24 20 01 4C 8D 45 EF 48 8B CE 48 8D 55 E7 FF D3 48 8D 4D E7 E8")));
        gPatterns.C_RiderPlayerControl__UnregisterMountedHintCallbacks = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE(
            "48 89 5C 24 18 48 89 7C 24 20 48 89 4C 24 08 55 48 8B EC 48 83 EC 20 E8 ? ? ? ? 48 8D 15 ? ? ? ? 48 8B 88 28 01 00 00 48 8B 79 60 48 8D 4D 18 48 8B 07 48 8B 58 58 E8 ? ? ? ? 48 8D 15 ? ? ? ? 48 8D 4D 10 E8 ? ? ? ? 41 B1 01 4C 8D 45 18 48 8D 55 10 48 8B CF FF D3 48 8D 4D 10 E8 ? ? ? ? 48 8D 4D 18 E8 ? ? ? ? 48 8B 07 48 8D 15 ? ? ? ? 48 8D 4D 18 48 8B 58 58 E8 ? ? ? ? 48 8D 15 ? ? ? ? 48 8D 4D 10 E8 ? ? ? ? 41 B1 01 4C 8D 45 18 48 8D 55 10 48 8B CF FF D3 48 8D 4D 10 E8 ? ? ? ? 48 8D 4D 18 E8 ? ? ? ? 48 8B 5C 24 40 48 8B 7C 24 48 48 83 C4 20 5D")));
        gPatterns.C_RiderPlayerControl__UpdateMountedHintBindings      = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 10 48 89 74 24 18 55 57 41 54 41 56 41 57 48 8B EC 48 83 EC 30 48 8B F1 E8 ? ? ? ? 48 8B 90 28 01 00 00")));
        gPatterns.C_Player__OnItemEquipped                             = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 08 55 56 57 48 8B EC 48 83 EC 30 48 8B F9 4C 89 45 30 48 8B 0D ? ? ? ? 48 8D 55 30 48 81 C1 68 01 00 00")));
        gPatterns.C_Player__OnItemUnequipped                           = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 08 55 56 57 48 8B EC 48 83 EC 30 48 8B F1 4C 89 45 30 48 8B 0D ? ? ? ? 48 8D 55 30 48 81 C1 68 01 00 00")));
        gPatterns.C_Player__RefreshHelmetActionHint                    = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 48 83 EC 20 48 8B D9 E8 ? ? ? ? 48 8D 54 24 38 C7 44 24 38 00 00 00 00 48 8B C8 E8 ? ? ? ? 83 7C 24 38 00 74 23")));
        gPatterns.C_Player__RefreshHeadLight                           = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 48 83 EC 50 48 8B D9 48 8D 4C 24 20 E8 ? ? ? ? 48 8D 15 ? ? ? ? 48 8D 4C 24 20 E8 ? ? ? ? B8 0A 00 00 00")));
        gPatterns.C_EquipmentManager__ReadEquippedVisorTypeId          = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE(
            "48 89 5C 24 ? 48 89 6C 24 ? 57 48 83 EC 20 48 8B 69 ? 48 8B DA 48 8B 79 ? 48 3B FD 74 ? 48 89 74 24 ? 0F 1F 40 ? 0F 1F 84 00 ? ? ? ? 48 8B 37 BA 10 00 00 00 48 8B CE E8 ? ? ? ? 84 C0 74 ? 48 8B CE E8 ? ? ? ? 33 D2 48 8B 08 4C 8B 81 ? ? ? ? 48 8B C8 41 FF D0 8B 88")));
        gPatterns.I_ActionSets__SetActionEnabled                       = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE(
            "48 8B C4 48 89 58 08 48 89 70 10 48 89 78 18 44 88 48 20 55 48 8B EC 48 81 EC 80 00 00 00 48 8D 79 F8 48 89 55 D0 48 8D 05 ? ? ? ? 48 89 7D C8 48 89 45 B8 4C 8D 4D B8 48 8D 45 28 48 8B CF 48 89 45 C0 49 8B F0 48 8D 45 B8 48 8B DA 48 89 45 F0 E8 ? ? ? ? 80 7D 30 00 74 28 48 8D 45 28 48 89 7D A0 48 8D 55 A0 48 89 45 B0 48 8D 4D B8 48 89 75 A8 E8 ? ? ? ? 4C 8B C0 48 8B D3 E8 ? ? ? ? 4C 8D 9C 24 80 00 00 00 49 8B 5B 10 49 8B 73 18 49 8B 7B 20 49 8B E3 5D C3 CC CC 48 89 5C 24 10")));
        gPatterns.C_Human__OnEntityRemoved                              = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 48 83 EC 20 80 B9 3D 06 00 00 00 48 8B D9 75 11 48 8B 89 D0 09 00 00 48 85 C9 74 05 E8")));
        gPatterns.C_Player__OnEntityRemoved                             = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 83 EC 48 E8 ? ? ? ? 48 8D 05 ? ? ? ? C7 44 24 28 29 00 00 00 48 89 44 24 20 C7 44 24")));
        gPatterns.C_Player__ProcessEvent                                = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B FA 48 8B D9 E8 ? ? ? ? 44 8B 07 45 85 C0 0F 84 ? ? ? ? BA 07 00 00 00 44 2B C2 0F 84 ? ? ? ? 44 2B C2"), -0x1F));
        gPatterns.C_StealthWatcher__BindLocalPlayer                     = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 10 57 48 83 EC 60 48 8B F9 48 8B 0D ? ? ? ? 48 8B 01 FF 10 48 89 47 20 E8")));
        gPatterns.C_PlayerStateHandler__BindPlayer                      = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 08 48 89 74 24 10 48 89 7C 24 18 55 48 8B EC 48 83 EC 30 48 89 91 48 02 00 00 48 8B F9 48 8B 02")));
        gPatterns.PlayerTrespass__OnAreaChanged                         = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57 48 83 EC 20 48 8B F1 41 8A E9 48 8B 0D ? ? ? ? 41 8B D8 48 8B FA")));
        gPatterns.C_NPCManager__RegisterNPC                             = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 18 55 56 57 41 56 41 57 48 8B EC 48 83 EC 50 8A 41 28 41 8B D8 4C 8B FA 48 8B F9")));
        gPatterns.C_NPCManager__LocalPlayerNPCCache                     = hook::get_address<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 05 ? ? ? ? E8 ? ? ? ? EB 09 E8 ? ? ? ? 49 89 46 10 49 83 7E 10 00 75 07")), 3, 7);
        gPatterns.RebindLocalPlayerScriptVariables                      = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 08 48 89 7C 24 18 55 48 8B EC 48 81 EC 80 00 00 00 48 8B 15 ? ? ? ? 48 8B D9 48 8D 4D A0 E8")));
        gPatterns.I_ActionSets__ClearActionCallbacks                    = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 81 EC 88 00 00 00 48 8B 05 ? ? ? ? 48 33 C4 48 89 44 24 70 C6 44 24")));
        gPatterns.C_ActionSets__ParseActionHelp                         = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 18 55 56 57 41 54 41 55 41 56 41 57 48 8D AC 24 50 FF FF FF 48 81 EC B0 01 00 00 48 8B 05 ? ? ? ? 48 33 C4 48 89 85 A0 00 00 00 4C 8B F1 48 89 4C 24 78")));
        gPatterns.C_ActionHintManager__HandleAction                     = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 55 53 56 57 41 56 48 8D 6C 24 B0 48 81 EC 50 01 00 00 48 8B 05 ? ? ? ? 48 33 C4 48 89 45 40 48 8B F2")));
        gPatterns.C_PlayerInput__Destructor                             = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 18 56 57 41 56 48 83 EC 20 4C 8D 71 08 48 8B F1 48 8D 05 ? ? ? ?")));
        gPatterns.C_Soul__UpdatePlayerMaterialAudio                     = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 10 56 57 41 56 48 83 EC 30 8B 81 F0 0C 00 00 8B DA C1 E8 04")));
        gPatterns.SubmitPlayerMaterialAudioSwitch                       = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 55 48 8B EC 48 83 EC 70 48 8B 0D ? ? ? ? 48 8D 05 ? ? ? ? 48 89 45 B0 0F 57 C0 8B 02 48 8D 55 D0 89 45 C8 41")));
        gPatterns.C_ExhaustedBuffInstance__TriggerPlayerExhaustionEffect = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B C8 E8 ? ? ? ? 84 C0 0F 85 ? ? ? ? 48 8B 9F 88 00 00 00 48 85 DB 0F 84 ? ? ? ? 8B 83 F0 0C 00 00"), -0x35));
        gPatterns.C_StarvationBuffInstance__TriggerHungerEffect          = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 08 48 89 74 24 18 57 48 83 EC 60 48 8B F9 E8 ? ? ? ? 48 8B F0 48 85 C0 74 5A 48 8B 10 48 8D 4C 24 78")));
        gPatterns.C_Player__SetFeedbackSound                             = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 08 44 88 4C 24 20 55 56 57 41 56 41 57 48 8B EC 48 83 EC 50 4C 63 F2 32 DB 4C 8B F9 41 83 FE 15")));
        // Tests the trigger id behind RDX before handing the entity in R8 on;
        // the ten bytes from `sub rsp, 30h` on are already unique.
        gPatterns.ExecuteEntityAudioTrigger                             = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 30 83 3A 00 48 8B F2 74 ? 49 8B D0 E8")));
        gPatterns.C_SoulContext__UpdateCombatDanger                     = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 08 48 89 74 24 18 57 48 83 EC 30 0F 29 74 24 20 48 8B F2 0F 28 F2 48 8B F9 E8")));
        gPatterns.C_SoulBuffInstance__StartActorEffect                  = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 08 57 48 83 EC 60 48 8B 02 48 8B F9 48 8B CA 48 8B DA FF 90 E8 03 00 00 84 C0 75 40 4C 8B 57 10")));
        gPatterns.C_ChatFollowManager__Update                           = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 20 48 8B F9 48 83 C1 70 E8 ? ? ? ?")));
        gPatterns.WHGlobalEnvironment__GetInstance                      = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 48 83 EC 20 8B 0D ? ? ? ? 65 48 8B 04 25 ? ? ? ? BA 18 00 00 00 48 8B 04 C8 8B 04 02 39 05 ? ? ? ? 7F ? 48 8B 05 ? ? ? ? 48 83 C4 20")));
        gPatterns.C_Player__ReviveLocalGlobalsReturn                    = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B F8 48 85 C0 74 52 48 8B 40 38 48 85 C0 74 0D 40 88 B0 FC 01 00 00")));
        gPatterns.C_CombatModule__DestroyCombatActor                    = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 57 48 83 EC 20 48 8B F9 48 8B DA 48 8B 0A 48 85 C9 74 ? 48 8B 01 FF 50 ? 48 8B C8")));
        gPatterns.C_Player__OnPostUpdate                                = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 57 48 83 EC 20 48 8D 99 ? ? ? ? 48 8D 7B")));
        gPatterns.C_DogInteractionManager__UpdateDogHudAvailability     = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 57 48 83 EC 20 48 8B D9 E8 ? ? ? ? 48 8B 48 ? 48 8B 01 FF 90 ? ? ? ? 48 8B CB")));
        gPatterns.C_Player__UpdateMovementAudio                         = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B C4 48 89 58 ? 48 89 70 ? 57 48 83 EC 60 0F 29 70 ? 49 8B F9")));
        gPatterns.C_Human__UpdateMovementAudio =
            reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B C4 48 89 58 ? 55 56 57 41 54 41 55 41 56 41 57 48 8D A8 ? ? ? ? 48 81 EC B0 01 00 00 0F 29 70 ? 0F 29 78 ? 44 0F 29 40 ? 44 0F 29 48 ? 48 8B 05 ? ? ? ? 48 33 C4 48 89 45 ? 44 8B BD")));
        gPatterns.C_Player__IsGod                                       = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 74 24 ? 57 48 83 EC 20 48 8B F1 48 8B 0D ? ? ? ? 48 8B 01 FF 90 ? ? ? ? 33 DB")));
        gPatterns.C_UIApseInventory__UseConsumableItem                  = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 74 24 ? 57 41 56 41 57 48 81 EC E0 00 00 00")));
        gPatterns.C_Player__UseItem                                     = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 6C 24 ? 56 57 41 54 48 83 EC 60")));
        gPatterns.C_MissileWeaponPlayerController__RegisterInputActions = reinterpret_cast<std::uint64_t>(
            hook::get_pattern(KCDC_HIDE("48 8B C4 48 89 58 10 48 89 70 18 48 89 78 20 55 41 56 41 57 48 8D 68 A1 48 81 EC A0 00 00 00 48 8B 05 ? ? ? ? 48 33 C4 48 89 45 37 48 8B F1 E8 ? ? ? ? 48 8B 4E 18 48 8B 90 28 01 00 00 48 8B 89 80 02 00 00 4C 8B 72 60")));
        gPatterns.C_MissileWeaponPlayerController__RefreshMissileActionMap = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B C4 48 89 58 08 48 89 68 10 48 89 70 18 48 89 78 20 41 56 48 83 EC 20 48 8B F9 48 8B 49 18 48 8B 89 80 02 00 00")));
        gPatterns.C_Player__HandleDeath                                    = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 48 83 EC 20 48 8B D9 E8 ? ? ? ? E8 ? ? ? ? 48 8B 48 50 48 85 C9")));
        // Loads the entity's vtable before anything else, then keeps the
        // list in RSI and the entity in R14 for the whole call.
        gPatterns.C_SoulList__GetOrCreateSoul                              = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 74 24 ? 48 89 7C 24 ? 41 56 48 83 EC 20 48 8B 02 48 8B F1 48 8B CA 4C 8B F2")));
        gPatterns.C_Player__InitLocalPlayer                                = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 55 53 56 57 41 54 41 56 41 57 48 8B EC 48 83 EC 50 4C 8B F9 48 8D 15")));
        gPatterns.C_PlayerProfileWHManager__SetSaveLock                    = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 08 48 89 74 24 18 88 54 24 10 57 48 83 EC 30 44 8B 0D")));
        gPatterns.CScreenEffects__PostUpdate                               = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 48 83 EC 50 80 79 2D 00 48 8B D9 0F 85 ? ? ? ? 48 83 C4 50 5B")));
        gPatterns.C_PlayerMaterialEffects__Update                          = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 10 48 89 6C 24 18 56 57 41 56 48 81 EC 00 02 00 00 48 8B 05")));
        gPatterns.C_Player__Init                                           = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 10 48 89 74 24 18 57 48 83 EC 60 48 8B DA 48 8B F1 48 8B D1")));
        gPatterns.CreateCombatPlayer                       = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 10 48 89 74 24 18 57 48 83 EC 20 83 3D ? ? ? ? 00 48 8B F2 48 8B F9 75 05 E8 ? ? ? ? 48 8B 05 ? ? ? ? 48 8D 54 24 30 45 33 C0 B9 70 04 00 00")));
        gPatterns.C_CombatPlayer__Init                     = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 08 48 89 7C 24 18 55 48 8B EC 48 83 EC 30 48 8B F9 E8 ? ? ? ? 84 C0 0F 84")));
        gPatterns.C_CombatPlayer__ShutdownCombatComponents = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 10 48 89 7C 24 18 55 48 8B EC 48 83 EC 30 48 8B 99 10 02 00 00 48 8D 05")));
        gPatterns.C_Player__Release                        = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 10 48 89 6C 24 18 48 89 74 24 20 57 48 81 EC B0 00 00 00 48 8B 05 ? ? ? ? 48 33 C4 48 89 84 24 A0 00 00 00 48 8D A9 08 0B 00 00")));
        gPatterns.C_Player__Update                         = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B C4 48 89 58 08 48 89 68 10 48 89 70 18 57 48 81 EC 10 01 00 00 0F 29 70 E8 48")));
        gPatterns.C_Player__PostUpdate                     = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 48 83 EC 20 48 8B D9 E8 ? ? ? ? 48 8B 8B 88 0B 00 00 48 85 C9")));
        gPatterns.C_Player__RegisterLocalInputActions      = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE(
            "48 8B C4 48 89 58 08 48 89 70 10 48 89 78 18 4C 89 70 20 55 48 8D 68 A1 48 81 EC A0 00 00 00 48 8B 05 ? ? ? ? 48 33 C4 48 89 45 47 48 8B F9 E8 ? ? ? ? 48 8D 4D EF 48 89 7D 0F 48 8B 90 28 01 00 00 48 8B 72 60 48 8D 15 ? ? ? ? 48 8B 06 48 8B 58 48 48 8D 05 ? ? ? ? 48 89 45 07 48 8D 45 07 48 89 45 3F E8 ? ? ? ? 48 8D 15")));
        gPatterns.C_Player__UnregisterLocalInputActions    = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE(
            "48 89 5C 24 18 48 89 7C 24 20 48 89 4C 24 08 55 48 8B EC 48 83 EC 20 E8 ? ? ? ? 48 8D 15 ? ? ? ? 48 8B 88 28 01 00 00 48 8B 79 60 48 8D 4D 18 48 8B 07 48 8B 58 58 E8 ? ? ? ? 48 8D 15 ? ? ? ? 48 8D 4D 10 E8 ? ? ? ? 41 B1 01 4C 8D 45 18 48 8D 55 10 48 8B CF FF D3 48 8D 4D 10 E8 ? ? ? ? 48 8D 4D 18 E8 ? ? ? ? 48 8B 07 48 8D 15 ? ? ? ? 48 8D 4D 18 48 8B 58 58 E8 ? ? ? ? 48 8D 15 ? ? ? ? 48 8D 4D 10 E8 ? ? ? ? 41 B1 01 4C 8D 45 18 48 8D 55 10 48 8B CF FF D3 48 8D 4D 10 E8 ? ? ? ? 48 8D 4D 18 E8 ? ? ? ? 48 8B 07 48 8D 15 ? ? ? ? 48 8D 4D 18 48 8B 58 58 E8 ? ? ? ? 48 8D 15 ? ? ? ? 48 8D 4D 10 E8 ? ? ? ? 41 B1 01 4C 8D 45 18 48 8D 55 10 48 8B CF FF D3 48 8D 4D 10 E8 ? ? ? ? 48 8D 4D 18 E8 ? ? ? ? 48 8B 07 48 8D 15 ? ? ? ? 48 8D 4D 18 48 8B 58 58 E8 ? ? ? ? 48 8D 15 ? ? ? ? 48 8D 4D 10 E8 ? ? ? ? 41 B1 01 4C 8D 45 18 48 8D 55 10 48 8B CF FF D3 48 8D 4D 10 E8 ? ? ? ? 48 8D 4D 18 E8 ? ? ? ? 48 8B 5C 24 40 48 8B 7C 24 48 48 83 C4 20 5D C3 CC")));
        gPatterns.C_PlayerInput__RegisterLocalInputActions = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B C4 48 89 58 10 48 89 70 18 48 89 78 20 55 48 8D 68 A1 48 81 EC A0 00 00 00 48 8B 05 ? ? ? ? 48 33 C4 48 89 45 47 48 8B 41 10 48 8B F1")));
        gPatterns.C_PlayerInput__OnAction                  = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B C4 48 89 58 10 48 89 70 18 F3 0F 11 58 20 55 57 41 56 48 8D 68 A1")));
        gPatterns.C_CameraManager__Update                  = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 48 83 EC 30 0F 29 74 24 20 48 8B D9 0F 28 F1 E8 2E F5 FF FF 48 8D")));
        gPatterns.C_PlayerInput__PreUpdate =
            reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B C4 48 89 58 10 48 89 70 18 48 89 78 20 55 41 56 41 57 48 8D A8 B8 FE FF FF 48 81 EC 30 02 00 00 0F 29 70 D8 48 8B 05 ? ? ? ? 48 33 C4 48 89 85 10 01 00 00 48 8B D9 E8")));
        gPatterns.C_PlayerMovementAction__CalculateState   = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B C4 48 89 58 08 48 89 68 10 48 89 70 18 48 89 78 20 41 54 41 56 41 57 48 81 EC C0 00 00 00 0F 29 70 D8")));
        gPatterns.C_PlayerMovementAction__ResolveActor     = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 48 83 EC 20 48 8B 81 A0 00 00 00 48 8B D9 48 85 C0 74 06 48 83 C4")));
        gPatterns.C_PlayerMovementAction__Construct = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 08 57 48 83 EC 60 33 C0 C7 44 24 30 06 00 00 00 89 44 24 50")));
        gPatterns.C_NPCActor__UpdateCollisionClassMask = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 08 57 48 83 EC 40 48 8B D9 40 8A FA 48 8B 49 38 48 85 C9 74")));
        gPatterns.C_Player__UpdateCollisionClassMask = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 48 83 EC 40 48 8B D9 48 8B 49 38 48 85 C9 74 71 48 8B")));
        gPatterns.C_ActorRotation__Update = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 48 83 EC 20 F2 0F 10 41 58 0F 28 CA 8B 41 60 48 8B D9 F2 0F 11 41")));
        gPatterns.CAIProxy__Update = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B C4 48 89 58 18 55 56 57 41 54 41 55 41 56 41 57 48 8D A8 08 FE FF FF 48 81 EC C0 02 00 00 0F 29 70 B8 0F 29 78 A8 48 8B")));
        gPatterns.C_Blacksmithing__Start                   = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 74 24 ? 55 57 41 56 48 8B EC 48 83 EC 30 8B DA")));
        gPatterns.C_Blacksmithing__AcceptSelectedRecipe             = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 18 48 89 74 24 20 57 48 83 EC 20 48 8B F1 E8 ? ? ? ? 48 8D 54 24 30 48 8B 88 B0 01 00 00 48 8B 01 FF 50 60")));
        gPatterns.C_Blacksmithing__Cancel                           = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 48 83 EC 20 33 D2 48 8B D9 E8 ? ? ? ? 48 8B CB 48 83 C4 20 5B E9 ? ? ? ? CC CC CC 40 53 48 83 EC 20 48 8B CA")));
        gPatterns.C_Blacksmithing__ConsumeRecipeMaterials           = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("88 54 24 10 55 53 56 57 41 54 41 55 41 56 41 57 48 8B EC 48 83 EC 58 40 8A F2 48 8B D9 E8")));
        gPatterns.C_Blacksmithing__FinishWorkpiece                  = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 18 55 56 57 41 56 41 57 48 8D 6C 24 E0 48 81 EC 20 01 00 00 48 8B 05 ? ? ? ? 48 33 C4 48 89 45 10 44 8A FA 48 8B F9 48 8B 51 18")));
        gPatterns.C_BlacksmithingSlotToolAttachment__Attach         = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 18 57 48 81 EC 00 01 00 00 48 8B 05 ? ? ? ? 48 33 C4 48 89 84 24 F0 00 00 00 80 79 14 00")));
        gPatterns.C_BlacksmithingSlotToolAttachment__Detach         = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 10 57 48 81 EC F0 00 00 00 48 8B 05 ? ? ? ? 48 33 C4 48 89 84 24 E0 00 00 00")));
        gPatterns.C_RPGMinigames__FinishBlacksmithing               = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B C4 55 56 57 41 54 41 55 41 56 41 57 48 8D 68 B8 48 81 EC 10 01 00 00 0F 29 70 B8 0F 29 78 A8 44 0F 29 40 98")));
        gPatterns.C_Alchemy__Start                         = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 57 48 83 EC 20 48 8B 01 8B DA")));
        gPatterns.C_ScriptBind_Alchemy__StartAlchemy       = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 08 57 48 83 EC 30 48 8B 49 60 49 8B C0 49 8B D9 C6 44 24 20 00 48 8B FA 41 B1 01 8B D0 41 B0 03")));
        gPatterns.C_UIInventoryBase__TransferItems         = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 55 56 57 48 81 EC 80 00 00 00 48 8B 05 ? ? ? ? 48 33 C4 48 89 44 24 ? 41 8B E9")));
        gPatterns.WH__DropItemsUpToAmount                           = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 74 24 ? 57 48 81 EC 80 00 00 00 48 8B 05 ? ? ? ? 48 33 C4 48 89 44 24 ? 41 8B F0")));
        gPatterns.C_Item__CanMergeStackWith                = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 20 41 0F B6 F1 48 8B DA 48 8B F9 48 3B D1")));
        gPatterns.C_RPGInventory__CalculateItemDecayRaw    = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B C4 48 89 58 ? 48 89 68 ? 48 89 70 ? 57 48 83 EC 60 0F 29 70 ? 49 8B D9")));
        // Unique from 26 bytes on; its first 20 also open 0x180F8AE84, 0x1820CE660 and 0x1834AA85C.
        gPatterns.C_RPGInventory__PrepareItems                      = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 10 48 89 74 24 18 48 89 4C 24 08 55 57 41 54 41 55 41 57 48 8B EC 48 83 EC 50 49 8B 41 08 33 FF 49 8B F1 4D 8B E0")));
        gPatterns.C_Actor__UseItem                         = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 6C 24 18 56 41 56 41 57 48 83 EC 40 45 8B F9 49")));
        gPatterns.C_RPGInventory__ApplyFoodConsumption              = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE(
            "48 8B C4 48 89 58 ? 55 56 57 41 54 41 55 41 56 41 57 48 8D A8 ? ? ? ? 48 81 EC 90 01 00 00 0F 29 70 ? 0F 29 78 ? 44 0F 29 40 ? 44 0F 29 48 ? 44 0F 29 90 ? ? ? ? 44 0F 29 98 ? ? ? ? 44 0F 29 A0 ? ? ? ? 44 0F 29 A8 ? ? ? ? 44 0F 29 B0 ? ? ? ? 44 0F 29 B8 ? ? ? ? 48 8B 05 ? ? ? ? 48 33 C4 48 89 45 ? 49 8B D8")));
        gPatterns.C_EquippableItemRuntimeData__RemoveOnEquipBuffRaw = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B C4 48 89 58 08 48 89 70 10 57 48 83 EC 50 48 83 79 48 00 48 8B F2 0F 28 02 48 8B F9 66 0F 7F 40 C8 74 0D 48 83 C1 70 48 8D 50 C8")));
        gPatterns.C_EquippableItemRuntimeData__AddOnEquipBuffRaw    = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 18 57 48 83 EC 30 48 8B 41 48 48 8B FA 48 8B D9 48 85 C0 74 19 F6 40 60 01 74 13 0F 10")));
        gPatterns.C_ActorActionShootingMain__TryDischargeRaw        = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B C4 48 89 58 08 48 89 68 10 48 89 70 18 57 48 83 EC 50 48 8B F1 0F 29 70 E8 48 8B 49 58")));
        gPatterns.C_Item__SetHealthCore                             = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 57 48 83 EC 40 F3 0F 10 41 ? 49 8B F8")));
        gPatterns.C_RPGInventory__ApplyRepairKit                    = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B C4 55 53 56 57 41 54 41 56 41 57 48 8D 68 ? 48 81 EC A0 00 00 00")));
        gPatterns.C_Item__SetQualityCore                            = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 74 24 ? 57 48 83 EC 30 0F 29 74 24 ? 49 8B F8 8B F2")));
        gPatterns.C_ItemHolder__AddItem                             = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 55 56 57 41 56 41 57 48 81 EC A0 00 00 00 48 8B 05 ? ? ? ? 48 33 C4 48 89 84 24 90 00 00 00 33 C0 45 8A F8 89 44 24 30")));
        gPatterns.C_BodyPartData__Construct                         = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 48 83 EC 20 48 8B D9 C7 01 FF FF FF FF 48 83 C1 08 48 8D 15")));
        // Unique block at 0x181935785 belongs to entry 0x18193573C.
        constexpr std::ptrdiff_t kAlchemyFinishPatternToEntry    = -0x49;
        gPatterns.AlchemyBackCallback                            = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 48 83 EC 20 48 8B 59 08 80 BB 00 03 00 00 02 75 53 48 8D 4B 78 BA 01 00 00 00 E8 ? ? ? ? 84 C0")));
        gPatterns.C_AlchemyResource__BaseConstructor             = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 10 48 89 7C 24 18 55 48 8D 6C 24 C0 48 81 EC 40 01 00 00")));
        gPatterns.C_AlchemyResource__Constructor                 = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 08 57 48 83 EC 20 48 8D 05 ? ? ? ? 48 8B DA 48 89 01 48 8B F9 E8 ? ? ? ? E8 ? ? ? ? 48 8B CB")));
        gPatterns.C_Alchemy__CreateReturnAction                  = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 08 55 48 8B EC 48 83 EC 20 33 C0 48 8B DA 45 85 C0")));
        gPatterns.C_Alchemy__Destructor                          = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 08 57 48 83 EC 30 80 B9 00 03 00 00 08 48 8D 05 ? ? ? ? 48 89 01 48 8B D9 48 8D 05 ? ? ? ? 48 89 41 08")));
        gPatterns.C_Alchemy__EvaluateContainer                   = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 18 55 56 57 48 8D 6C 24 B9 48 81 EC C0 00 00 00 48 8B 05 ? ? ? ? 48 33 C4 48 89 45 37 48 63 FA 48 8B D9 48 C7 45 C7 00 00")));
        gPatterns.C_Alchemy__OnEntryActionFinished               = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 48 83 EC 20 80 B9 00 03 00 00 02 48 8B D9 74 07 C6 81 00 03 00 00 02")));
        gPatterns.C_Alchemy__QueueAction                         = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 08 57 48 83 EC 20 48 83 3A 00 48 8B DA 48 8B F9 74 22 48 8D")));
        gPatterns.C_Alchemy__Reset                               = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 74 24 ? 57 48 83 EC 20 48 8B D9 48 8B 49 ? 48 8B 01 FF 90 ? ? ? ? 48 85 C0")));
        gPatterns.C_Alchemy__SelectObject                        = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 20 89 54 24 10 57 48 83 EC 20 80 B9 00 03 00 00 02 8B FA 48")));
        gPatterns.C_Alchemy__RequestCleanup                      = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 57 48 83 EC 20 80 B9 ? ? ? ? ? 48 8B D9 74 ? 48 83 C1 78")));
        gPatterns.C_Alchemy__Shutdown                            = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 18 48 89 74 24 20 55 57 41 54 41 56 41 57 48 8B EC 48 83 EC 30 4C 8B F9")));
        gPatterns.C_Alchemy__Update                              = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 57 48 83 EC 30 40 8A B9")));
        gPatterns.C_RPGMinigames__IsAlchemyRecipeRangeKnown      = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57 48 83 EC 20 48 8B 02 48 8B DA 48 8B E9 33 D2 48 8B CB 41 8B F9 41 8B F0")));
        gPatterns.C_RPGMinigames__IsAlchemyRecipeStepKnown       = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 6C 24 ? 56 57 41 56 48 83 EC 20 48 8B 02 48 8B DA")));
        gPatterns.C_RPGMinigames__OnAlchemyFinished              = reinterpret_cast<std::uint64_t>(hook::get_pattern(
            KCDC_HIDE(
                "48 8B 03 48 8B CB FF 10 48 8B 54 24 30 4C 8B 00 E8 ? ? ? ? 48 8B 07 48 8B CF 48 8B 54 24 30 FF 10 48 8D 4C 24 30 E8 ? ? ? ? 48 8B 5C 24 40 48 83 C4 20 5F C3 48 89 5C 24 10 48 89 4C 24 08 57 48 83 EC 20 83 3D ? ? ? ? 00 49 8B F8 48 8B DA 0F 84 ? ? ? ? 45 33 C0 48 8D 54 24 30 41 8D 48 10 FF 15 ? ? ? ? 8B 4C 24 30 F0 01 0D ? ? ? ? F0 83 05 ? ? ? ? 10 F0 FF 05 ? ? ? ? 48 85 C0 74 1B 48 8D 0D ? ? ? ? 48 89 08 48 8B 0F 48 89 48 08 48 8D 0D ? ? ? ? 48 89 08 4C 8D 44 24 30 48 89 44 24 30 48 8B D3 E8 ? ? ? ? 48 8B 4C 24 30 48 85 C9 0F 85 ? ? ? ? 48 8B C3 48 8B 5C 24 38 48 83 C4 20 5F C3 EB D0 CC CC 48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 20 49 8B 00 48 8B F1 49 8B C8 49 8B D8 48 8B FA"),
            kAlchemyFinishPatternToEntry));
        gPatterns.C_RPGMinigames__OnAlchemyStarted               = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B C4 48 89 70 08 48 89 78 18 55 48 8D 68 A1 48 81 EC 90 00 00 00")));
        gPatterns.C_ReturnIngredientAction__ReturnHeldIngredient = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B C4 48 89 58 10 48 89 70 18 48 89 78 20 41 56 48 83 EC 30 48 8B D9 48 8B 49 58 48 63 B9 A0")));
        gPatterns.C_TakeIngredientAction__OnAnimationEvent       = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 08 57 48 83 EC 20 8B 41 68 49 8B F8 48 8B D9 41 39 40 1C 75 1E 48 8B 41 58 8B 90")));
        gPatterns.C_UseBellowsAction__Pump                       = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 10 48 89 7C 24 18 55 48 8B EC 48 81 EC 80 00 00 00 48 8B 05 ? ? ? ? 48 33 C4 48 89 45 F0 83 79 78 01 48 8B F9 0F 85 62 01 00 00")));
        gPatterns.C_UseBellowsAction__ReturnBellows              = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 10 48 89 7C 24 18 55 48 8B EC 48 81 EC 80 00 00 00 48 8B 05 ? ? ? ? 48 33 C4 48 89 45 F8 4C 8B 49 58 48 8B F9 C7 41 78 03 00 00")));
        gPatterns.C_UseMortarAction__StartAction                 = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B C4 48 89 58 10 48 89 70 18 48 89 78 20 55 48 8D 68 A1 48 81 EC 90 00 00 00 48 8B 05 ? ? ? ? 48 33 C4 48 89 45 47 48 8B D9 48 8B 49 58")));
        gPatterns.C_UseResourceAction__OnAnimationEvent          = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 10 48 89 74 24 18 57 48 83 EC 30 41 8B 40 1C 48 8B D9 3B 41 68 74 1E")));
        gPatterns.C_UseRetortAction__FinishDistillation          = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 10 48 89 74 24 18 48 89 7C 24 20 55 48 8B EC 48 81 EC 80 00 00 00 48 8B 05 ? ? ? ? 48 33 C4 48 89 45 F0 4C 8B 49 58 48 8B F1 C7")));
        gPatterns.C_UseRetortAction__GetRequiredBellowsCycles    = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 48 83 EC 20 48 8B 49 58 48 8D 54 24 38 E8 ? ? ? ? 48 8B 4C 24 38 4C 8D 44 24 30")));
        gPatterns.C_UseRetortAction__Pump                        = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 10 48 89 7C 24 18 55 48 8B EC 48 81 EC 80 00 00 00 48 8B 05 ? ? ? ? 48 33 C4 48 89 45 F8 83 79 70 01 48 8B F9 0F 85 47 01 00 00")));

        // The destructor is folded. Resolve TakeDamage's unique call site.
        gPatterns.C_BodyPartData__Destroy = hook::get_address<std::uint64_t>(hook::get_pattern(KCDC_HIDE("FF 90 ? ? ? ? 48 8D 4C 24 ? E8 ? ? ? ? 48 8B 8C 24 ? ? ? ? 48 33 CC E8 ? ? ? ? 4C 8D 9C 24 ? ? ? ? 41 0F 28 73")), 12, 16);
        // CRenderer::StartRenderIntroMovies allocates its 0x28-byte
        // CIntroMovieRenderer and immediately null-checks the result; that
        // sized allocation wedged into the tiny prologue pins the entry point.
        gPatterns.CD3D9Renderer__StartRenderIntroMovies = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 48 83 EC 20 48 8B D9 B9 28 00 00 00 E8 ? ? ? ? 48 8B C8 33 C0 48 85 C9 74 ? E8")));

        // A 17-byte thunk that loads gEnv->pEntitySystem and tail-calls
        // CEntitySystem::GetEntity (+0x70). The field it reads is gEnv+0xA0,
        // per CEngineModule_EntitySystem::Initialize.
        gPatterns.SSystemGlobalEnvironment__Base = hook::get_address<std::uint64_t>(hook::get_pattern(KCDC_HIDE("8B 51 28 48 8B 0D ? ? ? ? 48 8B 01 48 FF 60 70")), 6, 10) - 0xA0;

        gPatterns.ExecuteGlobalAudioTrigger180558908 = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 74 24 ? 48 89 7C 24 ? 55 48 8B EC 48 81 EC 80 00 00 00 48 8B FA 45 84 C0")));

        // C_UIFlashBase setup loads the registry, selects its lookup slot,
        // then obtains the page's element name. Unique in the verified DLL.
        gPatterns.IFlashUI__Instance                   = hook::get_address<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B 35 ? ? ? ? 48 8B 06 48 8B 58 70 48 8B 01 FF 50 58")), 3, 7);
        gPatterns.CCryPak__OpenPackWithBindingRoot     = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 55 56 57 41 56 41 57 B8 48 10 00 00 E8 ? ? ? ? 48 2B E0 48 8B 05 ? ? ? ? 48 33 C4")));
        // The perk screen's state query: the soul's perk list off I_Soul +0x390, then the row's GUID loaded whole.
        gPatterns.PerkScreenState = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 30 49 8B 00 49 8B C8 48 8B DA FF 90 90 03 00 00 0F 10 03")));
#if defined(KCDC_AGENT_TESTING)
        gPatterns.CXmlNode__SaveToFile = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 74 24 ? 48 89 7C 24 ? 55 41 56 41 57 48 8B EC 48 83 EC 40 48 8B F9")));
        // The debug-level check on +0xA0 and `movzx esi, dl` keeping the flag pin the prologue.
        gPatterns.CHardwareMouse__ConfineCursor = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 18 48 89 74 24 20 57 48 83 EC 40 48 8B 05 ? ? ? ? 48 33 C4 48 89 44 24 30 83 B9 A0 00 00 00 00 48 8B D9 0F B6 F2")));
#endif
        gPatterns.CryGFxFSCommandHandler__Callback     = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 08 57 48 83 EC 20 48 8B 02 48 8B CA 49 8B D9 49 8B F8 FF 90 98 01 00 00")));
        gPatterns.CFlashUIElement__AddEventListener    = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 83 EC 28 48 81 C1 D0 01 00 00 4C 8B C2 48 85 D2")));
        gPatterns.CFlashUIElement__RemoveEventListener = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 48 83 EC 20 4C 8B 81 D8 01 00 00 48 8B D9 48 8B 81 D0 01 00 00")));
        gPatterns.CFlashUIElement__Render = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 48 83 EC 20 48 83 79 58 00 48 8B D9 74 13 E8 ? ? ? ? 84 C0 75 0A 48 8B 4B 58 48 8B 01 FF 50 68")));
        gPatterns.C_UIHudMask__Instance = hook::get_address<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 48 83 EC 20 48 8B 1D ? ? ? ? 48 85 DB 74 20 48 8B 01 FF 50 78 48 8D 54 24 38 48 8D 48 0A")), 9, 13);

        // CSystem's loading-time service pump, ISystem vtable slot +0x30. It
        // guards on the main thread id, throttles its own service work to
        // 20ms, and is reached from the XML, CGF, texture, material and
        // 3D-engine loading paths -- which is what gives it coverage of a
        // whole level load rather than of one phase.
        gPatterns.CSystem__SynchronousLoadingTick = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 08 55 56 57 48 83 EC 20 48 8B F1 FF 15 ? ? ? ? 39 05 ? ? ? ?")));

        // The trailing `lea rsi, [rcx-58h]`, which takes the owning menu out
        // of the sub-object it is called on, is what pins this prologue.
        gPatterns.C_UIMenu__EnterState = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 74 24 ? 55 57 41 56 48 8B EC 48 83 EC 50 48 8D 71")));

        // ShowRootMain frames over a 0x70 reserve and immediately moves the
        // RootMain page id into DL for the SetPage call; that literal ahead of
        // the first call is what separates it from every sibling page.
        gPatterns.C_UIMenu__ShowRootMain = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 55 53 56 57 41 56 48 8B EC 48 83 EC 70 B2 01")));

        // The first-time overlay gate opens by fetching the
        // wh_ui_showFirstTimeOverlays CVar through gEnv->pConsole; the console
        // load plus the GetCVar call at +0xB8 make the prologue unique.
        gPatterns.C_UIMenu__ShowFirstTimeOverlays = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 6C 24 ? 48 89 74 24 ? 57 41 56 41 57 48 83 EC 40 4C 8B F1 48 8D 15 ? ? ? ? 48 8B 0D ? ? ? ? 48 8B 01 FF 90 B8 00 00 00")));

        // CCryAction::CompleteInit's prologue runs four back-to-back
        // single-argument subsystem calls on `this`, which is unique in
        // WHGame.dll. It is CCryAction rather than CGame, but it is the
        // CompleteInit that drives the game's own post-init sequence.
        gPatterns.CGame__CompleteInit = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 57 48 83 EC 70 48 8B D9 E8 ? ? ? ? 48 8B CB E8 ? ? ? ? 48 8B CB E8 ? ? ? ? 48 8B CB E8")));

        // Unique signatures for the native action helpers.
        gPatterns.CryActionAllocate         = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 48 83 EC 20 83 3D ? ? ? ? 00 48 8B D9 75 05 E8 ? ? ? ? 48 8B 05 ? ? ? ? 48 8D 54 24 38 45 33 C0 FF D0")));
        gPatterns.TAction__Construct        = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("45 33 D2 C7 41 14 00 00 80 BF 4C 89 51 08 48 8D 05 ? ? ? ? 48 89 01 8B 44 24 30 89 41 18 8B 44 24 28 89 41 2C")));
        gPatterns.IAction__Release          = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 83 EC 28 83 C8 FF F0 0F C1 41 58 83 E8 01 7F 09 48 8B 01 FF 90 B8 00 00 00 48 83 C4 28 C3")));
        gPatterns.IAction__ForceFinish      = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 20 83 61 2C FB 33 DB 48 8B 81 88 00 00 00 48 8B F9 C7 41 28 04 00 00 00")));
        gPatterns.IAction__Stop             = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 20 81 49 2C 01 20 00 00 33 DB 48 8B 81 88 00 00 00 48 8B F9")));
        gPatterns.IAction__SetFragment      = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 48 83 EC 20 89 51 ? 48 8B D9 41 0F 10 00")));
        gPatterns.IAction__Enter            = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("83 49 2C 20 C3")));
        gPatterns.IAction__Exit             = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("83 61 2C DF C7 41 28 00 00 00 00 C3")));
        gPatterns.IAction__GetTargetPos     = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 08 57 48 83 EC 20 49 8B F8 4C 8B D1 4C 8D 05 ? ? ? ? 83 CA FF B0 54 BB 01 00 00 00 3C 5A")));
        gPatterns.CActionController__Update = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B C4 48 89 58 ? 48 89 68 ? 56 57 41 54 41 56 41 57 48 83 EC 60 F6 81")));
        gPatterns.CActionController__Queue  = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 57 48 83 EC 20 48 8B DA F3 0F 11 52")));
        gPatterns.CActionController__Flush  = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 6C 24 ? 56 57 41 56 48 83 EC 20 F6 81")));
        // CProceduralContextAim::Update (0x180C4F744) is byte for byte the
        // same but for the offset of the call to its own smoothing helper, and
        // that offset differs between storefront builds, so no literal tells
        // the two apart. The class's constructor does: it allocates 0x68 bytes
        // where Aim's allocates less, then stores the vftable, whose slot 5 is
        // Update.
        {
            auto *lookConstructor                    = reinterpret_cast<char *>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 08 48 89 74 24 20 55 57 41 56 48 8B EC 48 83 EC 40 45 33 F6 48 8B F2 44 89 75 28 44 39 35 ? ? ? ? 0F 84 ? ? ? ? 41 B8 08 00 00 00 48 8D 55 28 41 8D 48 60 FF 15 ? ? ? ? 8B 4D 28 48 8B D8 F0 01 0D ? ? ? ? F0 83 05 ? ? ? ? 68 F0 FF 05 ? ? ? ? 44 39 35 ? ? ? ? 4C 89 70 08 4C 89 70 10 48 8D 05 ? ? ? ?")));
            const auto *lookVftable                  = reinterpret_cast<const std::uint64_t *>(hook::get_address<std::uint64_t>(lookConstructor + 0x6B, 3, 7));
            gPatterns.CProceduralContextLook__Update = lookVftable[5];
        }
        // Both XGenAI lookups are template instances with byte-identical
        // siblings, so each is resolved through the one script bind that uses
        // it: `C_ScriptBindXGenAIModule::GetEntityIdByWUID` loads the AI
        // object manager and calls its WUID lookup, `GetMyWUID` loads the
        // EntityGUID map and calls its lookup.
        {
            auto *entityIdByWuid                            = reinterpret_cast<char *>(hook::get_pattern(KCDC_HIDE("40 53 48 83 EC 20 48 8B 0D ? ? ? ? 48 8B DA 48 8D 54 24 40 4C 89 44 24 40 E8 ? ? ? ? 48 85 C0 74 29 48 8B 48 18")));
            gPatterns.C_AIObjectManager__Instance           = hook::get_address<std::uint64_t>(entityIdByWuid + 0x06, 3, 7);
            gPatterns.C_AIObjectManager__FindAIObjectByWUID = hook::get_address<std::uint64_t>(entityIdByWuid + 0x1A, 1, 5);
            auto *myWuid                                    = reinterpret_cast<char *>(hook::get_pattern(KCDC_HIDE("48 8B 03 48 8B CB 8B 54 24 48 FF 50 70 48 85 C0 0F 84 ? ? ? ? 48 8B 10 48 8B C8 48 8B 1D ? ? ? ? FF 52 10 48 8B D0 48 8B CB E8")));
            gPatterns.xgenaimodule__EntityGuidWuidMap       = hook::get_address<std::uint64_t>(myWuid + 0x1C, 3, 7);
            gPatterns.xgenaimodule__FindWUIDByEntityGuid    = hook::get_address<std::uint64_t>(myWuid + 0x2C, 1, 5);
            // XGenAIModule.GetBrainVariable loads the registry, then looks the WUID up.
            auto *brainVariable                              = reinterpret_cast<char *>(hook::get_pattern(KCDC_HIDE("48 8B C4 48 89 58 08 48 89 68 10 48 89 70 20 57 48 83 EC 40 48 8B 0D ? ? ? ? 48 8B F2 48 8D 50 18 4C 89 40 18 49 8B E9 E8")));
            gPatterns.xgenaimodule__IntelligentObjectManager = hook::get_address<std::uint64_t>(brainVariable + 0x14, 3, 7);
            gPatterns.C_IntelligentObjectManager__FindObject = hook::get_address<std::uint64_t>(brainVariable + 0x29, 1, 5);
        }
        // The carry link writer makes both of the other end's lookups as its
        // first two calls, so they are read out of it: the pair is then the
        // very pair the engine asks, rather than two look-alikes.
        {
            auto *writeCarryLinks = reinterpret_cast<char *>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 10 48 89 6C 24 18 48 89 4C 24 08 56 57 41 56 48 83 EC 30 48 8B 0D ? ? ? ? 45 8A F0 48 8B DA 48 8B 01 FF 10 48 8B CB 48 8B F8 E8 ? ? ? ? 48 8D 54 24 50 48 89 44 24 50 E8")));
            gPatterns.xgenaimodule__WriteCarryLinks      = reinterpret_cast<std::uint64_t>(writeCarryLinks);
            gPatterns.xgenaimodule__GetActorWuid         = hook::get_address<std::uint64_t>(writeCarryLinks + 0x2F, 1, 5);
            gPatterns.xgenaimodule__FindLinkableAIObject = hook::get_address<std::uint64_t>(writeCarryLinks + 0x3E, 1, 5);
            gPatterns.xgenaimodule__WriteStealthAttackLinks =
                reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 08 48 89 6C 24 10 56 57 41 56 48 83 EC 30 48 8B 0D ? ? ? ? 41 8A F1 49 8B D8 48 8B 01 FF 10 48 8B 0B 48 8B F8 48 8B 2D ? ? ? ? 48 81 C5 40 01 00 00 48 8B 51 38")));
        }
        // Both by their own prologues, unique with margin: the first 35 and
        // 22 bytes already are. `SendMessageToWuid`'s queued twin
        // (0x1804C96F8) opens differently.
        gPatterns.xgenaimodule__SendMessageToWuid =
            reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B C4 48 89 58 10 48 89 68 18 48 89 70 20 48 89 48 08 57 48 83 EC 30 48 8B CA 49 8B F1 49 8B E8 E8 ? ? ? ? 48 89 44 24 40 E8 ? ? ? ? 4C 8B CE 48 8D 54 24 40 4C 8B C5")));
        gPatterns.xgenaimodule__StopNpcMovement = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 20 57 48 83 EC 60 41 8A D8 48 C7 44 24 58 00 00 00 00 45 8A C8 48 8B F9 4C 8D 44 24 20 E8 ? ? ? ? 44 8A CB 41 B0 01 8B D0 48 8B CF")));
        // The one constructor that writes it: the vtable store right after
        // two members the others do not initialise at those offsets.
        gPatterns.C_CallbackAction__VTable            = hook::get_address<std::uint64_t>(reinterpret_cast<char *>(hook::get_pattern(KCDC_HIDE("F3 0F 7F 43 70 48 89 83 88 00 00 00 48 8D 05 ? ? ? ? 48 89 03 48 89 8B C8 00 00 00"))) + 0x0C, 3, 7);
        gPatterns.C_Signal__EmitBool                  = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57 41 56 41 57 48 83 EC 20 33 ED 48 8D 41 08 48 8B D9 40 8A F2")));
        gPatterns.C_AlchemyAnimAction__VTable         = hook::get_address<std::uint64_t>(reinterpret_cast<char *>(hook::get_pattern(KCDC_HIDE("48 8D 05 ? ? ? ? 48 89 9F 68 02 00 00 48 8B D3 48 89 07 48 8B CF E8"))), 3, 7);
        // Each sharpening action's constructor writes its vtable after the
        // shared base constructor; the moves around each lea tell them apart.
        gPatterns.C_HumanSharpeningInAction__VTable  = hook::get_address<std::uint64_t>(reinterpret_cast<char *>(hook::get_pattern(KCDC_HIDE("48 8D 05 ? ? ? ? 48 8D 54 24 40 48 89 06 E8 ? ? ? ? 4C 8B C0 48 8B CE E8"))), 3, 7);
        gPatterns.C_HumanSharpeningAction__VTable    = hook::get_address<std::uint64_t>(reinterpret_cast<char *>(hook::get_pattern(KCDC_HIDE("48 8D 05 ? ? ? ? 41 C6 86 A8 00 00 00 00 49 89 06"))), 3, 7);
        gPatterns.C_HumanSharpeningOutAction__VTable = hook::get_address<std::uint64_t>(reinterpret_cast<char *>(hook::get_pattern(KCDC_HIDE("48 8D 05 ? ? ? ? 48 89 06 E8 ? ? ? ? 4C 8D 5C 24 60 48 8B C6 49 8B 5B 18 49 8B 73 20"))), 3, 7);
        gPatterns.CActionController__GetFragID        = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B 41 20 48 8B 08 48 8B 49 18 E9 ? ? ? ? 40 53 48 83 EC 20 48 89 0A")));
        gPatterns.CActionController__GetTagDefinition = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("83 FA FF 74 10 48 8B 41 20 48 8B 08 48 8B 49 18 E9 ? ? ? ? 33 C0 C3")));
        gPatterns.CTagDefinition__FindCRC             = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B 49 08 33 C0 39 41 FC 7E 1D 4C 63 49 FC 44 8B C0 48 83 C1 10 39 11 74 11 FF C0 49 FF C0 48 83 C1 20 4D 3B C1 7C EE")));
        gPatterns.CTagDefinition__Set                 = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("45 8A D1 4C 8B D9 45 85 C0 78 7C 48 8B 49 08 44 3B 41 FC 7D 72 49 8B 43 18 4D 63 C0 4E 8D 0C 40 49 C1 E0 05")));
        gPatterns.CTagDefinition__SetGroupImpl        = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("45 85 C0 78 ? 48 8B 41 ? 44 3B 40 ? 7D ? 48 8B 41")));

        // The lea five bytes into `CScriptBind_Entity::SetAttachmentCGF`'s
        // inline construction is `CCGFAttachment`'s vtable. The two zeroed
        // members and the call assigning the loaded mesh are spelled out
        // because four constructions of this shape share the lea.
        gPatterns.CCGFAttachment__VTable = hook::get_address<std::uint64_t>(reinterpret_cast<char *>(hook::get_pattern(KCDC_HIDE("48 85 FF 74 1A 48 8D 05 ? ? ? ? 48 C7 47 08 00 00 00 00 48 89 07 48 C7 47 10 00 00 00 00 48 8D 4F 08 48 8B D5 E8"))) + 5, 3, 7);
        gPatterns.CEntityAttachment__Construct       = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 08 48 89 74 24 10 48 89 7C 24 18 41 56 48 83 EC 20 4C 8B F1 B8 00 00 80 3F 33 C9 41 8A D9 49 8B F0 48 8B FA")));
        gPatterns.C_ProceduralClipAnimEvent__OnEnter = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 08 57 48 83 EC 30 49 83 79 08 00 48 8D 15 ? ? ? ? 48 8B F9 0F 29 74 24 20 49 0F 45 51 08 0F 28 F2")));
        // CCryAction::CCryAction publishes `this` to the global framework
        // holder. Resolve the RIP-relative destination of that store.
        gPatterns.IGameFramework__Instance = hook::get_address<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 0D ? ? ? ? 88 81")), 3, 7);

        // The environment singleton getter returns the RIP-relative global
        // holder immediately before its shared epilogue.
        gPatterns.WHGlobalEnvironment__Instance = hook::get_address<std::uint64_t>(hook::get_pattern(KCDC_HIDE("7F ? 48 8B 05 ? ? ? ? 48 83 C4 20 5B C3")), 5, 9);

        // Looks up a uint8 UI event in the sorted trigger table at +0x18 and
        // sends the resolved trigger through the game's UI audio object. This
        // signature is unique in WHGame.dll.
        gPatterns.C_UISoundController180559ED0__Play = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 57 48 83 EC 20 4C 8B 51 ? 44 8A DA")));

        // The ray tracer every ground and line-of-sight query goes through.
        // Its prologue is shared byte for byte with a near-identical sibling,
        // so the pattern runs past the first call to where the two diverge.
        gPatterns.CPhysicalWorld__RayWorldIntersection =
            reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B C4 48 89 58 ? 48 89 70 ? 48 89 78 ? 4C 89 70 ? 55 48 8D 68 ? 48 81 EC 90 00 00 00 48 8B DA 49 8B F8 33 D2 4C 8B F1 48 8D 4D ? 41 8B F1 44 8D 42 ? E8 ? ? ? ? 8B 43 ? 48 8D 55 ? F2 0F 10 03 41 B9")));

        // The 1.0f in EAX reused for three of the four scale fields, wedged
        // between the `mov rbx, rcx` and the 0x100 word store, is unique.
        gPatterns.SpawnParams__Construct = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 48 83 EC 20 48 8B D9 B8 00 00 80 3F 33 C9 89 0B 66 C7 43 04 00 01 C6 43 06 01")));

        // The `lea` of the arena's +0x28 offset wedged into a four-instruction
        // prologue is unique in WHGame.dll.
        gPatterns.SEntityProximityQuery__Construct = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 48 83 EC 20 48 8D 41 ? 48 C7 44 24")));

        // Pinned by its pair of tests: the inline arena's in-use byte 4096
        // bytes in, and otherwise a tail call to the game's deallocator.
        gPatterns.SEntityProximityQuery__FreeResultBuffer = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B 01 4C 8B CA 48 85 C0 74 ? 48 3B D0 75 ? C6 80 ? ? ? ? ? C3 4A 8D 14 C5 ? ? ? ? 49 8B C9 E9 ? ? ? ? 48 8B C4")));

        // CTimeOfDay::SetTime reads the global 3D-engine pointer immediately
        // before updating the engine's sun direction. The surrounding vector
        // operations make this RIP-relative load unique in WHGame.dll.
        gPatterns.I3DEngine__Instance = hook::get_address<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B 0D ? ? ? ? 0F 28 DA F3 0F 59 C2 F3 44 0F 59 C5 48 8B 01")), 3, 7);

        // C3DEngine::GetStatInstGroup (0x183493338) loads the object manager
        // and bounds the group id against the table at +0x10/+0x18.
        gPatterns.CObjManager__Instance = hook::get_address<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B 05 ? ? ? ? 4C 63 CA 4C 8B 40 10 48 8B 48 18")), 3, 7);

        // Takes the C_InventorySoul embedded at soul +0x198, asks it for its
        // inventory through its first virtual and builds the inventory and
        // equipment manager only when that answers null. The `lea` of the
        // fixed +0x198 offset followed by that virtual call pins the prologue.
        gPatterns.C_Soul__EnsureInventoryRuntime = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 57 48 83 EC 20 48 8D 99 ? ? ? ? 48 8B F9 48 8B 03 48 8B CB FF 10 48 85 C0 75")));

        // Loads the entity from actor +0x38, clears the clothing flag at
        // +0x9B1 and sets it again only when the entity answers both of its
        // tests. The immediate store of 0 into +0x9B1 pins it.
        gPatterns.C_Actor__RefreshClothingSupport = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 57 48 83 EC 20 48 8B 79 38 48 8B D9 C6 81 B1 09 00 00 00 48 85 FF 74")));

        // Allocates 0x18 bytes for the handler, constructs it against the
        // actor and stores it at +0x678. The allocation size and that store
        // are what separate it from every other allocate-and-construct helper
        // sharing this prologue.
        gPatterns.C_Actor__CreateEquipmentHandler =
            reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 48 83 EC 20 83 3D ? ? ? ? 00 48 8B D9 75 ? E8 ? ? ? ? 48 8B 05 ? ? ? ? 48 8D 54 24 ? 45 33 C0 41 8D 48 18 FF D0 8B 4C 24 ? F0 01 0D ? ? ? ? F0 83 05 ? ? ? ? 18 F0 FF 05 ? ? ? ? "
                                                                        "48 85 C0 74 ? 48 8B D3 48 8B C8 E8 ? ? ? ? 48 8B 8B 78 02 00 00 48 89 83 78 06 00 00")));

        // Bounds-checks the group, tests the group's bits in the state and
        // then walks the group's own tag-id list looking for the one that is
        // set. The wide prologue with the two register moves pins it.
        gPatterns.CTagDefinition__GetGroup = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 6C 24 ? 48 89 74 24 ? 57 41 54 41 55 41 56 41 57 48 83 EC 20 4C 8B EA 48 8B E9 45 85 C0")));

        // Forwards the tag state and its 20-byte size to the tag definition's
        // group setter. The immediate 0x14 alongside the state pointer pins it.
        gPatterns.CTagState__SetGroup = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 83 EC 38 48 8D 41 08 C7 44 24 ? 14 00 00 00 48 8B 09 45 8B C8")));

        // Sets the actor's MoveDir tag from the angle between its movement and
        // its facing, and its MoveSpeed tag from a speed table. The wide
        // prologue with the saved xmm register pins it.
        gPatterns.C_Actor__UpdateMannequinTags = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B C4 48 89 58 ? 48 89 50 ? 55 56 57 41 54 41 55 41 56 41 57 48 8D 68 ? 48 81 EC 90 00 00 00 0F 29 70 ? 48 8B D9")));

        // Reads the owner actor through the action, tests the locomotion gate
        // and only then compares the squared velocity against 0.01. The
        // frame-pointer prologue with three saved xmm registers pins it.
        gPatterns.CAnimActionAIMovement__SelectState = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B C4 48 89 58 ? 48 89 68 ? 56 57 41 56 48 81 EC 80 00 00 00 0F 29 70 ? 0F 29 78 ? 44 0F 29 40")));

        // The saved-register prologue followed by a read of the holder's
        // action through RCX and the params kept in R9.
        gPatterns.C_Actor__EnsureLocomotionAction = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 6C 24 ? 48 89 74 24 ? 57 48 83 EC 30 48 8B 01 49 8B F1")));

        // Unique at 0x18083B82C. The native caller passes this, skeleton,
        // const QuatT& and float frame time in RCX/RDX/R8/XMM3 respectively.
        gPatterns.CAnimatedCharacter__SetDesiredLocalLocation = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B C4 55 53 56 57 41 54 41 55 41 56 41 57 48 8D 68 ? 48 81 EC D8 00 00 00 0F 29 70 ? 4C 8B EA")));

        // Unique at 0x1814827AC. The activation mode arrives in R9D and the
        // handler's own prologue moves it into EDI before taking this.
        gPatterns.C_PlayerInput__OnSprint = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 74 24 ? 57 48 83 EC 20 41 8B F9 48 8B D9")));

        // Unique at 0x181482A08, on the opening test of the sprint bit.
        gPatterns.C_PlayerInput__StopSprint         = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 48 83 EC 20 F6 41 18 04 48 8B D9 74 ? F3 0F 10 81 ? ? ? ? 0F 2E 05")));
        gPatterns.C_PlayerInput__SetSprintRequested = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 48 83 EC 20 8B 41 18 48 8B D9 84 D2 74 0A A8 40 74 18 48 83 C4 20 5B C3 83 E0 BF C7 81 D4 00 00 00 00")));
        gPatterns.C_PlayerInput__StartSprint        = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 30 0F 29 74 24 20 48 8B D9 E8 ? ? ? ? F6 43 18 04 48 8B 70 20")));
        gPatterns.C_PlayerInput__OnXiMoveX          = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 83 EC 28 F3 0F 10 4C 24 50 4C 8D 49 70 4C 8D 81 94 02 00 00 E8")));
        gPatterns.C_PlayerInput__OnXiMoveY          = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 83 EC 28 F3 0F 10 4C 24 50 4C 8D 49 74 4C 8D 81 98 02 00 00 E8")));
        gPatterns.C_RiderPlayerInput__HandleAction  = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 83 EC 48 48 83 79 18 00 4C 8B CA 48 8B D1 41 8B C0 75 07 32 C0 48 83 C4 48 C3 4C 8B 41 10")));
        gPatterns.C_RiderPlayerInput__OnSlowdown    = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("C6 81 C8 09 00 00 01 32 C0 C3")));

        // Unique at 0x180799E9C. The owning C_Player arrives in RDX and is
        // stored at +0x10 before anything else.
        gPatterns.C_PlayerInput__Construct = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B C4 48 89 58 ? 48 89 70 ? 48 89 78 ? 4C 89 70 ? 41 57 48 83 EC 30 4C 8D 71 ? 48 89 51 ?")));

        // Unique at 0x18285EF98, on the two stores into actor +0x409 and
        // +0x40A that name it.
        gPatterns.C_Actor__SetMovementRestriction = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 55 56 57 48 81 EC ? ? ? ? 48 8B 05 ? ? ? ? 48 33 C4 48 89 84 24 ? ? ? ? 44 88 81 09 04 00 00 48 8B D9 88 91 0A 04 00 00")));

        // Writes the put-away bit into actor +0x9B2 from its bool argument,
        // then branches into the tear-down or wake-up half and ends by
        // refreshing the update slots. The frame-pointer prologue followed by
        // the immediate byte load from the state field pins it.
        // The C_ActorActionRider factory. It opens with the shared allocator
        // preamble, so the 0xA8 allocation size is what separates it from the
        // other factories cut from the same template.
        gPatterns.C_ActorActionRider__Create = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 10 48 89 74 24 18 57 48 83 EC 20 83 3D ? ? ? ? 00 49 8B F0 48 8B 59 08 48 8B FA 0F 84 ? ? ? ? 48 8B 05 ? ? ? ? 48 8D 54 24 30 45 33 C0 B9 A8 00 00 00")));

        // Looks up expansion type 3, validates its native type, and lazily
        // creates it through C_ActionActorExpansionFactory when absent. The
        // lookup/type-check sequence makes this pattern unique in WHGame.dll.
        gPatterns.C_ActionActor__GetItemHandlingExpansion = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 20 48 8B 01 B2 03 48 8B F1 FF 50 70 33 DB 48 8B F8 48 85 C0 74 25")));

        // Same shape as the item-handling accessor above; the type immediate
        // is what separates the two.
        gPatterns.C_ActionActor__GetRiderExpansion = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 74 24 ? 57 48 83 EC 20 48 8B 01 B2 05")));

        // One of several find-an-action-of-type helpers cut from the same
        // template; the action-type byte stored into the predicate's capture
        // is what separates them, so `C6 44 24 20 01` stays literal.
        gPatterns.C_ActionActor__FindRiderAction = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 48 81 EC 80 00 00 00 48 8B 05 ? ? ? ? 48 33 C4 48 89 44 24 ? 48 8B 01 C6 44 24 20 01 FF 50 ? 48 8D 0D")));

        // The rider action's entry. Its prologue saving three non-volatiles
        // into the home space, then `this` into RSI before the first call, is
        // unique.
        gPatterns.C_ActorActionRider__Enter = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 74 24 ? 48 89 7C 24 ? 55 41 56 41 57 48 8B EC 48 83 EC 40 48 8B F1 E8")));

        // The rider action's rollback of a refused entry: loading the child
        // range's end pointer at `+0x80` and its last element at `-8` straight
        // after the prologue is unique.
        gPatterns.C_ActorActionRider__RemoveLastChildAction = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 48 83 EC 20 48 8B 91 80 00 00 00 48 8B D9 48 8B 52 F8")));

        // The seat and the unseat. The pair of moves that stash the rider-sync
        // argument before loading the horse's runtime pointer is unique.
        gPatterns.C_Horse__SetRiderSync = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 6C 24 ? 48 89 74 24 ? 57 48 83 EC 20 48 8B DA 48 8B F9 48 8B 89")));

        // The ride state machine. The load of the current state byte from
        // the context at +0x08 straight into the argument comparison is what
        // separates it from an otherwise stock prologue.
        gPatterns.C_HorseRideContext__SetState = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B C4 48 89 58 ? 48 89 68 ? 48 89 70 ? 48 89 78 ? 41 56 48 83 EC 40 44 8A 71 08 41 8A E8 40 8A F2 48 8B F9")));

        // C_Horse's tag-pass override. The store of the caller's movement
        // params into the inline state at +0x9F0 is what makes an otherwise
        // ordinary prologue unique.
        gPatterns.C_Horse__UpdateMannequinTags = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 6C 24 ? 48 89 74 24 ? 57 48 83 EC 20 0F 10 02 49 8B D8 48 8B F9 0F 11 81 F0 09 00 00")));
        gPatterns.HorseRuntimeData__ApplyPendingJumpRequest = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57 41 54 41 55 41 56 41 57 48 83 EC 20 44 8A B9 0C 01 00 00 48 8B EA")));
        gPatterns.SubmitHorseMoveCommand = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("80 79 08 00 4C 8B D9 75 3A 8B 02 4C 8B 11 41 89 82 1C 01 00 00 41 8B 01")));

        // The navmesh snap: an ordinary prologue, made unique by the game
        // module's +0x160 -> +0x78 navigation lookup it opens with.
        gPatterns.HorseRiddenUpkeep__SnapToNavMesh = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 57 48 83 EC 60 48 8B D9 E8 ? ? ? ? 48 8B 90 60 01 00 00 48 8B 03 48 8B 7A 78")));
        gPatterns.C_RiderPlayerControl__TryPlaceHorseNearPlayer = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 6C 24 ? 48 89 74 24 ? 57 41 56 41 57 48 83 EC 70 41 8A E9 45 8A F8 4C 8B F2 48 8B F1 E8")));

        gPatterns.C_SoulGroup__PossessAnimal                 = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 6C 24 ? 56 57 41 56 48 83 EC 30 45 8A F0 48 8B FA")));
        gPatterns.C_SoulGroup__ReleaseAnimal                 = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 74 24 ? 55 57 41 56 48 8B EC 48 83 EC 70 48 8B F1 48 8B FA 48 8B CA")));
        gPatterns.C_RiderPlayerControl__SetPlayerHorse       = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 74 24 ? 55 57 41 54 41 56 41 57 48 8B EC 48 83 EC 20 8B DA")));
        gPatterns.C_RiderPlayerControl__OnHorseRideStateChanged   = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 57 48 83 EC 20 80 79 ? ? 40 8A FA 48 8B D9 0F 85")));
        gPatterns.C_RiderPlayerInput__Update                      = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 74 24 ? 55 57 41 54 41 56 41 57 48 8B EC 48 83 EC 70 40 32 FF")));
        gPatterns.RequestPlayerMonolog                            = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 18 48 89 4C 24 08 57 48 83 EC 40 48 8B FA")));
        gPatterns.PlayHorseFeedbackSound                          = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B C4 48 89 58 08 48 89 68 10 48 89 70 18 48 89 78 20 41 56 48 83 EC 40 44 8B 05 ? ? ? ? 48 8B F1 65 48 8B 04 25 58 00 00 00 B9 18 00 00 00 0F B6 DA")));
        gPatterns.C_RiderPlayerInput__OnHorseRideStateChanged     = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 08 57 48 83 EC 20 48 8B D9 40 8A FA 48 8B 49 10 48 81 C1 D0 01 00 00")));
        gPatterns.C_Player__GetRiddenHorse                        = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 83 EC 28 48 81 C1 D0 01 00 00 E8 ? ? ? ?")));
        gPatterns.ShouldHorseThrowOffRider                        = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 08 57 48 83 EC 20 48 8B D9 E8 ? ? ? ? 48 8B F8 48 85 C0 74 ? E8 ? ? ? ?")));
        gPatterns.C_HorseDirector__UpdateFastStopReaction         = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B C4 48 89 58 10 48 89 68 18 48 89 70 20 57 48 83 EC 30 0F 29 70 E8 48 8B F9 48 8B 81 F8 00 00 00 0F 28 F1 48 8B 88 E8 09 00 00")));
        gPatterns.UpdateHorseThrowOffRiderState                   = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B C4 48 89 58 10 48 89 68 18 48 89 78 20 41 56 48 83 EC 30 0F 29 70 E8 48 8B F9 8A 41 30 3C 03 0F 84 ? ? ? ? 3C 04 0F 84")));
        // Unique in the whole image; the head through `mov al, [rcx+10Ah]` and
        // the tail from it each still match once.
        gPatterns.TryStartHorseReactionAction = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 55 56 57 48 8D 6C 24 ? 48 81 EC A0 00 00 00 48 8B 05 ? ? ? ? 48 33 C4 48 89 45 ? 8A 81 0A 01 00 00 48 8B D9 88 45 ? 33 FF")));
        // Unique from its 32nd byte, the load of the runtime's gait (+0x11C)
        // after a prologue many functions share; still unique with that
        // offset wildcarded.
        gPatterns.C_HorseDirector__UpdateGaitTransitionReactions =
            reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B C4 48 89 58 08 48 89 68 10 48 89 70 18 48 89 78 20 41 56 48 83 EC 60 44 8B B1 1C 01 00 00 48 8B F1 0F 29 70 E8 40 B5 01 F3 0F 10 B1 10 01 00 00 0F 29 78 D8 0F 28 F9")));
        gPatterns.UpdateHorseCollisionAvoidance                   = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B C4 48 89 58 08 48 89 70 10 48 89 78 18 4C 89 70 20 55 48 8B EC 48 83 EC 70 0F 29 70 E8 48 8B F9 0F 29 78 D8 33 DB 48 8B 01 45 8A F0")));
        // Each unique in the image from its prologue: the speed update by its
        // `comiss xmm6, [rcx+110h]` against the runtime speed, the pace by
        // the two calls that fetch the game modules and the horse cvars, the
        // exhaustion by the virtual call on the runtime's first word.
        gPatterns.UpdateHorseMovementSpeed                  = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 48 83 EC 50 0F 29 74 24 40 48 8B D9 48 8B 09 0F 57 F6 0F 29 7C 24 30 0F 28 F9 44 0F 29 44 24 20 0F 2F B1 10 01 00 00")));
        gPatterns.HorseRuntimeData__ApplyRuntimePaceAndDelta = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B C4 48 89 58 10 48 89 70 18 48 89 78 20 41 56 48 83 EC 40 0F 29 70 E8 48 8B F2 0F 57 F6 48 8B F9 E8 ? ? ? ? E8 ? ? ? ?")));
        gPatterns.C_HorseDirector__UpdateExhaustedReaction  = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 48 83 EC 20 48 8B 01 48 8B D9 FF 50 08 48 85 C0 74 ? 48 8B 8B F8 00 00 00 E8 ? ? ? ? 84 C0 74 ? 48 8B 03 48 8B CB")));
        gPatterns.DeferredRayQueue__Queue                         = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 08 48 89 74 24 10 57 48 81 EC A0 02 00 00 8B 02 48 8B F1 BA 04 00 00 00 89 44 24 20")));
        gPatterns.DeferredRayQueue__QueueNoSkip                   = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("4C 8B DC 48 81 EC A8 02 00 00 41 0F 10 00 8B 02 48 81 C1 00 02 00 00 41 0F 10 48 10 89 44 24 20 48 8D 54 24 20 C6 44 24")));
        gPatterns.DeferredRayQueue__Cancel                        = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57 41 56 41 57 48 83 EC 20 48 8D B9 F0 01 00 00 48 8B E9 48 8B CF 48 8B F2 E8 ? ? ? ? 48 8B D8")));
        gPatterns.DeferredRayQueue__Update                        = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("F3 0F 11 4C 24 10 53 55 56 57 41 54 41 56 41 57 48 81 EC 60 02 00 00 48 8D 99 68 01 00 00 48 8B F9 4C 8D B9 00 02 00 00")));
        gPatterns.C_Actor__SubmitCharacterMoveRequest             = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 55 56 57 48 81 EC 80 00 00 00 48 8B 05 ? ? ? ? 48 33 C4 48 89 44 24 ? 48 8B 01")));
        gPatterns.C_CombatPlayerInput__FilterMovementRequest      = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B C4 48 89 58 ? 48 89 70 ? 48 89 78 ? 55 41 54 41 55 41 56 41 57 48 8D 68 ? 48 81 EC 30 01 00 00 0F 29 70 ? 4C 8B F1")));
        gPatterns.C_CombatModule__GetLocalPlayerCameraManager     = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B 41 ? 48 85 C0 74 ? 48 8B 80 ? ? ? ? C3 C3 CC CC 40 53 48 83 EC 20")));
        gPatterns.C_CombatAlignmentManager__ProcessClipEvents     = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 55 56 57 48 83 EC 20 48 8B D9 E8 ? ? ? ? 48 8D 73")));
        gPatterns.C_CombatAlignmentManager__UpdateSolverTargets   = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B C4 48 89 58 ? 48 89 70 ? 55 57 41 56 48 8D 68 ? 48 81 EC A0 00 00 00 4C 8B F1")));
        gPatterns.C_AlignmentManager__SetPositionSolver           = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 55 56 57 41 56 41 57 48 8B EC 48 83 EC 50 48 8B F2 48 8B F9")));
        gPatterns.C_AlignmentManager__SetRotationSolver           = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 55 53 56 57 41 56 48 8B EC 48 83 EC 40 48 8B F2 48 8B F9")));
        gPatterns.C_CombatProceduralClipAnimateCamera__IsOwnerPlayerControlled = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 48 83 EC 20 48 8B 49 ? 48 8B 01 FF 50 ? 48 8B 0D")));
        gPatterns.C_ProceduralClipCameraBoneAnimated__OnEnter      = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 74 24 ? 57 48 83 EC 20 48 8B D9 E8 ? ? ? ? 48 8B 4B ? 48 8B B0 ? ? ? ? 48 8B 06 48 8B 78 ? 48 8B 01 FF 50 ? 8B D0 48 8B CE FF D7")));
        gPatterns.C_ProceduralClipCameraBoneAnimated__OnExit       = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 83 EC 28 E8 ? ? ? ? 33 D2 48 8B 48")));
        gPatterns.C_ProceduralClipCameraBoneAnimated__Update       = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 83 EC 38 0F 29 74 24 ? 0F 28 F1 E8 ? ? ? ? 0F 28 CE")));
        gPatterns.C_ProceduralClipAnimateCamera__OnEnter           = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 57 48 83 EC 30 0F 29 74 24 ? 49 8B F9 0F 28 F1")));
        gPatterns.C_ProceduralClipAnimateCamera__OnExit            = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 57 48 83 EC 30 0F 29 74 24 ? 48 8B F9 0F 28 F1 E8 ? ? ? ? 48 85 C0 74 ? 48 8B 10 48 8B C8 FF 92")));
        gPatterns.CProceduralClipCameraNear__OnEnter               = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 48 83 EC 20 49 8B D9 E8 ? ? ? ? 48 85 C0 74 ? 48 8B 08")));
        gPatterns.CProceduralClipCameraNear__OnExit                = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 83 EC 28 E8 ? ? ? ? 48 85 C0 74 ? 48 8B 08 48 8B 91 ? ? ? ? 48 8B C8 FF D2 84 C0 74 ? E8")));
        gPatterns.C_CombatActorMercyKill__RequestMercyKill         = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 74 24 ? 55 57 41 54 41 56 41 57 48 8B EC 48 83 EC 50 44 8B E2")));
        gPatterns.C_CombatActorMercyKill__OnMercyKillActionFinished = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 48 83 EC 20 48 8B 41 ? 48 8B D9 48 8B 90 ? ? ? ? 48 8B 82 ? ? ? ? 48 85 C0 74 ? 48 8B 80")));
        gPatterns.C_RiderPlayerControl__SetHorseMountingGlobal    = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 74 24 ? 57 48 83 EC 20 40 8A FA 48 8B F1 E8 ? ? ? ? 48 8B 48 ? 48 8B 01 FF 90 ? ? ? ? 48 8D 15")));
        gPatterns.C_RiderPlayerControl__SetHorseMountedGlobal       = reinterpret_cast<std::uint64_t>(
            hook::get_pattern(KCDC_HIDE("40 53 48 83 EC 20 8A DA E8 ? ? ? ? 48 8B 48 ? 48 8B 01 FF 90 ? ? ? ? 44 8A C3 48 8D 15 ? ? ? ? 48 8B 08 4C 8B 89 ? ? ? ? 48 8B C8 48 83 C4 20 5B 49 FF E1 CC CC CC 48 89 5C 24 ? 48 89 74 24 ? 57 48 83 EC 20 40 8A FA 48 8B F1 E8")));
        gPatterns.C_RiderPlayerControl__SetHorseDismountingGlobal = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE(
            "40 53 48 83 EC 20 8A DA E8 ? ? ? ? 48 8B 48 ? 48 8B 01 FF 90 ? ? ? ? 44 8A C3 48 8D 15 ? ? ? ? 48 8B 08 4C 8B 89 ? ? ? ? 48 8B C8 48 83 C4 20 5B 49 FF E1 CC CC CC 40 53 48 83 EC 20 8A DA E8 ? ? ? ? 48 8B 48 ? 48 8B 01 FF 90 ? ? ? ? 44 8A C3 48 8D 15 ? ? ? ? 48 8B 08 4C 8B 89 ? ? ? ? 48 8B C8 48 83 C4 20 5B 49 FF E1 CC CC CC 48 89 5C 24")));
        gPatterns.C_ActorMovementController__RequestMovement = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 6C 24 ? 48 89 74 24 ? 57 48 83 EC 20 48 8B E9 4C 8D 51")));
        gPatterns.C_ActorMovementController__UpdateMovementState = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B C4 48 89 58 10 55 56 57 41 54 41 55 41 56 41 57 48 8D A8 08 FF FF FF 48 81 EC C0 01 00 00 0F 29 70 B8 45 33 F6 0F 29 78 A8")));
        gPatterns.OnHorseRiderChanged = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B C4 48 89 58 08 48 89 70 10 48 89 78 18 55 41 56 41 57 48 8D 68 A1 48 81 EC F0 00 00 00 48 8B 01")));
        gPatterns.C_ActorMovementController__SetMoveTransitions = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 48 83 EC 20 8A DA 44 8A C2")));
        gPatterns.C_DogCompanion__UpdateObjectivePlanner     = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 55 48 8D 6C 24 ? 48 81 EC B0 00 00 00 48 8B 05 ? ? ? ? 48 33 C4 48 89 45 ? 48 83 79")));
        gPatterns.C_DogCompanion__GetObjective               = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("88 54 24 ? 53 48 83 EC 40 48 8D 99")));
        gPatterns.C_WaitDogObjective__SelectNextWaitBehavior = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 55 56 57 48 83 EC 20 48 8B B9 ? ? ? ? 4C 8D 89")));
        gPatterns.C_DogCompanion__DeactivateRunningObjective = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 48 83 EC 40 48 8D 54 24 ? 48 8B D9 E8 ? ? ? ? 4C 8B 5B")));

        gPatterns.C_ActionActor__QueueAction                   = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 57 48 83 EC 20 48 8B 01 48 8B FA 48 8B D9 FF 50 ? F6 80")));
        gPatterns.C_DogExternalActionSyncAnimMaster__Construct = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 74 24 ? 57 48 83 EC 20 83 3D ? ? ? ? ? 48 8B F2 48 8B D9 75 ? E8 ? ? ? ? 48 8B 05 ? ? ? ? 48 8D 54 24 ? 45 33 C0 B9 F0 00 00 00")));
        gPatterns.GetDogBondClipId                             = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 83 EC 28 48 8B 01 FF 90 ? ? ? ? 48 8B 08 48 8B 91 ? ? ? ? 48 8B C8 FF D2 48 85 C0")));
        gPatterns.GetDogBondPartnerClipId                      = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 83 EC 28 E8 ? ? ? ? 48 8B 48 ? 48 8B 01 FF 90 ? ? ? ? 48 85 C0 74 ? 48 8B 08 48 8B 91 ? ? ? ? 48 8B C8 FF D2 48 8B 08")));
        gPatterns.SetPlayerDogMode                             = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 74 24 ? 57 48 81 EC A0 00 00 00 8A D9")));
        gPatterns.ResolveDogActivityOwner                      = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 83 EC 28 48 8B 0D ? ? ? ? 48 8B 01 FF 50 ? 48 85 C0 74 ? 48 8B D0")));
        gPatterns.C_NPCManager__Instance                       = hook::get_address<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 83 EC 28 48 8B 0D ? ? ? ? 48 8B 01 FF 50 ? 48 85 C0 74 ? 48 8B D0")), 7, 11);
        gPatterns.AIBrainTemplateRegistry                      = hook::get_address<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B 0D ? ? ? ? 48 8D 54 24 ? 45 33 C9 C7 44 24 ? 00 00 00 00 4C 8B C3")), 3, 7);
        gPatterns.BuildBrainForGuid                            = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 74 24 ? 57 48 83 EC 20 49 8B F0 41 8A F9")));
        gPatterns.C_DogInteractionManager__QueueDogObjective   = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B C4 53 55 56 57 41 54 41 55 41 56 41 57 48 81 EC 98 00 00 00 0F 29 70")));

        gPatterns.C_Actor__SetPutAway = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B C4 48 89 58 ? 48 89 68 ? 48 89 70 ? 57 41 54 41 55 41 56 41 57 48 83 EC 40 40 8A A9")));

        // The put-away request. The read of the state byte at actor +0x9B2
        // is what makes this unique; the prologue it opens with is not.
        gPatterns.C_Actor__RequestPutAway = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 48 83 EC 20 48 8B D9 0F B6 89 B2 09 00 00 0F B6 C1 C0 E8 07 3A C2")));

        // Fetches the actor's clothing object through its guarded getter
        // and, only when that answers, calls the object's refresh. The bare
        // call-test-return-else-load shape pins it.
        gPatterns.C_Actor__RebuildClothing = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 83 EC 28 E8 ? ? ? ? 48 85 C0 75 05 48 83 C4 28 C3 48 8B 08 48 8B 91 C0 00 00 00")));

        // The equip the quick-slot dispatcher falls back to once it has
        // decided the item is plain armor. Checks the item is equippable at
        // all before doing anything, which is the leading call here.
        gPatterns.C_EquipmentManager__EquipItemRaw = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 74 24 ? 48 89 7C 24 ? 41 56 48 83 EC 20 48 8B D9 41 8A F0 48 8B CA 48 8B FA E8 ? ? ? ? 84 C0")));

        // The only two writers of the weapon slot array at +0x98.
        gPatterns.C_EquipmentManager__SetWeaponSlotItem   = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 57 48 83 EC 20 48 8B F9 48 8B DA 48 8B CA E8 ? ? ? ? 33 D2")));
        gPatterns.C_EquipmentManager__ClearWeaponSlotItem = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 6C 24 ? 48 89 74 24 ? 57 48 83 EC 20 48 8B E9 49 8B D9 48 8B CA")));

        // The single writer of the hand-item state.
        gPatterns.C_ItemAttachmentManager__SetHandItem = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B C4 48 89 58 ? 48 89 68 ? 48 89 70 ? 48 89 78 ? 41 56 48 83 EC 20 48 8B 41 ? 49 8B D8")));
        gPatterns.C_ModelBitsetProperty_E_ActorState__Set = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 74 24 ? 57 48 83 EC 20 48 8B 01 41 8A F0")));

        // The unequip C_EquipmentManager::UnequipAllArmor drains its worn list
        // with, one item at a time.
        gPatterns.C_EquipmentManager__UnequipItemRaw = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 6C 24 ? 56 57 41 56 48 83 EC 20 48 8B F9 49 8B D9 48 8B CA 45 8B F0 48 8B F2 E8 ? ? ? ? 33 D2")));

        // Tail-calls swscanf_s with the 8-4-4-4-12 GUID format string, which
        // together with the leading brace handling pins it.
        gPatterns.ParseGuidString = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B C4 48 89 58 ? 48 89 68 ? 48 89 70 ? 48 89 78 ? 41 56 48 83 EC 70 4C 8B F2")));

        // GetInstance shares its prologue with every other double-checked
        // singleton in the module, so it is taken from a call site instead:
        // C_AddBuffEffect::Apply is three instructions of setup, the call, and
        // then the movups of its own buff GUID, which is what makes it unique.
        gPatterns.C_BuffManager__GetInstance = hook::get_call<std::uint64_t>(reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 48 83 EC 50 48 8B D9 E8 ? ? ? ? 0F 10 43"), 9)));

        // Unique on its own prologue: four register saves into the caller's
        // shadow space, then a 0x50 frame with the manager in R14.
        gPatterns.C_BuffManager__CreateAndAttachBuff = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B C4 48 89 58 ? 48 89 68 ? 48 89 70 ? 48 89 78 ? 41 56 48 83 EC 50 4C 8B F1 49 8B E8")));

        // The getter CScriptBind_Entity::TriggerEvent calls before dispatching
        // an AIEVENT: it tests the entity's AI object id and, when set, resolves
        // it through IAIObjectManager. The id test plus the manager fetch make
        // the prologue unique.
        gPatterns.IEntity__GetAIObject = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 48 83 EC 20 33 C0 48 8B D9 39 81 ? ? ? ? 75")));

        // Its prologue reserves the 0x410-byte frame the AIObjectParams copy
        // is built in, which is unique in the image.
        gPatterns.CEntity__RegisterInAISystem = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 55 56 57 48 8D AC 24 ? ? ? ? 48 81 EC 10 04 00 00")));

        // CDeviceResourceLayout_DX12::Init allocates its 0x15A0-byte root-
        // signature construction scratch frame immediately after this unique
        // preserved-register prologue.
        gPatterns.CDeviceResourceLayout_DX12__Init = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 74 24 ? 57 B8 A0 15 00 00")));

        // CCryDX12GIFactory::Create invokes CreateDXGIFactory1 before its
        // unique tracked 0x18-byte wrapper allocation and native retention.
        gPatterns.CCryDX12GIFactory__Create = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 48 83 EC 20 48 8D 54 24 ? 48 C7 44 24 ? 00 00 00 00 48 8D 0D ? ? ? ? FF 15 ? ? ? ? 85 C0")));

        // CCryDX12GIAdapter::Create enumerates IDXGIAdapter1 through the
        // retained factory, queries IDXGIAdapter3, then allocates 0x20 bytes.
        gPatterns.CCryDX12GIAdapter__Create = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 56 48 83 EC 20 33 F6 48 89 7C 24 ? 48 8B F9 48 89 74 24 ? 48 8B 49 10 4C 8D 44 24")));

        // CCryDX12GIOutput::Create enumerates through the adapter's native
        // interface, queries the required output interface, then allocates
        // the exact 0x20-byte wrapper.
        gPatterns.CCryDX12GIOutput__Create = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 57 41 56 48 83 EC 38 45 33 F6 48 89 5C 24 ? 44 89 74 24 ? 4C 8D 44 24 ? 48 89 6C 24")));

        // C_Game publishes itself immediately before allocating its 0x68-byte
        // C_RuntimePrefabManager at +0x150.
        gPatterns.C_Game__Instance                        = hook::get_address<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 1D ? ? ? ? E8 ? ? ? ? 48 85 C0 74 ? 48 8B C8 E8 ? ? ? ? EB ? 48 8B C7 48 89 83 50 01 00 00")), 3, 7);
        gPatterns.C_RuntimePrefabManager__CreatePrefabRaw = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 7C 24 ? 44 89 44 24 ? 55 48 8B EC 48 83 EC 40")));
        gPatterns.C_RuntimePrefabManager__MovePrefabRaw   = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("89 54 24 ? 53 48 83 EC 20 48 8D 59 ? 48 8B CB 48 8D 54 24 ? E8 ? ? ? ? 48 3B 03 74 ? 48 8B 48 ? E8 ? ? ? ? 48 83 C4 20 5B C3 CC CC 48 89 5C 24")));
        gPatterns.C_RuntimePrefabManager__DeletePrefabRaw = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B C4 48 89 58 ? 48 89 70 ? 89 50 ? 57 48 83 EC 40 48 8D 79")));

        // C_AnimalSpawner's native processing pass walks every 0x10-byte
        // SpawnActorEntry and selects its actor spawn or retirement path.
        gPatterns.C_AnimalSpawner__ProcessSpawnEntriesRaw = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 57 48 83 EC 20 33 DB 48 8B F9 38 99 ? ? ? ? 74 ? 38 99 ? ? ? ? 75")));
        // Single-entry variants used by the pass above. The spawn path builds
        // the native SpawnedAnimal_%s_%X_%u request; the retirement path
        // cancels or deletes that request and clears the actor reference.
        gPatterns.C_AnimalSpawner__ProcessSpawnEntryRaw = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 74 24 ? 55 57 41 54 41 56 41 57 48 8D AC 24 ? ? ? ? 48 81 EC ? ? ? ? 48 8B 05 ? ? ? ? 48 33 C4 48 89 85 ? ? ? ? 48 8B 41 70")));
        gPatterns.C_AnimalSpawner__RetireSpawnEntryRaw  = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 55 56 57 41 54 41 55 41 56 41 57 48 8D 6C 24 ? 48 81 EC B0 00 00 00 48 8B 05 ? ? ? ? 48 33 C4 48 89 45 ? 48 8B 79 70")));
        // The enable setter behind both of those, taking the wanted state in
        // DL and the query state the spawner caches from in R8.
        gPatterns.C_AnimalSpawner__SetSpawningEnabled = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 74 24 ? 57 48 83 EC 20 49 8B F0 40 8A FA 48 8B D9 84 D2")));

        // C_SoulPoolBase::FinalizeAndSubmitSpawnRequest parks the request in
        // RDI and the spawn slot in RBX before stamping the pool's name onto
        // the request; that trio of moves is unique in WHGame.dll.
        gPatterns.C_SoulPoolBase__FinalizeAndSubmitSpawnRequest = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 57 48 83 EC 20 48 8B FA 49 8B D8 48 8D 51 ? 48 8D 4F ? E8 ? ? ? ? 48 89 5F ? E8")));

        // C_ScopedEngineLock. The constructor is recognisable by the lock-slot
        // arithmetic it opens with -- `lea rbx, [rcx+8]` onto its own mutex
        // slot followed by `movsxd rax, edx` on the slot index. The destructor
        // tests the owns byte at +0x10 before unlocking.
        gPatterns.C_ScopedEngineLock__Construct = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 57 48 83 EC 20 48 8D 59 ? 48 63 C2")));
        gPatterns.C_ScopedEngineLock__Destruct  = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 48 83 EC 20 80 79 ? ? 48 8B D9 74 ? 48 8B 49 ? 48 85 C9 74 ? FF 15")));

        // The butcher claim and the gate the interactor draws it from.
        gPatterns.C_ScriptBindActor__Butcher = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 6C 24 ? 48 89 74 24 ? 57 48 83 EC 20 49 8B D8 48 8B FA 48 8B E9 E8 ? ? ? ? 48 8B F0")));
        gPatterns.C_Soul__CanBeButchered     = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 57 48 83 EC 20 33 D2 48 8B F9 E8 ? ? ? ? 48 8B C8 48 85 C0 74 ? 48 8B 97 ? ? ? ? B3 01 83 7A ? ? 75 ? 32 C0 40 8A BF")));
        gPatterns.C_Actor__RequestItemExchange      = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 55 56 57 41 54 41 55 41 56 41 57 48 8D 6C 24 ? 48 81 EC 80 01 00 00 48 8B 05 ? ? ? ? 48 33 C4 48 89 45 ? 8B DA")));
        gPatterns.C_Human__RequestItemExchange      = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 57 48 83 EC 20 8B DA 48 8B F9 E8 ? ? ? ? E8 ? ? ? ? 48 8B 48")));
        gPatterns.C_ActorActionButchering__Enter    = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B C4 48 89 58 ? 48 89 70 ? 48 89 78 ? 55 41 56 41 57 48 8D A8 ? ? ? ? 48 81 EC E0 01 00 00")));
        gPatterns.C_ActorActionButchering__Complete = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B C4 48 89 58 ? 48 89 70 ? 48 89 78 ? 55 41 54 41 55 41 56 41 57 48 8D A8 ? ? ? ? B8 20 14 00 00")));
        gPatterns.C_ActorActionButchering__Exit     = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 74 24 ? 57 48 83 EC 30 48 8B F9 E8 ? ? ? ? 48 8B CF")));
        gPatterns.C_ScriptBindBase__ResolveActorFromArgs =
            reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 57 48 83 EC 20 48 8B 02 48 8B F9 48 8B CA FF 50 ? 48 8B D8 48 85 C0 74 ? 48 8B 4F ? 48 8B 11 FF 92 ? ? ? ? 8B D3 48 8B 08 4C 8B 41 ? 48 8B C8 41 FF D0 48 8B 5C 24 ? 48 83 C4 20 5F C3 EB")));

        // C_EntityModule publishes itself after constructing its owned
        // services, including C_ItemSlotManager at +0xD8.
        gPatterns.C_EntityModule__Instance = hook::get_address<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 3D ? ? ? ? E8 ? ? ? ? 48 85 C0")), 3, 7);

        // CVegetation::SetScale clamps scale * 64 to one byte and stores it
        // at the concrete vegetation node's encoded-scale field.
        gPatterns.CVegetation__SetScale = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("F3 0F 59 0D ? ? ? ? 0F 57 C0 0F 2F C1 77")));

        // The terrain/indoor.dat loader publishes the newly constructed
        // CVisAreaManager through this unique RIP-relative global store.
        gPatterns.CVisAreaManager__Instance = hook::get_address<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 0D ? ? ? ? 48 8D 55")), 3, 7);

        // C_ClothingSystem's constructor initializes its final state fields,
        // then publishes `this` through a RIP-relative global store.
        gPatterns.C_ClothingSystem__Instance = hook::get_address<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 86 B8 01 00 00 C7 86 C0 01 00 00 00 00 80 BF 89 86 C4 01 00 00 48 89 35 ? ? ? ? E8 ? ? ? ?")), 26, 30);

        // C_InventoryManager's owner publishes the freshly constructed
        // manager through this unique RIP-relative global store.
        gPatterns.C_InventoryManager__Instance               = hook::get_address<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 1D ? ? ? ? 48 8B 5C 24 ? 48 83 C4 20 41 5E C3 CC CC 48 8D 05")), 3, 7);
        gPatterns.C_InventoryManager__FindInventoryByWuidRaw = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 54 24 ? 48 83 EC 28 48 83 C1 28 48 8D 54 24 ? E8 ? ? ? ? 48 83 C4 28 C3 E9")));
        gPatterns.C_InventoryManager__GetOrCreateInventory   = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 56 57 41 54 41 55 41 56 41 57 48 83 EC 30")));
        gPatterns.C_InventoryManager__DestroyInventory       = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 6C 24 ? 48 89 74 24 ? 57 41 56 41 57 48 83 EC 30 4C 8D B1")));

        // Both discovery vtables come from the `lea` pair every constructor
        // emits for a class with a second base: primary table into +0x00, then
        // the second into +0x10. Anchoring on the surrounding stores is what
        // makes each unique -- the `lea` alone is not.
        gPatterns.C_POI__DiscoveryVTable             = hook::get_address<std::uint64_t>(hook::get_pattern(KCDC_HIDE("0F 28 02 48 8D 05 ? ? ? ? 48 89 01 33 F6 89 71 08 48 8D 05 ? ? ? ? 48 89 41 10")), 21, 25);
        gPatterns.C_RPGLocation__DiscoveryVTable     = hook::get_address<std::uint64_t>(hook::get_pattern(KCDC_HIDE("4D 85 F6 74 ? 48 8D 05 ? ? ? ? 41 C7 46 08 00 00 00 00 49 89 06 48 8D 05 ? ? ? ? 49 89 46 10 49 89 5E 18")), 26, 30);
        // The constructor's first `lea`, anchored on the stores after it; unique with margin.
        gPatterns.C_RPGLocationManager__VTable       = hook::get_address<std::uint64_t>(hook::get_pattern(KCDC_HIDE("45 33 F6 48 8D 05 ? ? ? ? 48 89 01 48 8B D9 66 44 89 71 18 48 8D 05 ? ? ? ? 48 89 41 08")), 6, 10);
        gPatterns.C_POI__SetDiscoveryStatusAndNotify = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 83 EC 28 44 8B 99 ? ? ? ? 4C 8B D1 44 3B DA 75 ? 32 C0 48 83 C4 28 C3 89 91")));

        // The fog interning routine; its prologue is unique in WHGame.dll.
        gPatterns.C_FOWManager__GetOrCreateFogOfWar = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 55 56 57 41 56 41 57 48 83 EC 40 45 8B F8")));
        gPatterns.C_FOWManager__Load               = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 54 24 10 48 89 4C 24 08 55 53 56 57 41 54 41 55 41 56 41 57 48 8B EC 48 83 EC 58 48 8B CA C7 45 58 00 00 00 00")));
        gPatterns.C_FOWManager__Reset              = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 20 48 C7 41 18 00 00 00 00 48 8B F9 48 8B 59 20")));

        // The weather-profile database initializer returns the address of its
        // process-global object through this unique RIP-relative LEA.
        gPatterns.C_WeatherProfileDatabase__Instance = hook::get_address<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8D 05 ? ? ? ? 48 83 C4 38 C3 48 89 5C 24 ? 57 48 83 EC 20")), 3, 7);

        // The two preset databases, each read where the soul's inventory
        // resolves its top override: the object is the LEA's operand and the
        // lookup is the call right behind it.
        gPatterns.C_ClothingPresetDatabase__Instance   = hook::get_address<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B 03 48 8B CB FF 90 58 01 00 00 48 8B D0 48 8D 0D ? ? ? ? E8 ? ? ? ? 48 85 C0 74"), 15), 3, 7);
        gPatterns.C_ClothingPresetDatabase__FindByGuid = hook::get_call<std::uint64_t>(reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B 03 48 8B CB FF 90 58 01 00 00 48 8B D0 48 8D 0D ? ? ? ? E8 ? ? ? ? 48 85 C0 74"), 22)));
        gPatterns.C_WeaponPresetDatabase__Instance     = hook::get_address<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B CA 49 8B D8 48 8B F2 E8 ? ? ? ? 48 8B D3 48 8D 0D ? ? ? ? E8 ? ? ? ? 48 8B D8 48 85 C0"), 17), 3, 7);
        gPatterns.C_WeaponPresetDatabase__FindByGuid   = hook::get_call<std::uint64_t>(reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B CA 49 8B D8 48 8B F2 E8 ? ? ? ? 48 8B D3 48 8D 0D ? ? ? ? E8 ? ? ? ? 48 8B D8 48 85 C0"), 24)));

        // The recipe databases: alchemy where the finished brew's identified recipe is looked up
        // (0x182E163F4), smithing in C_Blacksmithing::GetCurrentRecipe (0x180857D44). The object is
        // the LEA's operand and the lookup the call right behind it; both unique.
        gPatterns.C_AlchemyRecipeDatabase__Instance       = hook::get_address<std::uint64_t>(hook::get_pattern(KCDC_HIDE("44 8B 2A 4C 8B E2 48 8B F9 49 63 D9 48 8D 55 ? 44 89 6D ? 48 8D 0D ? ? ? ? 49 8B F0 E8"), 20), 3, 7);
        gPatterns.C_AlchemyRecipeDatabase__FindRecipeById = hook::get_call<std::uint64_t>(reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("44 8B 2A 4C 8B E2 48 8B F9 49 63 D9 48 8D 55 ? 44 89 6D ? 48 8D 0D ? ? ? ? 49 8B F0 E8"), 30)));
        gPatterns.C_BlacksmithRecipeDatabase__Instance    = hook::get_address<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 48 83 EC 20 48 83 C1 68 48 8B 01 FF 50 60 48 8D 54 24 30 48 8B 08 4C 8B 41 08 48 8B C8 41 FF D0 48 8B D0 48 8D 0D ? ? ? ? E8"), 37), 3, 7);
        gPatterns.C_BlacksmithRecipeDatabase__FindRecipeById =
            hook::get_call<std::uint64_t>(reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 48 83 EC 20 48 83 C1 68 48 8B 01 FF 50 60 48 8D 54 24 30 48 8B 08 4C 8B 41 08 48 8B C8 41 FF D0 48 8B D0 48 8D 0D ? ? ? ? E8"), 44)));

        // The prologue, unique; reached only through I_UIBook slot 6.
        gPatterns.C_UIBook__SetRecipe = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 10 48 89 74 24 18 55 57 41 54 41 56 41 57 48 8B EC 48 81 EC 80 00 00 00 48 8B F1 4D 8B F1 48 8D 4D C8 4D 8B F8 4C 8B E2 E8")));

        // The item database, read where the clothing-preset expansion looks up
        // each class it is about to keep or skip.
        gPatterns.C_ItemDatabase__Instance      = hook::get_address<std::uint64_t>(hook::get_pattern(KCDC_HIDE("0F 10 36 48 8D 54 24 50 48 8D 0D ? ? ? ? 0F 29 74 24 20 0F 29 74 24 50 E8"), 8), 3, 7);
        gPatterns.C_ItemDatabase__FindItemClass = hook::get_call<std::uint64_t>(reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("0F 10 36 48 8D 54 24 50 48 8D 0D ? ? ? ? 0F 29 74 24 20 0F 29 74 24 50 E8"), 25)));

        // The three world-clock natives. Each prologue is unique in WHGame.dll.
        gPatterns.C_Calendar__SetWorldTime             = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 57 48 83 EC 20 48 8B 79 ? 48 8B D9 48 3B D7")));
        gPatterns.C_Calendar__SetWorldTimeRatio        = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 48 83 EC 60 0F 29 74 24 ? 48 8B 05 ? ? ? ? 48 33 C4 48 89 44 24 ? 0F 28 F1")));
        gPatterns.C_Calendar__PublishTimeOfDay         = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 57 48 83 EC 40 F3 0F 10 0D ? ? ? ? 0F 57 C0")));
        gPatterns.C_Signal180875DF8__Broadcast         = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE(
            "48 8B C4 48 89 58 ? 48 89 68 ? 48 89 70 ? 48 89 78 ? 41 56 48 83 EC 20 33 F6 48 8D 41 ? 48 8B D9 8B D6 48 8D 48 ? 83 CD FF 44 8D 76 ? EB ? 66 39 28 74 ? 41 03 D6 48 83 C0 02 48 3B C1 75 ? 8B D6 48 8B 03 48 63 FA 0F B7 48 ? 66 41 2B CE 78 ? 0F B7 C1 0F B7 D1")));
        gPatterns.C_TimeOfDayBlender__BlendTimeOfDay   = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B C4 48 89 58 ? 48 89 70 ? 48 89 78 ? 4C 89 60 ? 55 41 56 41 57 48 8D 68 ? 48 81 EC 90 00 00 00 48 8B F1")));
        gPatterns.C_TimeOfDayBlender__ApplyPresetBlend = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 57 48 83 EC 60 48 8B 05 ? ? ? ? 48 33 C4 48 89 44 24 ? 48 8B F9 48 8B 0D")));
        gPatterns.C_TimeOfDayBlender__MakeRandomEntry  = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B C4 48 89 58 ? 48 89 70 ? 48 89 78 ? 55 41 54 41 55 41 56 41 57 48 8B EC 48 83 EC 50 4C 8B E9")));
        gPatterns.ExecuteBlendTimeOfDayCommand         = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 48 83 EC 20 4C 8B 41 ? 48 8B D9 48 8B 09 48 83 C1 08")));
        gPatterns.C_WeatherSystem__Update              = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B C4 48 89 58 ? 48 89 68 ? 48 89 70 ? 57 48 83 EC 50 48 8B F9 0F 29 70")));

        // C_UIModalDialogQuestion's non-virtual open routine has eleven game
        // call sites and emits the OpenQuestionDialog Flash command.
        gPatterns.C_UIModalDialogQuestion__OpenQuestionDialog = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 55 56 57 41 54 41 55 41 56 41 57 48 8D 6C 24 ? 48 81 EC D0 00 00 00 48 8B 05 ? ? ? ? 48 33 C4 48 89 45 ? 48 8B 45")));
        gPatterns.C_UIHudInteractiveDialog__Ctor              = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 83 EC 28 E8 ? ? ? ? 48 8D 05 ? ? ? ? 48 89 41 ? 4C 8D 05 ? ? ? ? 4C 89 01 48 8D 05 ? ? ? ? 48 89 41 ? 33 C0 88 41")));

        // C_UIModalDialogInfo's non-virtual open routine emits the
        // OpenInfoDialog Flash command and owns one close callback.
        gPatterns.C_UIModalDialogInfo__OpenInfoDialog = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 55 53 56 57 41 54 41 56 41 57 48 8D 6C 24 ? 48 81 EC A0 00 00 00 48 8B 05 ? ? ? ? 48 33 C4 48 89 45 ? 4C 8B 65 ? 4C 8B FA 48 8B F1 49 8B D1 48 83 C1 58")));
        gPatterns.C_UIModalDialogInfo__CloseRaw       = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 74 24 ? 57 48 83 EC 20 48 8B F1 C6 81 A0 00 00 00 00")));

        // C_UIModalDialogRandomEvent's non-virtual open routine configures
        // four choices and emits the OpenRandomEventDialog Flash command.
        gPatterns.C_UIModalDialogRandomEvent__OpenRaw  = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 55 53 56 57 41 54 41 55 41 56 41 57 48 8D 6C 24 ? 48 81 EC 18 01 00 00 48 8B 05 ? ? ? ? 48 33 C4 48 89 45 ? 48 8B 9D")));
        gPatterns.C_UIModalDialogRandomEvent__CloseRaw = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 88 54 24 ? 57 48 83 EC 20 B8 00 00 80 BF")));

        // C_UIModalDialogItem's non-virtual open and close paths emit the
        // corresponding OpenItemDialog and CloseItemDialog Flash commands.
        gPatterns.C_UIModalDialogItem__OpenRaw  = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B C4 55 53 56 57 41 54 41 56 41 57 48 8D 68 ? 48 81 EC F0 00 00 00 0F 29 70 ? 48 8B 05 ? ? ? ? 48 33 C4 48 89 45 ? 4C 8B 65")));
        gPatterns.C_UIModalDialogItem__CloseRaw = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B C4 48 89 58 ? 48 89 70 ? 57 48 83 EC 70 48 8B F1")));

        // The unique ShowUINotification scripting bridge calls the otherwise
        // template-shaped C_UIHudMessages lookup routine at its second call.
        gPatterns.C_UIHudMessages__GetInstance = hook::get_address<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 57 48 83 EC 20 48 8B FA 48 8B D9 E8 ? ? ? ? 48 8B 88 E8 00 00 00 E8 ? ? ? ? 44 8B 0F 48 8D 53 08 45 33 C0 48 8D 48 58 48 8B 01")), 29, 33);

        // C_TutorialManager's named tutorial entry point is shared by the
        // ShowTutorial scripting bridge and numerous native gameplay paths.
        gPatterns.C_Actor__Fall = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 48 81 EC F0 00 00 00 48 8B 05 ? ? ? ? 48 33 C4 48 89 84 24 E0 00 00 00 48 8B D9 4C 8B D2 48 8D 4C 24 40 E8 ? ? ? ? 41 8B 42 08 48 8D 4C 24 20 F2 41 0F 10 0A 45 33 C0")));
        gPatterns.C_Player__StopAnimations        = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 48 83 EC 20 48 8B 49 38 33 D2 48 8B 01 FF 90 28 03 00 00 48 8B D8 48 85 C0 74 47 48 8B 08 48 8B 51 28 48 8B C8 FF D2 48 8B 08 48 8B 51 58")));
        gPatterns.C_TutorialManager__ShowTutorial = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 48 83 EC 60 48 8B D9 4C 8B C2 48 8D 54 24 20 48 8D 0D ? ? ? ? E8 ? ? ? ? 48 83 7C 24 20 00 0F 84 ? ? ? ?")));

        // The platform's statistics service is a class per storefront (Steam's
        // CStatistics, the GDK's), so its own methods share no signature. What
        // reaches it is shared: rpgmodule fetches it through this getter, whose
        // two callers -- the achievement queue's unlock and the achievement
        // reset -- skip when it answers null, and the g_DebugUnlockAchievement
        // and g_DebugProgressAchievement console commands, the only callers of
        // the progress entry, fetch it themselves.
        gPatterns.rpgmodule__GetPlatformStatistics         = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 83 EC 28 48 8B 0D ? ? ? ? E8 ? ? ? ? 48 8B C8 48 85 C0 75 05 48 83 C4 28 C3 48 8B 00 48 83 C4 28 48 FF A0 D0 00 00 00")));
        gPatterns.ConsoleCommand__DebugUnlockAchievement   = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 48 83 EC 20 48 8B 01 48 8B D9 FF 50 08 83 C0 FE 0F 85")));
        gPatterns.ConsoleCommand__DebugProgressAchievement = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 10 48 89 74 24 18 57 48 83 EC 40 48 8B 01 48 8B D9")));

        // C_SkipTime::BeginSkipTime's prologue, unique in WHGame.dll. It is
        // the mode-carrying entry every skip-time screen opens through.
        gPatterns.C_SkipTime__BeginSkipTime = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B C4 48 89 58 ? 48 89 68 ? 48 89 70 ? 57 48 81 EC 10 01 00 00 0F 29 70 ? 8B DA 0F 29 78 ? 0F 28 F3 0F 28 FA 48 8B F1")));

        // C_Inventory's native mutation and class lookup paths. These are
        // shared by scripting bridges and native gameplay code.
        gPatterns.C_Inventory__AddItemRaw            = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 55 56 57 41 54 41 55 41 56 41 57 48 81 EC F0 00 00 00")));
        gPatterns.C_ItemHolder__RemoveItem           = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B C4 48 89 58 ? 48 89 68 ? 48 89 70 ? 48 89 78 ? 41 54 41 56 41 57 48 83 EC 30 4C 8B B2")));
        gPatterns.C_Item__AdjustAmount               = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("85 D2 75 ? B0 01 C3 44 8B C2 48 8B D1 48 8B 89 90 00 00 00 E9 ? ? ? ?")));
        gPatterns.C_Inventory__RemoveAllItemsRaw     = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 57 48 83 EC 20 48 8B F9 48 8B 57")));
        gPatterns.C_Inventory__FindItemByClassRaw    = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 6C 24 ? 48 89 74 24 ? 57 48 83 EC 20 48 8B 59 ? 48 8B EA 48 8B 79 ? EB")));
        gPatterns.C_Inventory__GetMoneyRaw           = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 83 EC 28 48 81 C1 E8 00 00 00")));
        gPatterns.C_Inventory__RemoveMoneyRaw        = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 56 57 48 83 EC 30 41 8B F8")));
        gPatterns.C_Inventory__CreateItems           = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 55 53 56 57 41 54 41 55 41 56 41 57 48 8D AC 24 ? ? ? ? 48 81 EC C8 01 00 00 48 8B 05 ? ? ? ? 48 33 C4 48 89 85 ? ? ? ? 49 8B 00")));
        gPatterns.C_Inventory__RemoveItemsByClass    = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("44 89 4C 24 ? 55 48 8D 6C 24 ? 48 81 EC C0 00 00 00 48 8D 05")));
        gPatterns.C_Inventory__DeleteItems           = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 44 88 4C 24 ? 55 56 57 48 8D 6C 24")));
        gPatterns.C_Inventory__MoveItems             = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 55 56 57 48 8D 6C 24 ? 48 81 EC 00 01 00 00 48 8B F9")));
        gPatterns.C_ItemHolder__TransferItemToHolder = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 55 56 57 41 54 41 55 41 56 41 57 48 81 EC 90 00 00 00 48 8B 05 ? ? ? ? 48 33 C4 48 89 84 24 ? ? ? ? 48 8B B2")));

        // C_Human's central drop worker is shared by inventory and held-item
        // drops. Its output is the post-split C_Item after native spawning.
        gPatterns.C_Human__DropInventoryItem = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B C4 48 89 58 ? 55 56 57 41 56 41 57 48 8D 68 ? 48 81 EC E0 00 00 00 0F 29 70 ? 0F 29 78 ? 44 0F 29 40 ? 48 8B 05 ? ? ? ? 48 33 C4 48 89 45 ? 4C 8B 75")));

        // C_Item's bind seam either invokes the callback immediately with
        // its C_PickableItem or queues creation at the supplied pose. The
        // inverse removes/cancels that binding without deleting the RPG item.
        gPatterns.C_Item__SpawnPickable  = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 6C 24 ? 48 89 74 24 ? 57 48 83 EC 30 48 8B DA 49 8B F1")));
        gPatterns.C_Item__UnbindPickable = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 48 83 EC 20 48 8B 91 ? ? ? ? 48 8B D9 48 85 D2 75 ? E8")));

        // RTTR exposes these I_ItemDescriptor filters with an InputItems
        // vector. Both adapters dispatch the descriptor's slot-one matcher
        // with the native full-match policy enabled.
        gPatterns.I_ItemDescriptor__GetMatchingItems  = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 74 24 ? 57 48 83 EC 40 48 C7 02 00 00 00 00 48 8B FA 48 C7 42 08 00 00 00 00 41 B1 01")));
        gPatterns.I_ItemDescriptor__FullyMatchesItems = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 48 83 EC 40 48 8B 01 4C 8B C2 41 B1 01 48 8D 54 24 ? FF 50 08 48 8B 48 08 48 39 08")));

        // RTTR registers this exact free function as
        // wh::entitymodule::CreatePlayerReward.
        gPatterns.CreatePlayerReward            = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 55 56 57 41 56 41 57 48 8D AC 24 ? ? ? ? 48 81 EC C0 02 00 00")));
        gPatterns.CreateItemDelivery            = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 56 41 54 41 55 41 57 48 83 EC 68")));
        gPatterns.CreateItemReferenceDescriptor = reinterpret_cast<std::uint64_t>(
            hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 6C 24 ? 48 89 74 24 ? 57 48 83 EC 30 83 3D ? ? ? ? ? 48 8B EA 48 8B F1 75 ? E8 ? ? ? ? 48 8B 05 ? ? ? ? 48 8D 54 24 ? 45 33 C0 41 8D 48 ? FF D0 48 8B F8 8B 44 24 ? F0 01 05 ? ? ? ? F0 83 05 ? ? ? ? ? "
                                        "B8 01 00 00 00 F0 01 05 ? ? ? ? 48 85 FF 74 ? 89 47 ? 48 8D 5F ? 89 47 ? 48 8D 05")));
        gPatterns.CreateItemReferenceVectorDescriptor = reinterpret_cast<std::uint64_t>(
            hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 6C 24 ? 48 89 74 24 ? 57 48 83 EC 30 83 3D ? ? ? ? ? 48 8B EA 48 8B F1 75 ? E8 ? ? ? ? 48 8B 05 ? ? ? ? 48 8D 54 24 ? 45 33 C0 41 8D 48 ? FF D0 48 8B F8 8B 44 24 ? F0 01 05 ? ? ? ? F0 83 05 ? ? ? ? ? "
                                        "B8 01 00 00 00 F0 01 05 ? ? ? ? 48 85 FF 74 ? 89 47 ? 48 8D 5F ? 89 47 ? 48 8D 4B")));

        // RTTR registers this exact member as EquipPlayersItem, with
        // ItemDescriptor and EquipToSlot parameters.
        gPatterns.C_EquipmentManager__EquipPlayersItem         = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("4C 8B DC 45 88 43 ? 53 48 81 EC 80 00 00 00 48 8B 05")));
        gPatterns.C_EquipmentManager__UnequipPlayersItem       = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("4C 8B DC 53 48 81 EC 80 00 00 00 48 8B 05 ? ? ? ? 48 33 C4 48 89 44 24 ? 48 8D 05 ? ? ? ? 49 89 4B ? 49 89 43 ? 4C 8B C1")));
        gPatterns.C_EquipmentManager__UnequipPlayersArmorSlots = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 55 56 57 48 83 EC 30 48 8B F9 48 8B DA")));
        gPatterns.C_EquipmentManager__UnequipAllArmor          = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 74 24 ? 57 48 83 EC 30 49 8B D8 8B F2")));

        // ReleaseAttack is the controller boundary that commits an ordinary
        // punch. The RunAction hook below proves the attack was accepted,
        // so denied or staged input never produces a network event.
        gPatterns.C_CombatPlayerController__ReleaseAttack = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 74 24 ? 57 48 83 EC 20 48 8B F9 8B F2 48 8B 49 18 48 8D 54 24 ? 45 33 C0 48 8B 89 00 02 00 00")));

        // The engine's own accepted-row resolver indexes this process-global
        // typed database with a 0x230-byte stride. This LEA sequence is unique.
        gPatterns.C_CombatActionAttackDatabase__Instance = hook::get_address<std::uint64_t>(hook::get_pattern(KCDC_HIDE("4C 8D 44 24 30 49 8B D1 48 8D 0D ? ? ? ? E8 ? ? ? ?")), 11, 15);

        // The lower factory consumes an already selected database row rather
        // than consulting player/controller state, so it also works for a
        // channel-less puppet. The exact shared action-state setter body is
        // unique (a second folded-looking body differs in its branch spans).
        gPatterns.C_CombatActionAttackFactory__CreateSelectedAttack = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 57 48 83 EC 20 48 8B FA 49 8B D8 48 8B 51 08 48 8B CF E8 ? ? ? ? 48 8B 03 48 8B 0F 48 89 41 58")));
        gPatterns.C_Action__SetState                                = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 83 EC 28 4C 8B 49 40 44 8B 41 38 89 51 38 4D 85 C9 74 0E 8B 41 3C 83 F8 FF 74 06 80 79 28 00 75 05 48 83 C4 28 C3")));

        // Start/complete are the two halves of the local player's native
        // animation-gated transfer. Observers never enter this gameplay path.
        gPatterns.C_ItemPickUpHelper__StartPickup  = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 74 24 ? 55 57 41 54 41 56 41 57 48 8B EC 48 81 EC 80 00 00 00 48 8B F2 48 8B F9")));
        gPatterns.C_ItemPickUpHelper__CancelPickup = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 48 83 EC 20 48 8B D9 B2 01 48 8B 49 ? E8 ? ? ? ? 48 8B 53")));
        // Begin is the direct transfer seam used by C_Human slot 277. The six
        // other pickup systems that mutate the destination converge on
        // complete; the helper completion above is only one caller.
        gPatterns.C_ItemAttachmentManager__BeginItemTransfer    = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B C4 48 89 58 ? 55 56 57 41 56 41 57 48 8D A8 ? ? ? ? 48 81 EC 60 02 00 00")));
        gPatterns.C_ItemAttachmentManager__CompleteItemTransfer = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 6C 24 ? 56 57 41 56 48 83 EC 50 48 8B DA 48 8B E9")));
        // The first free helper allocates the generic 0x208 actor-animation
        // request in a game-owned shared control block. Item expansion slot 7
        // then turns it into a visual action without dispatching a transfer.
        // Both patterns are unique in WHGame.dll.
        gPatterns.CreateSharedActorAnimRequestImpl                = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 74 24 ? 57 48 83 EC 20 83 3D ? ? ? ? 00 48 8B F1 0F 84 ? ? ? ? 48 8B 05 ? ? ? ? 48 8D 54 24 ? BB 18 02 00 00")));
        gPatterns.C_ActorAnimsItemHandlingExpansion__CreateAction = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 74 24 ? 48 89 7C 24 ? 55 48 8B EC 48 83 EC 60 49 8B F8 48 8B DA 48 8B F1 E8 ? ? ? ? 48 85 C0")));

        // The load/lookup pair that walks env -> animation hub -> per-actor
        // registry is unique to this resolver.
        gPatterns.GetActorAnimationProxy = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 74 24 ? 57 48 83 EC 20 48 8B F1 E8 ? ? ? ? 48 8B 90 ? ? ? ? 48 8B 8A ? ? ? ? 48 8B 01 FF 50 ? 48 8B 4E ? 48 8B F8 48 8B 10 48 8B 5A ? 48 8B 11 FF 52")));

        // Both prologues are unique. IsLightItemById has a byte-identical twin
        // (0x1807DA4D4), so it is taken from the per-hand step's own call.
        gPatterns.AddHandledItemToHand             = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 55 56 57 41 56 48 81 EC 80 00 00 00 48 8B 05 ? ? ? ? 48 33 C4 48 89 44 24 ? 48 8B 72")));
        gPatterns.C_HumanHandHelper__AddItemToHand = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 55 56 57 41 55 41 56 41 57 48 83 EC 70 48 8B 05 ? ? ? ? 48 33 C4 48 89 44 24 ? 49 8B 01 48 8B F2 48 8B F9 4D 8B F1 41 8B E8 45 32 FF 8A 10 88 51 50")));
        gPatterns.IsLightItemById                  = hook::get_call<std::uint64_t>(gPatterns.AddHandledItemToHand + 0x81);

        // The 0x140-byte event frame this builds for state event 118 is
        // unique to the item-handling request.
        gPatterns.C_ItemAttachmentManager__RequestItemHandling = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 55 56 57 41 54 41 55 41 56 41 57 48 8D 6C 24 ? 48 81 EC 40 01 00 00 48 8B 05 ? ? ? ? 48 33 C4 48 89 45 ? 45 33 FF")));

        // Invokes slot 1 of the item-handling driver at +0xD8 with the
        // manager's typed owner. The complete body is unique.
        gPatterns.C_ItemAttachmentManager__FinishItemHandling = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 83 EC 28 48 8B D1 48 8B 89 D8 00 00 00 48 85 C9 74 ? 48 8B 01 48 8B 52 38 FF 50 08 48 83 C4 28 C3")));

        // Forwards hidden WUID return storage to actor model subobject +0x258
        // virtual slot 1, and takes nothing else. The complete body is unique.
        gPatterns.C_ItemAttachmentManager__GetHandledItemId = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 48 83 EC 20 48 8B 41 38 48 8B DA 48 8B 88 90 09 00 00 48 81 C1 58 02 00 00 48 8B 01 FF 50 08 48 8B C3 48 83 C4 20 5B C3")));

        // The local R-toggle calls this exact leaf when the torch is already
        // drawn. The +0x78 load is the manager's typed torch WUID slot.
        gPatterns.C_ItemAttachmentManager__HolsterTorch = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 83 EC 28 48 8B 51 78 48 3B 15 ? ? ? ? 74 ? E8 ? ? ? ? 48 83 C4 28 C3")));

        // The engine's global allocation entry point, taken from a `mov rax,
        // cs:alloc` + fixed-size preamble. Every native creator loads the same
        // global, so any one site will do; the trailing `mov ecx, [rbp+30h]`
        // pins the site at 0x1816B8250, the only one of fourteen with it, so
        // the signature matches once whether it is scanned or seeded from the
        // pattern table. Nothing calls it today; it is kept because
        // constructing any game-heap object needs it.
        gPatterns.CryModuleAllocFnPtr = hook::get_address<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B 05 ? ? ? ? 48 8D 55 ? 45 33 C0 B9 68 02 00 00 FF D0 8B 4D 30")), 3, 7);

        // Runtime item presentation and use.
        gPatterns.C_UIFlashListItem__Construct        = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 20 55 56 57 41 54 41 55 41 56 41 57 48 8B EC 48 83 EC 70 0F 29 74 24 60")));
        gPatterns.C_UIFlashInfoItem__Construct        = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("4C 89 44 24 18 55 53 56 57 41 54 41 55 41 56 41 57 48 8B EC 48 83 EC 28")));
        gPatterns.C_UIApseInventory__BuildActionHint  = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 08 55 56 57 41 54 41 55 41 56 41 57 48 83 EC 50 48 8B E9")));
        gPatterns.C_UIApseInventory__ActivateItem     = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 20 48 8B F1 48 8B FA 48 8B CA E8 ? ? ? ? 48 8B D7")));
        gPatterns.InventoryActionCallback__Invoke     = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 10 48 89 74 24 18 55 57 41 56 48 8D 6C 24 B9 48 81 EC E0 00 00 00 48 8B 02")));
        gPatterns.InventorySecondaryActionCallback__Invoke = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 57 48 83 EC 20 48 8B 02 48 8B F9 48 8B 09")));
        gPatterns.C_Item__NotifyChanged               = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 20 41 8B F8 45 8B C8")));
        gPatterns.C_Item__CloneForHolder              = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 20 48 8B DA 48 8B F1 E8 ? ? ? ? 48 8B 56 48")));
        gPatterns.C_ScriptBindPickableItem__GetUIName = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 48 83 EC 20 48 8B DA E8 ? ? ? ? 48 8B 08 4C 8B 81 00 04 00 00")));
        // RTTR exposes this C_Item member as the read-only `ItemClass`
        // property returning S_ItemClassGUID by value.
        gPatterns.C_Item__GetItemClassRaw = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 48 83 EC 20 48 8B DA E8 ? ? ? ? 0F 10 40")));
        // Answers *(item+0x48) ? *(that+0x18) : "Unknown item". The two
        // loads either side of the null test pin it.
        gPatterns.C_Item__GetNameCStrRaw = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B 41 48 48 85 C0 74 ? 48 8B 40 18 C3 48 8D 05")));

        gPatterns.C_Item__GetNameRaw              = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 48 83 EC 20 48 8B DA E8 ? ? ? ? 48 8B D0 48 8B CB E8 ? ? ? ? 48 8B C3 48 83 C4 20 5B C3 CC CC 48 8B 41")));
        gPatterns.C_Item__GetCategoryRaw          = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 83 EC 28 E8 ? ? ? ? 48 8B 08 48 8B 51 08 48 8B C8 48 83 C4 28 48 FF E2 CC CC")));
        gPatterns.C_Item__GetTypeRaw              = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 83 EC 28 E8 ? ? ? ? 48 8B 08 48 8B 51 50")));
        gPatterns.C_Item__GetSubtypeRaw           = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 83 EC 28 E8 ? ? ? ? 48 8B 08 48 8B 51 ? 48 8B C8 48 83 C4 28 48 FF E2 CC CC 48 63 81")));
        gPatterns.C_Item__GetNewUnitPriceRaw      = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 48 83 EC 20 48 8B D9 E8 ? ? ? ? 48 8B 05 ? ? ? ? 48 8D 54 24 ? 4C 8B C3 48 8D 0D ? ? ? ? FF 50 ? 8B 44 24 ? 48 83 C4 20 5B C3 CC CC CC 40 53")));
        gPatterns.C_Item__GetCurrentUnitPriceRaw  = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 48 83 EC 20 48 8B D9 E8 ? ? ? ? 48 8B 05 ? ? ? ? 48 8D 54 24 38 4C 8B C3 48 8D 0D ? ? ? ? FF 50 48 8B 44 24 38 48 83 C4 20 5B C3 CC CC CC")));
        gPatterns.C_Item__GetNewStackPriceRaw     = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 48 83 EC 20 8B 59 ? E8 ? ? ? ? 0F AF C3 48 83 C4 20 5B C3 CC 48 8D 05")));
        gPatterns.C_Item__GetCurrentStackPriceRaw = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 48 83 EC 20 8B 59 ? E8 ? ? ? ? 0F AF C3 48 83 C4 20 5B C3 CC 48 89 5C 24")));
        gPatterns.C_Item__GetQualityRaw           = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B 49 ? 48 85 C9 74 ? 48 8B 01 48 FF 60 ? B8 01 00 00 00 C3 CC CC 48 89 5C 24")));
        gPatterns.C_Item__GetConditionRaw         = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 48 83 EC 20 BA 1A 00 00 00 48 8B D9 E8 ? ? ? ? 84 C0 74 ? 48 8B CB E8 ? ? ? ? 48 8D 54 24")));
        gPatterns.C_Item__GetMaxQualityRaw        = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 83 EC 28 E8 ? ? ? ? 48 8B 08 48 8B 51 ? 48 8B C8 48 83 C4 28 48 FF E2 CC CC 40 53 48 83 EC 20 48 8B 02")));
        gPatterns.C_Item__SetItemPhaseIdRaw       = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 57 48 83 EC 20 8B DA 48 8B F9 E8 ? ? ? ? 33 D2 48 8B C8 4C 8B 00 41 FF 90 C0 00 00 00")));
        gPatterns.C_Item__AdvanceItemPhaseRaw     = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("F3 0F 58 49 64 E9 ? ? ? ? CC CC 48 89 5C 24")));
        gPatterns.C_Item__SetItemHealthRaw        = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 48 83 EC 40 0F 29 74 24 ? BA 1A 00 00 00")));
        // CCryDX12SwapChain::Present loads the engine device from +0x10 and
        // the native NCryDX12::CSwapChain from +0x18 before running the
        // renderer's presentation prologue. That prologue pair is what makes
        // this prologue unique in WHGame.dll.
        gPatterns.CCryDX12SwapChain__Present = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 6C 24 ? 48 89 74 24 ? 57 48 83 EC 20 48 8B 59 10 8B EA 48 8B 51 18 48 8B F9")));

        // CSystem::WndProc reads the global ISystem into rsi before walking its
        // IWindowMessageHandler vector; the load plus the four-argument shuffle
        // that follows pins the prologue.
        gPatterns.CSystem__WndProc = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 6C 24 ? 48 89 74 24 ? 57 41 54 41 55 41 56 41 57 48 83 EC 50 48 8B 35 ? ? ? ? 4D 8B F9 4D 8B E0 44 8B EA 4C 8B F1")));

        // The lea of the quit-reason member at +0x5A0 is what separates this
        // from every other spill-and-frame prologue.
        gPatterns.CSystem__Quit = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 74 24 ? 48 89 7C 24 ? 41 56 48 83 EC 40 45 8B F0 48 8D 99 A0 05 00 00")));

        // Loads the enabled list (+0x30/+0x38) and the std::function's
        // callable slot (+0x38) straight after the prologue.
        gPatterns.C_ModManager__ForEachEnabledMod = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57 48 83 EC 20 48 8B 79 30 48 8D 5A 38 48 8B 69 38 48 8B F2 48 3B FD 0F 85")));
        // The scan opens by clearing the enabled list (+0x30/+0x38) after a
        // 0xD0 frame; with the prologue that is unique in WHGame.dll.
        gPatterns.C_ModManager__ScanMods = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B C4 48 89 58 10 48 89 70 18 48 89 78 20 55 41 54 41 55 41 56 41 57 48 8D 68 A1 48 81 EC D0 00 00 00 48 8B 41 30 48 8B D9 48 3B 41 38")));

        // CCryAction::PostUpdate opens by testing the editor bit of its update
        // flags and jumping over the whole body; the test/jcc pair ahead of the
        // 0x80-byte frame prologue is unique.
        gPatterns.CCryAction__PostUpdate = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("41 F6 C0 20 0F 85 ? ? ? ? 48 8B C4 48 89 58 ? 48 89 70 ? 44 89 40 ? 55 57 41 57 48 8B EC 48 81 EC 80 00 00 00")));

        // The zeroed r12 and the movzx of the reason out of r8w are what pin
        // this prologue; the frame ahead of them is not unique on its own.
        gPatterns.CCryAction__PauseGame = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 74 24 ? 48 89 7C 24 ? 41 54 41 56 41 57 48 81 EC 80 00 00 00 45 33 E4 45 0F B7 F8")));

        // The getter's own body has a twin elsewhere; its call in
        // C_UIFullUIModeHelper::SetWorldSlowdown, stored straight into the
        // helper's +0xC8, does not.
        gPatterns.GetApsePauseRatio = hook::get_call<std::uint64_t>(reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("E8 ? ? ? ? E8 ? ? ? ? F3 0F 11 86 C8 00 00 00"), 5)));

        gPatterns.CView__UpdateAudioListener = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 83 EC 28 48 8B 89 ? ? ? ? 48 85 C9 74 ? 48 8B 01 45 33 C0")));

        // Both angle conversions open by loading their source straight out
        // of the second argument, which is what separates them from the rest
        // of the inlined vector maths.
        gPatterns.Quat__SetRotationXYZ = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B C4 48 89 58 ? 57 48 81 EC 80 00 00 00 0F 29 70 ? 48 8B DA F3 0F 10 32")));
        gPatterns.Ang3__FromQuat       = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 57 48 83 EC 30 F3 0F 10 0A")));

        // The debug-camera surface. SetDebugCameraMode is pinned by the
        // compare of its mode argument against 4 right after the prologue;
        // the other three by their own frames, SetMode by its 0x170-byte
        // reserve and SetOrbitPosition by its 0xB0-byte one.
        gPatterns.C_CameraManager__SetDebugCameraMode = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 74 24 ? 57 48 83 EC 20 8B F2 48 8B F9 83 FA 04")));
        gPatterns.C_CameraDebug__SetMode              = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B C4 48 89 58 ? 48 89 70 ? 48 89 78 ? 55 48 8D 68 ? 48 81 EC 70 01 00 00 0F 29 70 ? 8B FA")));
        gPatterns.C_CameraDebug__SetOrbitPosition     = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B C4 48 89 58 ? 48 89 70 ? 48 89 78 ? 55 41 56 41 57 48 8D 68 ? 48 81 EC B0 00 00 00 0F 29 70 ? 4C 8B FA")));
        gPatterns.C_CameraDebug__EnsureInitialized    = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 48 83 EC 20 80 B9 ? ? ? ? ? 48 8B D9 75 ? C6 81 ? ? ? ? ? 48 8B 0D")));

        gPatterns.C_Item__WashItemRaw = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 48 83 EC 30 0F 29 74 24 ? BA 1A 00 00 00 0F 28 F1 48 8B D9 E8 ? ? ? ? 84 C0 74 ? 48 8B CB E8 ? ? ? ? 0F 28 CE")));

        // The armour accessor asks the item's class for id 4 through slot 3
        // straight after its prologue. The same class test sits inside
        // 0x1809D9CE4 and C_IsWeaponLoaded, but never there, so the pattern
        // stays unique even with the 4 wildcarded. SetDirt's first eight
        // bytes -- a float taken out of EDX and scaled -- are unique on their
        // own, and so is SetBlood's compare of the dword at +0xA9 against its
        // source.
        gPatterns.C_Item__GetOrCreateArmorRuntimeData = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 48 83 EC 20 48 8B D9 E8 ? ? ? ? 48 8B C8 48 8B 10 4C 8B 42 18 BA 04 00 00 00 41 FF D0 84 C0 74")));
        gPatterns.C_ArmorRuntimeData__SetDirt         = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("66 0F 6E C2 F3 0F 59 05 ? ? ? ? F3 0F 2C C0 3A 81 A8 00 00 00 75 01 C3 83 89 B0 00 00 00 01")));
        gPatterns.C_ArmorRuntimeData__SetBlood        = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 83 EC 28 8B 81 A9 00 00 00 3B 02 75 ? 0F B7 81 AD 00 00 00 66 3B 42 04 74")));

        // C_UISaveLoad::StartNewGame's prologue reserves its exact 0x1B8-byte
        // frame after the same six preserved registers.
        gPatterns.C_UISaveLoad__StartNewGame = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 55 53 56 57 41 56 41 57 48 8D AC 24 ? ? ? ? 48 81 EC B8 01 00 00")));

        // C_UIMenu::C_UIMenu stores its primary vtable as the first thing it
        // does after its base; that `lea` is the only signature-reachable
        // handle on the vtable.
        gPatterns.C_UIMenu__VTable = hook::get_address<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 48 83 EC 30 48 8B D9 E8 ? ? ? ? 48 8D 0D ? ? ? ? 48 8B D3 48 89 0B")), 17, 21);

        // C_UILoadingScreen's destructor takes its own sub-object into rbx
        // before it stores the vtable, which is what makes it unique; the
        // destructor entry below is the same function.
        gPatterns.C_UILoadingScreen__VTable                   = hook::get_address<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 57 48 83 EC 20 48 8D 59 58 48 8B F9 48 8D 05 ? ? ? ? 48 89 01")), 20, 24);
        gPatterns.C_UILoadingScreen__ReturnToMainMenuState    = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 48 83 EC 20 48 8B D9 48 8B 49 ? E8 ? ? ? ? 48 8D 48")));
        gPatterns.C_UILoadingScreen__OnSystemEvent            = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 74 24 ? 57 48 83 EC 20 48 8B F9 49 8B F1 48 8B 0D ? ? ? ? 8B DA 48 8B 01 FF 90 C8 06 00 00")));
        gPatterns.C_UILoadingScreen__Destructor               = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 57 48 83 EC 20 48 8D 59 58 48 8B F9 48 8D 05 ? ? ? ? 48 89 01 48 8D 05 ? ? ? ? 48 89 41 10")));
        gPatterns.C_UILoadingScreen__StopVideoOrLoadingScreen = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 74 24 ? 57 48 83 EC 20 48 8B F9 48 8B 0D ? ? ? ? 48 8B 01 FF 90 10 03 00 00 48 8B 4F 08 48 8B D8")));

        // The constructor stores its vtable and nulls +0x08 before anything
        // else; the destructor re-stores it and unregisters through the
        // service's slot 1; Start reads the callback's impl pointer at +0x38.
        gPatterns.C_ReadinessProgress__Constructor = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 74 24 ? 48 89 7C 24 ? 41 56 48 83 EC 20 48 8D 05 ? ? ? ? 48 C7 41 ? ? ? ? ? 48 89 01 41 8B F0")));
        gPatterns.C_ReadinessProgress__Destructor  = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 48 83 EC 20 48 8D 05 ? ? ? ? 48 8B D9 48 89 01 48 8B 49 ? 48 85 C9 74 ? 48 8B 01 48 8B D3 FF 50 ? 48 C7 43")));
        gPatterns.C_ReadinessObserver__Start       = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("4C 8B DC 49 89 5B ? 49 89 73 ? 57 48 83 EC 70 48 8B F9 49 C7 43 ? ? ? ? ? 49 8B 48")));
        // StartEx(mask, timeout, delayFrames, callback): turns the timeout into an absolute deadline
        // at +0x20; a negative one stores -100000, which the expiry test reads as never.
        gPatterns.C_ReadinessObserver__StartEx     = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 10 48 89 74 24 20 57 48 83 EC 20 48 8B 7C 24 50 0F 28 CA 0F 2F 0D ? ? ? ? 41 8B F1 89 51 18 48 8B D9")));
        // Both test the latch byte at +0x10 before anything else.
        gPatterns.GameSessionLatch__Start = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 48 83 EC 40 80 79 ? ? 48 8B D9 75 ? 48 8D 05")));
        gPatterns.GameSessionLatch__End   = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 48 83 EC 40 48 8B D9 33 C9 38 4B")));

        // C_UIOverlay's constructor stores three vtables after its base; the
        // primary one is the `lea rcx` that follows the +0x10 store. The
        // publisher loads the 3D engine and the first parameter name before
        // it branches on `enabled`, which no other function repeats.
        gPatterns.C_UIOverlay__VTable         = hook::get_address<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B D9 E8 ? ? ? ? 45 33 F6 C7 83 F1 00 00 00 00 00 00 FF 48 8D 05 ? ? ? ? 4C 89 B3 BC 00 00 00 48 89 43 10 48 8D 0D")), 42, 46);
        gPatterns.C_UIOverlay__ApplyUserColor = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 48 83 EC 20 48 8B D9 84 D2 48 8B 0D ? ? ? ? 48 8D 15 ? ? ? ? 41 B1 01 48 8B 01 0F 84")));
        gPatterns.guimodule__RaiseLevelGameplayStart = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 83 EC 28 E8 ? ? ? ? 48 8B 88 70 01 00 00 48 8B 01 FF 90 A8 02 00 00 45 33 C9 45 33 C0 BA 21 00 00 00")));

        // CLocalizedStringsManager::LocalizeLabel saves six registers, frames
        // with `mov rbp, rsp` and clears the result flag before touching the
        // label -- a prologue no other function in WHGame.dll repeats.
        gPatterns.CLocalizedStringsManager__LocalizeLabel = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 74 24 ? 48 89 7C 24 ? 55 41 54 41 55 41 56 41 57 48 8B EC 48 83 EC 70 33 FF")));

        // C_Keybinds::ParseSuperactions spills both its arguments to the shadow
        // space before saving a register at all, then frames with `lea rbp` off
        // a stack large enough to need the full displacement -- the one
        // prologue in WHGame.dll that opens that way.
        gPatterns.C_Keybinds__ParseSuperactions = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 54 24 ? 48 89 4C 24 ? 55 53 56 57 41 54 41 55 41 56 41 57 48 8D AC 24 F8 FE FF FF")));

        // CryStringT<char>::operator=(const char *) keeps the source pointer
        // in both R9 and RCX before measuring it, which is what distinguishes
        // it from the rest of the string template's tiny thunks.
        gPatterns.CryStringT_char__AssignCStr = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 48 83 EC 20 48 8B D9 4C 8B CA 48 8B CA")));

        // The constructor is pinned by the load of the shared empty-buffer
        // guard right after its prologue; the destructor by the `payload - 12`
        // step onto the refcount header, which no other tiny routine repeats.
        gPatterns.CryStringT_char__Construct = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 6C 24 ? 48 89 74 24 ? 57 48 83 EC 20 44 8B 05 ? ? ? ? 48 8B F1")));
        gPatterns.CryStringT_char__Destruct  = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 83 EC 28 48 8B 09 48 83 C1 F4")));

        // The constructor seeds both caches from the empty-string allocator
        // and steps each payload past its 12-byte header; the destructor tears
        // the delimiter at +0x20 down first. The append keeps the type in EBX.
        gPatterns.SUIArguments__Construct            = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 48 83 EC 20 48 8D 05 ? ? ? ? 48 8B D9 48 89 01 E8 ? ? ? ? 48 83 C0 0C 48 89 43 ? E8 ? ? ? ? 48 83 C0 0C 48 8D 4B")));
        gPatterns.SUIArguments__Destruct             = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 48 83 EC 20 48 8B D9 48 83 C1 20 E8 ? ? ? ? 48 8D 4B ? E8 ? ? ? ? 48 8B 4B ? 48 83 E9 0C E8 ? ? ? ? 48 8D 4B")));
        gPatterns.SUIArguments__AppendStringWithType = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 57 48 83 EC 60 48 8B F9 41 8B D8")));

        // SetFlashArray builds its arguments on the stack before handing them
        // on; the other two open by calling the element resolver at vtable
        // +0x60 and then read the element at +0x48.
        gPatterns.C_UIFlashBase__SetFlashArray     = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 74 24 ? 57 48 83 EC 50 48 8B F1 49 8B D8 48 8D 4C 24 ? 48 8B FA E8 ? ? ? ? 4C 8D 44 24")));
        gPatterns.C_UIFlashBase__SetFlashArguments = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 74 24 ? 57 48 83 EC 20 48 8B 01 49 8B F8 48 8B F2 48 8B D9 FF 50 ? 48 8B 4B")));
        gPatterns.C_UIFlashBase__CallFlashFunction = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 57 48 83 EC 60 48 8B 01 48 8B FA 48 8B D9 FF 50 ? 48 8B 5B ? 48 85 DB 74")));

        // Readable books. CallFlashFunction<float,int,int> has byte-identical
        // template siblings, so it is taken off the one call in
        // C_UIBook::SetFullBook (I_UIBook slot 1) that passes it "SetFullBook".
        gPatterns.C_UIFlashBase__CallFlashFunctionFloatIntInt = hook::get_call<std::uint64_t>(reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("44 89 4C 24 ? F3 0F 11 54 24 ? 55 53 56 57 41 56 48 8D 6C 24"), 0x11E)));
        gPatterns.SUIArguments__AppendUInt                    = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 57 48 83 EC 60 8B 02 41 B0 01")));
        gPatterns.C_Actor__GetMinigame                        = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 6C 24 ? 48 89 74 24 ? 57 48 83 EC 30 41 8A E9 41 8A D8")));
        // Stone throwing. The pile lookup is taken off StartWithPile's own call to it.
        gPatterns.C_StoneThrowing__StartWithPile           = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 18 55 56 57 48 8B EC 48 81 EC 80 00 00 00 48 8B 05 ? ? ? ? 48 33 C4 48 89 45 F0 48 8B D9")));
        gPatterns.C_StoneThrowing__RequestThrow            = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 10 48 89 7C 24 18 55 48 8B EC 48 81 EC 80 00 00 00 48 8B 05 ? ? ? ? 48 33 C4 48 89 45 F0 48 8B D9 BA 03 00 00 00 48 83 C1 70 E8")));
        gPatterns.C_StoneThrowing__Finish                  =reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B C4 48 89 58 10 48 89 70 18 48 89 78 20 55 48 8D 68 A1 48 81 EC 00 01 00 00 48 8B D9")));
        gPatterns.C_StoneThrowingPile__Find                = hook::get_call<std::uint64_t>(reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("8B CA E8 ? ? ? ? 48 89 83 98 00 00 00"), 2)));
        gPatterns.C_StoneThrowingPile__ResetStonePile      = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B C4 48 89 58 10 48 89 70 18 48 89 78 20 55 41 55 41 56 48 8D 68 A1 48 81 EC C0 00 00 00")));
        gPatterns.C_StoneThrowingPile__SetActiveStoneCount = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B C4 48 89 58 08 48 89 68 10 48 89 70 18 48 89 78 20 41 56 48 83 EC 40 8B EA")));
        gPatterns.C_StoneThrowingPile__SetStoneUsageState  = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B C4 48 89 58 08 48 89 70 10 55 57 41 57")));
        gPatterns.C_StoneThrowingPile__PrepareStoneForGrab = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 10 57 48 83 EC 30 48 83 79 78 00")));
        // The lean. Start opens on the PrepareInteraction call (slot 18, +0x90) that gates it.
        gPatterns.C_BattleArchery__Start                      = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 08 48 89 74 24 10 4C 89 4C 24 20 55 57 41 54 41 56 41 57 48 8B EC 48 83 EC 40 48 8B 01 45 8B F8 48 8B DA 48 8B F1 FF 90 90 00 00 00")));
        gPatterns.C_Battlement__GetUnoccupiedSlot             = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("4C 8B 51 68 4C 8B 41 70 4D 2B C2 48 63 CA 49 C1 F8 03 49 3B C8 73 ? 83 FA FF")));
        gPatterns.C_BattlementManager__TryStartBattleArchery  = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B C4 48 89 58 08 48 89 68 10 48 89 70 18 57 41 54 41 55 41 56 41 57 48 83 EC 60 0F 29 70 C8 45 8B E0 4C 8B F2")));
        gPatterns.C_Player__CanReadBookHere                   = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 6C 24 ? 48 89 74 24 ? 57 48 83 EC 20 48 8B 01 48 8B F9 FF 90")));
        gPatterns.C_Reading__Start                            = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 55 56 57 48 8D 6C 24 ? 48 81 EC A0 00 00 00 48 8B 05 ? ? ? ? 48 33 C4 48 89 45 ? 48 8B 01")));
        gPatterns.C_Reading__Finish                           = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 57 48 81 EC 10 01 00 00 48 8B D9")));
        gPatterns.C_Reading__ReturnToInventory                = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("4C 8B DC 49 89 5B ? 57 48 81 EC D0 00 00 00 48 8B 05 ? ? ? ? 48 33 C4 48 89 84 24 ? ? ? ? 33 D2")));
        gPatterns.C_DocumentItem__PushContentToUI             = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B C4 48 89 58 ? 48 89 70 ? 48 89 78 ? 41 56 48 83 EC 40 0F 29 70 ? 48 8B F1 E8")));
        gPatterns.C_RPGMinigames__ApplyReadingStarted         = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B C4 55 56 57 41 54 41 55 41 56 41 57 48 81 EC 90 00 00 00")));

        // The placer frames with `mov rbp, rsp` over an 0x80-byte reserve and
        // immediately tests the cursor-on-map flag at +0x608; that byte
        // compare against the fixed offset pins the prologue.
        gPatterns.C_UIMap__PlaceCheckpointAtCursor = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 55 53 57 48 8B EC 48 81 EC 80 00 00 00 80 B9 08 06 00 00 00")));

        // The remover hands the finder its stack return slot and then tests
        // the shared_ptr it got back for null; the has-a-marker test is the
        // same call followed by a compare of the object word against zero.
        gPatterns.C_UIMap__RemoveCheckpoint = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 48 83 EC 40 48 8D 54 24 ? 48 8B D9 E8 ? ? ? ? 48 83 7C 24")));
        gPatterns.C_UIMap__HasCheckpoint    = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 48 83 EC 30 48 8D 54 24 ? E8 ? ? ? ? 48 8B 4C 24 ? 48 83 38 00")));

        // The finder keeps the hidden return pointer in RBX and `this` in RSI
        // before resolving the current map through the WH environment.
        gPatterns.C_UIMap__FindCheckpointForCurrentMap = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 6C 24 ? 48 89 74 24 ? 57 48 83 EC 30 48 8B DA 48 8B F1 E8 ? ? ? ? 48 8B 88")));

        // The constructor stores its primary vtable right after taking `this`
        // into rdi; that `lea` is the only signature-reachable handle on it.
        gPatterns.C_UIApse__VTable = hook::get_address<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 6C 24 ? 48 89 74 24 ? 57 41 56 41 57 48 83 EC 30 48 8B F9 48 8D 05 ? ? ? ? 48 89 01 83 CA FF 33 ED 48 8D 05")), 30, 34);

        // Hide frames on rbp with a 0x90 reserve and a stack cookie, then
        // takes the state machine at this+0x68 into rsi before anything else.
        gPatterns.I_UIApse__Hide = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B C4 48 89 58 ? 48 89 70 ? 48 89 78 ? 55 41 56 41 57 48 8D 68 ? 48 81 EC 90 00 00 00 48 8B 05 ? ? ? ? 48 33 C4 48 89 45 ? 48 8D 71")));

        gPatterns.I_UIApse__Show = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 55 56 57 48 83 EC 20 80 B9 ? ? ? ? ? 49 8B F8")));
        gPatterns.C_UIApseCamera__MoveToView = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B C4 48 89 58 ? 48 89 70 ? 55 57 41 54 41 55 41 56 48 8D 68 ? 48 81 EC D0 00 00 00 F2 0F 10 41")));

        // ChangeView frames on rbp over a bare 0x20 reserve and takes the
        // view property at this+0x28 into rbx straight away.
        gPatterns.C_UIApse__ChangeView = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 74 24 ? 48 89 7C 24 ? 55 41 56 41 57 48 8B EC 48 83 EC 20 48 8D 59")));

        // Six other functions open the same way; this one alone reads its
        // owner off this+0x430.
        gPatterns.C_UIApseCharacter__BindCharacterHub = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("4C 8B DC 49 89 5B ? 49 89 73 ? 57 48 83 EC 60 8B 15 ? ? ? ? 48 8D 05 ? ? ? ? 48 8B B9 30 04 00 00 48 8B D9")));

        // Reads the FOV off this+0x158 and the position off this+0x140; one
        // other function shares the shape with different offsets.
        gPatterns.C_UIApseCamera__GetCameraParams = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("F3 0F 10 81 58 01 00 00 33 C0 F3 0F 59 05 ? ? ? ? F3 0F 11 42 ? 48 89 42 ? 89 42 ? C7 42 ? ? ? ? ? F2 0F 10 81 40 01 00 00")));

        // Frames 0x2A0 bytes and stores the layout at this+0xB0 before anything else.
        gPatterns.CKeyboard__ChangeLanguage = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 18 55 56 57 41 56 41 57 48 8D AC 24 60 FE FF FF 48 81 EC A0 02 00 00 48 8B 05 ? ? ? ? 48 33 C4 48 89 85 90 01 00 00 48 89 91 B0 00 00 00")));

        // The pass builder frames on the old stack top in rax, spills the
        // pipeline key beside it and takes the result pointer into rbx; the
        // context lookup tests the key's variant index (+8) and falls back to
        // the base context at this+0x2E70.
        gPatterns.SRenderingPassInfo__CreateGeneralPassRenderingInfo = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B C4 48 89 58 08 48 89 70 18 89 50 10 57 48 83 EC 30 8B 15 ? ? ? ? 48 8B D9 0F 29 70 E8 41 8B F1")));
        gPatterns.CRenderer__FindDisplayContext = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 74 24 ? 57 48 83 EC 20 33 FF 48 8B F2 48 8B D9 40 38 7A 08 0F 85 ? ? ? ? 48 8B 81 70 2E 00 00")));

        // The camera's constructor frames on rax and spills four registers
        // before zeroing r15d; SetFrustum tests the adapt-FOV byte at +0x48
        // straight after its frame. The light's constructor zeroes ebp and
        // writes -1 into +0x08; SetLightColor copies the colour into +0x20
        // field by field; SetRadius stores its two floats at +0x204/+0x210.
        gPatterns.CCamera__Construct          = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B C4 48 89 58 08 48 89 68 10 48 89 70 18 48 89 78 20 41 54 41 56 41 57 48 83 EC 40 45 33 FF")));
        gPatterns.CCamera__SetFrustum         = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B C4 48 89 58 08 48 89 70 10 57 48 83 EC 50 80 79 48 00")));
        gPatterns.SRenderLight__Construct     = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57 48 83 EC 20 33 ED C7 41 08 FF FF FF FF")));
        gPatterns.SRenderLight__Destruct      = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 48 83 EC 20 48 8B D9 E8 ? ? ? ? 48 8D 8B ? ? ? ? E8 ? ? ? ? 48 8D 8B ? ? ? ? E8 ? ? ? ? 48 8D 8B ? ? ? ? E8 ? ? ? ? 48 8B 8B")));
        gPatterns.SRenderLight__SetLightColor = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("8B 02 89 41 20 8B 42 04 89 41 24 8B 42 08 89 41 28 8B 42 0C 89 41 2C 8B 02")));
        gPatterns.SRenderLight__SetRadius     = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("F3 0F 11 89 04 02 00 00 F3 0F 11 91 10 02 00 00")));

        // The copy reads the dummy's soul off +0x668 and passes `true` in r8b
        // to the soul copy before anything else.
        gPatterns.C_Actor__CopyForInventoryPreview = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 57 48 83 EC 20 48 8B F9 48 8B DA 48 8B 89 ? ? ? ? 41 B0 01")));

        // The only routine spilling a byte argument with `mov r8b` and then
        // framing with `lea rbp, [rsp-50h]` over a 0x150 reserve.
        gPatterns.C_UIMap__SetPoiMarkersOfTypeVisible = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 44 88 44 24 ? 55 56 57 41 56 41 57 48 8D 6C 24 ? 48 81 EC 50 01 00 00")));

        // Opens by testing the map-screen flag at +0x748.
        gPatterns.C_UIMap__SyncCheckpointToFlash = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 74 24 ? 57 48 83 EC 30 80 B9 48 07 00 00 00 48 8B D9")));

        // Also opens by testing the map-screen flag, but keeps the incoming
        // state byte in dil rather than taking `this` into rbx.
        gPatterns.C_UIMap__OnUIStateChanged = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 74 24 ? 57 48 83 EC 20 80 B9 48 07 00 00 00 40 8A FA")));

        // The POI rebuild pushes seven registers over a 0x130 frame and then
        // reads its function-static guard.
        gPatterns.C_UIMap__RefreshPoiMarkers = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 55 53 56 57 41 54 41 56 41 57 48 8D 6C 24 ? 48 81 EC 30 01 00 00 8B 15")));
        // Unique; its body from `0F 10 43 10` alone is too.
        gPatterns.C_UIMap__FindMapLabelLocation = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 48 83 EC 30 48 8B D9 E8 ? ? ? ? 0F 10 43 10 48 8B 90 30 01 00 00 F3 0F 7F 44 24 20 48 8B 8A B8 00 00 00")));

        // The row factory has a near-identical twin, so it is taken from its
        // call inside the rebuild: type in EDX, the visibility test, then the
        // mark in RDX and the return slot in RCX.
        gPatterns.MakeFlashMapPoiMarker = hook::get_call<std::uint64_t>(reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("8B 53 04 48 8B CE E8 ? ? ? ? 84 C0 74 ? 48 8B D3 48 8D 4D ? E8"), 22)));

        // Opens by measuring the travel route vector at +0x90 and +0x98.
        gPatterns.C_FastTravel__ValidateTravelStart = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 74 24 ? 57 48 83 EC 50 48 8B 81 98 00 00 00 48 8B F9 48 2B 81 90 00 00 00")));

        // The offer opens by taking the page into RSI and the GUID string
        // into RBX over a 0x80 reserve; the teardown opens by testing the
        // three fields it guards on, at +0x5C8, +0x5D0 and +0x458.
        gPatterns.PrepareFastTravelToPoi = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 74 24 ? 57 48 81 EC 80 00 00 00 0F 29 74 24 ? 48 8B 05 ? ? ? ? 48 33 C4 48 89 44 24 ? 48 8B DA 48 8B F1")));
        gPatterns.ClearFastTravelPoi     = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 57 48 83 EC 20 33 C0 48 8B FA 48 8B D9 48 39 81 C8 05 00 00 74 ? 38 81 D0 05 00 00 75 ?")));

        // _Decref is pinned by its interlocked decrement of the uses field at
        // control block +0x08 immediately after the prologue.
        gPatterns.std__Ref_count_base__Decref = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 48 83 EC 20 48 8B D9 83 C8 FF F0 0F C1 41 ? 83 F8 01 74")));

        // The compass lookup is one instantiation of the GUI module's
        // page-finding template, and its ten siblings share its prologue byte
        // for byte -- only the vtable slots and the memo global differ, and
        // those sit past a relative call. So it is taken from the unique call
        // inside C_UIMap::RemoveCheckpoint instead, where `mov rcx, [rbx+8]`
        // loads the GUI module out of the map page.
        gPatterns.GetUICompass = hook::get_opcode_address("48 8B 4B 08 E8 ? ? ? ? 48 8D 54 24 ? 4C 8B C0", 4);

        // The factory pairs its sret in RSI with the state pointer in R14,
        // which no other prologue in the module does.
        gPatterns.MakeCompassMark = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 6C 24 ? 48 89 74 24 ? 57 41 56 41 57 48 83 EC 30 48 8B F1 4D 8B F1")));

        // Both mark-vector entry points step `this` onto the vector at +0x60
        // as their first act; the adder saves the incoming shared_ptr in RDI
        // first, which is what separates the two. The reset clears the lookup
        // hash at +0x78 instead, and SetState loads the flash owner at +0x38.
        gPatterns.C_UICompass__AddMark        = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 57 48 83 EC 20 48 8B D9 48 8B FA 48 83 C1 60 E8 ? ? ? ? 48 8B 0F")));
        gPatterns.C_UICompass__RemoveMark     = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 57 48 83 EC 20 48 83 C1 60")));
        gPatterns.C_UICompass__RemoveAllMarks = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 48 83 EC 30 48 8B D9 48 83 C1 78")));
        gPatterns.C_CompassMark__SetState     = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 57 48 83 EC 20 48 8B 79 ? 48 8B D9 89 51")));

        // The lazy combat-actor getter opens by testing the action queue at
        // +0x280 against zero and keeping the actor in RBX.
        gPatterns.C_Actor__GetOrCreateCombatActor = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 48 83 EC 20 48 83 B9 ? ? ? ? ? 48 8B D9 75 ? 33 C0 EB")));

        // IsPlayerControlled is a three-step read -- game object at +0x28, its
        // vtable slot 16, then a 16-bit test of the channel id.
        gPatterns.C_Actor__IsPlayerControlled = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 83 EC 28 48 8B 49 ? 48 8B 01 FF 90 ? ? ? ? 66 85 C0")));
        // The IsPlayerControlled calls that mean "this machine's player", as
        // the return address each call leaves (NativePlayerModule::IsLocalPlayerSite).
        gPatterns.LocalPlayerSite__RefreshRiderMagnetismController = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B 89 F8 00 00 00 E8 ? ? ? ? 48 85 C0 0F 84 ? ? ? ? 48 8B C8 E8 ? ? ? ? 84 C0 0F 84 ? ? ? ?"), 29));
        gPatterns.LocalPlayerSite__EndInteractiveAction            = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 FF A0 A8 05 00 00 CC CC CC 40 53 48 83 EC 20 48 8B D9 E8 ? ? ? ? 84 C0 74 ? E8 ? ? ? ?"), 24));
        gPatterns.LocalPlayerSite__StartInteractiveActionByName    = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("41 8B D8 48 8B EA 48 8B F9 E8 ? ? ? ? 84 C0 74 ? E8 ? ? ? ?"), 14));
        gPatterns.LocalPlayerSite__StartJumpAction                 = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("C7 43 04 00 00 00 00 48 8B E8 C6 43 08 00 E8 ? ? ? ? 84 C0 74 ? E8 ? ? ? ?"), 19));
        gPatterns.LocalPlayerSite__BeginCorpsePutDown              = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("0F 29 74 24 50 48 8B 89 D8 00 00 00 E8 ? ? ? ? 84 C0 74 ? 48 8B 06"), 17));
        gPatterns.LocalPlayerSite__UnregisterDBAGroups             = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("E8 ? ? ? ? 84 C0 0F 85 ? ? ? ? E8 ? ? ? ? 8B 53 30"), 5));
        gPatterns.LocalPlayerSite__RiderMagnetismFollow            = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("33 FF 48 85 C0 0F 84 ? ? ? ? 48 8B C8 E8 ? ? ? ? 84 C0 0F 84 ? ? ? ?"), 19));
        gPatterns.LocalPlayerSite__ScriptIsPlayer                  = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 85 C0 74 ? 48 8B C8 E8 ? ? ? ? 84 C0 74 ? 48 8D 54 24 40"), 13));
        gPatterns.LocalPlayerSite__CanStartChatWithSelf            = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("41 FF D0 48 8B CB 48 8B F8 E8 ? ? ? ? 84 C0 75 ? 48 85 FF"), 14));
        gPatterns.LocalPlayerSite__CanStartChatWithCounterpart     = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("75 ? 48 85 FF 0F 84 ? ? ? ? 48 8B CF E8 ? ? ? ? 84 C0 0F 84 ? ? ? ?"), 19));
        gPatterns.LocalPlayerSite__CanStartChatWithParticipant     = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 1B FF 48 23 F9 48 8B CB E8 ? ? ? ? 84 C0 74 ? 48 8B 06"), 14));
        gPatterns.LocalPlayerSite__HorseScaredTutorial             = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B 8F F8 00 00 00 E8 ? ? ? ? 48 85 C0 74 ? 48 8B C8 E8 ? ? ? ? 84 C0 74 ? E8 ? ? ? ?"), 25));
        gPatterns.LocalPlayerSite__UnconsciousWake                 = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 85 C0 74 ? 48 8B C8 E8 ? ? ? ? 84 C0 74 ? 48 8B 0D ? ? ? ?"), 13));
        gPatterns.LocalPlayerSite__IsThirdPerson                   = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 08 57 48 83 EC 20 E8 ? ? ? ? 33 DB 84 C0 75 ?"), 15));
        gPatterns.LocalPlayerSite__SpeedFeedbackSound              = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("CC CC CC 40 53 48 83 EC 30 0F 29 74 24 20 48 8B D9 0F 28 F1 E8 ? ? ? ? 84 C0 75 ? 0F 28 74 24 20"), 25));
        gPatterns.LocalPlayerSite__AuxTorchLight                   = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("E8 ? ? ? ? 48 8B 4F 20 E8 ? ? ? ? 48 8D 4D D0 84 C0"), 14));
        gPatterns.LocalPlayerSite__TorchLightArchetype             = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("57 48 83 EC 20 48 8B E9 E8 ? ? ? ? 84 C0 0F 85 ? ? ? ?"), 13));
        gPatterns.LocalPlayerSite__FirstPersonViewState            = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("41 8A F0 40 8A FA 48 8B D9 E8 ? ? ? ? 84 C0 74 ? E8 ? ? ? ?"), 14));
        gPatterns.LocalPlayerSite__ShootingAimSway                 = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B 01 FF 50 10 48 8B C8 E8 ? ? ? ? 33 DB 84 C0 74 ?"), 14));
        gPatterns.LocalPlayerSite__StartInteractiveAction          = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("41 8B F0 8B DA 48 8B F9 E8 ? ? ? ? 84 C0 74 ? E8 ? ? ? ?"), 13));
        gPatterns.LocalPlayerSite__CombatStealthInput              = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("E8 ? ? ? ? 48 8B 8F F8 01 00 00 E8 ? ? ? ? 84 C0 75 ? 48 8B 7C 24 50"), 17));
        gPatterns.LocalPlayerSite__StoneThrowingStart = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("E8 ? ? ? ? 48 8B 4B 18 E8 ? ? ? ? 84 C0 0F 84"), 14));
        gPatterns.LocalPlayerSite__StoneThrowingInputPhase = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("0F 84 ? ? ? ? 48 8B 49 18 E8 ? ? ? ? 84 C0 0F 84 ? ? ? ? E8"), 15));
        gPatterns.LocalPlayerSite__StoneThrowingInteractionAction = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B F9 48 8B 49 18 E8 ? ? ? ? 84 C0"), 12));
        gPatterns.LocalPlayerSite__StoneThrowingFinishInput = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("41 FF D2 48 8B 0F E8 ? ? ? ? 84 C0"), 11));
        gPatterns.LocalPlayerSite__StoneThrowingFinishControl = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("74 ? 48 8B 4B 18 E8 ? ? ? ? 84 C0 74 ? 48 8B CB"), 11));
        gPatterns.LocalPlayerSite__MinigameBeginInteraction = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("E8 ? ? ? ? 48 8B 4B 18 E8 ? ? ? ? 84 C0 74 ? E8 ? ? ? ? 41 B0 01"), 14));
        gPatterns.LocalPlayerSite__MinigamePrepareInteraction = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("74 ? 48 8B 4E 18 E8 ? ? ? ? 84 C0"), 11));
        gPatterns.LocalPlayerSite__MinigameEndInteraction = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("E8 ? ? ? ? 48 8B 4B 18 E8 ? ? ? ? 84 C0 74 ? E8 ? ? ? ? 45 33 C0"), 14));
        gPatterns.LocalPlayerSite__MinigameReleasePlayerInput = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("74 ? 48 8B 49 18 E8 ? ? ? ? 84 C0"), 11));
        gPatterns.LocalPlayerSite__MinigameSetPlayerInputEnabled = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 8A FA 48 8B 49 18 E8"), 12));
        gPatterns.LocalPlayerSite__MinigameClearActionMap = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B 4F 18 48 8B F0 E8 ? ? ? ? 84 C0"), 12));
        gPatterns.LocalPlayerSite__MinigameSetActionMap = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("49 8B 4D 18 4C 8B F8 E8"), 12));
        gPatterns.LocalPlayerSite__MinigameSetInteractionTarget = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B D9 48 8B 49 18 E8 ? ? ? ? 84 C0 74 ? 48 8B 43 18 48 8B 90 08 0B 00 00"), 12));
        gPatterns.LocalPlayerSite__MinigameClearInteractionTarget = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B D9 48 8B 49 18 E8 ? ? ? ? 84 C0 74 ? 48 8B 43 18 48 8B 88 08 0B 00 00 C7 81 F0 00 00 00 00 00 00 00"), 12));
        gPatterns.LocalPlayerSite__BattleArcheryStart              = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B 4E 18 89 86 04 01 00 00 E8 ? ? ? ? 84 C0 0F 84 ? ? ? ? B2 1E"), 15));
        gPatterns.LocalPlayerSite__BattleArcheryControls           = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("C6 87 FD 00 00 00 01 48 8B 0B E8 ? ? ? ? 84 C0 74"), 15));
        gPatterns.LocalPlayerSite__BattleArcheryFinish             = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("89 B9 80 00 00 00 48 8B 49 18 E8 ? ? ? ? 33 F6 84 C0"), 15));
        gPatterns.LocalPlayerSite__MovementAudioSuffix = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 83 EC 28 E8 ? ? ? ? 84 C0 48 8D 15 ? ? ? ? 48 8D 0D ? ? ? ? 48 0F 45 CA 48 8B C1 48 83 C4 28 C3"), 9));
        gPatterns.LocalPlayerSite__MaterialMovementAudio = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B CE E8 ? ? ? ? 8A C8 48 8D 15 ? ? ? ? F6 D9 48 8D 0D ? ? ? ? 4D 1B C0 41 83 E0 03"), 8));
        gPatterns.LocalPlayerSite__BodyfallAudio = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B 01 FF 50 10 48 8B C8 E8 ? ? ? ? 8A D8 E8 ? ? ? ? 48 83 C0 0C 4C 8D 05"), 14));
        gPatterns.LocalPlayerSite__CombatTimewarp = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 C7 41 20 60 79 FE FF 48 8B 49 08 0F 29 74 24 30 48 8B 89 F8 01 00 00 E8"), 29));
        gPatterns.LocalPlayerSite__CombatTimewarpFx = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B 4B 08 48 8B B9 30 02 00 00 48 8B 89 F8 01 00 00 E8 ? ? ? ? 84 C0 74 27"), 23));
        gPatterns.LocalPlayerSite__HorseMovementAudio = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B CB E8 ? ? ? ? 48 8B C8 E8 ? ? ? ? 84 C0 48 8D 15 ? ? ? ? 48 8D 0D ? ? ? ? 48 0F 45 CA 48 8B C1 48 83 C4 20 5B C3"), 16));
        // The controller's own `this` in RSI, its actor at +8, and the zero
        // register the rest of the set-up uses (R13D). The first 19 bytes are
        // already unique.
        gPatterns.LocalPlayerSite__ActorConditionControllerInit = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B F1 48 8B 49 08 E8 ? ? ? ? 45 33 ED 84 C0 0F 85 ? ? ? ? 48 8B 4E 08 E8"), 12));

        // SetAttackInputClass keeps the active flag in R8B and adds the model's
        // +0x240 property offset as an immediate, which is what pins it.
        gPatterns.C_CombatActor__SetAttackInputClass = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 57 48 83 EC 20 48 8B D9 41 8A F8 48 8B 89 ? ? ? ? 48 81 C1 40 02 00 00")));

        // BeginAttack loads a literal 1 into R8B for its first gating call.
        gPatterns.C_CombatPlayerController__BeginAttack = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 74 24 ? 57 48 83 EC 30 48 8B F9 41 B0 01")));

        // UpdateAttackTarget saves four registers through RAX, keeps the
        // fighter (+0x18) in R14 and the range flag in BPL.
        gPatterns.C_CombatPlayerController__UpdateAttackTarget = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B C4 48 89 58 08 48 89 68 10 48 89 70 18 48 89 78 20 41 56 48 83 EC 20 4C 8B 71 18 40 8A EA 48 8B F9 E8")));

        // AbortAttack clears the input class with -1, its active bit and the
        // attack request. This prologue and the first SetAttackInputClass
        // setup are unique in WHGame.dll.
        gPatterns.C_CombatPlayerController__AbortAttack = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 48 83 EC 20 48 8B D9 45 33 C0 48 8B 49 ? 83 CA FF")));
        gPatterns.C_CombatPlayerController__BeginGuard  = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 74 24 ? 57 48 83 EC 20 48 8B 51 ? 48 8B D9 80 7A")));
        gPatterns.C_CombatPlayerController__EndGuard    = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 48 83 EC 20 48 8B D9 E8 ? ? ? ? F3 0F 10 80")));

        // The request-bit setter sign-extends its index and keeps the value in
        // R8B right after a four-register prologue, which is what pins it.
        gPatterns.C_ModelArrayPropertyBool__SetElement = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B C4 48 89 58 ? 48 89 68 ? 48 89 70 ? 48 89 78 ? 41 56 48 83 EC 30 48 63 EA 41 8A F0")));

        // Resolved from the player's own read of it: EnsureFightingStance
        // loads the id straight into the stance request it is about to build.
        gPatterns.CombatActionTypeId__FreeGuard = hook::get_address<std::uint64_t>(hook::get_pattern(KCDC_HIDE("8B 05 ? ? ? ? 48 8D 54 24 ? 89 44 24 ? C6 44 24 ? 00 C6 44 24 ? 01 E8")), 2, 6);

        // The multi-subbrain tick is pinned by its 0x1A0-byte frame and the
        // security cookie that follows; the single-subbrain one by the flags byte
        // it tests straight out of the prologue.
        gPatterns.C_AIBrainMultiSubb__Update  = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 55 56 57 41 54 41 55 41 56 41 57 48 8D AC 24 ? ? ? ? 48 81 EC A0 01 00 00 48 8B 05 ? ? ? ? 48 33 C4 48 89 85 ? ? ? ? 4C 8B F1 48 89 54 24")));
        gPatterns.C_AIBrainSingleSubb__Update = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 6C 24 ? 48 89 74 24 ? 57 48 83 EC 20 80 B9 ? ? ? ? ? 48 8B EA 48 8B D9")));

        // TryEnterCombatMode's prologue is pinned by the `cmp byte [rdx], 0` it
        // tests the optional request with before anything else.
        gPatterns.C_CombatActor__TryEnterCombatMode = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 57 48 83 EC 20 80 3A 00 48 8B D9")));

        // LeaveCombatMode opens with the timer read its whole body is built on.
        gPatterns.C_CombatActor__LeaveCombatMode = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 7C 24 ? 55 48 8B EC 48 83 EC 30 48 8B D9 E8 ? ? ? ? 48 8B D0")));
        gPatterns.C_CombatActor__PostUpdate = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 57 48 83 EC 20 48 8B F9 48 8B 89 ? ? ? ? 48 8B 01 FF 50 ? 84 C0 75")));
        gPatterns.C_CombatActorClinch__IsPairedAttackAlignmentValid = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B C4 53 48 81 EC 80 00 00 00 0F 29 70 ? 48 8B D9 48 8B 49")));
        gPatterns.C_CombatActor__LeaveCombatModeIfActive = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 48 83 EC 20 48 8B D9 E8 ? ? ? ? 84 C0 74 ? 48 8B 03 48 8B CB FF 90 ? ? ? ? 48 83 C4 20")));

        // The stance request keeps the request struct in RBX and walks the
        // fighter to its owner and then to the owner's action queue -- the two
        // chained loads off +0x1F8 and +0x280 are what pin it.
        gPatterns.C_CombatActor__RequestFightingStance = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 48 83 EC 20 48 8B 89 ? ? ? ? 48 8B DA 48 8B 89")));

        // The guard request loads the model from the fighter at +0x210 with
        // the requested value already parked in BPL and the source in R8B,
        // which is what separates it from every other prologue of its shape.
        gPatterns.C_CombatActor__RequestGuard = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 6C 24 ? 48 89 74 24 ? 57 48 83 EC 20 48 8B F9 40 8A EA 48 8B 89 ? ? ? ? 41 8A D0")));

        // The zone setter opens by rejecting the absent id before it frames at
        // all; that `cmp edx, -1` ahead of the prologue is unique.
        gPatterns.C_CombatActor__SetCombatZone = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("83 FA FF 74 ? 48 89 5C 24 ? 48 89 74 24")));

        // RunAction loads the combat action queue from the fighter at +0x200
        // and then takes the address of its own stack copy of the handle.
        gPatterns.C_CombatActor__RunAction = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 48 83 EC 20 48 8B 99 ? ? ? ? 48 8D 4C 24")));

        // CanBeReplacedBy is the whole compare: the incoming action's state
        // through its slot 15, against the running one's at +0x38.
        gPatterns.C_CombatActorAction__CanBeReplacedBy = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 48 83 EC 20 48 8B D9 48 8B 0A 48 85 C9 74 12 48 8B 01 FF 50 78 3B 43 38 0F 9F C0 48 83 C4 20 5B C3")));
        // The take-next slot's own adjustment to the action (`lea rsi, [rcx-88h]`) and its next-action read; unique.
        gPatterns.C_CombatActorActionAttack__QueueNextAttack = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8D B1 78 FF FF FF 48 8B FA 48 8B 46 10 48 8D 54 24 ? 48 83 C1 88 FF 50 30"), -0x0F));

        // SetOpponent frames three non-volatile registers, then loads the
        // manager's owning combat actor from +0x08 while the candidate entity
        // is already parked in RDX -- the `mov rax,[rcx+8]` / `mov rdi,rcx` /
        // `mov rbp,rdx` run is what separates it.
        gPatterns.C_CombatActorOpponentManager__SetOpponent = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 6C 24 ? 56 57 41 56 48 83 EC 20 48 8B 41 ? 48 8B F9 48 8B EA")));

        // The stealth request frames three registers, then tests its own
        // pending byte at +0x5C before parking the kind, the victim id and
        // the interface in EBP, EBX and RDI.
        gPatterns.C_CombatActorStealth__RequestStealthAttack = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 6C 24 ? 48 89 74 24 ? 57 48 83 EC 20 80 79 ? 00 41 8B E8 8B DA 48 8B F9")));

        // The timewarp start opens by stamping its own start timestamp back to
        // the never value; that immediate store through RCX right after the
        // prologue is unique to it.
        gPatterns.C_CombatActorTimewarp__BeginTimewarp = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 57 48 83 EC 40 48 8B D9 48 C7 41")));

        // The slot dispatch takes the entry list in RCX and the planner in R8,
        // and opens by loading the list head from the receiver at +0x08 before
        // it touches the model it was handed in RDX.
        gPatterns.C_CombatActorSlotPlanner__DispatchDueSlots = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 56 57 41 56 48 83 EC 20 48 8B 59 ? 49 8B F8")));

        // The window lookup opens by reserving 0x48 bytes and taking the map
        // at +0xA0 into R11 before it spills the kind. The window opener
        // spills the attacker, pushes five registers under a frame pointer and
        // saves XMM6 for the window length. The block window's table is read
        // off its creator, the one store of type id 1 after the three cleared
        // vector words.
        gPatterns.C_CombatActorDirector__FindSlotObject            = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 83 EC 48 4C 8D 99 ? ? ? ? 89 54 24")));
        gPatterns.C_CombatActorMultiTrigger__AddTriggerForOpponent = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 54 24 ? 55 53 56 57 41 56 48 8B EC 48 83 EC 50 0F 29 74 24")));
        gPatterns.C_CombatMultiTriggerBlock__VTable                = hook::get_address<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 C7 43 20 00 00 00 00 48 8D 05 ? ? ? ? 48 C7 43 28 00 00 00 00 48 C7 43 30 00 00 00 00 48 89 03 C7 43 18 01 00 00 00")), 11, 15);

        // The zone request keeps the zone in ESI and points RBX at the model
        // pointer (+0x210) straight after its prologue. The riposte request
        // reads the counter window at model +0x830 before anything else. The
        // bool setter compares the new value against +0x08 first.
        gPatterns.C_CombatActor__RequestCombatZone = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 74 24 ? 57 48 83 EC 40 8B F2 48 8D 99")));
        // The zone apply keeps the side in R14D and the zone in R15D and then
        // reads the fighter's +0x200; matched past its prologue.
        gPatterns.C_CombatActor__TryApplyCombatZone = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("45 8B F0 89 55 ? 44 8B FA 48 8B F9 48 8B 89 00 02 00 00"), -0x17));
        gPatterns.C_CombatActor__RequestRiposte    = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 48 83 EC 20 48 8B D9 48 8B 89 ? ? ? ? 48 81 C1 30 08 00 00")));
        gPatterns.C_ModelPropertyBool__SetValue    = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 74 24 ? 57 48 83 EC 30 40 8A F2 48 8B F9 38 51")));

        // The contact itself reserves 0xC0 bytes for the collision record it
        // fills and stacks a frame pointer over it; that reserve plus the load
        // of the owning combat actor from +0x08 pins the prologue.
        gPatterns.C_CombatActorAnimCollisions__ProcessHit = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 7C 24 ? 55 48 8D 6C 24 ? 48 81 EC C0 00 00 00 48 8B 41")));

        // The arming has no prologue at all -- it zeroes R11 and immediately
        // compares the held sequence at +0x30, then writes the pending, forced
        // and enabled bytes back to back. That run of three stores is what
        // pins it.
        gPatterns.C_CombatActorAnimCollisions__ArmContact = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("45 33 DB 44 39 59 ? 75 ? 88 51 ? 44 88 41 ? C6 41 ? 01 44 38 5C 24")));

        // ProcessCombatHit and ProcessMissileHit are byte-identical for their
        // first 0x44 bytes -- same frame, same three argument moves -- so the
        // signature has to run past the prologue to the one thing only the
        // melee path does: `mov rdx, rdi` into the CreateCombatHitEvent call.
        gPatterns.C_CombatSoul__ProcessCombatHit = reinterpret_cast<std::uint64_t>(
            hook::get_pattern(KCDC_HIDE("40 55 53 56 57 41 56 48 8D AC 24 60 FF FF FF 48 81 EC A0 01 00 00 48 8B 05 ? ? ? ? 48 33 C4 48 89 85 90 00 00 00 48 8B 05 ? ? ? ? 4C 8B F1 49 8B F8 48 8B F2 48 8B 88 B0 00 00 00 48 8B 01 FF 50 08 48 8B D7 48 8D 4C 24 28 E8 ? ? ? ?")));

        // The two hit handlers share a 0x150-byte frame and a security
        // cookie; the register each zeroes right after it is what tells them
        // apart -- R14 in the geometric one, R15 in the animation one.
        gPatterns.C_CombatActorCollisionProcess__OnGeometricHit = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 55 56 57 41 56 41 57 48 8D 6C 24 ? 48 81 EC 50 01 00 00 48 8B 05 ? ? ? ? 48 33 C4 48 89 45 ? 45 33 F6")));
        gPatterns.C_CombatActorCollisionProcess__OnAnimationHit = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 55 56 57 41 56 41 57 48 8D 6C 24 ? 48 81 EC 50 01 00 00 48 8B 05 ? ? ? ? 48 33 C4 48 89 45 ? 45 33 FF")));
        gPatterns.C_CombatActorCollisions__SweepWeaponShape     = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B C4 48 89 58 ? 48 89 70 ? 48 89 78 ? 44 89 40 ? 55 41 54 41 55 41 56 41 57 48 8D 68 ? 48 81 EC 30 01 00 00")));

        // The record's constructor opens by writing 2 into its byte at +0x24;
        // QueueHitRecord takes the address of its record vector (+0x1D8) into
        // R9 straight away; the impact reserves 0x130 and loads the entity
        // system off gEnv before anything else.
        gPatterns.S_CombatHitRecord__Construct        = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 83 EC 18 45 33 C0 C6 41")));
        gPatterns.CGameRules__QueueHitRecord          = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 83 EC 28 4C 8D 89 ? ? ? ? 49 8B 49")));
        gPatterns.CombatHitRecord__PlayMaterialEffect = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 74 24 ? 55 57 41 57 48 8D 6C 24 ? 48 81 EC 30 01 00 00 48 8B 05 ? ? ? ? 48 33 C4 48 89 45 ? 48 8B 0D")));

        // IsHostileTo opens by loading its owning combat actor from +0x08 and
        // bailing out on null before it touches either soul.
        gPatterns.C_CombatRPG__IsHostileTo = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 57 48 83 EC 20 48 8B 41 ? 48 85 C0 74 ? 48 8B 80")));

        // The candidate refusal keeps the list in RSI and saves XMM6 for the
        // angle and distance it measures at the end.
        gPatterns.CombatOpponentCandidates__ShouldSkip = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 74 24 ? 57 48 83 EC 60 48 8B F1 0F 29 74 24")));

        // The candidate refresh reserves 0xD0 bytes and keeps the list in RDI;
        // the grid callback loads the list through the captured reference and
        // keeps the capture in RSI.
        gPatterns.CombatOpponentCandidates__Update      = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B C4 48 89 58 ? 55 56 57 41 56 41 57 48 8D 68 ? 48 81 EC D0 00 00 00 0F 29 70 ? 48 8B F9")));
        gPatterns.CombatOpponentCandidates__OnCandidate = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 74 24 ? 57 48 83 EC 50 48 8B 01 48 8B F1")));

        // BeginAttackTimeline takes the address of its own recursive lock
        // (+0x88) into RSI straight after the 0x150 reserve; ApplySlotDelay
        // reserves 0xD0, saves XMM6 and takes an address off RCX into RDX.
        gPatterns.C_CombatActorTimeCop__BeginAttackTimeline = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B C4 48 89 58 ? 48 89 70 ? 55 57 41 56 48 8D 68 ? 48 81 EC 50 01 00 00 0F 29 70 ? 48 8D B1")));
        gPatterns.C_CombatActorTimeCop__ApplySlotDelay      = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B C4 48 89 58 ? 48 89 68 ? 48 89 70 ? 57 48 81 EC D0 00 00 00 0F 29 70 ? 48 8D 51")));

        // The group lock is a two-load tail call: the group index off +0x384
        // and the lock manager off +0x208. The recursive acquire compares the
        // current thread id against the owner it keeps at +0x0C; the release
        // decrements the depth at +0x08 before it releases.
        gPatterns.C_CombatActor__GetCombatGroupLock = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("44 0F B7 81 ? ? ? ? 48 8B 89 ? ? ? ? E9")));
        gPatterns.RecursiveSRWLock__Acquire         = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 57 48 83 EC 20 48 8B D9 FF 15 ? ? ? ? 8B F8 3B 43")));
        gPatterns.RecursiveSRWLock__Release         = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("8B 41 ? 85 C0 74 ? FF C8 89 41 ? C3 C7 41 ? ? ? ? ? 48 FF 25 ? ? ? ? CC 40 53 48 83 EC 20 48 8B D9")));

        // The 3D engine's system event listener resolves a layer id through
        // IEntitySystem::GetLayerId (+0x2B0) and hands it straight to the
        // manager held in this global.
        gPatterns.C_PhysicalizationManager__Instance            = hook::get_address<std::uint64_t>(hook::get_pattern(KCDC_HIDE("FF 90 B0 02 00 00 48 8B 0D ? ? ? ? 44 8A C7 0F B7 D0")), 9, 13);
        gPatterns.C_PhysicalizationManager__BuildLayerColliders = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B C4 48 89 58 ? 48 89 70 ? 48 89 78 ? 44 89 40 ? 55 41 56 41 57 48 8D 68")));
        gPatterns.C_PhysicalizationManager__DeactivateLayer     = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 6C 24 ? 89 54 24 ? 56 57 41 56 48 83 EC 30 48 8D 79")));

        // The speed multiplier walks this -> owner -> actor before its
        // IsPlayerControlled test.
        gPatterns.C_CombatRPG__GetAttackSpeedMultiplier = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 74 24 ? 57 48 83 EC 20 48 8B F9 48 8B 49 ? 48 8B 89")));

        // The lethal dry run reserves 0x250 bytes behind a frame pointer and a
        // security cookie before it loads its owner.
        gPatterns.CombatRPG__WouldKillOpponent = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 55 53 56 57 41 54 41 56 41 57 48 8D AC 24 ? ? ? ? 48 81 EC 50 02 00 00 48 8B 05 ? ? ? ? 48 33 C4 48 89 85 ? ? ? ? 48 8B 41")));

        // Each allocator asks for its own class size, which is what pins it:
        // 0xF0 perfect block, 0xF8 sync perfect block, 0x78 failed attack,
        // 0xD8 hit reaction.
        gPatterns.C_CombatActorActionPerfectBlock__Allocate         = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 74 24 ? 57 48 83 EC 20 48 8B F9 48 8B F2 B9 F0 00 00 00")));
        gPatterns.C_CombatActorActionSyncPerfectBlock__Allocate     = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 57 48 83 EC 20 83 3D ? ? ? ? ? 48 8B FA 48 8B D9 75 ? E8 ? ? ? ? 48 8B 05 ? ? ? ? 48 8D 54 24 ? 45 33 C0 B9 F8 00 00 00")));
        gPatterns.C_CombatActorActionFailedAttack__Allocate         = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE(
            "48 89 5C 24 ? 48 89 74 24 ? 57 48 83 EC 20 83 3D ? ? ? ? ? 48 8B F2 48 8B F9 75 ? E8 ? ? ? ? 48 8B 05 ? ? ? ? 48 8D 54 24 ? 45 33 C0 41 8D 48 ? FF D0 8B 4C 24 ? 48 8B D8 F0 01 0D ? ? ? ? F0 83 05 ? ? ? ? ? F0 FF 05 ? ? ? ? 48 85 C0 74 ? 48 8B D6 48 8B C8 E8 ? ? ? ? 48 8D 05 ? ? ? ? 48 89 03 48 8D 4B")));
        gPatterns.C_CombatActorActionHit__Allocate                  = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 57 48 83 EC 20 83 3D ? ? ? ? ? 48 8B FA 48 8B D9 75 ? E8 ? ? ? ? 48 8B 05 ? ? ? ? 48 8D 54 24 ? 45 33 C0 B9 D8 00 00 00")));
        gPatterns.C_CombatActionPerfectBlockFactory__ApplyAlignment = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 57 48 83 EC 50 48 8B 42")));

        // The block's allocator asks for 0xB0 bytes, as the sync attack's
        // (0x180C61F1C) does, so it is taken from the call the block factory
        // (0x180C55BB4) makes before storing the queried row at +0x58.
        gPatterns.C_CombatActorActionBlock__Allocate = hook::get_call<std::uint64_t>(reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("49 8B 56 08 48 8D 4C 24 30 E8 ? ? ? ? 48 8B 45 ? 48 8D 54 24 30 48 8B 08 48 8B 44 24 30 48 89 48 58"), 9)));

        // The dodge query reserves 0x350 bytes behind a frame pointer for its
        // candidate list. The builder's prologue is shared by four other
        // 0xF8-and-smaller allocators, so it is taken from the call the
        // dodge's runner (0x181482F08) makes on the action manager's +0x68.
        gPatterns.C_CombatActionDodgeFactory__Query  = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 20 55 56 57 48 8D AC 24 B0 FD FF FF 48 81 EC 50 03 00 00")));
        gPatterns.C_CombatActionDodgeFactory__Create = hook::get_call<std::uint64_t>(reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B 89 A8 03 00 00 48 8D 50 08 49 8B E9 48 8B 49 68 E8"), 18)));

        // Each vtable is taken from the `lea` its own constructor installs it
        // with.
        gPatterns.C_CombatActorActionAttack__VTable              = hook::get_address<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8D 05 ? ? ? ? 39 3D ? ? ? ? 4C 8D B3 ? ? ? ? 48 89 03 48 8D 05")), 3, 7);
        gPatterns.C_CombatActorActionPerfectBlock__VTable        = hook::get_address<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8D 05 ? ? ? ? 48 89 03 48 8B CB C6 83 ? ? ? ? ? 48 8D 05 ? ? ? ? 48 89 43 ? 48 8D 05 ? ? ? ? 48 89 83")), 3, 7);
        gPatterns.C_CombatActorActionSyncPerfectBlock__VTable    = hook::get_address<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8D 05 ? ? ? ? 39 3D ? ? ? ? 48 89 03 48 8D 05")), 3, 7);
        gPatterns.C_CombatActorActionSyncPerfectBlockHit__VTable = hook::get_address<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8D 05 ? ? ? ? 48 89 03 48 8D 8B ? ? ? ? 48 8D 05 ? ? ? ? 89 BB")), 3, 7);
        gPatterns.C_CombatActorActionSyncAttack__VTable          = hook::get_address<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8D 05 ? ? ? ? 48 89 03 4C 8D B3 88 00 00")), 3, 7);
        gPatterns.C_CombatActorActionSyncHit__VTable             = hook::get_address<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8D 05 ? ? ? ? C7 83 80 00 00 00 02 00 00")), 3, 7);
        gPatterns.C_CombatActorActionSyncTransition__VTable      = hook::get_address<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8D 05 ? ? ? ? 33 FF 39 3D ? ? ? ? 48 89 03 48 8D 05 ? ? ? ? 48 89 43 10 48 8D 05 ? ? ? ? 48 89 43 68 40 88 BB 90 00 00 00")), 3, 7);
        gPatterns.C_CombatActorActionFailedAttack__VTable        = hook::get_address<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8D 05 ? ? ? ? C7 43 ? ? ? ? ? 48 89 03 BA 01 00 00 00")), 3, 7);
        gPatterns.C_CombatActorActionHit__VTable                 = hook::get_address<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8D 05 ? ? ? ? 48 8B D3 48 89 07 48 8D 8F")), 3, 7);
        gPatterns.C_CombatActorActionBlock__VTable               = hook::get_address<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8D 05 ? ? ? ? 48 C7 C5 60 79 FE FF 48 89 03 4C 8D B3 88 00 00 00")), 3, 7);
        gPatterns.C_CombatActorActionDodge__VTable               = hook::get_address<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8D 05 ? ? ? ? 48 89 03 48 8D 8B A0 00 00 00 48 8D 05")), 3, 7);

        // Each table is taken from a query that hands it to its row walk.
        gPatterns.C_CombatActionPerfectBlockDatabase__Instance = hook::get_address<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8D 05 ? ? ? ? 48 83 C4 20 5B C3 CC CC CC 48 89 5C 24 ? 55")), 3, 7);
        gPatterns.C_CombatActionFailedAttackDatabase__Instance = hook::get_address<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8D 0D ? ? ? ? E8 ? ? ? ? 48 8B 4D ? 48 85 C9 74 ? 48 8B 01 48 8D 55 ? 48 3B CA 0F 95 C2 FF 50 ? 48 8D 4D")), 3, 7);
        gPatterns.C_CombatActionHitDatabase__Instance          = hook::get_address<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8D 05 ? ? ? ? 48 83 C4 20 5B C3 CC CC CC 48 89 5C 24 ? 48 89 6C 24")), 3, 7);
        gPatterns.C_CombatActionBlockDatabase__Instance        = hook::get_address<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8D 4D C7 E8 ? ? ? ? 4C 8D 45 C7 48 8D 54 24 30 48 8D 0D ? ? ? ? E8"), 18), 3, 7);
        gPatterns.C_CombatActionDodgeDatabase__Instance        = hook::get_address<std::uint64_t>(hook::get_pattern(KCDC_HIDE("4C 8D 45 E0 48 8D 54 24 60 48 8D 0D ? ? ? ? E8 ? ? ? ? 48 8D 4D E0"), 9), 3, 7);

        // The sync perfect-block-hit table is only ever named by its own
        // static initialiser, which fills its header right after the `lea`.
        gPatterns.C_CombatActionSyncPerfectBlockHitDatabase__Instance =
            hook::get_address<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8D 05 ? ? ? ? 0F 11 05 ? ? ? ? 88 0D ? ? ? ? 66 0F 7F 05 ? ? ? ? 66 0F 7F 0D ? ? ? ? 66 0F 7F 0D ? ? ? ? 89 0D ? ? ? ? 88 0D ? ? ? ? 48 83 C4 28 C3 4C 8B DC")), 3, 7);

        // The attack factory reserves 0x260 bytes for the query data it
        // assembles and stacks the frame pointer above it; the reserve size
        // and the security cookie load pin the prologue.
        gPatterns.C_CombatActionAttackFactory__CreateAttackAction =
            reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B C4 48 89 58 ? 55 56 57 41 54 41 55 41 56 41 57 48 8D A8 ? ? ? ? 48 81 EC 60 02 00 00 0F 29 70 ? 48 8B 05 ? ? ? ? 48 33 C4 48 89 85 ? ? ? ? 48 8B F9")));

        // PerformAttack loads the controller's fighter from +0x18 and moves
        // the input class straight into R8D for the factory call.
        gPatterns.C_CombatPlayerController__PerformAttack = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 57 48 83 EC 30 48 8B 41 ? 48 8B F9 44 8B C2")));

        // The owner load from +0x08 ahead of the copy is what separates Speak
        // from the two assignment helpers beside it.
        gPatterns.C_ActorSpeakMan__Speak = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 74 24 ? 57 48 83 EC 40 48 8B F9 0F 29 74 24 ? 48 8B 49 ? 48 8B F2")));

        // Unique only past the frame setup: 16 bytes also match 0x182E88F50.
        gPatterns.C_UISubtitles__SetText = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 10 48 89 74 24 18 44 88 4C 24 20 55 57 41 56 48 8B EC 48 83 EC 50 C7 45 20 00 00 00 00")));

        // Unique only through the full frame: a 0xA8 frame after saving every
        // non-volatile register.
        gPatterns.C_DialogInstance__SpeakLine = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B C4 48 89 50 10 55 53 56 57 41 54 41 55 41 56 41 57 48 8D 68 A1 48 81 EC A8 00 00 00")));

        // The null test of the configuration's cutscene at +0x20 ahead of the
        // deque at +0x48 is unique by itself; the frame setup is margin.
        gPatterns.C_CutscenePlayer__EnqueueCutscene = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 08 57 48 83 EC 20 48 8B 02 48 8B DA 48 8B F9 48 83 78 20 00 74 2F 48 83 C1 48 E8")));

        // Short enough to pin by its whole head: the walk back from the
        // interface sub-object and the move of the position pointer into R9.
        gPatterns.C_UIHudBubbles__SetBubblePosition = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 83 EC 28 48 83 C1 A8 4D 8B C8")));

        // The early-out on a zero bubble id over a bare 0x20 reserve is the
        // whole of HideBubble's shape.
        gPatterns.C_ActorSpeakMan__HideBubble = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 48 83 EC 20 83 B9 ? ? ? ? ? 48 8B D9 75 ? 48 83 C4 20 5B C3 E8")));

        // --- Native main menu -------------------------------------------

        // SetPage writes the requested page straight into the fixed +0xA1
        // field before doing anything else.
        gPatterns.C_UIMenu__SetPage = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 55 53 57 48 8B EC 48 83 EC 50 48 8B F9 88 91")));

        // The button event keeps the incoming name in RBX and hands the parser
        // its own stack slot before dereferencing the string.
        gPatterns.C_UIMenu__OnFlashButton = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 48 83 EC 20 48 8B DA 48 8D 54 24 ? 48 8B 0B")));

        // AddButton reserves 0x80 bytes over the full preserved-register set
        // and opens by loading the tooltip argument off the stack.
        gPatterns.C_UIMenuBase__AddButton = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 55 56 57 41 54 41 55 41 56 41 57 48 8B EC 48 81 EC 80 00 00 00 48 8B 75")));

        // The emit is resolved through AddButton's own tail call: three stack
        // spills interleaved with the register arguments, closing on the
        // `[rsp+20h]` store of the text pointer.
        gPatterns.C_UIMenuBase__EmitBasicButton = hook::get_address<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B 4D ? 48 8D 45 ? 48 89 44 24 30 4C 8D 4D ? 48 8D 45 ? 48 89 44 24 28 4C 8D 45 ? 48 8D 45 ? 48 8D 55 ? 48 89 44 24 20 E8")), 44, 48);

        // The id-to-name table opens by widening the enum and branching on the
        // first range boundary, with no prologue at all.
        gPatterns.E_ButtonId__ToString = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("0F B6 D1 83 FA 2F")));

        // ShowLoadPage spills its mode argument before framing, which no other
        // page builder does.
        gPatterns.C_UISaveLoad__ShowLoadPage = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("89 54 24 ? 55 53 56 57 41 54 41 55 41 56 41 57 48 8D 6C 24 ? 48 81 EC C8 00 00 00 8B DA")));

        // Folded with an identical twin, so it is resolved through the call
        // ShowLoadPage's loop makes: the two stack spills, `this` in RCX and
        // the `[r15+0Ch]` row-id lea pin the site.
        gPatterns.C_UISaveLoad__CallFlash5 = hook::get_address<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 7C 24 28 48 8D 55 ? 48 89 5C 24 20 49 8B CE 4D 8D 47 0C E8")), 22, 26);

        // The load-button event frames with `lea rbp, [rsp+...]` over an 0xB0
        // reserve and opens by walking to the owning menu.
        gPatterns.C_UISaveLoad__OnFlashLoadButton = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 55 48 8D 6C 24 ? 48 81 EC B0 00 00 00 48 8B 81")));

        // The three CFlashPlayer entry points the mod dispatches through, so
        // the vtable map can be checked against them before it is used.
        gPatterns.CFlashPlayer__SetVariable = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B C4 48 89 58 ? 48 89 68 ? 48 89 70 ? 48 89 78 ? 41 56 48 83 EC 40 48 8B B1 ? ? ? ? 48 8B D9")));
        gPatterns.CFlashPlayer__IsAvailable =
            reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 6C 24 ? 48 89 74 24 ? 57 48 83 EC 20 48 8B B1 ? ? ? ? 48 8B D9 48 8B CE 48 8B EA E8 ? ? ? ? 48 8B 8B ? ? ? ? 33 FF 48 85 C9 74 ? 48 8B 01 48 8B D5 FF 50")));
        gPatterns.CFlashPlayer__Invoke = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 55 41 54 41 55 41 56 41 57 48 83 EC 60 48 8D 6C 24 ? 48 89 5D ? 48 89 75 ? 48 89 7D ? 48 8B 05 ? ? ? ? 48 33 C5 48 89 45 ? 4C 8B A9")));

        // Resolved through ShowConfirmation's own call: four stack spills
        // interleaved with the register arguments, `this` reloaded into RCX
        // from R15 in the middle.
        // --- Settings menu rows -------------------------------------------

        gPatterns.C_UISettings__ShowSoundSettingsPage = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B C4 48 89 58 ? 48 89 78 ? 55 48 8D 68 ? 48 81 EC B0 00 00 00 48 8B D9")));
        gPatterns.CAnimationDatabaseManager__ReloadDatabase = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 08 48 89 74 24 18 57 48 83 EC 30 48 8B FA 48 8B F1 E8 ? ? ? ? 48 8D 4F 48 E8")));
        gPatterns.C_UISettings__ShowGameSettingsPage = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B EC 48 81 EC 80 00 00 00 48 8B F9 B2 0B 48 8B 89 30 03 00 00"), -7));
        gPatterns.C_UIMenu__ShowPage                  = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 48 83 EC 20 48 8B D9 48 8D 15 ? ? ? ? 48 8D 4C 24 ? E8 ? ? ? ? 48 8D 54 24 ? 48 8B CB E8 ? ? ? ? 48 8D 4C 24 ? E8 ? ? ? ? 48 83 C4 20 5B C3 CC CC")));

        // The three row emitters share a prologue and separate on frame size
        // and on which argument they spill first.
        gPatterns.C_UIMenuBase__AddValueButton   = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B C4 48 89 58 ? 48 89 68 ? 48 89 70 ? 57 48 83 EC 70 48 8B F2")));
        gPatterns.C_UIMenuBase__AddChoicesButton = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B C4 48 89 58 ? 48 89 68 ? 48 89 70 ? 57 48 83 EC 60 48 8B F2")));
        gPatterns.C_UIMenuBase__AddChoiceOption  = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B C4 48 89 58 ? 48 89 68 ? 48 89 70 ? 44 89 40 ? 57 48 83 EC 50")));

        // SetValue spills its float out of XMM2, SetChoice its int out of R8D.
        gPatterns.C_UIMenuBase__SetValue  = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 44 89 4C 24 ? F3 0F 11 54 24 ? 57")));
        gPatterns.C_UIMenuBase__SetChoice = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 44 89 4C 24 ? 44 89 44 24 ? 57 48 83 EC 40")));

        // The value event keeps its float in XMM6 across the page check; the
        // choice event keeps its index in ESI.
        gPatterns.C_UISettings__OnInteractiveValue  = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 57 48 83 EC 30 48 8B D9 0F 29 74 24 ? 48 8B 89 ? ? ? ? 0F 28 F2")));
        gPatterns.C_UISettings__OnInteractiveChoice = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 56 57 41 56 48 83 EC 20 48 8B F9 41 8B F0")));

        // --- Player profile attributes ------------------------------------

        // The string pair have prologues of their own: the getter asks for the
        // default-profile fallback (`mov r9b, 1`), the setter moves the value
        // pointer before building the variant.
        gPatterns.PlayerProfile__GetStringAttribute = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 48 83 EC 50 48 8B 05 ? ? ? ? 48 33 C4 48 89 44 24 ? 48 8B 01 49 8B D8 80 64 24 ? ? 4C 8D 44 24 ? 41 B1 01")));
        gPatterns.PlayerProfile__SetStringAttribute = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 57 48 83 EC 50 48 8B 05 ? ? ? ? 48 33 C4 48 89 44 24 ? 49 8B C0")));

        // The numeric four through their calls in C_UISettings: LoadFromProfile's
        // `sound_music` read and `res_y` read, SaveToProfile's `sound_sound`
        // write and `wh_ui_SubtitleSize` write. Each pattern opens on the call.
        gPatterns.PlayerProfile__GetFloatAttribute = hook::get_address<std::uint64_t>(hook::get_pattern(KCDC_HIDE("E8 ? ? ? ? 45 33 C9 4C 8D 45 ? 48 8D 15 ? ? ? ? 48 8B CB E8 ? ? ? ? 45 33 C0 48 8D 55 ? 48 8B CE E8 ? ? ? ? 48 8D 4C 24")), 1, 5);
        gPatterns.PlayerProfile__GetIntAttribute   = hook::get_address<std::uint64_t>(hook::get_pattern(KCDC_HIDE("E8 ? ? ? ? 44 8B 44 24 ? 48 8B CE 8B 54 24")), 1, 5);
        gPatterns.PlayerProfile__SetFloatAttribute = hook::get_address<std::uint64_t>(hook::get_pattern(KCDC_HIDE("E8 ? ? ? ? 4C 8D 45 ? 48 8B CB 48 8D 15 ? ? ? ? E8 ? ? ? ? 48 8D 4D ? E8 ? ? ? ? 48 8D 4D ? E8 ? ? ? ? 0F 28 74 24")), 1, 5);
        gPatterns.PlayerProfile__SetIntAttribute   = hook::get_address<std::uint64_t>(hook::get_pattern(KCDC_HIDE("E8 ? ? ? ? 4C 8D 45 ? 48 8B CB 48 8D 15 ? ? ? ? E8 ? ? ? ? 4C 8D 45 ? 48 8B CB 48 8D 15 ? ? ? ? E8 ? ? ? ? 48 8D 15")), 1, 5);

        gPatterns.C_UIMenuBase__CallFlashAddConfirmation =
            hook::get_address<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8D 45 ? 49 8B CF 48 89 44 24 38 4C 8D 4D ? 48 8D 45 ? 48 89 44 24 30 4C 8D 45 ? 48 8D 45 ? 48 89 44 24 28 48 8D 55 ? 48 8D 45 ? 48 89 44 24 20 E8")), 52, 56);

        // The confirm event keeps the string payload in RBX and re-loads it
        // into RCX for the parser before anything else.
        gPatterns.C_UIMenuBase__OnFlashConfirm = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 74 24 ? 57 48 83 EC 20 48 8B 1A 48 8B F2 48 8B CB")));

        // The confirmation id-to-name table branches on its first range
        // boundary with no prologue, as the button one does.
        gPatterns.E_ConfirmationId__ToString = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("0F B6 D1 83 FA 0D")));

        // Resolved from the left click's own read of it: the release branch of
        // C_CombatPlayerInput::OnAttackPrimaryMouse tests the charge flag at
        // +0x3C and then loads the id RIP-relative for ReleaseAttack.
        gPatterns.CombatInputClassId__AttackHeavy = hook::get_address<std::uint64_t>(hook::get_pattern(KCDC_HIDE("39 49 3C 75 ? 8B 15 ? ? ? ? 48 8B 49 20 E8")), 7, 11);

        // HasWeaponInSet opens by loading the invalid-item marker, so the
        // marker and the function come out of the same signature.
        gPatterns.C_ItemAttachmentManager__HasWeaponInSet = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B 05 ? ? ? ? 44 8D 04 12")));
        gPatterns.g_InvalidItemWUID                       = hook::get_address<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B 05 ? ? ? ? 44 8D 04 12")), 3, 7);

        // Draw and holster both open on the owner pointer at +0x38; the draw
        // loads the attach kind 2 straight into R8 for the first hand.
        gPatterns.C_ItemAttachmentManager__DrawWeapons    = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 48 83 EC 20 8B 41 ? 41 B8 02 00 00 00")));
        gPatterns.C_ItemAttachmentManager__HolsterWeapons = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("4C 8B DC 53 48 83 EC 60 48 8B 41 ? 48 8D 15")));

        // The set setter keeps `this` in R8 across its AreHandsEmpty call and
        // compares the requested set against the wildcard 3 on the way back.
        gPatterns.C_ItemAttachmentManager__SetSelectedWeaponSet = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 83 EC 28 4C 8B C1 E8 ? ? ? ? 84 C0 74 ? 83 FA 03")));

        // AreHandsEmpty walks straight from the owner at +0x38 to the actor's
        // item state at +0x990 with no prologue at all.
        gPatterns.C_ItemAttachmentManager__AreHandsEmpty = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B 41 ? 48 8B 88 ? ? ? ? 48 8B 05")));

        // IsWeaponDrawn spills three registers and takes its item-class
        // filter out of R9D before anything else.
        gPatterns.C_ItemAttachmentManager__IsWeaponDrawn = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 6C 24 ? 48 89 74 24 ? 57 41 56 41 57 48 83 EC 30 45 8B F1")));

        // Both by-id halves spill the WUID back to the stack to hand the item
        // registry a pointer to it; the attach keeps `this` in R11, the
        // detach in R10.
        gPatterns.C_ItemAttachmentManager__AttachItemById =
            reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 83 EC 28 4C 8B D9 48 89 54 24 38 48 8B 0D ? ? ? ? 48 8D 54 24 38 48 81 C1 68 01 00 00 45 8B D0 E8 ? ? ? ? 48 85 C0 75 05 48 83 C4 28 C3 45 8B C2 48 8B D0 49 8B CB 48 83 C4 28 E9 ? ? ? ?")));
        gPatterns.C_ItemAttachmentManager__DetachItemById = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 83 EC 28 4C 8B D1 48 89 54 24 ? 48 8B 0D ? ? ? ? 48 8D 54 24 ? 48 81 C1 68 01 00 00 E8 ? ? ? ? 48 85 C0 74 ? 48 8B D0 49 8B CA E8")));

        // The decision function opens by spilling three registers and framing
        // with `lea rbp` over an 0x80 reserve, then takes the item pair into
        // R13 before anything else.
        gPatterns.WH__DrawOrHolsterItem = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 74 24 ? 48 89 7C 24 ? 55 41 54 41 55 41 56 41 57 48 8B EC 48 81 EC 80 00 00 00 4C 8B E9")));

        // The hand-pair filler loads its two-entry count into R8D before it
        // touches either item.
        gPatterns.WH__BuildDrawHandPair = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 74 24 ? 57 48 83 EC 20 48 8B FA 48 8B F1 41 B8 02 00 00 00")));

        // The hand test walks straight to the actor's item state at +0x990
        // and seeds its answer with 1 before comparing either hand.
        gPatterns.WH__IsItemDrawn = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 74 24 ? 57 48 83 EC 20 48 8B B9 ? ? ? ? 48 8B F2 BB 01 00 00 00")));
        // Stashes and locks. Three neighbouring C_LockBase accessors share the
        // same shape -- load the entity from +0x38, fetch its script table,
        // then read one Lua field -- so each signature carries enough of the
        // body to reach the field it reads. The +0x38 displacement is spelled
        // out rather than wildcarded: a helper elsewhere in the image is
        // byte-identical apart from reading +0x30 instead.
        gPatterns.C_LockBase__IsLocked            = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 83 EC 28 48 8B 49 38 E8 ? ? ? ? 48 85 C0 74 ? 4C 8D 44 24")));
        gPatterns.C_LockBase__GetPropertiesLocked = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 57 48 83 EC 20 48 8B 49 38 E8 ? ? ? ? 48 85 C0 0F 84 ? ? ? ? 4C 8D 44 24 ? 48 C7 44 24 ? ? ? ? ? 48 8D 15 ? ? ? ? 48 8B C8 E8 ? ? ? ? 48 8B 7C 24")));
        gPatterns.C_LockBase__CanUnlockWithDynamicKey =
            reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 57 48 83 EC 20 48 8B 49 38 E8 ? ? ? ? 48 85 C0 0F 84 ? ? ? ? 4C 8D 44 24 ? 48 C7 44 24 ? ? ? ? ? 48 8D 15 ? ? ? ? 48 8B C8 E8 ? ? ? ? 48 8B 5C 24")));

        // Each generated at the function boundary as the shortest unique
        // signature in the image.
        gPatterns.C_LockBase__CanUnlock = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 48 83 EC 20 48 8B 02 48 8B DA 48 2B 05")));
        gPatterns.C_Stash__CanUnlock    = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 48 83 EC 20 48 8B D9 E8 ? ? ? ? 84 C0 74 ? B0 01")));
        gPatterns.C_ScriptBindEntityModule__HasPlayerVisibleItems =
            reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 57 48 83 EC 20 49 8B D8 48 8B FA E8 ? ? ? ? 48 8D 54 24 ? 48 89 5C 24 ? 48 8B 80 ? ? ? ? 48 8B 88 ? ? ? ? 48 83 C1 28 E8 ? ? ? ? 48 85 C0 75 ? 48 8B 07")));
        gPatterns.C_HerbGathering__Start                 = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 08 48 89 74 24 10 55 57 41 56 48 8B EC 48 83 EC 60 48 8B 01")));
        gPatterns.C_HerbGathering__AwardGatheredHerbs    = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 55 56 57 48 83 EC 40 48 8B 41 ? 48 8B F1")));
        gPatterns.C_RespawnManager__RestoreHarvestedArea = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B C4 48 89 58 ? 48 89 70 ? 48 89 78 ? 55 41 54 41 57 48 8D 68 ? 48 81 EC B0 00 00 00 F3 0F 10 42")));
        gPatterns.C_RespawnManager__EraseHarvestClaim    = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 57 48 83 EC 20 F3 0F 10 42 ? 4C 8B DA")));
        gPatterns.C_RespawnManager__HarvestArea          = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B C4 48 89 58 08 48 89 70 10 48 89 78 18 55 41 54 41 55 41 56 41 57 48 8D 68 A1 48 81 EC A0 00 00 00 0F 29 70 C8 49")));
        gPatterns.C_RespawnManager__HarvestSelected      = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 48 83 EC 20 48 8D 51 60 48 8B D9 80 7A 14 00 74")));
        gPatterns.C_RespawnManager__QuerySources         = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B C4 48 89 58 ? 55 56 57 41 54 41 55 41 56 41 57 48 8D 68 ? 48 81 EC B0 00 00 00 44 8B 05")));
        gPatterns.C_PlayerInteractor__AddLuaAction       = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 6C 24 ? 56 57 41 56 48 83 EC 30 48 8D A9")));
        gPatterns.entitymodule__InvokeLuaActionFunc      = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 4C 24 ? 53 48 83 EC 20 83 7A")));
        gPatterns.CScriptBindGame__SetPrefabPhaseById =
            reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 20 57 41 56 41 57 48 83 EC 40 48 8B 0D ? ? ? ? 48 8B F2 41 8B D0 4D 8B F9 49 8B D8 48 8B 01 FF 50 70 48 8B 0D ? ? ? ? 4C 8B F0 48 8B A9 50 01 00 00 48 85 C0 74")));
        gPatterns.entitymodule__FindInteractionType      = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 74 24 ? 57 48 83 EC 50 48 8B 05 ? ? ? ? 48 33 C4 48 89 44 24 ? 48 8B F1")));

        // Takes the desired flag in DL and the sink in R8 before loading the
        // entity, which is what separates its prologue from the readers above.
        gPatterns.C_LockBase__SetLocked = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 6C 24 ? 48 89 74 24 ? 57 48 83 EC 20 48 8B F1 49 8B D8 48 8B 49 38")));

        // Each signature was generated at the function boundary and checked
        // against the complete image: every one has exactly one match.
        gPatterns.C_ScriptBindLockBase__ReportLocked            = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 48 83 EC 20 48 8B DA E8 ? ? ? ? 48 85 C0 74 ? 48 8D 48 ? E8 ? ? ? ? 48 8B 03 48 8B CB 48 83 C4 20 5B 48 FF 60 ? CC 40 53")));
        gPatterns.C_ScriptBindLockBase__ReportUnlocked          = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 48 83 EC 20 48 8B DA E8 ? ? ? ? 48 85 C0 74 ? 48 8D 48 ? E8 ? ? ? ? 48 8B 03 48 8B CB 48 83 C4 20 5B 48 FF 60 ? CC 48 89 5C 24 ? 48 89 6C 24")));
        gPatterns.C_LockBase__OnLockpicked                      = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 57 48 83 EC 20 48 8B 49 ? E8 ? ? ? ? 48 89 44 24")));
        gPatterns.C_AnimDoor__Init                              = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 57 48 83 EC 20 48 8B F9 E8 ? ? ? ? 8A D8 E8 ? ? ? ? 48 8B 90")));
        gPatterns.C_ScriptBindAnimDoor__ReportManipulationStart = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 48 83 EC 20 48 8B DA E8 ? ? ? ? 48 85 C0 74 ? 48 8D 88 90 00 00 00 E8 ? ? ? ? 48 8B 03 48 8B CB 48 83 C4 20 5B 48 FF 60 58")));
        gPatterns.C_ScriptBindAnimDoor__GetObjectAnimName       = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 6C 24 ? 48 89 74 24 ? 57 41 56 41 57 48 83 EC 30 45 8A F9")));
        gPatterns.C_ScriptBindAnimDoor__IsInteractive           = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 48 83 EC 20 48 8B DA E8 ? ? ? ? 48 8D 54 24 ? 48 8B CB 48 85 C0 74 ? 83 B8 B0 00 00 00 00 0F 94 44 24")));
        gPatterns.CScriptSystem__PopStackValue                  = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 48 83 EC 20 41 83 C8 FF 48 8B D9 E8 ? ? ? ? 48 8B 4B 10 48 83 41 10 F0 48 83 C4 20 5B C3")));
        gPatterns.C_Actor__StartInteractiveActionByName         = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 6C 24 ? 48 89 74 24 ? 57 48 81 EC B0 00 00 00 48 8B 05 ? ? ? ? 48 33 C4 48 89 84 24 ? ? ? ? 41 8A F1")));
        gPatterns.CActionInteractive__Ctor                      = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 48 83 EC 20 48 8B D9 48 8D 05 ? ? ? ? 48 89 01 33 C9 48 8B 44 24 ?")));
        gPatterns.CActionInteractive__Enter                     = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 57 48 83 EC 30 83 49")));
        gPatterns.CActionInteractive__Exit                      = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 57 48 83 EC 30 83 61 2C DF 48 8B D9 C7 41 28 00 00 00 00 48 8B 89 90 00 00 00")));

        // Resolves the game object from the entity id and then walks the
        // static stash class-name list querying each extension in turn. The
        // entity-id move into EDX followed by the two indirect calls pins it.
        gPatterns.entitymodule__FindStashExtension = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 48 83 EC 20 8B D9 48 8B 0D ? ? ? ? 48 8B 01 FF 90 ? ? ? ? 48 8B C8 48 8B 10 4C 8B 82 ? ? ? ? 8B D3 41 FF D0 48 8B D8")));

        // Builds a 0x70-byte frame with a stack cookie and keeps the hidden
        // return pointer in RDI, which is the shape a member returning a WUID
        // by hidden pointer has.
        gPatterns.C_Stash__GetMasterInventoryWuid = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 74 24 ? 57 48 83 EC 70 48 8B 05 ? ? ? ? 48 33 C4 48 89 44 24 ? 48 8B FA 48 8B D9")));

        // Builds the preset item list on a 0x40-byte frame and hands it
        // straight to the inventory, then clears the pending restock slot.
        gPatterns.C_Stash__RestockNow = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 48 83 EC 40 48 8D 54 24 ? 48 8B D9 E8 ? ? ? ? 48 8B 8B")));

        // The only stash member with a 0xE0-byte cookie-guarded frame, which
        // it needs for the world position it reads off the entity.
        gPatterns.C_Stash__DumpItemsToWorld = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 74 24 ? 55 57 41 56 48 8D 6C 24 ? 48 81 EC E0 00 00 00 48 8B 05 ? ? ? ? 48 33 C4 48 89 45 ? 48 8B 89")));

        // Both open and close begin by broadcasting their own signal, so the
        // displacement of that signal is what tells them apart: `sub rcx, -0x80`
        // for the open at +0x80 and `add rcx, 0x90` for the close at +0x90.
        gPatterns.C_Stash__OnOpened = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 57 48 83 EC 20 48 8B F9 8A DA 48 83 E9 80")));
        gPatterns.C_Stash__OnClosed = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 48 83 EC 20 48 8B D9 48 81 C1 90 00 00 00 E8 ? ? ? ? 48 8B 93")));

        // Native missile entry points.
        gPatterns.C_Soul__ApplyModifiers                         = hook::get_address<std::uint64_t>(hook::get_pattern(KCDC_HIDE("E8 ? ? ? ? 4C 8B 07 48 8D 8F")), 1, 5);
        gPatterns.C_EventFactory__CreateMissileHitEvent          = hook::get_address<std::uint64_t>(hook::get_pattern(KCDC_HIDE("E8 ? ? ? ? 48 8B D6 48 8D 4C 24 ? E8 ? ? ? ? 48 8B D0 4C 8D 47 ? 48 8D 4C 24 ? E8 ? ? ? ? BB 01 00 00 00")), 1, 5);
        gPatterns.C_EffectVector__EmplaceBack                    = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("4C 8B C2 48 8B 51 ? 48 3B 51 ? 74 ? 49 8B 00 48 89 02 48 8B 41 ? 48 8D 50 ? 48 89 51 ? C3 E9 ? ? ? ? CC CC CC 40 53 48 83 EC 20 B8 01 00 00 00")));
        gPatterns.C_Item__EnsureRuntimeData                      = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 48 83 EC 20 48 8B 41 ? 48 8B D9 48 85 C0 74 ? 48 83 C4 20 5B C3 48 83 79")));
        gPatterns.C_Item__SetQuality                             = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 57 48 83 EC 30 8B FA 0F 29 74 24 ? BA 1A 00 00 00")));
        gPatterns.C_ItemHolder__CloneItem                        = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 6C 24 ? 48 89 74 24 ? 57 48 83 EC 20 48 8B C2 48 8B F1")));
        gPatterns.C_WeaponEquipRuntimeData__SetPoison            = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 83 EC 28 48 8B 81 ? ? ? ? 48 2B 02 0F 85")));
        gPatterns.C_ActorShootingExpansion__RefreshAnimationTags = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B C4 48 89 58 ? 48 89 68 ? 48 89 70 ? 48 89 78 ? 41 56 48 83 EC 20 48 8B F9 48 8B 49 ? 48 8B 01")));
        gPatterns.C_EventManager__ProcessEvent                   = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 74 24 ? 57 48 83 EC 20 48 8B DA 48 8B F9 48 85 D2 75 ? 32 C0 EB")));
        gPatterns.C_ActorShootingExpansion__MarkWeaponFired      = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 48 83 EC 20 48 8B D9 48 8B 49 ? E8 ? ? ? ? 48 8B C8 E8")));
        gPatterns.C_CombatSoul__ProcessMissileHit =
            reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 55 53 56 57 41 56 48 8D AC 24 ? ? ? ? 48 81 EC A0 01 00 00 48 8B 05 ? ? ? ? 48 33 C4 48 89 85 ? ? ? ? 48 8B 05 ? ? ? ? 4C 8B F1 49 8B F8 48 8B F2 48 8B 88 ? ? ? ? 48 8B 01 FF 50 ? 4C 8B C7")));
        gPatterns.S_MissileHitData__Constructor                   = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 57 48 83 EC 20 48 8B 1D ? ? ? ? 48 8B F9 48 89 19")));
        gPatterns.S_SoulEntityIdentity__Constructor               = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 57 48 83 EC 20 48 8B 02 48 8B F9 48 8B CA 48 8B DA FF 10 33 D2")));
        gPatterns.C_ItemAttachmentManager__FindMissileDefenseItem = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 6C 24 ? 48 89 74 24 ? 57 41 56 41 57 48 83 EC 20 83 79 ? ? 48 8B DA 48 8B F9")));
        gPatterns.C_MissileWeaponRuntimeData__SetCocked           = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 48 83 EC 20 48 8B D9 38 91 ? ? ? ? 0F 85")));
        gPatterns.C_MissileWeaponRuntimeData__LoadAmmunition      = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 57 48 83 EC 20 48 8B 42 ? 48 8B D9")));
        gPatterns.C_MissileWeaponRuntimeData__UnloadAmmunition    = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 48 83 EC 20 48 8B 81 ? ? ? ? 48 8D 54 24")));
        gPatterns.C_ShootingSystem__FireWeapon                    = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B C4 48 89 58 ? 48 89 70 ? 48 89 78 ? 55 41 54 41 55 41 56 41 57 48 8D A8 ? ? ? ? 48 81 EC B0 02 00 00 0F 29 70 ? 4D 8B E1")));
        gPatterns.CArrow__ProcessCollision                        = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 55 56 57 41 54 41 55 41 56 41 57 48 8D AC 24 ? ? ? ? 48 81 EC C0 01 00 00 48 8B 05 ? ? ? ? 48 33 C4 48 89 85 ? ? ? ? 4C 8B FA")));
        gPatterns.CProjectile__UpdateWhiz                         = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B C4 48 89 58 08 48 89 70 10 48 89 78 18 4C 89 60 20 55 41 56 41 57 48 8D 68 A1 48 81 EC B0 00 00 00 0F 29 70 D8 48 8B DA")));
        gPatterns.CProjectile__TrailEffect                        = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 48 83 EC 60 48 8B D9 84 D2 0F 84 ? ? ? ? F7 81 A0 00 00 00 00 08 00 00")));
        gPatterns.C_Decoy__ProcessCollision                       = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("4C 8B DC 49 89 5B 08 55 56 57 41 56 41 57 48 83 EC 30 49 8D 43 20")));
        gPatterns.CProjectile__Destroy                            = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 74 24 ? 57 48 81 EC F0 03 00 00")));
        gPatterns.CProjectile__Destructor                         = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 57 48 83 EC 20 48 8D 05 ? ? ? ? 48 8B D9 48 89 01 48 8D 79 ? 48 8D 05 ? ? ? ? 48 89 07 48 8D 05")));
        gPatterns.CArrow__SpawnRecoverableItem                    = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 57 48 83 EC 30 48 8B 99 ? ? ? ? 48 8B FA")));
        gPatterns.C_Item__SpawnPickableImmediate                  = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 55 56 57 41 54 41 55 41 56 41 57 48 8D 6C 24 ? 48 81 EC 80 01 00 00 48 8B 05 ? ? ? ? 48 33 C4 48 89 45 ? 4D 8B E8")));
        gPatterns.C_ActionActor__GetShootingExpansion             = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 74 24 ? 57 48 83 EC 20 48 8B 01 B2 04")));
        gPatterns.C_ActionActor__GetActorStateExpansion           = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 74 24 ? 57 48 83 EC 20 48 8B 01 B2 01 48 8B F1 FF 50 70")));
        gPatterns.C_ActorStateExpansion__RequestJump              = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 56 57 48 83 EC 20 48 8B F9 48 8B 49 08 48 8B 01 FF 50 10 48 8B C8 E8")));
        gPatterns.C_Actor__IsJumping                              = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B 89 90 09 00 00 B2 01 48 8B 01 48 FF A0 98 02 00 00")));
        gPatterns.C_ActorStateExpansion__RequestLadderAction      = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 57 48 83 EC 20 48 8B D9 89 54 24")));
        gPatterns.C_ActorActionLadder__Enter                      = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 74 24 ? 57 48 81 EC B0 00 00 00 48 8B 05 ? ? ? ? 48 33 C4 48 89 84 24 ? ? ? ? 48 8B F1 48 8B 49 ? 48 8B 01 FF 50 ? 48 8B C8")));
        gPatterns.C_ActorActionLadder__Exit                       = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 74 24 ? 57 48 81 EC B0 00 00 00 48 8B 05 ? ? ? ? 48 33 C4 48 89 84 24 ? ? ? ? 48 8B F1 48 8B 49 ? 48 8B 01 FF 50 ? 48 8B CE")));
        gPatterns.C_LadderTraffic__RegulateClimb = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B C4 48 89 58 08 48 89 70 18 48 89 78 20 89 50 10 55 41 56 41 57 48 8B EC 48 81 EC 80 00 00 00 45 8B F0 48 8B F9 E8 ? ? ? ? 48 8B D8 48 85 C0 0F 84 ? ? ? ? 80 7D 58 00 0F 84")));
        gPatterns.C_Actor__GetCarryCorpse = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 48 83 EC 20 48 83 B9 ? ? ? ? ? 48 8B D9 75 ? 48 8B D1 48 8D 4C 24 ? E8 ? ? ? ? 48 8B 10 48 C7 00 00 00 00 00 48 8B 8B ? ? ? ? 48 89 93 ? ? ? ? 48 85 C9 0F 85")));
        gPatterns.C_ActorCarryCorpse__EndCarrying           = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 48 83 EC 40 48 8B 81 ? ? ? ? 48 8B D9 48 8B 48")));
        gPatterns.C_ActorActionCarryCorpse__Enter           = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 48 83 EC 20 48 8B D9 48 8B 49 ? 48 8B 01 48 8B 50")));
        gPatterns.C_ActorActionCarryCorpse__Exit            = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 48 83 EC 20 48 8B D9 48 8B 49 ? 48 8B 01 FF 50 ? 41 B0 02 83 CA FF")));
        gPatterns.C_ActionActor__RequestCarryItem = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 10 57 48 83 EC 20 48 8B 01 48 8B DA 48 8B F9 FF 50 10 B2 0D")));
        gPatterns.C_ActorActionCarryItem__Enter = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 08 57 48 83 EC 20 48 8B D9 48 83 C1 68")));
        gPatterns.C_ActorActionCarryItem__Exit = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 18 56 57 41 56 48 83 EC 60 48 8B F1")));
        gPatterns.C_ActorActionCarryItem__SetCarryPhase = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 30 40 8A F2 48 8B F9 80 FA 03")));
        gPatterns.C_ActorActionCarryItem__SetCarryInputMode = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 08 57 48 83 EC 20 48 8B 49 70 40 8A FA 48 8B 01 FF 50 10 48 8B C8 E8 ? ? ? ? 84 C0")));
        gPatterns.C_ActorActionCarryItem__Update = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B C4 48 89 58 10 55 56 57 41 56 41 57 48 8D 68 ? 48 81 EC B0 00 00 00 80 B9 90 00 00 00 03")));
        gPatterns.C_ActorActionCarryItem__CompleteItemPickup =
            reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B C4 48 89 58 10 48 89 70 18 48 89 78 20 55 41 54 41 55 41 56 41 57 48 8D A8 ? ? ? ? 48 81 EC 90 01 00 00 48 8B 05 ? ? ? ? 48 33 C4 48 89 85 80 00 00 00 4C 8B F1")));
        gPatterns.C_ActorStateMovement__BeginLedge = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 20 55 56 57 41 54 41 55 41 56 41 57 48 8D 6C 24 ? 48 81 EC B0 00 00 00 48 8B 05 ? ? ? ? 48 33 C4 48 89 45 17 41 8B 58 28 48 8B F9")));
        gPatterns.C_ActorStateMovement__EndLedge = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 10 48 89 74 24 18 55 57 41 54 41 56 41 57 48 8B EC 48 83 EC 50 45 33 FF")));
        gPatterns.C_ActorActionCrouch__SetPlayerCrouchInputEnabled = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 18 55 56 57 41 56 41 57 48 8D 6C 24 ? 48 81 EC 90 00 00 00 48 8B 05 ? ? ? ? 48 33 C4 48 89 45 27 48 8B F9")));
        gPatterns.ActorActionAnimSlave__Enter = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 83 EC 28 48 8B 49 58 48 8B 01 FF 50 10 48 8B C8")));
        gPatterns.ActorActionAnimSlave__Exit = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 48 83 EC 20 48 8B D9 48 8B 49 58 48 8B 01 FF 50 10 48 8B C8 E8 ? ? ? ?")));
        gPatterns.I_ActionSets__SetActionCallback = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 83 EC 38 8A 44 24 60 48 83 C1 F8 C6 44 24 28 00")));
        gPatterns.C_ActorActionCarryItem__BeginPutDownAnimation = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 18 55 56 57 48 83 EC 30 48 8B D9 48 8B 49 70")));
        gPatterns.C_ActorActionCarryItem__BeginItemPickupAnimation = reinterpret_cast<std::uint64_t>(
            hook::get_pattern(KCDC_HIDE("48 8B C4 48 89 58 10 48 89 70 18 48 89 78 20 55 41 54 41 55 41 56 41 57 48 8D A8 ? ? ? ? 48 81 EC C0 01 00 00 0F 29 70 ? 0F 29 78 ? 44 0F 29 40 ? 44 0F 29 48 ? 44 0F 29 50 ? 44 0F 29 98 ? ? ? ? 48 8B 05 ? ? ? ?")));
        gPatterns.CMannequinParams__SetParam = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 10 48 89 6C 24 18 48 89 74 24 20 57 48 83 EC 20 4C 8B 09 33 C0 49 8B F0 8B EA 48 8B F9 49 63 59 ? 85 DB 75 ?")));
        // Unique already at its first 17 bytes; the copy-out runs to the end.
        gPatterns.CMannequinParams__GetParam = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 83 EC 28 4D 8B C8 E8 ? ? ? ? 48 85 C0 75 05 48 83 C4 28 C3 0F 10 40 04 41 0F 11 01 F2 0F 10 48 14 F2 41 0F 11 49 10 8B 40 1C 41 89 41 18 B0 01")));
        // Unique already at its first 19 bytes.
        gPatterns.I_LockPickingAction__OnFragmentStarted = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 48 83 EC 40 E8 ? ? ? ? 48 8B D8 48 85 C0 74 31 80 B8 88 00 00 00 02 75 28")));

        gPatterns.C_Action__Finish = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 08 57 48 83 EC 20 48 8B 01 8B FA 48 8B 59 40")));
        gPatterns.C_ChatFollowManager__StartFollowing = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 74 24 ? 57 48 83 EC 20 8B FA 48 8B F1 E8 ? ? ? ? 84 C0")));
        gPatterns.C_ChatFollowManager__IsFollowTargetEligible = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 74 24 ? 57 48 83 EC 20 8B F2 48 8B D9 E8 ? ? ? ? 84 C0")));
        gPatterns.C_ChatFollowManager__MeetsFollowDistanceRequirement = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B C4 48 89 58 ? 48 89 68 ? 48 89 70 ? 57 48 83 EC 50 48 8B E9 0F 29 70")));
        gPatterns.ClassifyFollowTarget = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 74 24 ? 48 89 7C 24 ? 41 56 48 83 EC 20 48 8B 0D")));
        gPatterns.C_ItemManager__FindItem = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 54 24 10 48 83 EC 28 48 81 C1 68 01 00 00")));
        gPatterns.TryLedgeGrab                              = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B C4 48 89 58 ? 48 89 70 ? 48 89 78 ? 55 41 56 41 57 48 8D 68 ? 48 81 EC 90 00 00 00 48 8B 05 ? ? ? ? 48 33 C4 48 89 45 ? 4C 8B 7D")));
        gPatterns.C_ActorShootingExpansion__SetAmmunition   = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 57 48 83 EC 20 48 8B FA 48 8B D9 48 3B 51 ? 74")));
        gPatterns.C_ActorShootingExpansion__Reload          = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 6C 24 ? 48 89 74 24 ? 57 48 83 EC 20 48 8B E9 41 8A F0")));
        gPatterns.C_ActorShootingExpansion__Charge          = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B C1 48 8B 49 ? 48 85 C9 0F 85")));
        gPatterns.C_ActorShootingExpansion__CancelCharging  = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 83 EC 28 48 8B 49 50 48 85 C9 74 ? 48 8B 89 98 00 00 00 48 8B 01 FF 90 E8 01 00 00")));
        gPatterns.C_ActorShootingExpansion__CancelReloading = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 83 EC 28 48 8B 49 50 48 85 C9 74 ? 48 8B 89 98 00 00 00 48 8B 01 FF 90 E0 01 00 00")));
        gPatterns.C_ActorShootingExpansion__Fire            = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 83 EC 28 48 8B 49 50 48 85 C9 74 ? 48 8B 89 98 00 00 00 48 8B 01 FF 90 F0 01 00 00")));
        gPatterns.C_ActorShootingExpansion__IsCharging      = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 57 48 83 EC 20 48 8B 79 50 33 DB 48 85 FF 74 ? 48 8B 07 48 8B CF FF 50 28 84 C0 75 ? 48 8B 8F 98 00 00 00 48 8B 01 FF 90 C8 01 00 00")));
        gPatterns.C_ActorShootingExpansion__IsReloading     = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 57 48 83 EC 20 48 8B 79 50 33 DB 48 85 FF 74 ? 48 8B 07 48 8B CF FF 50 28 84 C0 75 ? 48 8B 8F 98 00 00 00 48 8B 01 FF 90 C0 01 00 00")));
        gPatterns.C_ActorShootingExpansion__IsFiring        = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 74 24 ? 57 48 83 EC 20 48 8B 71 ? 33 FF 48 8B DA")));
        gPatterns.C_ActorShootingExpansion__CanFire         = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 74 24 ? 57 48 83 EC 20 48 8B 79 ? 33 DB 40 8A F2")));
        gPatterns.C_ActorShootingExpansion__CanReload       = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 57 48 81 EC 30 01 00 00 48 8B 05 ? ? ? ? 48 33 C4 48 89 84 24 ? ? ? ? 33 DB")));
        gPatterns.C_ActorShootingExpansion__CanCharge       = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 74 24 ? 57 48 83 EC 20 33 FF 48 8B F1 40 38 79")));

        // Missile state edges; see the group comment in patterns.h.
        gPatterns.C_ActorActionShootingBase__Enter                     = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 57 48 81 EC 40 01 00 00 48 8B 05 ? ? ? ? 48 33 C4 48 89 84 24 ? ? ? ? 48 8B 99")));
        gPatterns.C_ActorActionShootingBase__Exit                      = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 48 83 EC 20 48 8B D9 E8 ? ? ? ? 48 8B 03 48 8B CB FF 90 ? ? ? ? 84 C0")));
        gPatterns.C_ActorActionShootingMain__Exit                      = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 48 83 EC 30 C7 44 24 ? ? ? ? ? 48 8D 05 ? ? ? ? 48 89 44 24 ? 48 8D 54 24 ? 0F 28 44 24 ? 48 8B D9 4C 8B C1 66 0F 7F 44 24 ? 48 8B 49")));
        gPatterns.C_ActorActionShootingReloading__CockWeapon           = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 48 83 EC 20 48 8B D9 E8 ? ? ? ? 48 8D 4B ? E8 ? ? ? ? 48 8B D0")));
        gPatterns.C_ActorActionShootingReloading__LoadWeaponAmmunition = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 48 83 EC 20 33 D2 48 8B D9 E8 ? ? ? ? E8 ? ? ? ? 48 8B 4B")));
        // Unique; the body from `48 8B F1` alone also matches 0x1807255DF.
        gPatterns.C_ActorActionShootingBase__UpdateShootingRestrictions = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 6C 24 ? 48 89 74 24 ? 57 48 83 EC 30 48 8B F1 48 8B 49 60 48 8B 01 FF 50 10 48 8B C8 E8 ? ? ? ? 84 C0 0F 84")));
        gPatterns.CActionMapManager__ApplyFilterByReason                = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B C4 48 89 58 08 48 89 68 10 48 89 70 18 48 89 78 20 41 56 48 83 EC 50 41 8A F1 41 8A E8 48 8B FA 48 85 D2 0F 84 ? ? ? ? 80 3A 00 0F 84")));
        gPatterns.C_CombatPlayerController__SetInventoryBlocked         = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 48 83 EC 20 48 8B D9 38 91 F9 00 00 00 75 06 48 83 C4 20 5B C3 88 91 F9 00 00 00 E8")));
        gPatterns.C_ActorShootingExpansion__OnHandledItemChanged       = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B C4 48 89 58 ? 48 89 70 ? 4C 89 48 ? 4C 89 40")));

        gPatterns.C_Soul__GetState                         = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("8B C2 F3 0F 10 84 81 08 07 00 00 C3")));
        gPatterns.C_Soul__GetStateMax                      = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 83 EC 28 85 D2 75 ? E8 ? ? ? ? F3 0F 10 80 9C 03 00 00")));
        gPatterns.C_Soul__GetStat                          = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 10 48 89 74 24 18 57 48 83 EC 30 49 8B D8 8B FA")));
        gPatterns.C_Soul__GetRelativeStat                  = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 55 56 57 48 83 EC 38 8A 44 24 68 49 8B F8")));
        gPatterns.C_Soul__GetDerivedStat                   = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 08 48 89 74 24 18 57 48 83 EC 30 49 8B F0 8B DA")));
        gPatterns.C_Soul__GetSkill                         = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 83 EC 38 8B C2 0F 57 DB 44 8B 8C C1 E8 05 00 00")));
        gPatterns.C_Soul__GetRelativeSkill                 = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 83 EC 38 0F 29 74 24 20 E8 ? ? ? ? 0F 28 F0")));
        gPatterns.C_SoulStateStorage__CopyFrom             = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B C4 48 89 58 08 48 89 68 10 48 89 70 18 57 41 56 41 57 48 83 EC 40 48 8B D9 0F 29 70 ? 48 8D 4A 10")));
        gPatterns.C_SoulPropertyEvents__NotifyStateChanged = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("81 C2 17 01 00 00 E9 ? ? ? ?")));
        gPatterns.C_SoulPropertyNotifier__NotifyProperty   = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("8B C2 48 83 C1 08 48 03 C0 48 8D 15 ? ? ? ? 4C 8B 44 C2 08 48 8B 14 C2 E9")));
        gPatterns.C_RagdollManager__Rephysicalize          = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 08 57 48 83 EC 20 40 8A B9 A8 00 00 00 48 8B D9 48 8B 89 88 00 00 00 48 8B 01 FF 90 B8 04 00 00")));
        gPatterns.C_RagdollManager__SetPhysicsProfile      = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 08 48 89 74 24 10 57 48 81 EC 90 00 00 00 0F B6 F2")));
        gPatterns.C_RagdollManager__RequestStandUp         = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 08 57 48 83 EC 20 80 B9 AA 00 00 00 00 8B FA 48 8B D9 75 44 48 8B 81 88 00 00 00")));
        gPatterns.C_ActorStateExpansion__RequestDeadAction = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 10 48 89 74 24 20 57 48 83 EC 20 48 8B F9 40 8A F2 48 8B 49 08 48 8D 54 24 30 41 B0 0F")));
        gPatterns.C_ActorActionDead__Enter                 = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 30 48 8B F1 48 8B 49 58 48 8B 01 FF 50 10 48 8B C8 E8 ? ? ? ? 48 8B 4E 58")));
        gPatterns.C_Human__DropHandItems                   = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 20 48 8B 01 FF 90 F8 05 00 00 48 8B F0 33 DB")));
        gPatterns.C_ActorActionUnconscious__Enter          = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 08 57 48 83 EC 20 48 8B D9 E8 ? ? ? ? 84 C0 74 07 32 C0 E9")));
        gPatterns.C_ActorActionUnconscious__Exit           = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 48 83 EC 20 48 8B 41 58 48 8B D9 45 33 C0 48 8B 50 28 48 8B 8A 90 09 00 00 B2 0E")));
        gPatterns.C_ActorActionMinigame__Exit              = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 48 83 EC 20 48 8B D9 48 8B 49 68 48 8B 01 FF 50 40 84 C0 0F 85 ? ? ? ? 48 83 C4 20 5B C3")));
        // The 25h in the tail is the module message id (37) the dispatch
        // publishes, so the pattern is pinned by the function's own payload.
        gPatterns.C_Soul__DispatchSoulDeath = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 48 83 EC 50 48 8D 05 ? ? ? ? C7 44 24 28 25 00 00 00")));

        // The one entry every outfit application goes through; its armor half
        // takes off whatever the outfit does not name.
        gPatterns.C_InventorySoul__ApplyOutfit             = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 57 48 83 EC 20 48 8B 01 0F B6 FA 48 8B D9 FF 90 C0 00 00 00")));
        gPatterns.RaiseGameOver                            = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 10 48 89 74 24 18 57 48 83 EC 60 83 39 00 8B FA 48 8B D9 0F 85 ? ? ? ? E8")));
        gPatterns.C_Player__Revive                         = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 74 24 ? 57 48 83 EC 20 48 8B D9 E8 ? ? ? ? 48 8D 8B")));
        gPatterns.C_Actor__ApplyInertiaToAnimatedCharacter = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B C4 48 89 58 08 57 48 83 EC 70 48 8B 79 20 48 8B D9 0F 29 70 E8 0F 28 F3 0F 29 78 D8 0F 28 FA 44 0F 29 40 C8 44 0F 28 C1 48 85 FF 74")));
        gPatterns.C_PlayerMaterialEffects__PlayDeathEffect = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 57 48 81 EC E0 02 00 00")));
        gPatterns.C_SoulStateStorage__SetState             = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B C4 48 89 58 ? 48 89 68 ? 48 89 70 ? 48 89 78 ? 41 56 48 83 EC 40 0F 29 70 ? 49 8B E9 8B F2 0F 28 F2")));
        gPatterns.C_Soul__SetStateWithCause                = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B C4 48 89 58 ? 48 89 70 ? 57 48 83 EC 60 0F 29 70 ? 49 8B F1")));
        gPatterns.C_Soul__ResolveBodyDescriptionPart       = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 57 48 83 EC 20 48 8B D9 0F B6 FA 48 8B 89 ? ? ? ? 48 85 C9 74 ? E8 ? ? ? ? 4C 8B 00 41 83 78 F8 00")));
        gPatterns.C_RPGCombat__HasShootingStamina          = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 83 EC 28 48 8B 02 48 8B CA BA 01 00 00 00")));

        // The Firing action's direct call resolves the stripped singleton;
        // its allocator prologue is shared by several one-byte systems.
        gPatterns.C_ShootingSystem__GetInstance              = hook::get_address<std::uint64_t>(hook::get_pattern(KCDC_HIDE("E8 ? ? ? ? 49 8B 4F 60 48 8B E8 F3 41 0F 10 B7 A0 00 00 00")), 1, 5);
        gPatterns.C_ShootingSystem__SpawnAndLaunchProjectile = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B C4 48 89 58 ? 48 89 68 ? 48 89 70 ? 57 41 56 41 57 48 81 EC B0 00 00 00 0F 29 70")));
        // Native decoy spawn and descriptor construction.
        gPatterns.CWeaponSystem__SpawnAmmo                   = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 57 48 83 EC 20 48 8B DA 48 8B F9 E8 ? ? ? ? 48 85 C0 74 ? 80 78")));
        gPatterns.CProjectile__SProjectileDesc__Construct    = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 48 83 EC 20 48 8B D9 89 11 4C 89 41 ? C6 41")));
        gPatterns.C_Decoy__VTable                            = hook::get_address<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8D 05 ? ? ? ? 66 C7 83 ? ? ? ? ? ? 48 89 03 48 8D 05 ? ? ? ? 48 89 43 ? 48 8D 05")), 3, 7);
        gPatterns.CProjectile__Launch                        = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B C4 48 89 58 ? 48 89 70 ? 48 89 78 ? 4C 89 70 ? 55 48 8D 68 ? 48 81 EC 20 01 00 00 0F 29 70 ? 48 8B D9")));

        // `lea rsi, [rcx-0x70]` is the walk back from the I_ScheduleUpdatable
        // subobject to the C_Stash, and is what makes this the stash's own
        // scheduled tick rather than any other updatable's.
        gPatterns.C_Stash__ScheduledUpdate         = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 6C 24 ? 48 89 74 24 ? 57 48 83 EC 50 48 8D 71 90 48 8B F9 48 8B CE 8B EA")));
        gPatterns.C_InventorySoul__ScheduledUpdate = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 74 24 ? 57 48 81 EC B0 00 00 00 48 8B 05 ? ? ? ? 48 33 C4 48 89 84 24 ? ? ? ? 48 8D 79 ? 48 8B D9 48 8B 4F")));
        gPatterns.C_InventorySoul__FullyRestock    = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 74 24 ? 57 48 81 EC B0 00 00 00 48 8B 05 ? ? ? ? 48 33 C4 48 89 84 24 ? ? ? ? 41 0F B6 D8")));

        // The two slot property loaders share a prologue and separate on frame
        // size: the inlined-accessor body takes 0x30 and spills xmm6, the
        // vtable-dispatch body takes 0x70. Each matches once.
        gPatterns.C_ItemSlot__LoadScriptProperties            = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 55 53 57 48 8B EC 48 83 EC 30 48 8B DA 0F 29 74 24")));
        gPatterns.C_ItemSlot__LoadScriptPropertiesVirtualCall = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 55 53 57 48 8B EC 48 83 EC 70 48 8B F9 C6 45")));

        // `CWh_FmodStudioWrapper::SetMasterChannelGroupMute` is the shortest
        // reader of the holder and the only one that spells the wrapper's
        // whole shape in one prologue: load the global, test the ready state
        // at +0xD8 against 2, then take the Studio system out of +0xE0.
        gPatterns.CWh_FmodStudioWrapper__Instance = hook::get_address<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 4C 24 08 53 48 83 EC 20 48 8B 0D ? ? ? ? 8A DA 83 B9 D8 00 00 00 02 75 ? 48 8B 89 E0 00 00 00")), 13, 17);

        // --- Quest graph -------------------------------------------------

        // Three identical copies (same allocator, same counter); the padding before the third makes it unique, since GOG differs in the bytes after it.
        gPatterns.FreeActionStorage = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("CC CC CC CC CC CC CC CC 48 83 EC 28 48 85 C9 74 12 48 8B 05 ? ? ? ? 33 D2 FF D0 F0 01 05 ? ? ? ? 48 83 C4 28 C3"), 8));

        gPatterns.C_SharedResource__AddRef  = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 57 48 83 EC 20 0F B7 41 ? 48 8B F9")));
        gPatterns.C_SharedResource__Release = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 48 83 EC 20 48 8B D9 B8 FF FF FF FF 66 F0 0F C1 41")));

        gPatterns.ConceptResourceRegistry__Get = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 83 EC 28 E8 ? ? ? ? 48 8B 88 ? ? ? ? 48 8B 41 ? 48 83 C0 10")));

        gPatterns.C_Quest__Construct     = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 57 48 83 EC 20 48 8B F9 E8 ? ? ? ? 48 8D 05 ? ? ? ? 48 89 07 48 8D 9F ? ? ? ? 48 8D 05 ? ? ? ? 48 8B CB 48 89 87 ? ? ? ? 83 C8 FF")));
        gPatterns.C_Objective__Construct = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 57 48 83 EC 20 48 8B F9 E8 ? ? ? ? 48 8D 05 ? ? ? ? 48 89 07 48 8D 4F ? 48 8D 05 ? ? ? ? 48 89 47")));

        // The factory itself is not unique -- a byte-identical twin sits at
        // 0x18272EC1C -- so it is reached from the one-line forwarding thunk
        // in front of it, whose tail is. The pattern lands on that thunk's
        // `call`, and the target is decoded from the rel32 rather than counted
        // forward to: the bytes between the two are a `ret` and a `CC` pad,
        // and being one short of the entry point puts the call on the pad,
        // which executes as an int 3.
        gPatterns.C_ConstantPort__Create = hook::get_address<std::uint64_t>(hook::get_pattern(KCDC_HIDE("E8 ? ? ? ? 48 8B C3 48 83 C4 20 5B C3 CC 40 53 55 56 57 41 56 48 83 EC 50 48 8B 05 ? ? ? ? 48 33 C4 48 89 44 24 ? 33 F6")), 1, 5);

        gPatterns.C_Node__AddPort        = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 74 24 ? 57 48 83 EC 30 48 8B F9 48 8B DA 48 8B 0A")));
        gPatterns.C_Node__FindPortByName = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 48 83 EC 30 48 8B DA 48 C7 02 00 00 00 00")));

        gPatterns.MakeQuestProgressVariant = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE(
            "40 53 48 83 EC 40 48 8B 05 ? ? ? ? 48 33 C4 48 89 44 24 38 48 8B D9 48 8D 05 ? ? ? ? 8B 0A 48 8D 54 24 20 89 4C 24 20 48 8B CB 48 89 44 24 30 E8 ? ? ? ? 45 33 C0 48 8D 54 24 20 33 C9 FF 54 24 30 48 8B C3 48 8B 4C 24 38 48 33 CC E8 ? ? ? ? 48 83 C4 40 5B C3 CC 40 53 48 83 EC 40 48 8B 05 ? ? ? ? 48 33 C4 48 89 44 24 38 48 8B D9 48 8D 05 ? ? ? ? 8B 0A 48 8D 54 24 20 89 4C 24 20 48 8B CB 48 89 44 24 30 E8 ? ? ? ? 45 33 C0 48 8D 54 24 20 33 C9 FF 54 24 30 48 8B C3 48 8B 4C 24 38 48 33 CC E8 ? ? ? ? 48 83 C4 40 5B C3 CC 48 89 5C 24 08 57 48 83 EC 30 83 B9 24 01 00 00 FF 48 8B D9")));

        gPatterns.C_Quest__GetLevelId = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 55 48 8B EC 48 83 EC 30 48 8D 45")));

        gPatterns.GetCurrentLevelId = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 48 83 EC 20 E8 ? ? ? ? 83 CB FF")));

        gPatterns.C_UIQuestLog__BeginTrackingQuest = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 08 57 48 83 EC 40 48 8B DA C7 44 24 28 00 00 00 00 48 8D 05")));
        gPatterns.C_UIQuestLog__EndTrackingQuest   = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 08 57 48 83 EC 40 48 8B DA 48 8B F9 48 83 C2 10 48 83 C1 40")));
        gPatterns.LookupQuestTrackingId            = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 10 57 48 83 EC 20 4C 8D 4C 24 30 C7 44 24 30 FF FF FF FF 4C 8B C1")));
        gPatterns.AssignQuestTrackingSlot          = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 48 83 EC 20 33 DB 4C 8B D2 4C 8B D9 45 85 C0 75 ? 48 8D 15 ? ? ? ?")));

        gPatterns.C_UIQuestLog__CollectQuestMarkers = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 74 24 ? 57 48 83 EC 50 48 8B F2 48 C7 02 00 00 00 00")));
        gPatterns.C_UIHudEvents__InvokeQuestFlashCommand = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE(
            "48 89 5C 24 08 48 89 74 24 10 48 89 7C 24 18 41 56 48 81 EC 80 00 00 00 48 8B 01 49 8B F9 49 8B F0 4C 8B F2 48 8B D9 FF 50 60 48 8B 5B 48 48 85 DB 0F 84 ? ? ? ? 48 8D 4C 24 50 E8 ? ? ? ? 48 8B 84 24 D8 00 00 00 48 8D 4C 24 50 4C 8B 8C 24 B0 00 00 00 4C 8B C7 8B 16")));
        gPatterns.RefreshQuestMarkers               = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 74 24 ? 55 57 41 54 41 56 41 57 48 8D 6C 24 ? 48 81 EC C0 00 00 00 4C 8B F9")));

        // The match is the `add [rsi+8], 38h` of the in-place branch; the
        // reallocating branch's call sits 0x11 bytes on.
        gPatterns.std_vector_S_QuestMarkerEntry__Emplace_reallocate = hook::get_address<std::uint64_t>(reinterpret_cast<char *>(hook::get_pattern(KCDC_HIDE("48 83 46 08 38 EB ? 4C 8D 45 ? 48 8B D0 48 8B CE E8"))) + 0x11, 1, 5);

        // The lea nine bytes into the match is `C_Log`'s vtable. Its rel32 is
        // decoded rather than the address read out of the image, and the call
        // ahead of it -- `C_LogBase::C_LogBase` -- is spelled in full because
        // seven other reflected factories share this allocate-zero-construct
        // shape byte for byte.
        gPatterns.C_Log__VTable = hook::get_address<std::uint64_t>(reinterpret_cast<char *>(hook::get_pattern(KCDC_HIDE("0F 11 40 20 E8 ? ? ? ? 48 8D 05 ? ? ? ? 48 89 03 EB 02 33 DB 48 8B D3 48 8D 4C 24 20 E8 ? ? ? ? 48 8B D0 48 8B CF E8 ? ? ? ? 48 8B 5C 24 28"))) + 9, 3, 7);

        gPatterns.C_LogBase__Construct = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 48 83 EC 20 48 8D 05 ? ? ? ? C7 41 ? ? ? ? ? 48 89 01 48 8B D9 E8 ? ? ? ? 48 83 C0 0C 48 89 43 ? 48 8D 05")));

        gPatterns.C_Quest__GetProgress = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 81 C1 B0 00 00 00 E9 ? ? ? ? 40 53 48 83 EC 20 48 8B D9 48 8B 09")));

        gPatterns.C_QuestManager__CollectActiveQuests = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 74 24 ? 57 48 83 EC 20 48 C7 02 00 00 00 00 48 8B DA 48 C7 42 ? ? ? ? ? 48 C7 42 ? ? ? ? ? 48 8B 71")));
        gPatterns.C_QuestManager__FindQuestByName     = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 74 24 ? 48 89 7C 24 ? 41 56 48 83 EC 20 48 8B 79 ? 4D 8B F0 48 8B 71")));

        // The shop screen's anchors. Each pattern is unique in the image;
        // the price one is pinned by its 0x60 frame spilling XMM6 and moving
        // the item argument straight into RCX for the base-price lookup.
        gPatterns.C_RPGInventory__GetShopItemPrice        = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B C4 48 89 58 ? 48 89 68 ? 48 89 70 ? 48 89 78 ? 41 54 41 56 41 57 48 83 EC 60 0F 29 70 ? 49 8B C8")));
        gPatterns.C_ShoppingTransaction__ApplyTransaction = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 55 56 57 48 8D 6C 24 ? 48 81 EC E0 00 00 00 49 8B D8")));
        gPatterns.C_UIShop__Close                         = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 74 24 ? 48 89 7C 24 ? 55 41 56 41 57 48 8B EC 48 83 EC 30 48 8D 59")));
        gPatterns.shopmodule__GetShopSaleRefusal          = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 57 48 83 EC 20 48 8B DA 49 8B F8 48 8B CB BA 06 00 00 00")));
        gPatterns.shopmodule__TransferMoneyWithKeeper     = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 74 24 ? 48 89 4C 24 ? 57 48 83 EC 20 41 8B F0")));
        gPatterns.C_ShopModule__GetOrCreateTransaction    = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 74 24 ? 48 89 54 24 ? 55 57 41 56 48 8B EC 48 83 EC 50 4C 8D 71")));

        // The native NPC interaction menu's Talk and Rob gates, and the door
        // Talk opens. Each pattern is unique in the image.
        gPatterns.C_Human__CanTalk               = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B C4 48 89 58 ? 48 89 68 ? 48 89 70 ? 48 89 78 ? 41 56 48 83 EC 40 48 8B 01 4D 8B F0")));
        gPatterns.C_Actor__RequestDialog         = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 55 53 56 57 41 56 41 57 48 8D 6C 24 ? 48 81 EC 38 01 00 00")));
        gPatterns.C_ScriptBindHuman__CanBeRobbed = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 57 48 83 EC 20 48 8B FA E8 ? ? ? ? 48 85 C0 0F 84")));
        gPatterns.I_ShopTransaction__Close       = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 74 24 ? 57 48 81 EC 90 00 00 00 48 8B 05 ? ? ? ? 48 33 C4 48 89 84 24 ? ? ? ? 48 8B F9 48 8D 91")));

        // The native conversation's twin setup and the placement it runs for
        // each participant. Each pattern is unique in the image.
        gPatterns.dialogmodule__CreateAndPlaceTwins = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 6C 24 ? 48 89 74 24 ? 57 48 83 EC 20 48 8B F1 B3 01 33 FF")));
        gPatterns.dialogmodule__PlaceTwin           = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 74 24 ? 57 48 83 EC 20 48 8B F9 49 8B F0 48 8B CA 48 8B DA")));

        // The pickpocketing minigame and the two native seams it reaches past
        // itself. Each pattern is unique in the shipped image.
        gPatterns.C_Pickpocketing__Start                       = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 55 56 57 48 8D 6C 24 ? 48 81 EC B0 00 00 00 48 8B 05 ? ? ? ? 48 33 C4 48 89 45 ? 48 8B 01")));
        gPatterns.C_Pickpocketing__OpenLootUI                  = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 74 24 ? 55 57 41 55 41 56 41 57 48 8D AC 24 ? ? ? ? B8 20 16 00 00")));
        gPatterns.C_Pickpocketing__ResolvePickpocketingOutcome = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 55 56 57 41 54 41 55 41 56 41 57 48 8B EC 48 83 EC 40 41 8A F9")));
        gPatterns.C_Pickpocketing__Finish                      = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 74 24 ? 57 48 83 EC 20 48 8B F1 E8 ? ? ? ? 48 8B CE E8 ? ? ? ? E8")));
        gPatterns.C_Minigame__SendMessageToInteractionTarget   = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 74 24 ? 57 48 83 EC 20 48 8B F2 49 8B F8 48 8D 54 24")));
        gPatterns.C_StealItemsEffect__Apply                    = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("4C 8B DC 49 89 5B ? 49 89 6B ? 56 57 41 54 41 56 41 57 48 83 EC 20 48 8B 41")));
        gPatterns.entitymodule__GetPickpocketLootGroup         = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 48 83 EC 20 F6 41 ? ? 48 8B C2")));

        // The dice minigame. Each pattern is unique in the shipped image.
        // `TransitionToRollingState` shares its body with every other state
        // transition, so it is read off its call in
        // `C_ScoringDiceGameState::Advance`, the 21st byte.
        gPatterns.C_DieSet__UpdateDieNavigationCameraTransform = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B C4 48 89 58 ? 48 89 70 ? 48 89 78 ? 55 41 54 41 55 41 56 41 57 48 8D 68 ? 48 81 EC C0 00 00 00 0F 29 70 ? 4C 8D 91")));
        gPatterns.C_Dice__StartGameWithTargetScore          = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B C4 48 89 58 ? 48 89 70 ? 57 48 83 EC 30 C7 40 ? ? ? ? ? 48 8D 40")));
        gPatterns.C_Dice__InitializeDiceTableScene          = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 6C 24 ? 56 57 41 54 41 56 41 57 48 83 EC 30 48 8B 41 ? 48 8B D9")));
        gPatterns.C_Dice__ResetDiceTableTransforms          = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 74 24 ? 57 48 83 EC 30 48 8B 41 ? 48 8B D9 48 8B 0D")));
        gPatterns.C_Dice__OnHoldDieRequested                = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 74 24 ? 57 48 83 EC 30 48 8B 01 48 8B F9 FF 90")));
        gPatterns.C_Dice__OnScoreRequested                  = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 48 83 EC 20 48 8B 01 48 8B D9 FF 90 ? ? ? ? 84 C0 74 ? 48 8B CB E8 ? ? ? ? 48 8B C8")));
        gPatterns.C_Dice__OnPassRequested                   = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 48 83 EC 20 48 8B 01 48 8B D9 FF 90 ? ? ? ? 84 C0 74 ? 48 8B CB E8 ? ? ? ? 45 33 C0")));
        gPatterns.C_UIDice__ShowDiceScore                      = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("44 89 4C 24 ? 89 54 24 ? 55 53 56 57 41 56")));
        gPatterns.C_Dice__Finish                            = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 74 24 ? 57 48 83 EC 40 48 8B 01 48 8B D9 FF 50 ? 84 C0")));
        gPatterns.C_Dice__InitializeDiceSession             = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 48 83 EC 30 48 8B D9 33 D2 48 8B 0D ? ? ? ? 48 8B 01 FF 50 ? 48 8B 0D")));
        gPatterns.C_Dice__SetDiceActorsInteractionMode      = hook::get_call<std::uint64_t>(gPatterns.C_Dice__InitializeDiceSession + 0x108);
        gPatterns.C_Dice__InstallPlayerAction               = hook::get_call<std::uint64_t>(gPatterns.C_Dice__InitializeDiceSession + 0x11B);
        gPatterns.playermodule__s_diceLcgState              = hook::get_address<std::uint64_t>(gPatterns.C_Dice__InitializeDiceSession + 0x2A, 3, 7);
        gPatterns.C_DiceGameModel__RequestSetDieHeld        = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("4C 8B 91 ? ? ? ? 44 8B DA 48 8D 51")));
        gPatterns.C_DiceGameModel__RequestScoreSelectedDice = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 57 48 83 EC 20 48 8B D9 48 8B 89 ? ? ? ? 48 8B 01 FF 50")));
        gPatterns.C_DiceGamePlayerModel__RollDice           = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 55 56 57 41 54 41 55 41 56 41 57 48 8D 6C 24 ? 48 81 EC A0 00 00 00 33 FF")));
        gPatterns.playermodule__TransitionToRollingState =
            hook::get_call<std::uint64_t>(reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 48 83 EC 20 48 8B 01 48 8B DA FF 50 ? 48 8B D0 48 8B CB E8 ? ? ? ? B0 01 48 83 C4 20 5B C3 CC CC 48 89 5C 24 ? 57 48 83 EC 20 48 8B D9"), 21)));
        gPatterns.playermodule__TransitionToEndingState     = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("4C 8B DC 49 89 5B ? 45 89 43 ? 57 48 83 EC 40")));
        gPatterns.C_SimpleDiceAI__TryHandleGameState        = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B C4 48 89 58 ? 48 89 68 ? 48 89 70 ? 48 89 78 ? 41 56 48 83 EC 20 4C 8B 74 24 ? 48 8B F9")));
        gPatterns.C_DiceBadge__BindBackingItemById =
            reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 57 48 83 EC 20 48 8B DA 48 8B F9 E8 ? ? ? ? 48 8D 54 24 ? 48 89 5C 24 ? 48 8B 80 ? ? ? ? 48 8B 88 ? ? ? ? 48 81 C1 68 01 00 00 E8 ? ? ? ? 48 8B D8 48 85 C0 74")));
        gPatterns.ForceMetaroleDialog                       = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 74 24 ? 55 57 41 56 48 8D 6C 24 ? 48 81 EC 50 01 00 00 48 8B 05 ? ? ? ? 48 33 C4 48 89 45 ? 48 8B F1")));

        // Progression. Each pattern is unique in the shipped image. The four
        // XP effect creators are byte-identical but for the multiplier flag
        // and the constructor they call, so each is pinned by that call's
        // displacement. The event factory is folded with its siblings and is
        // reached through its call in `C_ScriptBindSoul::AddStatXP`, pinned by
        // the stat creator that call is followed by.
        gPatterns.C_SkillXPEffect__Apply             = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 55 56 57 48 83 EC 50 48 8B F9")));
        gPatterns.C_StatXPEffect__Apply              = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 55 56 57 48 8B EC 48 83 EC 60 48 8B F9 0F 29 74 24")));
        gPatterns.C_PerkLearningState__LearnPerk     = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B C4 48 89 58 ? 48 89 70 ? 57 48 83 EC 30 0F 28 02")));
        gPatterns.C_PerkList__AddPerk                = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 55 56 57 41 56 41 57 48 8B EC 48 83 EC 50 0F 29 74 24")));
        gPatterns.C_PerkList__RemovePerk             = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 57 48 83 EC 30 48 8B F9 48 83 C1 40 4C 8B 01")));
        gPatterns.C_PerkList__Respec                 = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B C4 48 89 58 ? 48 89 70 ? 48 89 78 ? 55 48 8D A8 ? ? ? ? 48 81 EC 80 04 00 00")));
        gPatterns.C_PerkList__RefreshAvailablePerks  = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B C4 48 89 58 ? 48 89 68 ? 48 89 70 ? 48 89 78 ? 41 56 48 83 EC 20 48 83 79 ? ? 48 8B F1")));
        gPatterns.C_PerkList__OnEvent                = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B C4 48 81 EC A8 00 00 00 48 89 48")));
        gPatterns.C_RPGInventory__ApplyInventoryPerk = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 6C 24 ? 48 89 74 24 ? 57 48 83 EC 40 48 8B 05 ? ? ? ? 41 8A D9")));
        // The global's address, off the `lea rcx` the perk screen's confirmed
        // learn (`LearnConfirmedPerk182AFF3DC`) passes slot 38 as `this`.
        gPatterns.C_RPGInventory__Instance               = hook::get_address<std::uint64_t>(reinterpret_cast<char *>(hook::get_pattern(KCDC_HIDE("48 8B 0D ? ? ? ? 4C 8D 44 24 ? 0F 28 07 41 B1 01 48 8B 93 68 06 00 00 66 0F 7F 44 24 ? 4C 8B 91 30 01 00 00 48 8D 0D"))) + 38, 3, 7);
        gPatterns.C_Soul__HasPerk                        = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 74 24 ? 57 48 83 EC 40 48 8B 05 ? ? ? ? 48 8B D9")));
        gPatterns.rpgmodule__CreateSkillXPEffect         = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE(
            "48 8B C4 48 89 58 ? 48 89 68 ? 48 89 70 ? 48 89 48 ? 57 48 83 EC 50 33 FF 41 8B F1 39 3D ? ? ? ? 49 8B D8 48 8B EA 0F 84 ? ? ? ? 48 8B 05 ? ? ? ? 48 8D 54 24 ? 45 33 C0 41 8D 48 ? FF D0 8B 4C 24 ? F0 01 0D ? ? ? ? F0 83 05 ? ? ? ? ? F0 FF 05 ? ? ? ? 48 85 C0 74 ? 48 8B 8C 24 ? ? ? ? 44 8B CE 89 7C 24 ? 4C 8B C3 C6 44 24 ? 01 8B 11 48 8D 4C 24 ? 48 89 4C 24 ? 48 8B C8 89 54 24 ? 48 8B D5 E8 1B 00 00 00")));
        gPatterns.rpgmodule__CreateStatXPEffect          = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE(
            "48 8B C4 48 89 58 ? 48 89 68 ? 48 89 70 ? 48 89 48 ? 57 48 83 EC 50 33 FF 41 8B F1 39 3D ? ? ? ? 49 8B D8 48 8B EA 0F 84 ? ? ? ? 48 8B 05 ? ? ? ? 48 8D 54 24 ? 45 33 C0 41 8D 48 ? FF D0 8B 4C 24 ? F0 01 0D ? ? ? ? F0 83 05 ? ? ? ? ? F0 FF 05 ? ? ? ? 48 85 C0 74 ? 48 8B 8C 24 ? ? ? ? 44 8B CE 89 7C 24 ? 4C 8B C3 C6 44 24 ? 01 8B 11 48 8D 4C 24 ? 48 89 4C 24 ? 48 8B C8 89 54 24 ? 48 8B D5 E8 E3 02 00 00")));
        gPatterns.rpgmodule__CreateSkillXPEffectUnscaled = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE(
            "48 8B C4 48 89 58 10 48 89 68 18 48 89 70 20 48 89 48 08 57 48 83 EC 40 33 FF 41 8B F1 39 3D ? ? ? ? 49 8B D8 48 8B EA 0F 84 ? ? ? ? 48 8B 05 ? ? ? ? 48 8D 54 24 50 45 33 C0 41 8D 48 40 FF D0 8B 4C 24 50 F0 01 0D ? ? ? ? F0 83 05 ? ? ? ? 40 F0 FF 05 ? ? ? ? 48 85 C0 74 32 48 8B 4C 24 70 44 8B CE 89 7C 24 30 4C 8B C3 40 88 7C 24 28 8B 11 48 8D 4C 24 78 48 89 4C 24 20 48 8B C8 89 54 24 78 48 8B D5 E8 ? ? ? ? 48 8B F8 48 8B 5C 24 58 48 8B C7 48 8B 6C 24 60 48 8B 74 24 68 48 83 C4 40 5F C3 CC CC CC 48 ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? 48 ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? EC ? ? ? ? ? ? ? ? ? ? 48 ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? 48")));
        gPatterns.rpgmodule__CreateStatXPEffectUnscaled  = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE(
            "48 8B C4 48 89 58 ? 48 89 68 ? 48 89 70 ? 48 89 48 ? 57 48 83 EC 40 33 FF 41 8B F1 39 3D ? ? ? ? 49 8B D8 48 8B EA 0F 84 ? ? ? ? 48 8B 05 ? ? ? ? 48 8D 54 24 ? 45 33 C0 41 8D 48 ? FF D0 8B 4C 24 ? F0 01 0D ? ? ? ? F0 83 05 ? ? ? ? ? F0 FF 05 ? ? ? ? 48 85 C0 74 ? 48 8B 4C 24 ? 44 8B CE 89 7C 24 ? 4C 8B C3 40 88 7C 24 ? 8B 11 48 8D 4C 24 ? 48 89 4C 24 ? 48 8B C8 89 54 24 ? 48 8B D5 E8 ? ? ? ? 48 8B F8 48 8B 5C 24 58 48 8B C7 48 8B 6C 24 60 48 8B 74 24 68 48 83 C4 40 5F C3 CC CC CC 48 89 5C 24 10 48 89 74 24 18 48 89 7C 24 20 55 41 56")));
        gPatterns.rpgmodule__FindBuffDefinition          = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE(
            "48 89 4C 24 ? 57 48 83 EC 30 4C 8B 05 ? ? ? ? 48 8D 4C 24 ? 48 8B FA 4C 8B CA 48 8B 15 ? ? ? ? E8 ? ? ? ? 48 8B 44 24 ? 48 3B 05 ? ? ? ? 74 ? 0F 10 00 66 48 0F 7E C1 48 2B 0F 75 ? 66 0F 73 D8 08 66 48 0F 7E C1 48 2B 4F ? 48 85 C9 74 ? 33 C0 48 83 C4 30 5F C3 CC CC 48 89 5C 24")));
        gPatterns.rpgmodule__NotifySkillChanged          = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 57 48 83 EC 20 8B DA 48 8B F9 48 8B 8F")));
        gPatterns.C_EventFactory__CreateAlchemyStartedEvent =
            hook::get_call<std::uint64_t>(reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B 46 30 4C 8D 44 24 ? 48 8D 54 24 ? 48 89 44 24 ? E8 ? ? ? ? 48 8B 03 48 8B CB FF 50 08 4C 8B 46 30 44 8B CD 48 8B 54 24 ? 44 89 74 24 ? E8 14 0F 00 00"), 19)));

        gPatterns.C_Cart__SetupCartOnReset  = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B C4 48 89 58 ? 48 89 68 ? 48 89 70 ? 57 41 56 41 57 48 83 EC 30 80 B9 ? ? ? ? ? 44 8A FA")));
        gPatterns.C_Cart__SetDriver         = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 6C 24 ? 48 89 74 24 ? 57 48 83 EC 20 48 8B D9 48 8B FA 48 83 C1 78")));
        gPatterns.C_Cart__ConfirmMoveStart  = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 57 48 83 EC 20 48 8B D9 48 83 C1 78 E8 ? ? ? ? 48 8D 8B")));
        gPatterns.C_Cart__CanOccupyPosition = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 48 83 EC 20 0F B6 C2 80 BC 08")));
        gPatterns.FindCartByEntityId =
            reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 48 83 EC 20 8B D9 E8 ? ? ? ? 8B D3 48 8B 48 ? 48 8B 01 FF 90 ? ? ? ? 48 85 C0 74 ? 48 8B 08 48 8D 15 ? ? ? ? 4C 8B 41 ? 48 8B C8 48 83 C4 20 5B 49 FF E0 48 83 C4 20 5B C3 48 8B C4")));
        gPatterns.SetCartPath                              = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B C4 48 89 58 ? 48 89 70 ? 48 89 78 ? 44 88 40 ? 55 41 54 41 55 41 56 41 57 48 8D 68 ? 48 81 EC 00 01 00 00")));
        gPatterns.MakeCartPathNode                         = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 48 83 EC 20 F2 0F 10 02 48 8B D9 F2 0F 11 01 8B 42 ? 89 41 ? 83 C8 FF")));
        gPatterns.C_CartPath__AppendNode                   = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B C4 48 89 58 ? 55 56 57 41 54 41 55 41 56 41 57 48 8D 68 ? 48 81 EC 40 01 00 00 0F 29 70 ? 48 8B F1 0F 29 78 ? 44 0F 29 40")));
        gPatterns.C_CartPath__BuildFromPathEntity          = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 74 24 ? 57 48 83 EC 20 48 8B FA 48 8B F1 E8 ? ? ? ? 48 8B 07")));
        gPatterns.SetCartActorState                        = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 6C 24 ? 56 57 41 56 48 83 EC 20 40 8A A9")));
        gPatterns.C_CartPlayerControl__MountPlayer         = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 74 24 ? 57 48 83 EC 30 48 8B D9 41 8A F8")));
        gPatterns.C_CartPlayerControl__CanPlayerMount      = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B C4 48 89 58 ? 48 89 68 ? 48 89 70 ? 48 89 78 ? 41 54 41 56 41 57 48 83 EC 20 4C 8B F9")));
        gPatterns.C_DisablePlayerCartDismount__SetDismountDisabled = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 48 83 EC 20 8A DA E8 ? ? ? ? 48 8B 48 10 48 8B 01 FF 90 00 02 00 00 48 8B 88 38 0D 00 00 8B 41 14 84 DB")));
        gPatterns.C_CartPlayerControl__OnSeated            = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 48 81 EC 90 00 00 00 48 8B 05 ? ? ? ? 48 33 C4 48 89 84 24 80 00 00 00 80 79 10 00 48 8B D9 0F 85 ? ? ? ? C6 41 10 01")));
        gPatterns.C_CartMoveContext__Update                = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B C4 48 89 58 ? 48 89 70 ? 57 48 81 EC A0 00 00 00 0F 29 70 ? B2 03")));
        gPatterns.C_CartMoveContext__Stop                  = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 48 83 EC 20 80 79 ? ? 48 8B D9 75 ? 84 D2")));
        gPatterns.C_ActorStateExpansion__RequestCartAction = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 57 48 83 EC 20 48 8B 41 ? 48 8B DA 48 8B F9 48 8D 4C 24")));
        gPatterns.C_ActorActionCart__RequestDismount       = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 57 48 83 EC 20 48 8B 41 ? 8D 5A")));
        gPatterns.AdvanceCartPathCursor                    = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B C4 48 89 58 ? 48 89 70 ? 57 48 83 EC 40 0F 29 70 ? 48 8B F9 0F 29 78 ? E8")));
        gPatterns.ApplyCartReinsModifier                   = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 57 48 83 EC 30 8B FA 48 8B D9 E8 ? ? ? ? 84 C0")));
        gPatterns.DriveHorseToHarness                      = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("49 8B F0 44 0F 28 E9 4C 8B F9 E8"), -0x5F));
        gPatterns.RollCartWheel                            = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B C4 48 89 58 08 48 89 70 10 48 89 78 18 55 48 8D 68 C8 48 81 EC 30 01 00 00 0F 29 70 E8 48 8B F9 48 8B 09 33 DB")));

        gPatterns.C_ProceduralContextAnimateCameraControlMethod__Request = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B C4 48 89 58 10 57 48 83 EC 50 0F 29 70 E8 48 8B D9 0F 29 78 D8 0F 28 F2 44 0F 29 40 C8 0F 28 FB 44 0F 28 C1")));
        gPatterns.C_ProceduralContextAnimateCameraControlMethod__Release = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 30 0F 29 74 24 20 49 8B F1 0F 28 F2 8B FA 48 8B D9 E8 ? ? ? ? 48 8D 4B 18")));

        // Clears the answer, then picks the writable or the read-only parser
        // from its third flag before anything else.
        gPatterns.CXmlUtils__LoadXmlFromFile = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 74 24 ? 55 57 41 56 48 8B EC 48 83 EC 50 80 7D")));

        // Spills RDX first, then takes the entity from +0x38 and tests it
        // before asking for its script table.
        gPatterns.CAnimatedCharacter__InitializeMannequin = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 54 24 10 55 53 56 57 41 56 41 57 48 8B EC 48 83 EC 38 48 8B F1 48 8B 49 38 48 85 C9 0F 84")));

        // Tests the header's loaded flag (0x800) before a binary search over
        // the sorted controller ids; the flag test with the id moved into
        // R10D is the whole prologue.
        gPatterns.GlobalAnimationHeaderCAF__GetControllerByJointCRC32 = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("F7 41 ? ? ? ? ? 44 8B D2")));

        // The 0x480-byte frame holds the two 304-byte ping-pong job lists the
        // sync walks when multithreaded animation is on.
        gPatterns.CharacterManager__SyncAllAnimations = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 74 24 ? 48 89 7C 24 ? 55 41 54 41 55 41 56 41 57 48 8D AC 24 ? ? ? ? 48 81 EC 80 04 00 00")));
        gPatterns.CAnimationManager__CreateGAH_CAF = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B C4 48 89 58 08 48 89 68 10 48 89 70 18 48 89 78 20 41 54 41 56 41 57 48 83 EC 20 48 8B FA 48 8B F1 48 8B 12")));
        gPatterns.CAnimationSet__LoadFileCAF = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 10 48 89 6C 24 18 48 89 74 24 20 57 41 54 41 55 41 56 41 57 48 83 EC 30 4C 8B F2 4C 8B F9 49 8B D0 48 83 C1 28")));
        gPatterns.CTexture__ReloadFile = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 10 48 89 7C 24 18 55 48 8D AC 24 50 FB FF FF")));
        // The wh_db_ReloadObjectDatabase command's body: the manager accessor, then a tail jump into
        // ReloadDatabases with `&manager->databases` (+0x30), reload on, everything off.
        {
            auto *reloadCommand                                 = reinterpret_cast<char *>(hook::get_pattern(KCDC_HIDE("48 83 EC 28 E8 ? ? ? ? 45 33 C9 41 B0 01 48 8B C8 48 8D 50 30 48 83 C4 28 E9")));
            gPatterns.C_ObjectDatabaseManager__Instance        = hook::get_address<std::uint64_t>(reloadCommand + 0x04, 1, 5);
            gPatterns.C_ObjectDatabaseManager__ReloadDatabases = hook::get_address<std::uint64_t>(reloadCommand + 0x1A, 1, 5);
        }
        // The scheduler module's dynamic initializer builds the static
        // C_SchedulerDatabase in place: the vftable store between the zeroing
        // of its fields is unique in WHGame.dll.
        gPatterns.C_SchedulerDatabase__VTable = hook::get_address<std::uint64_t>(reinterpret_cast<char *>(hook::get_pattern(KCDC_HIDE("0F 57 C9 48 C7 05 ? ? ? ? 00 00 00 00 48 8D 05 ? ? ? ? 66 0F 7F 0D ? ? ? ? 0F 57 C0 48 89 05 ? ? ? ? B9 20 00 00 00 66 0F 7F 05 ? ? ? ?"))) + 0x0E, 3, 7);
        // S_ModInfo's constructor: its vftable, six empty strings, the cleared flag.
        gPatterns.S_ModInfo__Construct = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 48 83 EC 20 48 8D 05 ? ? ? ? 48 8B D9 48 89 01 E8 ? ? ? ? 48 83 C0 0C 48 89 43 08 E8 ? ? ? ? 48 83 C0 0C 48 89 43 10 E8 ? ? ? ? 48 83 C0 0C 48 89 43 18 E8 ? ? ? ? 48 83 C0 0C 48 89 43 20 E8 ? ? ? ? 48 83 C0 0C 48 89 43 28 E8 ? ? ? ? 48 83 C0 0C 48 89 43 30 48 8B C3 C6 43 38 00 48 83 C4 20 5B C3")));
        // The 0x2B40-byte frame (an 8 KB line buffer and a 1 KB key buffer) is unique in WHGame.dll.
        gPatterns.CLocalizedStringsManager__LoadExcelXmlSpreadsheet = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 20 55 56 57 41 54 41 55 41 56 41 57 48 8D AC 24 C0 D5 FF FF B8 40 2B 00 00 E8")));
        gPatterns.CRenderer__EF_ReloadFile = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 08 57 48 83 EC 20 48 8B CA 48 8B FA E8 ? ? ? ? 48 8D 15 ? ? ? ?")));
        gPatterns.CWh_FmodStudioWrapper__RegisterBank = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 18 48 89 6C 24 20 56 57 41 56 48 81 EC 80 00 00 00 48 8B FA")));
        gPatterns.FSW_BANK__LoadMetadataAsync = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 10 48 89 6C 24 18 48 89 74 24 20 57 48 83 EC 60 48 8B 05 ? ? ? ? 48 33 C4 48 89 44 24 50 8B 41 68")));
        gPatterns.FSW_BANK__RequestMetadataUnload = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B C4 48 89 58 08 48 89 68 10 48 89 70 18 48 89 78 20 41 54 41 56 41 57 48 83 EC 20 48 8B 69 40")));
        gPatterns.FSW_B_EVENT_INSTANCE__Start = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B C4 48 89 58 10 48 89 70 18 48 89 78 20 55 48 8B EC 48 83 EC 70 0F 29 70 E8 48 8B 05 ? ? ? ?")));
        gPatterns.FSW_B_EVENT_INSTANCE__Update3DAttributes = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 20 48 8B 41 30 48 8D B9 80 00 00 00 C7 47 0C 00 00 00 00")));
        gPatterns.SAudioManagerRequestData512__Ctor = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8D 05 ? ? ? ? C7 41 08 01 00 00 00 48 89 01 48 8B C1 C7 41 10 00 02 00 00")));

        // Loads the owning actor from +0x10 and resolves its equipment
        // listener host before handing the handler to the first re-sync.
        gPatterns.C_ActorEquipmentHandler__ReattachToBody = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 48 83 EC 20 48 8B D9 48 8B 49 ? E8 ? ? ? ? 48 85 C0 74 ? 48 8B D3")));

        // Saves R8 and RDX into the home area at once: the path and the
        // skinning info it reads every bone from. The body's bone loop is
        // shared with the .skin loader, the prologue is not.
        gPatterns.SkelLoader_Helpers__InitializeBones = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B C4 48 89 58 ? 4C 89 40 ? 48 89 50 ? 55 56 57 41 54 41 55 41 56 41 57 48 8D 6C 24")));

        // The 0x2F0-byte frame; ReadController905 has the same prologue
        // with a 0x6F0-byte one.
        gPatterns.CDefaultSkeleton__LoadNewSKEL = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B C4 48 89 58 ? 55 56 57 41 54 41 55 41 56 41 57 48 8D A8 ? ? ? ? 48 81 EC F0 02 00 00 0F 29 70 ? 48 8B 05")));
        gPatterns.CInternalDatabaseInfo__ReadController905 = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B C4 48 89 58 ? 55 56 57 41 54 41 55 41 56 41 57 48 8D A8 ? ? ? ? 48 81 EC F0 06 00 00 0F 29 70 ? 48 8B 05")));

        // Compares the controller count against 0x200 before resizing.
        gPatterns.GlobalAnimationHeaderCAF__InitControllerLookup = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 74 24 ? 57 48 83 EC 20 8B DA 48 8B F1 81 FA 00 02 00 00")));

        // Tests the storage handle at +0x40 before restoring both tables.
        gPatterns.CInternalDatabaseInfo__Destructor = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 48 83 EC 20 48 83 79 ? ? 48 8D 05 ? ? ? ? 48 89 01 48 8B D9 48 8D 05")));

        // CModelMesh upload: this, hidden smart-pointer result, CMesh, path.
        gPatterns.CModelMesh__CreateRenderMesh = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("4C 8B DC 49 89 5B ? 49 89 6B ? 49 89 73 ? 57 41 56 41 57 48 81 EC 80 00 00 00 48 8B F9")));

        // Native traversal entries shared by octree and render workers. Their distinct
        // stack frames identify the otherwise similar RenderObject overloads.
        gPatterns.CObjManager__RenderObject         = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B C4 48 89 58 ? 55 56 57 41 54 41 55 41 56 41 57 48 81 EC A0 09 00 00")));
        gPatterns.CObjManager__RenderObjectWithMask = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B C4 48 89 58 ? 55 56 57 41 54 41 55 41 56 41 57 48 81 EC D0 05 00 00")));
        gPatterns.CObjManager__RenderBrush          = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 85 D2 0F 84 ? ? ? ? 48 8B C4 48 89 48 ? 53")));
        gPatterns.CObjManager__RenderVegetation     = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B C4 48 89 58 ? 55 56 57 41 54 41 55 41 56 41 57 48 81 EC 30 05 00 00")));
        gPatterns.CObjManager__RenderObjectShadows  = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("40 53 57 41 55 48 81 EC 80 01 00 00")));
        gPatterns.CMergedMeshRenderNode__Render     = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 8B C4 48 89 58 ? 55 56 57 41 54 41 55 41 56 41 57 48 8D 68 ? 48 81 EC 00 01 00 00 0F 29 70 ? 41 B5 01")));

        gPatterns.CParticleEmitter__Render = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 48 89 54 24 ? 55 56 57 41 54 41 55 41 56 41 57 48 8D 6C 24 ? 48 81 EC 90 00 00 00 41 F6 40")));

        gPatterns.CDeformableNode__Render = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("45 85 C0 0F 8F ? ? ? ? 48 8B C4")));

        // Spills the length to the home area before the CityHash rounds.
        gPatterns.CityHash64Lowercase = reinterpret_cast<std::uint64_t>(hook::get_pattern(KCDC_HIDE("48 89 5C 24 ? 89 54 24 ? 55 56 57 41 54 41 55 41 56 41 57 48 83 EC 60")));
    }
} // namespace SDK
