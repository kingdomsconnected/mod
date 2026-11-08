const STATE_EVENT = "kcdc-gamemode:dice.state";
const INTERACT_EVENT = "kcdc-gamemode:dice.interact";
const SETUP_TIMEOUT = 60_000;
const DEFAULT_SCORE = 2000;
const MIN_SCORE = 100;
const MAX_SCORE = 100_000;
const SCORE_STEP = 500;
/** How often the lobby looks again for tables a script or a world resource placed, or a level edit took out. */
const TABLE_SYNC_INTERVAL = 2_000;

interface Table {
    readonly info: DiceTableInfo;
    readonly area: Area;
    /** Shared by catalog rows that are one physical table on different quest layers. */
    readonly station: string;
}

interface Lobby {
    readonly key: string;
    readonly table: Table;
    readonly world: number;
    readonly members: Set<Member>;
    negotiation?: Negotiation;
    match: number;
}

interface Member {
    readonly player: Player;
    readonly lobby: Lobby;
    readonly seat: string;
}

interface Invitation {
    readonly first: Player;
    readonly second: Player;
    readonly table: Table;
    readonly score: number;
    readonly deadline: number;
}

interface Negotiation {
    readonly players: [Member, Member];
    readonly sessions: Map<number, number>;
    readonly accepted: Set<number>;
    turn: 0 | 1;
    score: number;
    deadline: number;
}

/** Readiness and consent belong to this resource; Dice owns the actual match. */
class DiceLobby {
    private readonly offers = new Map<number, { token: number; tables: Table[]; state: string }>();
    private readonly tables: Table[] = [];
    private readonly lobbies = new Map<string, Lobby>();
    private readonly members = new Map<number, Member>();
    private readonly invitations = new Map<number, Invitation>();
    /** The tables whose area each player stands in, from areaEnter/areaExit. */
    private readonly inside = new Map<number, Set<Table>>();
    private nextToken = 1;
    private nextTableSync = 0;
    /** Which tables each virtual world has, from Dice.tables(world): a table can be placed in, or removed from, one world only. */
    private readonly available = new Map<number, Set<string>>();

    install(): void {
        Messages.handle("playground:freeDice", (payload, reply) => {
            const id = typeof payload === "object" && payload !== null ? (payload as { player?: unknown }).player : undefined;
            const player = typeof id === "number" ? Player.getById(id) : null;
            reply(player ? this.teleportToFreeTable(player) : { error: "No such player." });
        });
        this.syncTables();
        const timer = setInterval(() => this.tick(), 250);
        Events.onClient("kcdc-gamemode:dice.sync", (sender) => this.offer(sender as Player, true));
        Events.onClient(INTERACT_EVENT, (sender, payload) => {
            let state: unknown;
            try {
                state = typeof payload === "string" ? payload.length <= 128 ? JSON.parse(payload) : null : payload;
            } catch { return; }
            if (!state || typeof state !== "object") return;
            const player = sender as Player;
            const previous = this.offers.get(player.id);
            this.refresh(player);
            const offer = this.offers.get(player.id);
            if (offer && offer === previous && offer.token === (state as Record<string, unknown>).token) {
                const table = offer.tables.find((table) => table.info.id === (state as Record<string, unknown>).table);
                if (!table) return;
                const member = this.members.get(player.id);
                if (member) this.cancelFor(player);
                else {
                    try { this.join(player, table); }
                    catch (error) { Chat.sendToPlayer(player, error instanceof Error ? error.message : String(error)); }
                }
            }
            this.offer(player, true);
        });
        Events.on("dialogueChoice", (session, player, option) => this.choose(session, player, option));
        Events.on("dialogueClosed", (session, player) => {
            const member = this.members.get(player.id);
            if (member?.lobby.negotiation?.sessions.get(player.id) === session) this.cancel(member.lobby, `${player.nickname} left the dice setup.`);
        });
        Events.on("playerDisconnect", (player) => { this.leave(player); this.offers.delete(player.id); this.inside.delete(player.id); });
        Events.on("playerDied", (player) => this.leave(player));
        Events.on("playerSpawning", (player) => this.leave(player));
        Events.on("areaEnter", (area, entity) => {
            const table = this.tables.find((table) => table.area.id === area.id);
            if (!table || !(entity instanceof Player)) return;
            const tables = this.inside.get(entity.id) ?? new Set<Table>();
            tables.add(table);
            this.inside.set(entity.id, tables);
            this.refresh(entity);
        });
        Events.on("areaExit", (area, entity) => {
            const table = this.tables.find((table) => table.area.id === area.id);
            if (!table || !(entity instanceof Player)) return;
            const tables = this.inside.get(entity.id);
            tables?.delete(table);
            if (!tables?.size) this.inside.delete(entity.id);
            if (this.members.get(entity.id)?.lobby.table === table || this.invitations.get(entity.id)?.table === table) this.leave(entity);
            this.refresh(entity);
        });
        Events.on("diceMatchStart", (_match, first, second) => {
            this.leave(first);
            this.leave(second);
        });
        Events.on("diceMatchEnd", (match) => {
            for (const lobby of this.lobbies.values()) {
                if (lobby.match === match) lobby.match = 0;
            }
        });
        Events.on("resourceStop", (name) => {
            if (name !== "kcdc-gamemode") return;
            clearInterval(timer);
            for (const lobby of this.lobbies.values()) {
                this.cancel(lobby);
                if (lobby.match) Dice.stop(lobby.match);
            }
            for (const member of [...this.members.values()]) this.leave(member.player);
            for (const table of this.tables) table.area.destroy();
        });
    }

