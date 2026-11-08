/* Loaded only by start_custom_buff_qa.ps1, beside the default gamemode. */
module.exports = function install(commands, options = {}) {
    const fs = require('node:fs');
    const path = require('node:path');
    const reports = new Map();
    const running = new Set();
    const visual = 'potion_marigold_decoction';
    Progression.setNativeXp(false);
    const blessing = Buffs.register('qa:warriors_blessing', {
        name: "Warrior's Blessing [QA]", description: 'Custom name, icon, timer, +5 strength and +20 maximum stamina.',
        visual, duration: 300, tickInterval: 1, modifiers: { strength: 5, stamina: 20 }
    });
    const short = Buffs.register('qa:short', { name: 'Short Blessing [QA]', description: 'Expires on the native simulation clock after two seconds.', visual, duration: 2, tickInterval: 0.25 });
    const stack = Buffs.register('qa:stack', { name: 'Stacking Blessing [QA]', description: 'Three independent +1 charisma stacks; fourth application is ignored.', visual, duration: 300, stackable: true, maxStacks: 3, refresh: false, modifiers: { charisma: 1 } });
    const regeneration = Buffs.register('qa:regeneration', { name: 'Healing Blessing [QA]', description: 'Adds two health per simulation second.', visual, duration: 4, modifiers: { healthRegeneration: 2 } });
    const testBuffs = [blessing, short, stack, regeneration];
    const maximum = Buffs.register('qa:maximum', { name: 'Frail but Tireless [QA]', description: 'Maximum health 50; capacity +25; run speed multiplied by 1.1; armor +3; native hunger and energy rates halved.', visual, duration: -1, modifiers: {
        maxHealth: { operation: 'set', value: 50 }, carryCapacity: 25, runSpeed: { operation: 'multiply', value: 1.1 }, armorRating: 3,
        hungerDrain: { operation: 'multiply', value: 0.5 }, energyDrain: { operation: 'multiply', value: 0.5 }
    } });
    testBuffs.push(maximum);
    const sleep = ms => new Promise(resolve => setTimeout(resolve, ms));
    const row = player => player.progression?.tracks.strength;
    const totalStrengthXp = player => Progression.xpForLevel('strength', row(player).level) + row(player).xp;
    const count = (player, name) => player.buffs.filter(buff => buff.name === name).length;
    const copy = value => JSON.parse(JSON.stringify(value));
    const check = (value, message) => { if (!value) throw new Error(message); };
    const close = (left, right) => Math.abs(left - right) < 0.05;
    async function until(predicate, description, timeout = 20000) {
        const deadline = Date.now() + timeout;
        while (!predicate()) { if (Date.now() >= deadline) throw new Error('Timeout: ' + description); await sleep(100); }
    }
    function report(player) {
        if (!reports.has(player.nickname)) reports.set(player.nickname, { nickname: player.nickname, events: [], checks: [], startedAt: new Date().toISOString() });
        return reports.get(player.nickname);
    }
    function save(player) {
        fs.writeFileSync(path.join(process.cwd(), 'buff-qa-' + player.nickname.replace(/[^a-zA-Z0-9_-]/g, '_') + '.json'), JSON.stringify(report(player), null, 2));
    }
    function note(player, name, data) {
        report(player).checks.push({ name, data, at: new Date().toISOString() });
        console.log('[Buff QA] ' + player.nickname + ' PASS ' + name + ' ' + JSON.stringify(data));
        save(player);
    }
    for (const event of ['playerCustomBuffApply', 'playerCustomBuffTick', 'playerCustomBuffRemove']) {
        Events.on(event, (player, buff, reason) => {
            if (!buff.name.startsWith('qa:')) return;
            report(player).events.push({ event, name: buff.name, reason, at: Date.now() });
            save(player);
        });
    }
    for (const event of ['playerBuffAdded', 'playerBuffRemoved']) Events.on(event, (player, name, reason) => {
        if (!name.startsWith('qa:')) return;
        report(player).events.push({ event, name, reason, at: Date.now() });
        save(player);
    });
    Events.on('playerProgressionRejected', (player, reason) => { report(player).rejected = reason; save(player); });
    async function auto(player) {
        if (running.has(player.nickname)) return;
        running.add(player.nickname);
        const result = report(player);
        delete result.error;
        delete result.rejected;
        delete result.cleanupError;
        result.ok = false;
        let baseline;
        let health;
        let maximumHealth;
        try {
            await until(() => player.progression !== null && player.maxHealth > 0, 'first progression and soul reports', 90000);
            baseline = copy(player.progression);
            health = player.health;
            maximumHealth = player.maxHealth;
            for (const buff of testBuffs) player.removeBuff(buff);
            await until(() => testBuffs.every(buff => !player.hasBuff(buff)), 'clean custom effects');
            check(player.setLevel('strength', 8), 'setLevel request');
            await until(() => row(player).level === 8 && row(player).xp === 0, 'level 8');
            const strengthFloor = Progression.xpForLevel('strength', 8);
            check(player.setXp('strength', strengthFloor + 12.5), 'setXp request');
            await until(() => row(player).level === 8 && row(player).xp === 12.5, 'XP inside level 8');
            const nativeBefore = totalStrengthXp(player);
            check(player.addXp('strength', 40), 'native XP baseline request');
            await until(() => totalStrengthXp(player) > nativeBefore, 'native XP baseline');
            const nativeGain = totalStrengthXp(player) - nativeBefore;
            player.setXp('strength', strengthFloor + 12.5);
            await until(() => row(player).level === 8 && row(player).xp === 12.5, 'reset after native XP baseline');
            player.addBuff('meat_str_exp_test');
            await until(() => player.hasBuff('meat_str_exp_test'), 'native per-stat XP multiplier');
            const boostedBefore = totalStrengthXp(player);
            check(player.addXp('strength', 40), 'native XP multiplier probe');
            await until(() => totalStrengthXp(player) > boostedBefore, 'native XP with multiplier');
            const boostedGain = totalStrengthXp(player) - boostedBefore;
            check(boostedGain > nativeGain, 'native per-stat XP multiplier must actually increase earned XP');
            player.setXp('strength', strengthFloor + 12.5);
            await until(() => row(player).level === 8 && row(player).xp === 12.5, 'reset before exact positive XP assignment');
            check(player.setXp('strength', strengthFloor + 20.5), 'exact XP under a native multiplier');
            await until(() => row(player).level === 8 && row(player).xp === 20.5, 'absolute XP bypasses native per-stat multiplier');
            player.removeBuff('meat_str_exp_test');
            await until(() => !player.hasBuff('meat_str_exp_test'), 'remove native XP modifier');
            check(player.setXp('strength', strengthFloor + 12.5), 'reset XP remainder');
            await until(() => row(player).xp === 12.5, 'reset remainder after multiplier check');
            note(player, 'absolute XP ignores verified native per-stat multiplier', { nativeGain, boostedGain });
            check(player.removeXp('strength', 5), 'removeXp request');
            await until(() => row(player).xp === 7.5, 'lower XP inside the same level');
            note(player, 'lower XP remainder', copy(row(player)));
            const lowered = copy(player.progression);
            check(player.removeXp('strength', 100000), 'clamped removeXp request');
            await until(() => row(player).level === 0 && row(player).xp === 0, 'clamp total XP to zero');
            check(player.restoreProgression(lowered), 'restore request');
            await until(() => row(player).level === 8 && row(player).xp === 7.5, 'exact restore upward');
            check(player.setLevel('strength', 12), 'higher level request');
            await until(() => row(player).level === 12, 'raise level');
            check(player.restoreProgression(lowered), 'live lowered restore request');
            await until(() => row(player).level === 8 && row(player).xp === 7.5, 'exact restore downward');
            note(player, 'live lower level and exact restore', copy(row(player)));
            check(player.setLevel('weapon_sword', 12), 'weapon skill higher level request');
            await until(() => player.progression.tracks.weapon_sword.level === 12, 'weapon skill level 12');
            check(player.setXp('weapon_sword', Progression.xpForLevel('weapon_sword', 3) + 2.5), 'weapon skill lower exact XP request');
            await until(() => player.progression.tracks.weapon_sword.level === 3 && player.progression.tracks.weapon_sword.xp === 2.5, 'weapon skill reduction and induced fencing recomputation');
            note(player, 'lower weapon skill and recompute fencing', { sword: copy(player.progression.tracks.weapon_sword), fencing: copy(player.progression.tracks.fencing) });
            check(player.setLevel('strength', 6), 'modifier baseline request');
            await until(() => row(player).level === 6 && close(player.strength, 6), 'effective strength baseline');
            const stamina = player.maxStamina;
            const eventIndex = result.events.length;
            check(player.addBuff(blessing), 'blessing request');
            await until(() => player.hasBuff(blessing) && close(player.strength, 11) && close(player.maxStamina, stamina + 20), 'native modifiers and readback');
            check(player.buffs.some(buff => buff.name === 'qa:warriors_blessing' && buff.custom && buff.displayName === "Warrior's Blessing [QA]"), 'custom metadata');
            await until(() => result.events.slice(eventIndex).some(event => event.event === 'playerCustomBuffTick' && event.name === 'qa:warriors_blessing'), 'custom tick event');
            player.addBuff(blessing);
            await until(() => result.events.slice(eventIndex).filter(event => event.event === 'playerCustomBuffApply' && event.name === 'qa:warriors_blessing').length >= 2, 'refresh apply event');
            check(count(player, 'qa:warriors_blessing') === 1, 'nonstackable refresh count');
            note(player, 'native modifiers, metadata, tick and refresh', { strength: player.strength, maxStamina: player.maxStamina });
            player.removeBuff(blessing);
            await until(() => !player.hasBuff(blessing) && close(player.strength, 6), 'native modifier removal');
            player.addBuff(short);
            await until(() => player.hasBuff(short), 'short custom effect attached');
            await until(() => !player.hasBuff(short), 'native expiry');
            check(result.events.some(event => event.name === 'qa:short' && event.reason === 'expired'), 'expiry lifecycle event');
            note(player, 'native expiry', {});
            const charisma = player.getDerivedStat('charisma');
            const stackEventIndex = result.events.length;
            for (let index = 0; index < 4; ++index) player.addBuff(stack);
            await until(() => count(player, 'qa:stack') === 3 && close(player.getDerivedStat('charisma'), charisma + 3), 'three stacks and native summed modifiers');
            await sleep(400);
            check(count(player, 'qa:stack') === 3, 'maxStacks ignored fourth');
            player.removeBuff(stack);
            await until(() => count(player, 'qa:stack') === 0 && close(player.getDerivedStat('charisma'), charisma), 'remove every stack');
            check(result.events.slice(stackEventIndex).filter(event => event.name === 'qa:stack' && event.event === 'playerBuffAdded').length === 1, 'legacy added event describes presence once');
            check(result.events.slice(stackEventIndex).filter(event => event.name === 'qa:stack' && event.event === 'playerBuffRemoved').length === 1, 'legacy removed event describes absence once');
            note(player, 'stack limit and complete removal', {});
            player.setHealth(10);
            await until(() => close(player.health, 10), 'health regeneration baseline');
            player.addBuff(regeneration);
            await until(() => player.health > 14, 'native health regeneration');
            note(player, 'native health regeneration', { health: player.health });
            player.removeBuff(regeneration);
            await until(() => !player.hasBuff(regeneration), 'remove health regeneration');
            player.setHealth(maximumHealth);
            await until(() => close(player.health, maximumHealth), 'full health before maximum clamp');
            const capacity = player.getDerivedStat('inventoryCapacity');
            player.addBuff(maximum);
            await until(() => close(player.maxHealth, 50) && player.health <= 50 && close(player.getDerivedStat('inventoryCapacity'), capacity + 25), 'maximum health and native derived capacity');
            note(player, 'maxHealth, health clamp and capacity', { health: player.health, maxHealth: player.maxHealth, capacity: player.getDerivedStat('inventoryCapacity'), runSpeed: player.getDerivedStat('runSpeedBase'), armor: player.getDerivedStat('armorRating') });
            player.removeBuff(maximum);
            await until(() => !player.hasBuff(maximum) && close(player.maxHealth, maximumHealth), 'permanent custom effect removed');
            check(!result.rejected, 'progression authority rejected: ' + result.rejected);
            result.ok = true;
        } catch (error) {
            result.ok = false;
            result.error = String(error.stack || error);
            console.log('[Buff QA] ' + player.nickname + ' FAILED ' + result.error);
            Chat.sendToPlayer(player, 'Buff QA failed: ' + error.message);
        } finally {
            player.removeBuff('meat_str_exp_test');
            for (const buff of testBuffs) player.removeBuff(buff);
            if (baseline) player.restoreProgression(baseline);
            if (health !== undefined) {
                try {
                    await until(() => testBuffs.every(buff => !player.hasBuff(buff)), 'cleanup custom effects', 5000);
                    if (baseline) await until(() => Object.entries(baseline.tracks).every(([name, state]) => name === 'fencing' || (player.progression.tracks[name].level === state.level && player.progression.tracks[name].xp === state.xp)) && JSON.stringify(player.progression.perkPoints) === JSON.stringify(baseline.perkPoints) && JSON.stringify([...player.progression.perks].sort()) === JSON.stringify([...baseline.perks].sort()), 'cleanup progression, perks and point banks');
                    player.setHealth(health);
                } catch (error) { result.cleanupError = String(error); result.ok = false; }
            }
            running.delete(player.nickname);
            result.finishedAt = new Date().toISOString();
            if (result.ok) {
                console.log('[Buff QA] ' + player.nickname + ' ALL AUTOMATED CHECKS PASSED');
                Chat.sendToPlayer(player, 'Buff QA passed. /buffqa ui shows custom labels and timers; /buffqa clear removes them.');
            }
            save(player);
        }
    }
    commands.add({ name: 'buffqa', summary: 'Custom buff and live XP QA.', usage: ['/buffqa auto', '/buffqa ui', '/buffqa clear', '/buffqa status', '/buffqa hunger'], run({ player, args, reply }) {
        if (args[0] === 'auto') return auto(player);
        if (args[0] === 'ui') { player.addBuff(blessing); player.addBuff(maximum); reply.line('Open the character/status effects panel: custom titles, descriptions, borrowed icon, five-minute countdown and a permanent effect.'); }
        else if (args[0] === 'clear') { for (const buff of testBuffs) player.removeBuff(buff); }
        else if (args[0] === 'hunger') { player.addBuff(maximum); reply.line('Native hunger/energy modifiers now halve drain. Compare nourishment/energy over equal unpaused periods, then /buffqa clear.'); }
        else reply.line(JSON.stringify({ result: report(player).ok, error: report(player).error, strength: player.strength, strengthProgression: row(player), buffs: player.buffs, maxHealth: player.maxHealth, maxStamina: player.maxStamina, charisma: player.getDerivedStat('charisma'), carryCapacity: player.getDerivedStat('inventoryCapacity'), runSpeed: player.getDerivedStat('runSpeedBase'), armorRating: player.getDerivedStat('armorRating'), hunger: player.getStat('hunger'), energy: player.getStat('exhaust') }));
    } });
    if (options.auto) Events.on('playerReady', player => { if (player.nickname.startsWith('BuffQA_')) auto(player); });
    console.log('[Buff QA] Definitions registered; automatic mode ' + !!options.auto);
};
