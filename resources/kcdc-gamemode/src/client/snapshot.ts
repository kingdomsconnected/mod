/** The local body, read fresh each tick and shipped whole to the page. */

/** A value and its ceiling, which is how the page draws a bar. */
export type Gauge = readonly [value: number, maximum: number];

export interface BuffLine {
    readonly name: string;
    readonly kind: string;
    readonly source: string;

    /** Seconds left, roughly, or null for an effect with no end. */
    readonly left: number | null;
}

export interface SelfSnapshot {
    readonly nickname: string;
    readonly slot: number;
    readonly entity: number;
    readonly ready: boolean;
    readonly alive: boolean;
    readonly canAct: boolean;

    readonly position: { readonly x: number; readonly y: number; readonly z: number };
    readonly speed: number;

    readonly health: Gauge;
    readonly stamina: Gauge;
    readonly exhaust: Gauge;
    readonly hunger: Gauge;

    readonly bleeding: number;
    readonly consciousness: number;
    readonly drunkenness: number;
    readonly poisoning: number;

    readonly stats: Readonly<Record<string, number>>;
    readonly skills: Readonly<Record<string, number>>;

    readonly inAir: boolean;
    readonly mounted: boolean;
    readonly horse: number;

    /** Raw Mannequin tag ids; 255 means nothing is set. */
    readonly tags: { readonly pace: number; readonly direction: number; readonly stance: number; readonly physics: number };

    readonly hands: { readonly right: string; readonly left: string };
    readonly worn: number;
    readonly buffs: readonly BuffLine[];
}

export function readSelf(): SelfSnapshot | null {
    const self = LocalPlayer;
    if (!self) {
        return null;
    }

    const velocity = self.velocity;
    const skills = self.skills;

    return {
        nickname: self.nickname,
        slot: self.playerIndex,
        entity: self.entityId,
        ready: self.ready,
        alive: self.alive,
        canAct: self.canAct,

        position: { x: self.position.x, y: self.position.y, z: self.position.z },
        speed: Math.hypot(velocity.x, velocity.y, velocity.z),

        health: [self.health, self.maxHealth],
        stamina: [self.stamina, self.maxStamina],
        exhaust: [self.exhaust, self.maxExhaust],
        hunger: [self.hunger, self.maxHunger],

        bleeding: self.bleeding,
        consciousness: self.consciousness,
        drunkenness: self.drunkenness,
        poisoning: self.poisoning,

        stats: { strength: self.strength, agility: self.agility, vitality: self.vitality },
        skills: {
            fencing: skills.fencing,
            sword: skills.sword,
            dagger: skills.dagger,
            unarmed: skills.unarmed,
            "heavy weapons": skills.heavyWeapons,
            "large weapons": skills.largeWeapons,
            marksmanship: skills.marksmanship,
            defense: skills.defense,
            survival: skills.survival,
        },

        inAir: self.inAir,
        mounted: self.mounted,
        horse: self.horseId,

        tags: { pace: self.moveSpeedTag, direction: self.moveDirTag, stance: self.stanceTag, physics: self.physicsProfile },

        hands: { right: self.rightHandItem, left: self.leftHandItem },
        worn: self.equipment.length,
        buffs: self.buffs.map((buff) => ({
            name: buff.name,
            kind: buff.class,
            source: buff.source,
            left: buff.duration < 0 ? null : Math.max(0, buff.duration - buff.since),
        })),
    };
}