    private eligible(player: Player, table: Table, world: number): boolean {
        return player.ready && player.health > 0 && player.virtualWorld === world && this.availableIn(world, table) && table.area.exists && table.area.enabled && table.area.containsPlayer(player) && Dice.matchOf(player.id) === 0;
    }

    /** Whether the world has the table: the lobby's areas cover every world's tables at once. */
    private availableIn(world: number, table: Table): boolean {
        let ids = this.available.get(world);
        if (!ids) {
            ids = new Set(Dice.tables(world).map((info) => info.id));
            this.available.set(world, ids);
        }
        return ids.has(table.info.id);
    }

    /** Server-resource request only. Waiting lobbies and invitations occupy a table too. */
    private teleportToFreeTable(player: Player): { message?: string; error?: string } {
        if (!player.ready || !player.alive || player.mounted || Dice.matchOf(player.id) || Crafting.session(player)) return { error: "Leave the current activity before teleporting." };
        const tables = this.tables.filter((table) => {
            if (table.info.id !== table.station || !this.availableIn(player.virtualWorld, table)) return false;
            const lobby = this.lobbies.get(`${player.virtualWorld}:${table.station}`);
            if (lobby && (lobby.match || lobby.negotiation || lobby.members.size)) return false;
            if ([...this.invitations.values()].some((invitation) => invitation.first.virtualWorld === player.virtualWorld && invitation.table.station === table.station)) return false;
            // Also cover direct Dice.start callers and people using the chairs.
            return !Player.all().some((other) => other.id !== player.id && other.ready && (other.virtualWorld === player.virtualWorld || other.virtualWorld === -1) && this.distance(other, table) < 3);
        }).sort((a, b) => this.distance(player, a) - this.distance(player, b));
        const table = tables[0];
        if (!table) return { error: "No unoccupied dice table is available in this level." };
        const p = table.info.position;
        // Use the server navmesh when supplied, otherwise the teleport's native
        // grounding handles the catalog height as it does for /tp.
        const approach = new Vector3(p.x + 2, p.y, p.z);
        const floor = Navigation.ready ? Navigation.closestPoint(approach, { searchRadius: 2, searchHeight: 2 }) : approach;
        if (!floor || !player.teleport(floor, "Nearest free dice table")) return { error: "Could not move to the nearest free dice table." };
        return { message: "Teleport requested to the nearest unoccupied dice table. Quest-layer tables may need their area to load." };
    }

    private distance(player: Player, table: Table): number {
        const p = player.position, q = table.info.position;
        return Math.hypot(p.x - q.x, p.y - q.y, p.z - q.z);
    }

    private refresh(player: Player): void {
        const member = this.members.get(player.id);
        if (member && !this.eligible(player, member.lobby.table, member.lobby.world)) this.leave(player);
        this.offer(player);
    }

    private offer(player: Player, force = false): void {
        const member = this.members.get(player.id);
        const candidates = member ? [member.lobby.table] : [...(this.inside.get(player.id) ?? [])].filter((table) => this.eligible(player, table, player.virtualWorld));
        const tables = candidates.filter((table) => {
            const lobby = this.lobbies.get(`${player.virtualWorld}:${table.station}`);
            return !lobby?.match && !lobby?.negotiation && !Dialogue.sessionOf(player.id) && !this.invitations.has(player.id) && (!!member || (lobby?.members.size ?? 0) < 2);
        });
        const payload = { tables: tables.map((table) => table.info), joined: !!member, world: player.virtualWorld };
        const state = JSON.stringify(payload);
        if (!force && this.offers.get(player.id)?.state === state) return;
        const token = this.nextToken++;
        this.offers.set(player.id, { token, tables, state });
        player.emit(STATE_EVENT, JSON.stringify({ token, ...payload }));
    }

