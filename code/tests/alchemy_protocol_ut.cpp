#include "shared/features/crafting/alchemy_protocol.h"

#include <stdexcept>

namespace {
    void RequireProtocol(bool condition, const char *message) {
        if (!condition) {
            throw std::runtime_error(message);
        }
    }
} // namespace

void AlchemyProtocolTests() {
    nlohmann::json decoded;
    std::string error;
    const nlohmann::json start = {{"request", "1"}, {"op", "start"}, {"station", "18446744073709551615"}, {"ticket", 1U}, {"callbackGeneration", "1"}};
    RequireProtocol(KCDC::Shared::Crafting::ReadRequest(start.dump(), decoded, error), "a start above the JavaScript safe integer survives the wire");
    std::uint64_t identity = 0;
    RequireProtocol(KCDC::Shared::Crafting::ReadIdentity(decoded, "station", identity) && identity == 18446744073709551615ULL, "the station identity is lossless");
    for (const auto &invalid : nlohmann::json::array({0, "0", "01", "-1", "+1", "1.0", " 1", "18446744073709551616", ""})) {
        auto candidate       = start;
        candidate["station"] = invalid;
        RequireProtocol(!KCDC::Shared::Crafting::ReadRequest(candidate.dump(), decoded, error), "a noncanonical or overflowed station is refused");
    }
    for (const char *claim : {"revision", "challenge", "outputs"}) {
        auto candidate   = start;
        candidate[claim] = "1";
        RequireProtocol(!KCDC::Shared::Crafting::ReadRequest(candidate.dump(), decoded, error), "a start carries no revision, identity or outcome claim");
    }
    auto candidate      = start;
    candidate["ticket"] = 4294967296ULL;
    RequireProtocol(!KCDC::Shared::Crafting::ReadRequest(candidate.dump(), decoded, error), "the callback ticket cannot narrow");
    candidate                   = start;
    candidate["continuationOf"] = "alchemy-3";
    RequireProtocol(KCDC::Shared::Crafting::ReadRequest(candidate.dump(), decoded, error), "the next batch names the one before it");
    candidate["continuationOf"] = "";
    RequireProtocol(!KCDC::Shared::Crafting::ReadRequest(candidate.dump(), decoded, error), "an empty predecessor is not a fresh entry");
    RequireProtocol(!KCDC::Shared::Crafting::ReadRequest("{\"request\":\"a\",\"op\":\"inspect\",\"op\":\"inspect\"}", decoded, error), "duplicate keys are refused");
    RequireProtocol(!KCDC::Shared::Crafting::ReadRequest("{", decoded, error), "truncated JSON is refused");
    RequireProtocol(!KCDC::Shared::Crafting::ReadRequest(std::string(KCDC::Shared::RPC::AlchemyRequest::kMaximumBytes + 1, ' '), decoded, error), "an oversized body is refused before parsing");
    RequireProtocol(KCDC::Shared::Crafting::ReadRequest("{\"request\":\"2\",\"op\":\"inspect\"}", decoded, error) && !KCDC::Shared::Crafting::ReadRequest("{\"request\":\"2\",\"op\":\"inspect\",\"session\":\"s\"}", decoded, error), "inspect carries nothing else");

    nlohmann::json action = {{"request", "3"}, {"op", "action"}, {"session", "alchemy-1"}, {"sequence", "1"}, {"target", 7U}, {"itemId", "12"}};
    RequireProtocol(KCDC::Shared::Crafting::ReadRequest(action.dump(), decoded, error), "an action names a target and a row");
    action["sequence"] = "0";
    RequireProtocol(!KCDC::Shared::Crafting::ReadRequest(action.dump(), decoded, error), "sequence zero belongs to no action");
    action["sequence"] = "1";
    action["target"]   = static_cast<unsigned>(KCDC::Shared::Crafting::AlchemyPosition::None);
    RequireProtocol(!KCDC::Shared::Crafting::ReadRequest(action.dump(), decoded, error), "None is the Back action, not a target");
    action["target"] = 7U;
    action["action"] = 9U;
    RequireProtocol(!KCDC::Shared::Crafting::ReadRequest(action.dump(), decoded, error), "the owner cannot name the action the server derives");

    const nlohmann::json back = {{"request", "4"}, {"op", "return"}, {"session", "alchemy-1"}, {"sequence", "2"}};
    RequireProtocol(KCDC::Shared::Crafting::ReadRequest(back.dump(), decoded, error), "Back is its own intent");
    for (const char *op : {"entryready", "pulse", "complete"}) {
        auto intent  = back;
        intent["op"] = op;
        RequireProtocol(KCDC::Shared::Crafting::ReadRequest(intent.dump(), decoded, error), "scalar intents carry a session and a sequence");
        for (const char *claim : {"target", "readyAtMs", "sessionQualityBonus", "heat"}) {
            auto forged   = intent;
            forged[claim] = 1U;
            RequireProtocol(!KCDC::Shared::Crafting::ReadRequest(forged.dump(), decoded, error), "a scalar intent cannot claim state");
        }
    }

    RequireProtocol(KCDC::Shared::Crafting::ReadRequest(nlohmann::json({{"request", "5"}, {"op", "cancel"}, {"session", "alchemy-1"}}).dump(), decoded, error), "cancel names a session");
    RequireProtocol(KCDC::Shared::Crafting::ReadRequest(nlohmann::json({{"request", "5"}, {"op", "cancel"}, {"startRequest", "1"}, {"continuationOf", "alchemy-1"}}).dump(), decoded, error), "or a start not yet answered");
    RequireProtocol(!KCDC::Shared::Crafting::ReadRequest(nlohmann::json({{"request", "5"}, {"op", "cancel"}}).dump(), decoded, error), "cancel names something");
    RequireProtocol(!KCDC::Shared::Crafting::ReadRequest(nlohmann::json({{"request", "5"}, {"op", "cancel"}, {"session", ""}, {"startRequest", "1"}}).dump(), decoded, error), "an empty session is left out, never sent");

    for (const char *reason : {"cancelled", "interrupted", "clientError"}) {
        const nlohmann::json cancel = {{"request", "5"}, {"op", "cancel"}, {"session", "alchemy-1"}, {"reason", reason}};
        RequireProtocol(KCDC::Shared::Crafting::ReadRequest(cancel.dump(), decoded, error) && decoded.at("reason") == reason, "known cancellation reasons round trip");
    }
    for (const nlohmann::json reason : {nlohmann::json("disconnected"), nlohmann::json("anything"), nlohmann::json(1), nlohmann::json(nullptr)}) {
        const nlohmann::json cancel = {{"request", "5"}, {"op", "cancel"}, {"session", "alchemy-1"}, {"reason", reason}};
        RequireProtocol(!KCDC::Shared::Crafting::ReadRequest(cancel.dump(), decoded, error), "clients cannot invent cancellation reasons");
    }

    KCDC::Shared::RPC::AlchemyRequest sent;
    sent.body = start.dump();
    MafiaNet::BitStream wire;
    sent.Serialize(&wire, true);
    RequireProtocol(sent.valid, "a bounded request serializes");
    wire.ResetReadPointer();
    KCDC::Shared::RPC::AlchemyRequest received;
    received.Serialize(&wire, false);
    RequireProtocol(received.valid && received.body == sent.body, "the request transport round trips");
    MafiaNet::BitStream truncated;
    truncated.Write(static_cast<std::uint32_t>(128));
    received.Serialize(&truncated, false);
    RequireProtocol(!received.valid, "a truncated body is refused");
    MafiaNet::BitStream oversized;
    oversized.Write(static_cast<std::uint32_t>(KCDC::Shared::RPC::AlchemyRequest::kMaximumBytes + 1));
    received.body.clear();
    received.Serialize(&oversized, false);
    RequireProtocol(!received.valid && received.body.empty(), "an oversized length is refused before allocating");
}
