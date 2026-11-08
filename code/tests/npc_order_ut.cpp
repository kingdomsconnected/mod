#include "features/navigation/navmesh.h"
#include "features/navigation/navmesh_codec.h"
#include "features/npc/npc_navigation.h"
#include "features/npc/npc_order.h"

#include <DetourNavMesh.h>

#include <glm/geometric.hpp>

#include <cmath>
#include <cstring>
#include <exception>
#include <iostream>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
    void Require(bool condition, const char *message) {
        if (!condition) {
            throw std::runtime_error(message);
        }
    }

    /** One game-format tile of ground polygons over 16 x 16 m, its heights packed between `minZ` and `maxZ`. */
    class GroundTile final {
      public:
        struct Poly {
            std::vector<std::uint16_t> verts;
            std::vector<std::uint16_t> neis;
        };

        std::vector<glm::vec3> verts;
        std::vector<Poly> polys;
        float minZ = -1.0F;
        float maxZ = 1.0F;

        KCDC::Features::Navigation::MeshFiles Files() const {
            std::vector<std::byte> tiles;
            const auto vertCount        = static_cast<std::int32_t>(verts.size());
            const auto polyCount        = static_cast<std::int32_t>(polys.size());
            const std::int32_t ints[17] = {DT_NAVMESH_MAGIC, KCDC::Features::Navigation::TileCodec::kGameTileVersion, 0, 0, 0, 0, polyCount, vertCount, vertCount, 0, 0, 0, 0, 0, 0, 0, polyCount};
            for (const std::int32_t value : ints) {
                Put(tiles, value);
            }
            const float floats[11] = {1.76F, 0.32F, 0.32F, 0.8F, 0.0F, 0.0F, minZ, 16.0F, 16.0F, maxZ, 12.5F};
            for (const float value : floats) {
                Put(tiles, value);
            }
            for (const glm::vec3 &vert : verts) {
                const auto x = static_cast<std::uint32_t>(std::lround(vert.x / 16.0F * 2047.0F));
                const auto y = static_cast<std::uint32_t>(std::lround(vert.y / 16.0F * 2047.0F));
                const auto z = static_cast<std::uint32_t>(std::lround((vert.z - minZ) / (maxZ - minZ) * 1023.0F));
                Put(tiles, (x << 21) | (y << 10) | z);
            }
            for (const Poly &poly : polys) {
                for (std::size_t j = 0; j < 6; ++j) {
                    Put(tiles, j < poly.verts.size() ? poly.verts[j] : std::uint16_t {0});
                }
                for (std::size_t j = 0; j < 6; ++j) {
                    Put(tiles, j < poly.neis.size() ? poly.neis[j] : std::uint16_t {0});
                }
                Put(tiles, std::uint8_t {1});
                Put(tiles, static_cast<std::uint8_t>(poly.verts.size()));
                Put(tiles, std::uint8_t {0});
                Put(tiles, std::uint8_t {1});
            }

            KCDC::Features::Navigation::MeshFiles files;
            const std::int32_t words[5] = {0, 0, 0, 1, static_cast<std::int32_t>(tiles.size())};
            files.index.resize(sizeof(words));
            std::memcpy(files.index.data(), words, sizeof(words));
            files.tiles = std::move(tiles);
            return files;
        }

      private:
        template <typename T>
        static void Put(std::vector<std::byte> &out, T value) {
            const std::size_t at = out.size();
            out.resize(at + sizeof(T));
            std::memcpy(out.data() + at, &value, sizeof(T));
        }
    };

    /** An L: east along y 0..2 to x 10, then north along x 8..10 to y 10. */
    GroundTile Bend() {
        GroundTile tile;
        tile.verts = {{0, 0, 0}, {8, 0, 0}, {10, 0, 0}, {10, 2, 0}, {10, 10, 0}, {8, 10, 0}, {8, 2, 0}, {0, 2, 0}};
        tile.polys = {{{0, 7, 6, 1}, {0, 0, 2, 0}}, {{1, 6, 3, 2}, {1, 3, 0, 0}}, {{6, 5, 4, 3}, {0, 0, 0, 2}}};
        return tile;
    }

    /** A dogleg: east along y 0..2 to x 8, then a gentle bend up to x 15 between y 3 and 5. */
    GroundTile Dogleg() {
        GroundTile tile;
        tile.verts = {{0, 0, 0}, {0, 2, 0}, {8, 2, 0}, {8, 0, 0}, {15, 3, 0}, {15, 5, 0}};
        tile.polys = {{{0, 1, 2, 3}, {0, 0, 2, 0}}, {{3, 2, 5, 4}, {1, 0, 0, 0}}};
        return tile;
    }

    /** A strip y 6..10 climbing one metre for every metre east, in 2 m polygons: a steep hillside. */
    GroundTile Hill() {
        GroundTile tile;
        tile.minZ = 0.0F;
        tile.maxZ = 16.0F;
        for (int i = 0; i <= 8; ++i) {
            tile.verts.emplace_back(2.0F * i, 6.0F, 2.0F * i);
        }
        for (int i = 0; i <= 8; ++i) {
            tile.verts.emplace_back(2.0F * i, 10.0F, 2.0F * i);
        }
        for (std::uint16_t i = 0; i < 8; ++i) {
            const std::uint16_t west = i;
            const std::uint16_t east = i < 7 ? static_cast<std::uint16_t>(i + 2) : std::uint16_t {0};
            tile.polys.push_back({{i, static_cast<std::uint16_t>(9 + i), static_cast<std::uint16_t>(10 + i), static_cast<std::uint16_t>(i + 1)}, {west, 0, east, 0}});
        }
        return tile;
    }

    void Load(KCDC::Features::Navigation::Navmesh &mesh, const GroundTile &tile) {
        std::vector<std::string> rejected;
        std::string error;
        Require(mesh.Build(tile.Files(), {}, rejected, error), "the test mesh loads");
    }

    /** One NPC as `Npcs` sees it: the replicated fields an order is read against. */
    struct Body {
        glm::vec3 position {0.0F};
        bool hasSimulator            = false;
        std::uint32_t intentRevision = 1;
        KCDC::Shared::Game::NpcIntent intent;
        KCDC::Shared::Game::NpcIntentStatus report = KCDC::Shared::Game::NpcIntentStatus::Idle;
        std::uint32_t reportRevision               = 0;
    };

    /**
     * `Npcs::Update` without the server around it: the same calls into the
     * order in the same order, with the entity reduced to the fields an
     * order is read against.
     */
    class Driver final {
      public:
        explicit Driver(const KCDC::Features::Navigation::Navmesh &mesh): _mesh(mesh) {}

        KCDC::Features::Npc::NpcOrders orders;
        std::map<std::uint64_t, Body> bodies;
        std::vector<std::pair<std::uint64_t, KCDC::Shared::Game::NpcIntentStatus>> announced;
        std::vector<MafiaNet::Time> announcedAt;
        std::vector<std::uint64_t> planned;
        MafiaNet::Time now = 1000;

        KCDC::Features::Npc::NpcOrderContext Context(const Body &body) {
            KCDC::Features::Npc::NpcOrderContext context;
            context.now            = now;
            context.position       = body.position;
            context.hasSimulator   = body.hasSimulator;
            context.meshReady      = _mesh.IsReady();
            context.reportStatus   = body.report;
            context.reportRevision = body.reportRevision;
            context.intentRevision = body.intentRevision;
            return context;
        }

        void Republish(std::uint64_t id) {
            Body &body  = bodies.at(id);
            body.intent = orders.For(id).SimulatorIntent(body.position);
            ++body.intentRevision;
            orders.For(id).Published(body.intentRevision);
        }

        void Settle(std::uint64_t id) {
            if (orders.For(id).NeedsPlan()) {
                orders.Track(id);
                return;
            }
            Republish(id);
        }

        void Command(std::uint64_t id, const KCDC::Shared::Game::NpcIntent &goal, KCDC::Features::Npc::Pathfinding mode) {
            orders.For(id).Command(goal, mode, Context(bodies.at(id)));
            Settle(id);
        }

        void Patrol(std::uint64_t id, const std::vector<glm::vec3> &route, float waitSeconds, KCDC::Features::Npc::Pathfinding mode) {
            orders.For(id).Patrol(route, true, KCDC::Shared::Game::NpcSpeed::Walk, waitSeconds, mode, Context(bodies.at(id)));
            Settle(id);
        }

        /** The simulator answers the revision it is running. */
        void Report(std::uint64_t id, KCDC::Shared::Game::NpcIntentStatus status) {
            Body &body          = bodies.at(id);
            body.report         = status;
            body.reportRevision = body.intentRevision;
        }

        void Tick(std::size_t budget = KCDC::Features::Npc::NpcOrders::kPlansPerTick, MafiaNet::Time step = 100) {
            now += step;
            for (auto &[id, body] : bodies) {
                KCDC::Features::Npc::NpcOrder &order               = orders.For(id);
                const KCDC::Features::Npc::NpcOrderContext context = Context(body);
                const KCDC::Features::Npc::NpcOrderStep observed   = order.Observe(context);
                if (observed.announce) {
                    announced.emplace_back(id, *observed.announce);
                    announcedAt.push_back(now);
                }
                const KCDC::Features::Npc::NpcOrderStep advanced = order.Advance(context, body.position);
                if (order.NeedsPlan()) {
                    orders.Track(id);
                }
                else if (observed.republish || advanced.republish) {
                    Republish(id);
                }
            }
            orders.RunPlans(budget, _mesh.IsReady(), [&](std::uint64_t id, KCDC::Features::Npc::NpcOrder &order) {
                KCDC::Features::Navigation::QueryOptions options;
                options.searchHeight = order.Navigation().SearchHeight();
                order.Plan(_mesh, options, bodies.at(id).position);
                Republish(id);
                planned.push_back(id);
                return true;
            });
        }

        KCDC::Shared::Game::NpcIntentStatus Status(std::uint64_t id) {
            return orders.For(id).Status(Context(bodies.at(id)));
        }

      private:
        const KCDC::Features::Navigation::Navmesh &_mesh;
    };

    KCDC::Shared::Game::NpcIntent MoveTo(const glm::vec3 &point) {
        KCDC::Shared::Game::NpcIntent goal;
        goal.kind   = KCDC::Shared::Game::NpcIntentKind::MoveTo;
        goal.point  = point;
        goal.radius = 1.5F;
        return goal;
    }

    void PatrolBackoffTests() {
        KCDC::Features::Navigation::Navmesh mesh;
        Load(mesh, Bend());
        Driver driver(mesh);
        driver.bodies[1].position = glm::vec3(1.0F, 1.0F, 0.0F);

        // Two waypoints nowhere near the mesh: every leg plans to nothing.
        driver.Patrol(1, {glm::vec3(500.0F, 500.0F, 0.0F), glm::vec3(600.0F, 500.0F, 0.0F)}, 0.0F, KCDC::Features::Npc::Pathfinding::Server);
        Require(driver.Status(1) == KCDC::Shared::Game::NpcIntentStatus::Running, "a leg waiting for its plan reads running");
        MafiaNet::Time plannedAt = 0;
        for (int tick = 0; tick < 200; ++tick) {
            const std::size_t plansBefore  = driver.planned.size();
            const std::size_t eventsBefore = driver.announced.size();
            driver.Tick();
            Require(driver.announced.size() - eventsBefore <= 1, "one event per tick at most");
            if (driver.announced.size() != eventsBefore) {
                Require(driver.now > plannedAt, "a leg never resolves in the tick that planned it");
            }
            if (driver.planned.size() != plansBefore) {
                plannedAt = driver.now;
            }
        }
        Require(driver.planned.size() == 2, "an unreachable lap is planned once per leg, not once per tick");
        Require(driver.announced.size() == 2, "each failed leg is announced once");
        Require(driver.announced[0].second == KCDC::Shared::Game::NpcIntentStatus::Blocked, "the first unreachable leg is blocked");
        Require(driver.announced[1].second == KCDC::Shared::Game::NpcIntentStatus::Failed, "a lap with no leg arriving abandons the patrol as failed");
        Require(driver.announcedAt[1] - driver.announcedAt[0] >= KCDC::Features::Npc::NpcOrder::kFailedLegBackoffMs, "a failed leg is not retried before the backoff");
        Require(!driver.orders.For(1).IsPatrolling() && driver.Status(1) == KCDC::Shared::Game::NpcIntentStatus::Failed, "the abandoned patrol stays ended");

        // A single order that cannot be planned is announced once and left alone.
        driver.Command(1, MoveTo(glm::vec3(500.0F, 500.0F, 0.0F)), KCDC::Features::Npc::Pathfinding::Server);
        for (int tick = 0; tick < 50; ++tick) {
            driver.Tick();
        }
        Require(driver.planned.size() == 3 && driver.announced.size() == 3 && driver.announced[2].second == KCDC::Shared::Game::NpcIntentStatus::Blocked, "an unplannable move is planned and announced once");
    }

    void PlanBudgetTests() {
        KCDC::Features::Navigation::Navmesh mesh;
        Load(mesh, Bend());
        Driver driver(mesh);
        for (std::uint64_t id = 1; id <= 10; ++id) {
            driver.bodies[id].position     = glm::vec3(1.0F, 1.0F, 0.0F);
            driver.bodies[id].hasSimulator = true;
        }
        for (std::uint64_t id = 1; id <= 10; ++id) {
            driver.Command(id, MoveTo(glm::vec3(9.0F, 9.0F, 0.0F)), KCDC::Features::Npc::Pathfinding::Auto);
            Require(driver.bodies[id].intentRevision == 1, "a move waiting for its route is not published yet");
        }
        Require(driver.orders.QueuedPlans() == 10, "every route waits in the queue");
        driver.Tick(4);
        Require(driver.planned == std::vector<std::uint64_t> {1, 2, 3, 4}, "the first four orders are planned first");
        Require(driver.orders.QueuedPlans() == 6, "the rest wait for later ticks");
        Require(driver.bodies[5].intentRevision == 1 && driver.Status(5) == KCDC::Shared::Game::NpcIntentStatus::Running, "an order over budget reads running and keeps its old step");
        driver.Tick(4);
        driver.Tick(4);
        Require(driver.planned.size() == 10 && driver.planned.back() == 10, "the queue drains in order");

        // Without a mesh a plan is a straight line and costs nothing.
        const KCDC::Features::Navigation::Navmesh none;
        Driver straight(none);
        for (std::uint64_t id = 1; id <= 10; ++id) {
            straight.bodies[id].position = glm::vec3(1.0F, 1.0F, 0.0F);
            straight.Command(id, MoveTo(glm::vec3(9.0F, 9.0F, 0.0F)), KCDC::Features::Npc::Pathfinding::Auto);
        }
        straight.Tick(4);
        Require(straight.planned.size() == 10, "straight lines are not budgeted");
    }

    void CornerLeadTests() {
        KCDC::Features::Navigation::Navmesh mesh;
        Load(mesh, Bend());
        Driver driver(mesh);
        driver.bodies[1].position     = glm::vec3(1.0F, 1.0F, 0.0F);
        driver.bodies[1].hasSimulator = true;
        driver.Command(1, MoveTo(glm::vec3(9.0F, 9.0F, 0.0F)), KCDC::Features::Npc::Pathfinding::Auto);
        driver.Tick();
        const glm::vec3 bend = driver.bodies[1].intent.point;
        Require(glm::distance(bend, glm::vec3(9.0F, 9.0F, 0.0F)) > 1.0F, "a streamed auto move is handed the bend, not the destination");
        const std::uint32_t revision = driver.bodies[1].intentRevision;

        driver.bodies[1].position = bend - glm::vec3(3.0F, 0.0F, 0.0F);
        driver.Report(1, KCDC::Shared::Game::NpcIntentStatus::Running);
        driver.Tick();
        Require(driver.bodies[1].intentRevision == revision, "a body still short of the bend keeps walking to it");

        // A right angle: leading it from 1.2 m out would cut the corner by a
        // metre, into whatever the route bends around -- a wall, a door jamb.
        driver.bodies[1].position = bend - glm::vec3(1.2F, 0.0F, 0.0F);
        driver.Tick();
        Require(driver.bodies[1].intentRevision == revision, "a sharp bend is not led, so the body does not cut into what it bends around");

        // A report against the step before is not read as this one's answer.
        driver.bodies[1].reportRevision = revision - 1;
        driver.bodies[1].report         = KCDC::Shared::Game::NpcIntentStatus::Reached;
        driver.Tick();
        Require(driver.bodies[1].intentRevision == revision && driver.announced.empty(), "a stale arrival neither ends nor steps anything");

        // Arriving at the sharp bend hands over the next corner.
        driver.bodies[1].position = bend - glm::vec3(0.3F, 0.0F, 0.0F);
        driver.Report(1, KCDC::Shared::Game::NpcIntentStatus::Reached);
        driver.Tick();
        Require(driver.bodies[1].intentRevision == revision + 1 && glm::distance(driver.bodies[1].intent.point, glm::vec3(9.0F, 9.0F, 0.0F)) < 0.1F, "arriving at a sharp bend hands over the next corner");
        Require(driver.announced.empty() && driver.Status(1) == KCDC::Shared::Game::NpcIntentStatus::Running, "a bend is not an outcome");

        driver.bodies[1].position = glm::vec3(9.0F, 8.5F, 0.0F);
        driver.Report(1, KCDC::Shared::Game::NpcIntentStatus::Reached);
        driver.Tick();
        Require(driver.announced.size() == 1 && driver.announced[0].second == KCDC::Shared::Game::NpcIntentStatus::Reached, "the final corner completes the move");
    }

    void GentleLeadTests() {
        KCDC::Features::Navigation::Navmesh mesh;
        Load(mesh, Dogleg());
        Driver driver(mesh);
        driver.bodies[1].position     = glm::vec3(1.0F, 1.0F, 0.0F);
        driver.bodies[1].hasSimulator = true;
        const glm::vec3 goal(14.0F, 4.0F, 0.0F);
        driver.Command(1, MoveTo(goal), KCDC::Features::Npc::Pathfinding::Auto);
        driver.Tick();
        const glm::vec3 bend = driver.bodies[1].intent.point;
        Require(glm::distance(bend, goal) > 1.0F, "the dogleg is walked through its bend");
        const std::uint32_t revision = driver.bodies[1].intentRevision;

        // A bend says how much route goes on past it, so the engine does not
        // brake for it as for the end of the path; the last corner says none.
        Require(driver.bodies[1].intent.pathBeyond > 6.0F && driver.bodies[1].intent.radius <= 0.3F, "a bend carries the route beyond it and a tight arrival radius");

        // A gentle bend cuts so little that it is led before arrival.
        const glm::vec3 approach = glm::normalize(bend - glm::vec3(1.0F, 1.0F, 0.0F));
        driver.bodies[1].position = bend - approach * 0.45F;
        Require(KCDC::Features::Npc::NpcOrder::CornerCut(bend - approach * 1.6F, bend, goal) > KCDC::Features::Npc::NpcOrder::kMaxCornerCut, "from further out the same bend would be cut too deep to lead");
        driver.Report(1, KCDC::Shared::Game::NpcIntentStatus::Running);
        driver.Tick();
        Require(driver.bodies[1].intentRevision == revision + 1 && glm::distance(driver.bodies[1].intent.point, goal) < 0.1F, "a gentle bend is handed over before the body reaches it");
        Require(KCDC::Features::Npc::NpcOrder::CornerCut(driver.bodies[1].position, bend, goal) <= KCDC::Features::Npc::NpcOrder::kMaxCornerCut, "the lead it took cuts the bend within the margin");
        Require(driver.announced.empty(), "a led bend is not an outcome");
        Require(driver.bodies[1].intent.pathBeyond == 0.0F && driver.bodies[1].intent.radius >= 1.5F, "the last corner is the end of the path");

        // Arrival is across the ground, but not from a storey below.
        const KCDC::Shared::Game::NpcIntent last = driver.bodies[1].intent;
        Require(last.HasArrived(goal + glm::vec3(0.5F, 0.0F, 1.0F), goal), "a body on a step beside the goal has arrived");
        Require(!last.HasArrived(goal + glm::vec3(0.5F, 0.0F, -4.0F), goal), "a body under a balcony has not reached a point on it");
    }

    void SingleOutcomeTests() {
        const KCDC::Features::Navigation::Navmesh none;
        Driver driver(none);
        driver.bodies[1].position     = glm::vec3(1.0F, 1.0F, 0.0F);
        driver.bodies[1].hasSimulator = true;
        driver.Command(1, MoveTo(glm::vec3(9.0F, 9.0F, 0.0F)), KCDC::Features::Npc::Pathfinding::Game);
        driver.Report(1, KCDC::Shared::Game::NpcIntentStatus::Running);
        driver.Tick();
        driver.Report(1, KCDC::Shared::Game::NpcIntentStatus::Blocked);
        driver.Tick();
        driver.Report(1, KCDC::Shared::Game::NpcIntentStatus::Running);
        driver.Tick();
        driver.Report(1, KCDC::Shared::Game::NpcIntentStatus::Blocked);
        driver.Tick();
        driver.Report(1, KCDC::Shared::Game::NpcIntentStatus::Reached);
        driver.Tick();
        Require(driver.announced.size() == 1 && driver.announced[0].second == KCDC::Shared::Game::NpcIntentStatus::Blocked, "blocked, running, blocked and reached on one revision announce blocked once");
        Require(driver.Status(1) == KCDC::Shared::Game::NpcIntentStatus::Blocked, "the accepted outcome is what scripts read");

        // With a mesh the first block is a fallback rather than an outcome,
        // and the route it plans ends the order once.
        KCDC::Features::Navigation::Navmesh mesh;
        Load(mesh, Bend());
        Driver recovering(mesh);
        recovering.bodies[1].position     = glm::vec3(1.0F, 1.0F, 0.0F);
        recovering.bodies[1].hasSimulator = true;
        recovering.Command(1, MoveTo(glm::vec3(9.0F, 9.0F, 0.0F)), KCDC::Features::Npc::Pathfinding::Game);
        Require(recovering.bodies[1].intent.point == glm::vec3(9.0F, 9.0F, 0.0F), "game mode steers straight at the goal");
        recovering.Report(1, KCDC::Shared::Game::NpcIntentStatus::Blocked);
        recovering.Tick();
        Require(recovering.announced.empty() && recovering.planned.size() == 1, "a blocked game move falls back to a server route");
        recovering.Report(1, KCDC::Shared::Game::NpcIntentStatus::Blocked);
        recovering.Tick();
        recovering.Report(1, KCDC::Shared::Game::NpcIntentStatus::Running);
        recovering.Tick();
        recovering.Report(1, KCDC::Shared::Game::NpcIntentStatus::Blocked);
        recovering.Tick();
        Require(recovering.announced.size() == 1 && recovering.announced[0].second == KCDC::Shared::Game::NpcIntentStatus::Blocked && recovering.planned.size() == 1, "a blocked server route is announced once and not replanned");
    }

    void HandoverTests() {
        const KCDC::Features::Navigation::Navmesh none;
        Driver driver(none);
        driver.bodies[1].position     = glm::vec3(1.0F, 1.0F, 0.0F);
        driver.bodies[1].hasSimulator = true;
        driver.Command(1, MoveTo(glm::vec3(9.0F, 9.0F, 0.0F)), KCDC::Features::Npc::Pathfinding::Game);
        driver.bodies[1].position = glm::vec3(9.0F, 9.0F, 0.0F);
        driver.Report(1, KCDC::Shared::Game::NpcIntentStatus::Reached);
        driver.Tick();
        Require(driver.announced.size() == 1, "the arrival is announced");

        // What `Npcs`'s handler does after `NpcEntity::OnSimulatorChanged` has bumped the revision.
        Body &body = driver.bodies[1];
        ++body.intentRevision;
        KCDC::Features::Npc::NpcOrder &order = driver.orders.For(1);
        order.OnSimulatorChanged(driver.now);
        body.intent = order.SimulatorIntent(body.position);
        order.Published(body.intentRevision);
        Require(body.intent.kind == KCDC::Shared::Game::NpcIntentKind::Hold, "a finished move is handed to the next simulator as a hold");

        driver.Report(1, KCDC::Shared::Game::NpcIntentStatus::Reached);
        driver.Tick();
        driver.Report(1, KCDC::Shared::Game::NpcIntentStatus::Blocked);
        driver.Tick();
        Require(driver.announced.size() == 1 && driver.Status(1) == KCDC::Shared::Game::NpcIntentStatus::Reached, "a finished order is neither walked nor reported again");

        // A standing order stays what it was: a look is still a look.
        KCDC::Shared::Game::NpcIntent look;
        look.kind            = KCDC::Shared::Game::NpcIntentKind::LookAt;
        look.targetNetworkId = 7;
        driver.Command(1, look, KCDC::Features::Npc::Pathfinding::Auto);
        driver.Report(1, KCDC::Shared::Game::NpcIntentStatus::Reached);
        driver.Tick();
        ++body.intentRevision;
        order.OnSimulatorChanged(driver.now);
        Require(order.SimulatorIntent(body.position) == look, "a finished look is handed over as the look");
    }

    void DormantTests() {
        KCDC::Features::Navigation::Navmesh mesh;
        Load(mesh, Bend());
        Driver driver(mesh);
        driver.bodies[1].position = glm::vec3(1.0F, 1.0F, 0.0F);
        driver.Command(1, MoveTo(glm::vec3(9.0F, 9.0F, 0.0F)), KCDC::Features::Npc::Pathfinding::Auto);
        for (int tick = 0; tick < 200 && driver.announced.empty(); ++tick) {
            driver.Tick();
            if (driver.announced.empty()) {
                Require(driver.Status(1) == KCDC::Shared::Game::NpcIntentStatus::Running, "a dormant move reads running until it is announced");
            }
        }
        Require(driver.announced.size() == 1 && driver.announced[0].second == KCDC::Shared::Game::NpcIntentStatus::Reached, "a dormant move ends reached once");
        Require(driver.Status(1) == KCDC::Shared::Game::NpcIntentStatus::Reached && driver.bodies[1].position.y > 7.0F, "the server walked it there itself");
    }

    void FleeSlopeTests() {
        KCDC::Features::Navigation::Navmesh hill;
        Load(hill, Hill());
        KCDC::Shared::Game::NpcIntent flee;
        flee.kind   = KCDC::Shared::Game::NpcIntentKind::Flee;
        flee.point  = glm::vec3(1.0F, 8.0F, 1.0F);
        flee.radius = 10.0F;
        const glm::vec3 body(2.0F, 8.0F, 2.0F);
        const glm::vec3 destination = flee.FleeDestination(body);
        Require(destination.z == body.z, "a flee destination is projected at the body's height");

        // Uphill, 11 m on: the ground there is 10 m above the projection.
        KCDC::Features::Npc::NpcNavigation navigation;
        navigation.Start(flee, KCDC::Features::Npc::Pathfinding::Auto);
        Require(navigation.StartFallback(destination, true), "the flee falls back");
        KCDC::Features::Navigation::QueryOptions narrow;
        navigation.Plan(hill, narrow, body);
        while (navigation.HasCorner()) {
            navigation.AdvanceCorner();
        }
        Require(navigation.Status() == KCDC::Shared::Game::NpcIntentStatus::Blocked, "the default 4 m search misses ground that far up the hill");

        navigation.Start(flee, KCDC::Features::Npc::Pathfinding::Auto);
        Require(navigation.StartFallback(destination, true) && navigation.SearchHeight() >= flee.radius, "a flee searches as far up or down as it runs");
        KCDC::Features::Navigation::QueryOptions wide;
        wide.searchHeight = navigation.SearchHeight();
        navigation.Plan(hill, wide, body);
        Require(navigation.HasCorner(), "the flee route climbs the hill");
        glm::vec3 position = body;
        navigation.Advance(position, 50.0F);
        Require(navigation.Status() == KCDC::Shared::Game::NpcIntentStatus::Reached && position.z > 5.0F && flee.HasArrived(position, glm::vec3(0.0F)), "the body ends up the hill, clear of the threat");
    }

    void AttackTests() {
        using namespace KCDC::Features::Npc;
        using namespace KCDC::Shared::Game;
        NpcIntent attack;
        attack.kind = NpcIntentKind::Attack;
        Require(!attack.IsSane(), "an attack must name a target");
        attack.targetNetworkId = 42;
        Require(attack.IsSane(), "an attack with a target is a valid intent");
        NpcOrder order;
        NpcOrderContext context;
        context.now          = 100;
        context.active       = true;
        context.hasSimulator = true;
        context.followTarget = glm::vec3(3.0F, 0.0F, 0.0F);
        order.Command(attack, Pathfinding::Auto, context);
        order.Published(7);
        context.intentRevision = 7;
        context.reportRevision = 6;
        context.reportStatus   = NpcIntentStatus::Failed;
        Require(!order.Observe(context).announce, "a previous order's failure cannot cancel an attack");
        context.reportRevision = 7;
        context.reportStatus   = NpcIntentStatus::Reached;
        order.Observe(context);
        Require(order.SimulatorIntent(context.position).kind == NpcIntentKind::Attack, "reaching the opponent does not finish combat");
        context.hasSimulator = false;
        auto position        = context.position;
        context.now += 5000;
        order.Advance(context, position);
        Require(position == context.position && !order.NeedsPlan(), "dormant combat neither moves nor plans offline attacks");
        order.OnSimulatorChanged(context.now);
        Require(order.SimulatorIntent(position).targetNetworkId == 42, "handover retains the designated opponent");
        context.followTarget.reset();
        const auto lost = order.Observe(context);
        Require(lost.republish && lost.announce == NpcIntentStatus::Failed, "target loss stops the attack without waiting for a client report");
        Require(order.SimulatorIntent(position).kind == NpcIntentKind::Hold, "target loss publishes hold to release native controls");
        Require(!order.Observe(context).announce, "target loss is reported once");
        context.followTarget = glm::vec3(3.0F);
        order.Command(attack, Pathfinding::Auto, context);
        NpcIntent hold;
        order.Command(hold, Pathfinding::Auto, context);
        Require(order.SimulatorIntent(position).kind == NpcIntentKind::Hold, "a replacement order cancels combat");
    }

    void NamedPatrolTests() {
        KCDC::Features::Navigation::Navmesh mesh;
        Driver driver(mesh);
        driver.bodies[1].hasSimulator = true;
        KCDC::Shared::Game::PatrolRouteDefinition route;
        route.id                    = "test.route";
        route.mode                  = KCDC::Shared::Game::PatrolMode::PingPong;
        route.waitSeconds           = 1;
        route.points                = {{{0, 0, 0}}, {{8, 0, 0}}, {{16, 0, 0}}};
        route.points[1].speed       = KCDC::Shared::Game::NpcSpeed::Run;
        route.points[1].radius      = 3;
        route.points[1].waitSeconds = 0;
        auto &order                 = driver.orders.For(1);
        order.Patrol(route, KCDC::Features::Npc::Pathfinding::Game, driver.Context(driver.bodies[1]));
        driver.Settle(1);
        route.points[1].position.x = 99;
        driver.Report(1, KCDC::Shared::Game::NpcIntentStatus::Reached);
        driver.Tick();
        Require(order.PatrolState()["phase"] == "waiting", "named patrol exposes wait phase");
        auto events = order.TakePatrolEvents();
        Require(events.size() == 1 && events[0]["waypointIndex"] == 0 && events[0]["status"] == "reached", "waypoint outcome carries its snapshot");
        driver.Tick(4, 1000);
        Require(driver.bodies[1].intent.point.x == 8 && driver.bodies[1].intent.speed == KCDC::Shared::Game::NpcSpeed::Run && driver.bodies[1].intent.radius == 3, "waypoint overrides and snapshot survive definition changes");
        for (const int expected : {2, 1, 0, 1}) {
            driver.Report(1, KCDC::Shared::Game::NpcIntentStatus::Reached);
            driver.Tick();
            driver.Tick(4, 1000);
            Require(order.PatrolState()["waypointIndex"] == expected, "ping-pong reverses without repeating endpoints");
        }
        Require(order.PatrolState()["lap"] == 1, "ping-pong counts round trips");
        order.TakePatrolEvents();
        driver.Command(1, {}, KCDC::Features::Npc::Pathfinding::Game);
        events = order.TakePatrolEvents();
        Require(order.PatrolState().is_null() && events.size() == 1 && events[0]["status"] == "cancelled", "hold cancels named patrol once");
        route.mode = KCDC::Shared::Game::PatrolMode::Once;
        route.points.resize(1);
        route.waitSeconds = 0;
        order.Patrol(route, KCDC::Features::Npc::Pathfinding::Game, driver.Context(driver.bodies[1]));
        driver.Settle(1);
        driver.Report(1, KCDC::Shared::Game::NpcIntentStatus::Reached);
        driver.Tick();
        events = order.TakePatrolEvents();
        Require(events.size() == 2 && events[1]["status"] == "completed" && order.PatrolState().is_null(), "once patrol emits completion after its last waypoint");
    }

    void DoorPassageTests() {
        using namespace KCDC::Features;
        using namespace KCDC::Shared::Game;
        Navigation::Navmesh mesh;
        mesh.SetDoorLeaves({{123, {9, 0, 0}, {9, 2, 0}}});
        Load(mesh, Bend());
        Npc::NpcOrder order;
        Npc::NpcOrderContext context;
        context.now          = 1000;
        context.hasSimulator = true;
        context.meshReady    = true;
        context.position     = {6, 1, 0};
        auto position        = context.position;
        auto passage         = DoorPassage::Opening;
        int calls            = 0;
        context.doorPassage  = [&](std::uint64_t guid) {
            Require(guid == 123, "only the route's door is used");
            ++calls;
            return passage;
        };
        auto goal = Npc::NpcOrder::PatrolLeg({9, 8, 0}, NpcSpeed::Walk);
        order.Command(goal, Npc::Pathfinding::Server, context);
        order.Plan(mesh, {}, position);
        Require(order.Navigation().Doors().size() == 1, "the route retains its synthetic door");
        order.Advance(context, position);
        Require(calls == 0, "a distant door is not opened at plan time");
        context.position = position = {7, 1, 0};
        auto step                   = order.Advance(context, position);
        Require(step.republish && order.SimulatorIntent(position).kind == NpcIntentKind::Hold, "moveTo pauses on approach while the door swings");
        order.Published(7);
        context.intentRevision = context.reportRevision = 7;
        context.reportStatus                            = NpcIntentStatus::Reached;
        Require(!order.Observe(context).announce && !order.IsFinished(), "a hold report cannot complete the move");
        context.now += 1600;
        passage = DoorPassage::Ready;
        step    = order.Advance(context, position);
        Require(step.republish && order.SimulatorIntent(position).kind == NpcIntentKind::MoveTo, "an open door resumes the same move");
        Require(!order.Observe(context).announce, "the old hold report is stale after release");
        passage = DoorPassage::Blocked;
        step    = order.Advance(context, position);
        Require(step.republish && order.SimulatorIntent(position).kind == NpcIntentKind::Hold, "a newly locked door stops motion immediately");
        Require(order.Observe(context).announce == NpcIntentStatus::Blocked && !order.Observe(context).announce, "a lock encountered during movement reports blocked exactly once");

        passage = DoorPassage::Opening;
        order.Patrol({goal.point, {2, 1, 0}}, true, NpcSpeed::Walk, 0, Npc::Pathfinding::Server, context);
        order.Plan(mesh, {}, position);
        order.Advance(context, position);
        Require(order.IsPatrolling() && order.SimulatorIntent(position).kind == NpcIntentKind::Hold, "patrols share the same door wait");
        context.now += 5000;
        order.Advance(context, position);
        Require(order.Observe(context).announce == NpcIntentStatus::Blocked, "a door that never finishes opening has a bounded wait");
        order.Command(goal, Npc::Pathfinding::Server, context);
        order.Plan(mesh, {}, position);
        passage = DoorPassage::Ready;
        order.Advance(context, position);
        Require(order.SimulatorIntent(position).kind == NpcIntentKind::MoveTo, "replacement commands clear old door waits");

        context.hasSimulator = false;
        context.now += 1000;
        context.position = position = {2, 1, 0};
        goal.speed                  = NpcSpeed::Run;
        order.Command(goal, Npc::Pathfinding::Server, context);
        order.Plan(mesh, {}, position);
        context.now += 1000;
        passage           = DoorPassage::Opening;
        const auto before = position;
        order.Advance(context, position);
        Require(position == before, "a dormant sprint cannot skip over a closed door in a long tick");
        passage = DoorPassage::Ready;
        context.now += 100;
        order.Advance(context, position);
        Require(position != before, "dormant movement resumes after the swing without accumulating waiting time");
    }

    void ValidationTests() {
        const KCDC::Shared::Game::NpcSpeed walk = KCDC::Shared::Game::NpcSpeed::Walk;
        Require(KCDC::Features::Npc::NpcOrder::IsUsableRoute({glm::vec3(1.0F), glm::vec3(2.0F)}, walk, 3.0F), "an ordinary route is usable");
        Require(!KCDC::Features::Npc::NpcOrder::IsUsableRoute({}, walk, 0.0F), "an empty route is refused");
        Require(!KCDC::Features::Npc::NpcOrder::IsUsableRoute({glm::vec3(1.0F), glm::vec3(40000.0F, 0.0F, 0.0F)}, walk, 0.0F), "a waypoint outside the world is refused rather than stalling the patrol at it");
        Require(!KCDC::Features::Npc::NpcOrder::IsUsableRoute({glm::vec3(std::numeric_limits<float>::quiet_NaN())}, walk, 0.0F), "a waypoint that is not a number is refused");
        Require(!KCDC::Features::Npc::NpcOrder::IsUsableRoute({glm::vec3(1.0F)}, walk, std::numeric_limits<float>::quiet_NaN()), "a wait that is not a number is refused");
        Require(!KCDC::Features::Npc::NpcOrder::IsUsableRoute({glm::vec3(1.0F)}, walk, std::numeric_limits<float>::infinity()), "an endless wait is refused");
        Require(KCDC::Features::Npc::NpcOrder::IsUsableRoute(std::vector<glm::vec3>(KCDC::Features::Npc::NpcOrder::kMaxPatrolPoints, glm::vec3(1.0F)), walk, 0.0F), "the longest route is usable");
        Require(!KCDC::Features::Npc::NpcOrder::IsUsableRoute(std::vector<glm::vec3>(KCDC::Features::Npc::NpcOrder::kMaxPatrolPoints + 1, glm::vec3(1.0F)), walk, 0.0F), "a longer route is refused");

        // A wait past the cap is clamped rather than refused.
        const KCDC::Features::Navigation::Navmesh none;
        Driver driver(none);
        driver.bodies[1].hasSimulator = true;
        driver.Patrol(1, {glm::vec3(0.0F), glm::vec3(5.0F, 0.0F, 0.0F)}, 1.0e9F, KCDC::Features::Npc::Pathfinding::Game);
        driver.Report(1, KCDC::Shared::Game::NpcIntentStatus::Reached);
        driver.Tick();
        const std::uint32_t waiting = driver.bodies[1].intentRevision;
        driver.Tick(4, static_cast<MafiaNet::Time>(KCDC::Features::Npc::NpcOrder::kMaxPatrolWaitSeconds * 1000.0F));
        Require(driver.bodies[1].intentRevision == waiting + 1 && driver.bodies[1].intent.point == glm::vec3(5.0F, 0.0F, 0.0F), "the wait is clamped to an hour");
    }
} // namespace

int RunNpcOrderSuite() {
    try {
        KCDC::Features::Npc::NpcOrders snapshotOrders;
        snapshotOrders.For(1);
        snapshotOrders.For(2);
        std::vector<std::uint64_t> ids;
        snapshotOrders.CollectIds(ids);
        snapshotOrders.Erase(1);
        snapshotOrders.For(3);
        Require(ids.size() == 2 && std::find(ids.begin(), ids.end(), 1) != ids.end() && std::find(ids.begin(), ids.end(), 3) == ids.end(), "Order ID snapshot survives erasure and excludes new orders");
        PatrolBackoffTests();
        PlanBudgetTests();
        CornerLeadTests();
        GentleLeadTests();
        SingleOutcomeTests();
        HandoverTests();
        DormantTests();
        FleeSlopeTests();
        AttackTests();
        ValidationTests();
        DoorPassageTests();
        NamedPatrolTests();
        std::cout << "NPC order tests passed\n";
        return 0;
    }
    catch (const std::exception &error) {
        std::cerr << "NPC order tests failed: " << error.what() << '\n';
        return 1;
    }
}