    private enroll(player: Player, table: Table): Member {
        if (!this.eligible(player, table, player.virtualWorld) || Dialogue.sessionOf(player.id) || this.members.has(player.id)) throw new Error("Move close to the dice table and finish any other conversation first.");
        const key = `${player.virtualWorld}:${table.station}`;
        let lobby = this.lobbies.get(key);
        if (!lobby) {
            lobby = { key, table, world: player.virtualWorld, members: new Set(), match: 0 };
            this.lobbies.set(key, lobby);
        }
        if (lobby.match || lobby.negotiation || lobby.members.size >= 2) throw new Error("This dice table is occupied.");
        const seat = lobby.table.info.seats.find((seat) => ![...lobby!.members].some((member) => member.seat === seat))!;
        const member = { player, lobby, seat };
        this.members.set(player.id, member);
        lobby.members.add(member);
        return member;
    }

    private join(player: Player, table: Table): void {
        const member = this.enroll(player, table);
        const opponent = [...member.lobby.members].find((other) => other !== member);
        if (opponent) this.begin(opponent, member, DEFAULT_SCORE);
        else {
            Chat.sendToPlayer(player, "Ready for dice. Waiting for another player. Interact again or walk away to leave.");
            this.offer(player, true);
        }
    }

    private updateHints(lobby: Lobby): void {
        for (const member of lobby.members) this.offer(member.player, true);
    }

    private leave(player: Player): void {
        this.cancelInvitation(player.id, `${player.nickname} left the dice setup.`);
        const member = this.members.get(player.id);
        if (!member) return;
        if (member.lobby.negotiation?.players.includes(member)) this.cancel(member.lobby, `${player.nickname} left the dice setup.`);
        member.lobby.members.delete(member);
        this.members.delete(player.id);
        this.offer(player, true);
    }

    /**
     * Follows Dice.tables(): the level's own are there from the start, but
     * SmartObject.spawn and world resources place more later, and
     * SmartObject.remove or destroying a placed one takes them away again.
     */
    private syncTables(): void {
        this.nextTableSync = Date.now() + TABLE_SYNC_INTERVAL;
        // The global world sees every world's tables, so its list is the union the areas cover.
        const listed = Dice.tables();
        const ids = new Set(listed.map((info) => info.id));
        for (const table of this.tables.filter((table) => !ids.has(table.info.id))) {
            this.dropTable(table);
        }
        // Asked again on use, for the worlds players are in now.
        this.available.clear();
        for (const info of listed) {
            if (this.tables.some((table) => table.info.id === info.id)) {
                continue;
            }
            const area = Area.create({ id: `kcdc-gamemode:dice:${info.id}`, name: "Dice table", type: "sphere", position: new Vector3(info.position.x, info.position.y, info.position.z), radius: 3, labels: ["dice"] });
            // Quest layers can place the same table twice; both rows seat the same chairs.
            const twin = this.tables.find((other) => Math.hypot(other.info.position.x - info.position.x, other.info.position.y - info.position.y, other.info.position.z - info.position.z) < 1);
            this.tables.push({ info, area, station: twin?.station ?? info.id });
        }
    }

    /** A table that is gone ends the setups at it; a match already running is Dice's to finish. */
    private dropTable(table: Table): void {
        for (const lobby of [...this.lobbies.values()]) {
            if (lobby.table !== table) {
                continue;
            }
            // cancel() tells only a negotiating pair; someone still waiting is told here.
            for (const member of lobby.members) {
                if (!lobby.negotiation?.players.includes(member)) {
                    Chat.sendToPlayer(member.player, "The dice table is gone.");
                }
            }
            this.cancel(lobby, "The dice table is gone.");
            if (!lobby.match) {
                this.lobbies.delete(lobby.key);
            }
        }
        for (const invitation of new Set(this.invitations.values())) {
            if (invitation.table === table) {
                this.cancelInvitation(invitation.first.id, "The dice table is gone.");
            }
        }
        this.tables.splice(this.tables.indexOf(table), 1);
        table.area.destroy();
        for (const [id, tables] of this.inside) {
            if (!tables.delete(table)) {
                continue;
            }
            if (!tables.size) {
                this.inside.delete(id);
            }
            const player = Player.getById(id);
            if (player) {
                this.refresh(player);
            }
        }
    }

