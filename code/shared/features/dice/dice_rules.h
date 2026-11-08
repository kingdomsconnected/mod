#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace KCDC::Shared::Game {
    /**
     * The game's dice rules, as the engine plays them for a seat with no
     * badge, so the server can decide a match the clients only animate.
     *
     * A face is an index, 0..5 for one to six pips, exactly as the engine
     * stores it. Every rule below is `C_DiceGameModel::BuildScoringRules`
     * (0x182E8DC9C) without a badge; the wildcard variants it also registers
     * need a joker face (6), which a server-dealt die never shows, so they
     * are left out -- they can never match, and dropping them changes neither
     * which rules match nor their order.
     */
    namespace Dice {
        inline constexpr std::size_t kDiceCount    = 6;
        inline constexpr std::uint8_t kFaceCount   = 6;
        inline constexpr std::uint8_t kAllDiceMask = 0x3F;

        /** One scoring combination: the faces it consumes and what it is worth. */
        struct ScoringRule {
            /** Sorted ascending, as the engine's rule vectors are. */
            std::vector<std::uint8_t> faces;
            std::uint32_t score = 0;
        };

        /**
         * Every rule, sorted by score from highest to lowest -- the order
         * `BuildScoringRules` leaves its per-seat vector in (its insertion sort
         * at 0x18200C0B0 moves a higher score ahead). Rules of equal score are
         * never both satisfiable by six dice, so their relative order cannot
         * change a result.
         */
        inline const std::vector<ScoringRule> &StandardRules() {
            static const std::vector<ScoringRule> rules = [] {
                std::vector<ScoringRule> built;
                built.push_back({{0}, 100});
                built.push_back({{4}, 50});
                built.push_back({{0, 1, 2, 3, 4}, 500});
                built.push_back({{1, 2, 3, 4, 5}, 750});
                built.push_back({{0, 1, 2, 3, 4, 5}, 1500});

                // Three of a face, each further die doubling: RegisterRepeatedFaceScoringRules.
                const std::array<std::uint32_t, kFaceCount> base {1000, 200, 300, 400, 500, 600};
                for (std::uint8_t face = 0; face < kFaceCount; ++face) {
                    for (std::uint8_t extra = 0; extra < 4; ++extra) {
                        built.push_back({std::vector<std::uint8_t>(static_cast<std::size_t>(3 + extra), face), base[face] << extra});
                    }
                }

                std::stable_sort(built.begin(), built.end(), [](const ScoringRule &left, const ScoringRule &right) {
                    return left.score > right.score;
                });
                return built;
            }();
            return rules;
        }

        /** Whether the sorted multiset `rule` is contained in the sorted multiset `dice` (0x182E86078). */
        inline bool RuleFits(const std::vector<std::uint8_t> &rule, const std::vector<std::uint8_t> &dice) {
            return std::includes(dice.begin(), dice.end(), rule.begin(), rule.end());
        }

        /** Takes one of each of `rule`'s faces out of `dice`. */
        inline void RemoveRule(const std::vector<std::uint8_t> &rule, std::vector<std::uint8_t> &dice) {
            for (const std::uint8_t face : rule) {
                dice.erase(std::find(dice.begin(), dice.end(), face));
            }
        }

        /** What `Evaluate` decided. */
        struct Evaluation {
            bool scores         = false;
            std::uint32_t score = 0;
        };

        /**
         * `C_DiceGameModel::EvaluateDiceScore` (0x182E94138), restated.
         *
         * The engine does not search for the best split. It walks the sorted
         * rules from a starting rule, greedily taking every rule that still
         * fits the dice left, and restarts one rule further on whenever a
         * pass either uses all the dice or runs out of rules. Each later rule
         * of a pass is looked for from that pass's first rule onwards, so the
         * same rule may be taken twice. The best pass that used every die wins
         * once there is one; before that, the best partial pass.
         *
         * `allowLeftovers` true is the bust test after a throw -- anything
         * scoring at all is enough. False is scoring a selection: every die in
         * it must be consumed. `emptyScores` lets an empty set succeed with 0.
         *
         * Restated rather than replaced with an exhaustive search, so that a
         * server decision can never disagree with what the engine on both
         * clients shows.
         */
        inline Evaluation Evaluate(const std::vector<std::uint8_t> &faces, bool allowLeftovers, bool emptyScores) {
            std::vector<std::uint8_t> dice = faces;
            std::sort(dice.begin(), dice.end());
            if (emptyScores && dice.empty()) {
                return {true, 0};
            }

            const std::vector<ScoringRule> &rules = StandardRules();
            std::vector<std::uint8_t> work        = dice;
            std::size_t start                     = 0;
            bool passStarted                      = false;
            std::uint32_t score                   = 0;
            std::uint32_t best                    = 0;
            std::size_t bestLeftovers             = 0;

            const auto restart = [&] {
                ++start;
                score       = 0;
                work        = dice;
                passStarted = false;
            };

            while (!work.empty()) {
                if (start >= rules.size()) {
                    break;
                }

                std::size_t index = start;
                while (index < rules.size() && !RuleFits(rules[index].faces, work)) {
                    ++index;
                }
                if (index >= rules.size()) {
                    if (score == 0) {
                        break;
                    }
                    restart();
                    continue;
                }

                // A pass's first rule is where the next restart counts from.
                if (!passStarted) {
                    start       = index;
                    passStarted = true;
                }
                score += rules[index].score;
                RemoveRule(rules[index].faces, work);

                const bool haveFull  = best > 0 && bestLeftovers == 0;
                const bool usedAll   = work.empty();
                const bool improves  = haveFull == usedAll && score > best;
                const bool firstFull = !haveFull && usedAll;
                if (improves || firstFull) {
                    best          = score;
                    bestLeftovers = work.size();
                }

                if (usedAll) {
                    restart();
                }
            }

            if (allowLeftovers || bestLeftovers == 0) {
                return {best > 0, best};
            }
            return {false, best};
        }

        /** A throw scores nothing at all. */
        inline bool IsBust(const std::vector<std::uint8_t> &thrown) {
            return !Evaluate(thrown, true, false).scores;
        }

        /** What a seat does with the dice it holds. */
        enum class MoveKind : std::uint8_t {
            /** Score the held dice and throw the rest. */
            Continue = 0,

            /** Score the held dice, if any, and bank the turn. */
            Pass = 1,
        };

        /** Why the server ended a match. */
        enum class EndReason : std::uint8_t {
            /** A seat banked the target score. */
            Won = 0,

            /** A player gave up -- left the table or quit the minigame. */
            GaveUp = 1,

            /** A player disconnected or their body went. */
            Left = 2,

            /** A seat did not move in time. */
            TimedOut = 3,

            /** A client could not start or keep the native game in step. */
            Failed = 4,

            /** A script stopped it. */
            Stopped = 5,
        };

        inline constexpr std::uint8_t kNoSeat = 0xFF;

        /**
         * One match between seat 0 and seat 1, decided entirely from the faces
         * the server dealt, turn by turn as the engine plays it:
         *
         * - A turn starts with all six dice free. A throw rolls every die not
         *   set aside; one that scores nothing busts the turn, its points lost.
         * - Continue scores the held dice -- every one must count -- sets them
         *   aside and throws the rest; with all six set aside, all six again.
         * - Pass banks the turn, first scoring the held dice when there are
         *   any. A pass holding dice that do not all count is refused: the
         *   engine would resolve it against the dice left loose, and the
         *   clients never send one.
         * - Banking the target score or more wins.
         */
        class Table {
          public:
            explicit Table(std::uint32_t targetScore, std::uint8_t firstSeat): _target(targetScore), _turn(firstSeat & 1) {}

            /** Applies a deal to the seat whose throw is due. True when the throw busts, which ends the turn. */
            bool Throw(const std::vector<std::uint8_t> &dealt) {
                std::size_t next = 0;
                for (std::size_t die = 0; die < kDiceCount; ++die) {
                    if ((_scored >> die & 1) == 0 && next < dealt.size()) {
                        _faces[die] = dealt[next++];
                    }
                }
                _throwNeeded = false;
                if (IsBust(FacesOf(FreeMask()))) {
                    _turnScore = 0;
                    HandOver();
                    return true;
                }
                return false;
            }

            /**
             * Applies one move of the seat to play. Empty when it is refused,
             * otherwise the points it added to the turn.
             */
            std::optional<std::uint32_t> Move(MoveKind kind, std::uint8_t heldMask) {
                if (_over || _throwNeeded || (heldMask & ~FreeMask()) != 0) {
                    return std::nullopt;
                }
                const Evaluation held = Evaluate(FacesOf(heldMask), false, false);

                if (kind == MoveKind::Continue) {
                    if (!held.scores) {
                        return std::nullopt;
                    }
                    _turnScore += held.score;
                    _scored |= heldMask;
                    if (_scored == kAllDiceMask) {
                        _scored = 0;
                    }
                    _throwNeeded = true;
                    return held.score;
                }

                if (heldMask != 0 && !held.scores) {
                    return std::nullopt;
                }
                const std::uint32_t added = heldMask != 0 ? held.score : 0;
                _totals[_turn] += _turnScore + added;
                _turnScore = 0;
                if (_totals[_turn] >= _target) {
                    _over   = true;
                    _winner = _turn;
                    return added;
                }
                HandOver();
                return added;
            }

            /** How many dice the next throw rolls, and so how many faces its deal carries. */
            std::size_t DiceToThrow() const {
                std::size_t count = 0;
                for (std::size_t die = 0; die < kDiceCount; ++die) {
                    count += (_scored >> die & 1) == 0 ? 1 : 0;
                }
                return count;
            }

            /** The dice on the table that may still be held: thrown this time and not set aside. */
            std::uint8_t FreeMask() const {
                return static_cast<std::uint8_t>(kAllDiceMask & ~_scored);
            }

            void Finish(std::uint8_t winner) {
                _over   = true;
                _winner = winner;
            }

            std::uint8_t Turn() const {
                return _turn;
            }

            bool ThrowNeeded() const {
                return _throwNeeded && !_over;
            }

            bool Over() const {
                return _over;
            }

            std::uint8_t Winner() const {
                return _winner;
            }

            std::uint32_t Total(std::uint8_t seat) const {
                return _totals[seat & 1];
            }

            std::uint32_t TurnScore() const {
                return _turnScore;
            }

            std::uint32_t Target() const {
                return _target;
            }

            std::uint8_t Face(std::size_t die) const {
                return _faces[die];
            }

          private:
            std::vector<std::uint8_t> FacesOf(std::uint8_t mask) const {
                std::vector<std::uint8_t> faces;
                for (std::size_t die = 0; die < kDiceCount; ++die) {
                    if ((mask >> die & 1) != 0) {
                        faces.push_back(_faces[die]);
                    }
                }
                return faces;
            }

            void HandOver() {
                _turn        = static_cast<std::uint8_t>(_turn ^ 1);
                _scored      = 0;
                _throwNeeded = true;
            }

            std::uint32_t _target;
            std::uint8_t _turn;
            std::array<std::uint32_t, 2> _totals {};
            std::uint32_t _turnScore = 0;
            std::array<std::uint8_t, kDiceCount> _faces {};
            std::uint8_t _scored = 0;
            bool _throwNeeded    = true;
            bool _over           = false;
            std::uint8_t _winner = kNoSeat;
        };
    } // namespace Dice
} // namespace KCDC::Shared::Game
