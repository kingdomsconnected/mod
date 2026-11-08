#pragma once

namespace KCDC::Scripting {
    /**
     * The TypeScript shapes the `Npc` verbs take their option bags in.
     *
     * Here rather than beside the bindings for the same reason the other
     * features keep theirs here: the contract generator reads these strings
     * verbatim into `server-api.d.ts`, so the one place a shape is written down
     * has to be the one place it is maintained. A resource author reads the
     * result; nobody reads two copies that agree.
     */
    inline constexpr const char *kNpcCreateOptionsType =
        "{ soul?: string; class?: string; name?: string; outfit?: string; wearing?: string[]; appearance?: Partial<Appearance>; position?: Vector3 | Partial<Vector3>; rotation?: Quaternion | Vector3 | Partial<Vector3>; faction?: number; health?: number; maxHealth?: number; "
        "locomotion?: 'kinematic' | 'native'; invulnerable?: boolean; frozen?: boolean; interactable?: boolean; nametag?: boolean; lootable?: boolean; virtualWorld?: number }";

    inline constexpr const char *kNpcMoveOptionsType = "{ speed?: 'walk' | 'jog' | 'run'; radius?: number }";

    inline constexpr const char *kNpcMoveToOptionsType = "{ speed?: 'walk' | 'jog' | 'run'; radius?: number; pathfinding?: 'auto' | 'game' | 'server' }";

    inline constexpr const char *kNpcPatrolOptionsType = "{ speed?: 'walk' | 'jog' | 'run'; loop?: boolean; mode?: 'once' | 'loop' | 'pingPong'; radius?: number; waitSeconds?: number; pathfinding?: 'auto' | 'game' | 'server' }";
} // namespace KCDC::Scripting