    private tick(): void {
        if (Date.now() >= this.nextTableSync) {
            this.syncTables();
        }
        // Only players standing at a table can change what they are offered.
        for (const player of Player.all()) {
            if (this.inside.has(player.id) || this.members.has(player.id)) this.refresh(player);
        }
        for (const invitation of new Set(this.invitations.values())) {
            if (Date.now() >= invitation.deadline) this.cancelInvitation(invitation.first.id, "The dice invitation expired.");
        }
        for (const lobby of this.lobbies.values()) {
            if (lobby.negotiation && Date.now() >= lobby.negotiation.deadline) this.cancel(lobby, "The dice setup timed out. Interact with the table again to retry.");
            if (!lobby.members.size && !lobby.match) this.lobbies.delete(lobby.key);
        }
    }

    private pairValid(first: Member, second: Member): boolean {
        return first !== second && first.lobby === second.lobby && first.seat !== second.seat && !first.lobby.match && [first, second].every((member) => this.members.get(member.player.id) === member && this.eligible(member.player, member.lobby.table, member.lobby.world));
    }

    private begin(first: Member, second: Member, score: number): void {
        if (!this.pairValid(first, second) || [first, second].some((member) => Dialogue.sessionOf(member.player.id) !== 0)) {
            this.cancel(first.lobby);
            for (const member of [first, second]) Chat.sendToPlayer(member.player, "Both players must be free and close to the same dice table. Interact again when you are.");
            return;
        }
        first.lobby.negotiation = { players: [first, second], sessions: new Map(), accepted: new Set(), turn: 0, score, deadline: Date.now() + SETUP_TIMEOUT };
        this.updateHints(first.lobby);
        this.show(first.lobby);
    }

    private show(lobby: Lobby): void {
        const negotiation = lobby.negotiation;
        if (!negotiation) return;
        const active = negotiation.players[negotiation.turn];
        for (const member of negotiation.players) {
            const page: DialoguePage = {
                line: member === active ? `First to ${negotiation.score}. Agree to this score or propose another.` : `${active.player.nickname} is choosing the score. First to ${negotiation.score}. You can still quit.`,
                options: member === active ? [
                    { id: "agree", text: negotiation.accepted.size ? `Agree and play to ${negotiation.score}.` : `Propose ${negotiation.score}.` },
                    { id: "lower", text: `Lower to ${Math.max(MIN_SCORE, negotiation.score - SCORE_STEP)}.`, enabled: negotiation.score > MIN_SCORE },
                    { id: "raise", text: `Raise to ${Math.min(MAX_SCORE, negotiation.score + SCORE_STEP)}.`, enabled: negotiation.score < MAX_SCORE },
                    { id: "quit", text: "I do not want to play." },
                ] : [
                    { id: "quit", text: "I do not want to play." },
                    { id: "waiting", text: `Waiting for ${active.player.nickname}: first to ${negotiation.score}.`, enabled: false },
                ],
            };
            const existing = negotiation.sessions.get(member.player.id);
            const session = existing === undefined ? Dialogue.open(member.player.id, page) : Dialogue.update(existing, page) ? existing : 0;
            if (!session) {
                this.cancel(lobby, "The dice conversation could not open. Interact with the table again to retry.");
                return;
            }
            negotiation.sessions.set(member.player.id, session);
        }
    }

    private choose(session: number, player: Player, option: string): void {
        const member = this.members.get(player.id);
        const negotiation = member?.lobby.negotiation;
        if (!member || !negotiation || negotiation.sessions.get(player.id) !== session) return;
        if (Date.now() >= negotiation.deadline || !this.pairValid(...negotiation.players)) {
            this.cancel(member.lobby, "The dice setup is no longer available.");
            return;
        }
        if (option === "quit") {
            this.cancel(member.lobby, `${player.nickname} declined the game of dice.`);
            return;
        }
        if (negotiation.players[negotiation.turn] !== member) return;
        if (option === "lower" || option === "raise") {
            const score = Math.max(MIN_SCORE, Math.min(MAX_SCORE, negotiation.score + (option === "lower" ? -SCORE_STEP : SCORE_STEP)));
            if (score === negotiation.score) return;
            negotiation.score = score;
            negotiation.accepted.clear();
        } else if (option === "agree") {
            negotiation.accepted.add(player.id);
            if (negotiation.accepted.size === 2) {
                this.start(member.lobby, negotiation);
                return;
            }
            negotiation.turn = negotiation.turn === 0 ? 1 : 0;
            negotiation.deadline = Date.now() + SETUP_TIMEOUT;
        } else {
            return;
        }
        this.show(member.lobby);
    }

    private start(lobby: Lobby, negotiation: Negotiation): void {
        // Detach before close: dialogueClosed is synchronous and means cancel
        // only while these sessions still belong to a pending negotiation.
        this.cancel(lobby);
        const first = negotiation.players.find((member) => member.seat === lobby.table.info.seats[0])!;
        const second = negotiation.players.find((member) => member !== first)!;
        try {
            lobby.match = Dice.start(first.player.id, second.player.id, { targetScore: negotiation.score, firstName: first.player.nickname, secondName: second.player.nickname });
            this.updateHints(lobby);
        } catch (error) {
            for (const member of negotiation.players) Chat.sendToPlayer(member.player, `Could not start dice: ${error instanceof Error ? error.message : String(error)}`);
        }
    }

    private cancel(lobby: Lobby, message?: string): void {
        const negotiation = lobby.negotiation;
        lobby.negotiation = undefined;
        if (negotiation) {
            for (const session of negotiation.sessions.values()) Dialogue.close(session);
            if (message) for (const member of negotiation.players) Chat.sendToPlayer(member.player, message);
        }
        for (const member of [...lobby.members]) {
            lobby.members.delete(member);
            this.members.delete(member.player.id);
            this.offer(member.player, true);
        }
    }

    private cancelInvitation(playerId: number, message?: string): void {
        const invitation = this.invitations.get(playerId);
        if (!invitation) return;
        for (const player of [invitation.first, invitation.second]) this.invitations.delete(player.id);
        for (const player of [invitation.first, invitation.second]) {
            if (message) Chat.sendToPlayer(player, message);
            this.offer(player, true);
        }
    }

    cancelFor(player: Player): void {
        this.cancelInvitation(player.id, `${player.nickname} declined the dice invitation.`);
        const member = this.members.get(player.id);
        if (!member) return;
        if (member.lobby.negotiation?.players.includes(member)) this.cancel(member.lobby, `${player.nickname} declined the game of dice.`);
        else this.leave(player);
    }

    async invite(player: Player, opponent: Player, score: number): Promise<void> {
        if (!Number.isInteger(score) || score < MIN_SCORE || score > MAX_SCORE) throw new Error("The target score is a whole number from 100 to 100000.");
        if (player === opponent || player.virtualWorld !== opponent.virtualWorld) throw new Error("Choose another player in your world.");
        const world = player.virtualWorld;
        const nearby = await World.entitiesInRadius(player, player.position, 6, { class: "SmartObjectHolder", max: 64 });
        if (!nearby.answered) throw new Error("Could not check the nearby dice tables. Try again.");
        const loaded = new Set(nearby.entities.map((entity) => entity.guid));
        const table = this.tables.filter((table) => loaded.has(table.info.id) && this.eligible(player, table, world) && this.eligible(opponent, table, world)).sort((a, b) => this.distance(player, a) + this.distance(opponent, a) - this.distance(player, b) - this.distance(opponent, b))[0];
        if (!table) throw new Error("Both players must be close to the same loaded dice table.");
        if ([player, opponent].some((p) => this.members.has(p.id) || this.invitations.has(p.id) || Dialogue.sessionOf(p.id))) throw new Error("One of you is already waiting for a game or in a conversation.");
        const lobby = this.lobbies.get(`${world}:${table.station}`);
        if (lobby && (lobby.match || lobby.negotiation || lobby.members.size)) throw new Error("This dice table is occupied.");
        // Seats are taken on acceptance, so an unanswered invitation never blocks the table.
        const invitation = { first: player, second: opponent, table, score, deadline: Date.now() + SETUP_TIMEOUT };
        for (const invited of [player, opponent]) {
            this.invitations.set(invited.id, invitation);
            this.offer(invited, true);
        }
        Chat.sendToPlayer(opponent, `${player.nickname} invites you to dice, first to ${score}. /dice accept opens score negotiation; /dice decline backs out.`);
        Chat.sendToPlayer(player, `Invited ${opponent.nickname}. /dice cancel withdraws the invitation.`);
    }

    accept(player: Player): void {
        const invitation = this.invitations.get(player.id);
        if (!invitation || invitation.second.id !== player.id) throw new Error("Nobody has invited you to dice.");
        this.cancelInvitation(player.id);
        if (Date.now() >= invitation.deadline) throw new Error("That dice invitation expired.");
        const first = this.enroll(invitation.first, invitation.table);
        let second: Member;
        try { second = this.enroll(invitation.second, invitation.table); }
        catch (error) { this.leave(invitation.first); throw error; }
        this.begin(first, second, invitation.score);
    }
}

export const diceLobby = new DiceLobby();
