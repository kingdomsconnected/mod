#pragma once

#include "shared/game/custom_item_registry.h"
#include "shared/image/png_image.h"

#include <utils/crypto.h>
#include <utils/hashing.h>
#include <utils/string_utils.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace CustomItemRegistryTests {
    // A real 1x1 RGBA PNG, 70 bytes.
    constexpr const char *kTinyPng = "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8BQDwAEhQGAhKmMIQAAAABJRU5ErkJggg==";

    inline bool Rejects(const nlohmann::json &definition) {
        try {
            KCDC::Shared::Game::CustomItemRegistry::Get().Register("qa:invalid", definition);
            return false;
        }
        catch (const std::invalid_argument &) {
            return KCDC::Shared::Game::CustomItemRegistry::Get().Resolve("qa:invalid").empty();
        }
    }

    inline nlohmann::json Borrowed() {
        return {{"name", "Wood"}, {"visual", "bsmt_woodExotic"}, {"weight", 2.0}, {"price", 12}};
    }

    inline nlohmann::json Own() {
        return {{"name", "Box"}, {"model", "kcdc/qa/box.cgf"}, {"icon", kTinyPng}, {"weight", 1}};
    }

    inline nlohmann::json With(nlohmann::json definition, const char *key, const nlohmann::json &value) {
        definition[key] = value;
        return definition;
    }

    inline nlohmann::json Without(nlohmann::json definition, const char *key) {
        definition.erase(key);
        return definition;
    }

    inline void BigEndian(std::string &out, std::uint32_t value) {
        for (int shift = 24; shift >= 0; shift -= 8) {
            out.push_back(static_cast<char>((value >> shift) & 0xFF));
        }
    }

    inline void Chunk(std::string &png, const char *type, const std::string &data) {
        const std::string body = std::string(type, 4) + data;
        BigEndian(png, static_cast<std::uint32_t>(data.size()));
        png += body;
        BigEndian(png, Framework::Utils::Hashing::CalculateCRC32(body.data(), body.size()));
    }

    /** The bytes of an empty IDAT chunk in a Png, and where it starts: after the signature and IHDR. */
    constexpr std::size_t kEmptyDataChunk = 12;
    constexpr std::size_t kDataChunkAt    = 33;

    /**
     * A PNG for `width` x `height` whose chunks hold together -- IHDR, an IDAT
     * zero-filled so the whole is `size` bytes, IEND -- which is all the
     * registry reads of one: it does not inflate the image data.
     */
    inline std::string Png(std::uint32_t width, std::uint32_t height, std::size_t size = 57) {
        std::string header;
        BigEndian(header, width);
        BigEndian(header, height);
        header += std::string("\x08\x06\x00\x00\x00", 5); // 8-bit RGBA, no interlace
        std::string png("\x89PNG\r\n\x1a\n", 8);
        Chunk(png, "IHDR", header);
        const std::size_t framing = png.size() + 2 * kEmptyDataChunk;
        Chunk(png, "IDAT", std::string(size > framing ? size - framing : 0, '\0'));
        Chunk(png, "IEND", "");
        return png;
    }

    inline std::string Base64(const std::string &bytes) {
        return Framework::Utils::StringUtils::Base64Encode(bytes);
    }

    /** The registry's GUID shape, rebuilt here so a change to it shows as a test failure, not as silently new identities. */
    inline std::string GuidOf(const std::string &key) {
        const std::string hash = Framework::Utils::Crypto::Sha256Hex(key);
        return hash.substr(0, 8) + "-" + hash.substr(8, 4) + "-" + hash.substr(12, 4) + "-" + hash.substr(16, 4) + "-" + hash.substr(20, 12);
    }

    /** Opaque pixels numbered in reading order: pixel n of the image is {n + 1, n + 1, n + 1, 255}. */
    inline std::vector<std::uint8_t> Numbered(std::uint32_t width, std::uint32_t height) {
        std::vector<std::uint8_t> rgba;
        for (std::uint32_t n = 0; n < width * height; ++n) {
            rgba.insert(rgba.end(), {static_cast<std::uint8_t>(n + 1), static_cast<std::uint8_t>(n + 1), static_cast<std::uint8_t>(n + 1), 255});
        }
        return rgba;
    }

    /** FitImage onto the icon square as {width, height, left, top}. */
    inline std::vector<std::uint32_t> Fit(std::uint32_t width, std::uint32_t height) {
        const KCDC::Shared::Image::ImageFit fit = KCDC::Shared::Image::FitImage(width, height, KCDC::Shared::Game::kCustomItemIconSide);
        return {fit.width, fit.height, fit.left, fit.top};
    }
} // namespace CustomItemRegistryTests

MODULE(custom_item_registry, {
    auto &registry = KCDC::Shared::Game::CustomItemRegistry::Get();

    IT("accepts the text, weight and price bounds and refuses everything past them", {
        const std::vector<nlohmann::json> accepted = {
            CustomItemRegistryTests::Borrowed(),
            CustomItemRegistryTests::With(CustomItemRegistryTests::Borrowed(), "name", std::string(256, 'n')),
            CustomItemRegistryTests::With(CustomItemRegistryTests::Borrowed(), "description", std::string(4096, 'd')),
            CustomItemRegistryTests::With(CustomItemRegistryTests::Borrowed(), "useLabel", std::string(64, 'u')),
            CustomItemRegistryTests::With(CustomItemRegistryTests::Borrowed(), "weight", 0),
            CustomItemRegistryTests::With(CustomItemRegistryTests::Borrowed(), "weight", 1000000),
            CustomItemRegistryTests::With(CustomItemRegistryTests::Borrowed(), "price", 0),
            CustomItemRegistryTests::With(CustomItemRegistryTests::Borrowed(), "price", 2147483583U),
        };
        for (const auto &definition : accepted) {
            registry.Reset();
            EQUALS(CustomItemRegistryTests::Rejects(definition), false);
        }
        registry.Reset();
        const std::vector<nlohmann::json> refused = {
            CustomItemRegistryTests::Without(CustomItemRegistryTests::Borrowed(), "name"),
            CustomItemRegistryTests::With(CustomItemRegistryTests::Borrowed(), "name", ""),
            CustomItemRegistryTests::With(CustomItemRegistryTests::Borrowed(), "name", std::string(257, 'n')),
            CustomItemRegistryTests::With(CustomItemRegistryTests::Borrowed(), "name", "Wood\nPlank"),
            CustomItemRegistryTests::With(CustomItemRegistryTests::Borrowed(), "name", 7),
            CustomItemRegistryTests::With(CustomItemRegistryTests::Borrowed(), "description", std::string(4097, 'd')),
            CustomItemRegistryTests::With(CustomItemRegistryTests::Borrowed(), "useLabel", ""),
            CustomItemRegistryTests::With(CustomItemRegistryTests::Borrowed(), "useLabel", std::string(65, 'u')),
            CustomItemRegistryTests::Without(CustomItemRegistryTests::Borrowed(), "weight"),
            CustomItemRegistryTests::With(CustomItemRegistryTests::Borrowed(), "weight", -0.5),
            CustomItemRegistryTests::With(CustomItemRegistryTests::Borrowed(), "weight", 1000000.5),
            CustomItemRegistryTests::With(CustomItemRegistryTests::Borrowed(), "weight", "2"),
            CustomItemRegistryTests::With(CustomItemRegistryTests::Borrowed(), "price", -1),
            CustomItemRegistryTests::With(CustomItemRegistryTests::Borrowed(), "price", 1.5),
            CustomItemRegistryTests::With(CustomItemRegistryTests::Borrowed(), "price", 2147483584U),
            CustomItemRegistryTests::With(CustomItemRegistryTests::Borrowed(), "stackable", "yes"),
            CustomItemRegistryTests::With(CustomItemRegistryTests::Borrowed(), "colour", "red"),
            nlohmann::json::array(),
        };
        for (const auto &definition : refused) {
            EQUALS(CustomItemRegistryTests::Rejects(definition), true);
        }
        EQUALS(registry.Revision(), static_cast<std::uint64_t>(0));
    });

    IT("borrows from an optional visual and needs an own model and icon without one", {
        registry.Reset();
        const auto &borrowed = registry.Register("qa:borrowed", CustomItemRegistryTests::Borrowed());
        EQUALS(borrowed.visual.empty(), false);
        EQUALS((borrowed.model.empty() && borrowed.material.empty() && borrowed.icon.empty() && borrowed.iconId.empty()), true);

        const auto &own = registry.Register("qa:own", CustomItemRegistryTests::Own());
        EQUALS(own.visual.empty(), true);
        EQUALS(own.model, std::string("kcdc/qa/box.cgf"));
        EQUALS(own.material.empty(), true);
        EQUALS(own.iconId.empty(), false);

        // A visual with its own model: the model replaces the visual's, and both are kept.
        const auto &mixed = registry.Register("qa:mixed", CustomItemRegistryTests::With(CustomItemRegistryTests::Borrowed(), "model", "kcdc/qa/box.cgf"));
        EQUALS(mixed.visual, borrowed.visual);
        EQUALS(mixed.model, std::string("kcdc/qa/box.cgf"));

        EQUALS(CustomItemRegistryTests::Rejects(CustomItemRegistryTests::Without(CustomItemRegistryTests::Own(), "icon")), true);
        EQUALS(CustomItemRegistryTests::Rejects(CustomItemRegistryTests::Without(CustomItemRegistryTests::Own(), "model")), true);
        EQUALS(CustomItemRegistryTests::Rejects(CustomItemRegistryTests::With(CustomItemRegistryTests::Borrowed(), "material", "kcdc/qa/box")), true);
        EQUALS(CustomItemRegistryTests::Rejects(CustomItemRegistryTests::With(CustomItemRegistryTests::Own(), "visual", "unknown visual")), true);
        EQUALS(CustomItemRegistryTests::Rejects(CustomItemRegistryTests::With(CustomItemRegistryTests::Own(), "visual", 5)), true);
        // A weapon is not a look a MiscItem can borrow.
        EQUALS(CustomItemRegistryTests::Rejects(CustomItemRegistryTests::With(CustomItemRegistryTests::Borrowed(), "visual", "artefakt_shortswordBavor")), true);
        registry.Reset();
    });

    IT("takes model and material paths the way the item tables write them, relative to Objects/", {
        registry.Reset();
        // The game adds "Objects/" in front, and ".cgf" to a model without an extension.
        const std::vector<std::pair<std::string, std::string>> accepted = {
            {"kcdc/qa/box.cgf", ""},
            {"kcdc/qa/box", ""},
            {"kcdc/qa/box", "kcdc/qa/box"},
            {"kcdc/qa/box.cgf", "kcdc/qa/box.mtl"},
            {"manmade/common_tools/hammer", ""},
            {"Kcdc/QA/Box_02-b.cgf", "Kcdc/QA/Box_02-b"},
        };
        int index = 0;
        for (const auto &[model, material] : accepted) {
            auto definition = CustomItemRegistryTests::With(CustomItemRegistryTests::Own(), "model", model);
            if (!material.empty()) {
                definition["material"] = material;
            }
            const auto &registered = registry.Register("qa:path-" + std::to_string(index++), definition);
            EQUALS(registered.model, model);
            EQUALS(registered.material, material);
        }
        const std::vector<std::string> refused = {"", "objects/kcdc/qa/box.cgf", "Objects/kcdc/qa/box.cgf", "OBJECTS/kcdc/qa/box", "/kcdc/qa/box.cgf", "../kcdc/qa/box.cgf", "kcdc/../../box.cgf", "kcdc\\qa\\box.cgf", "kcdc/qa/b ox.cgf", "kcdc/qa/box.cgf\n", "C:/kcdc/box.cgf",
            std::string(257, 'a')};
        for (const auto &path : refused) {
            EQUALS(CustomItemRegistryTests::Rejects(CustomItemRegistryTests::With(CustomItemRegistryTests::Own(), "model", path)), true);
            EQUALS(CustomItemRegistryTests::Rejects(CustomItemRegistryTests::With(CustomItemRegistryTests::Own(), "material", path)), true);
        }
        EQUALS(CustomItemRegistryTests::Rejects(CustomItemRegistryTests::With(CustomItemRegistryTests::Own(), "model", 3)), true);
        registry.Reset();
    });

    IT("takes a canonical base64 PNG up to 256 KiB and 4096x4096 pixels as the icon", {
        registry.Reset();
        const auto &tiny = registry.Register("qa:tiny", CustomItemRegistryTests::Own());
        EQUALS(tiny.icon, std::string(CustomItemRegistryTests::kTinyPng));
        EQUALS(tiny.iconPng.size(), static_cast<std::size_t>(70));
        EQUALS(CustomItemRegistryTests::Base64(tiny.iconPng), tiny.icon);

        const std::vector<std::string> accepted = {
            CustomItemRegistryTests::Png(1, 1),
            CustomItemRegistryTests::Png(4096, 4096),
            CustomItemRegistryTests::Png(4096, 1),
            CustomItemRegistryTests::Png(64, 64, 256 * 1024),
        };
        EQUALS(CustomItemRegistryTests::Png(64, 64, 256 * 1024).size(), static_cast<std::size_t>(256 * 1024));
        int index = 0;
        for (const auto &png : accepted) {
            const auto &registered = registry.Register("qa:icon-" + std::to_string(index++), CustomItemRegistryTests::With(CustomItemRegistryTests::Own(), "icon", CustomItemRegistryTests::Base64(png)));
            EQUALS(registered.iconPng, png);
        }

        std::string withWhitespace = CustomItemRegistryTests::kTinyPng;
        withWhitespace.insert(8, "    ");
        std::string loosePadding = CustomItemRegistryTests::kTinyPng;
        loosePadding.replace(loosePadding.size() - 3, 1, "h"); // the same bytes with bits set past the end: not canonical
        const std::string whole = CustomItemRegistryTests::Png(1, 1);
        std::string badCrc      = whole;
        badCrc[CustomItemRegistryTests::kDataChunkAt - 1] ^= 1; // the last byte of IHDR's CRC
        std::string noData = whole;
        noData.erase(CustomItemRegistryTests::kDataChunkAt, CustomItemRegistryTests::kEmptyDataChunk);
        const std::vector<nlohmann::json> refused = {
            "",
            "====",
            "iVBORw0KGgo",      // not a multiple of four
            "iVBORw0KGgo=AAAA", // padding in the middle
            withWhitespace,
            loosePadding,
            std::string("data:image/png;base64,") + CustomItemRegistryTests::kTinyPng,
            CustomItemRegistryTests::Base64("GIF89a"),
            CustomItemRegistryTests::Base64(std::string("\x89PNG\r\n\x1a\n", 8)), // signature without a header
            CustomItemRegistryTests::Base64(whole.substr(0, 23)),                  // header cut short
            CustomItemRegistryTests::Base64(whole.substr(0, 24)),                  // the sides and nothing after them
            CustomItemRegistryTests::Base64(whole.substr(0, whole.size() - 12)),   // no IEND
            CustomItemRegistryTests::Base64(whole + "x"),                          // bytes past IEND
            CustomItemRegistryTests::Base64(badCrc),
            CustomItemRegistryTests::Base64(noData),
            CustomItemRegistryTests::Base64(CustomItemRegistryTests::Png(0, 1)),
            CustomItemRegistryTests::Base64(CustomItemRegistryTests::Png(1, 0)),
            CustomItemRegistryTests::Base64(CustomItemRegistryTests::Png(4097, 1)),
            CustomItemRegistryTests::Base64(CustomItemRegistryTests::Png(1, 4097)),
            CustomItemRegistryTests::Base64(CustomItemRegistryTests::Png(0xFFFFFFFFU, 0xFFFFFFFFU)),
            CustomItemRegistryTests::Base64(CustomItemRegistryTests::Png(64, 64, 256 * 1024 + 1)),
            7,
            nlohmann::json::object(),
        };
        for (const auto &icon : refused) {
            EQUALS(CustomItemRegistryTests::Rejects(CustomItemRegistryTests::With(CustomItemRegistryTests::Own(), "icon", icon)), true);
            EQUALS(CustomItemRegistryTests::Rejects(CustomItemRegistryTests::With(CustomItemRegistryTests::Borrowed(), "icon", icon)), true);
        }
        registry.Reset();
    });

    IT("names an icon by its bytes, the same on every peer and registration", {
        registry.Reset();
        const auto &first  = registry.Register("qa:first", CustomItemRegistryTests::Own());
        const auto &second = registry.Register("qa:second", CustomItemRegistryTests::With(CustomItemRegistryTests::Borrowed(), "icon", CustomItemRegistryTests::kTinyPng));
        EQUALS(first.iconId, std::string("kcdc_ic_") + Framework::Utils::Crypto::Sha256Hex(first.iconPng).substr(0, 16));
        EQUALS(second.iconId, first.iconId);
        const auto &other = registry.Register("qa:other", CustomItemRegistryTests::With(CustomItemRegistryTests::Own(), "icon", CustomItemRegistryTests::Base64(CustomItemRegistryTests::Png(2, 2))));
        NEQUALS(other.iconId, first.iconId);
        const std::string iconId = first.iconId;
        registry.Reset();
        EQUALS(registry.Register("qa:first", CustomItemRegistryTests::Own()).iconId, iconId);
        registry.Reset();
    });

    IT("keeps every identity a function of the key and the definition", {
        registry.Reset();
        // The logical GUID is the key's alone, so a type keeps it when its look changes.
        const auto &borrowed = registry.Register("qa:wood", CustomItemRegistryTests::Borrowed());
        EQUALS(borrowed.guid, CustomItemRegistryTests::GuidOf("KCDC/custom-item/id/qa:wood"));
        // A definition that borrows its whole look encodes exactly as before the
        // own-look fields existed, so the native classes it named stay the same.
        const nlohmann::json before = {{"name", "Wood"}, {"description", ""}, {"visual", borrowed.visual}, {"weight", 2.0F}, {"price", 12U}, {"stackable", true}, {"usable", false}, {"useLabel", "Use"}};
        EQUALS((borrowed.Encode() == before), true);
        EQUALS(borrowed.nativeGuid, CustomItemRegistryTests::GuidOf("KCDC/custom-item/native/qa:wood/" + before.dump()));

        const auto &own             = registry.Register("qa:own", CustomItemRegistryTests::Own());
        const std::string ownGuid   = own.guid;
        const std::string ownNative = own.nativeGuid;
        EQUALS(ownGuid, CustomItemRegistryTests::GuidOf("KCDC/custom-item/id/qa:own"));
        EQUALS((own.Encode().contains("visual") || own.Encode().contains("material")), false);
        EQUALS((own.Encode().at("model") == "kcdc/qa/box.cgf"), true);
        EQUALS((own.Encode().at("icon") == CustomItemRegistryTests::kTinyPng), true);
        const auto &twin = registry.Register("qa:own-twin", CustomItemRegistryTests::Own());
        NEQUALS(twin.guid, ownGuid);
        NEQUALS(twin.nativeGuid, ownNative);
        NEQUALS(ownNative, ownGuid);

        // A server restart registers the same key and definition again: the same identities.
        registry.Reset();
        const auto &restarted = registry.Register("qa:own", CustomItemRegistryTests::Own());
        EQUALS(restarted.guid, ownGuid);
        EQUALS(restarted.nativeGuid, ownNative);
        // Any change to the look is a new native class under the same logical GUID.
        registry.Reset();
        const auto &remodelled = registry.Register("qa:own", CustomItemRegistryTests::With(CustomItemRegistryTests::Own(), "model", "kcdc/qa/box"));
        EQUALS(remodelled.guid, ownGuid);
        NEQUALS(remodelled.nativeGuid, ownNative);
        registry.Reset();
        const auto &rematerialled = registry.Register("qa:own", CustomItemRegistryTests::With(CustomItemRegistryTests::Own(), "material", "kcdc/qa/box"));
        NEQUALS(rematerialled.nativeGuid, ownNative);
        registry.Reset();
    });

    IT("rebuilds the same definition from the message a client receives", {
        registry.Reset();
        auto full = CustomItemRegistryTests::Own();
        full.update({{"description", "A box of the village"}, {"material", "kcdc/qa/box"}, {"weight", 0.1}, {"price", 9}, {"stackable", false}, {"usable", true}, {"useLabel", "Open"}});
        const auto &sent                                        = registry.Register("qa:box", full);
        const std::string body                                  = nlohmann::json {{"id", sent.id}, {"definition", sent.Encode()}}.dump();
        const KCDC::Shared::Game::CustomItemDefinition expected = sent;

        registry.Reset(); // the client's registry starts empty
        const auto message   = nlohmann::json::parse(body);
        const auto &received = registry.Register(message.at("id").get<std::string>(), message.at("definition"));
        EQUALS(received.id, expected.id);
        EQUALS(received.guid, expected.guid);
        EQUALS(received.nativeGuid, expected.nativeGuid);
        EQUALS(received.name, expected.name);
        EQUALS(received.description, expected.description);
        EQUALS(received.visual, expected.visual);
        EQUALS(received.model, expected.model);
        EQUALS(received.material, expected.material);
        EQUALS(received.icon, expected.icon);
        EQUALS(received.iconPng, expected.iconPng);
        EQUALS(received.iconId, expected.iconId);
        EQUALS(received.weight, expected.weight);
        EQUALS(received.price, expected.price);
        EQUALS(received.stackable, expected.stackable);
        EQUALS(received.usable, expected.usable);
        EQUALS(received.useLabel, expected.useLabel);
        EQUALS((received.Encode() == expected.Encode()), true);

        // The same holds for a type that borrows its look.
        registry.Reset();
        const auto &borrowed             = registry.Register("qa:wood", CustomItemRegistryTests::Borrowed());
        const std::string borrowedBody   = nlohmann::json {{"id", borrowed.id}, {"definition", borrowed.Encode()}}.dump();
        const std::string borrowedNative = borrowed.nativeGuid;
        registry.Reset();
        const auto borrowedMessage = nlohmann::json::parse(borrowedBody);
        EQUALS(registry.Register(borrowedMessage.at("id").get<std::string>(), borrowedMessage.at("definition")).nativeGuid, borrowedNative);
        registry.Reset();
    });

    IT("fits an icon's longer side to the stock 64 px, aspect kept and centred", {
        EQUALS(KCDC::Shared::Game::kCustomItemIconSide, 64U);
        EQUALS((CustomItemRegistryTests::Fit(64, 64) == std::vector<std::uint32_t> {64, 64, 0, 0}), true);
        EQUALS((CustomItemRegistryTests::Fit(1, 1) == std::vector<std::uint32_t> {64, 64, 0, 0}), true);
        EQUALS((CustomItemRegistryTests::Fit(4096, 4096) == std::vector<std::uint32_t> {64, 64, 0, 0}), true);
        EQUALS((CustomItemRegistryTests::Fit(128, 64) == std::vector<std::uint32_t> {64, 32, 0, 16}), true);
        EQUALS((CustomItemRegistryTests::Fit(64, 128) == std::vector<std::uint32_t> {32, 64, 16, 0}), true);
        // The 128x126 PNG of the first in-game test.
        EQUALS((CustomItemRegistryTests::Fit(128, 126) == std::vector<std::uint32_t> {64, 63, 0, 0}), true);
        // 21.33 rounds to 21; the odd pixel row goes below.
        EQUALS((CustomItemRegistryTests::Fit(300, 100) == std::vector<std::uint32_t> {64, 21, 0, 21}), true);
        // 42.67 rounds to 43.
        EQUALS((CustomItemRegistryTests::Fit(3, 2) == std::vector<std::uint32_t> {64, 43, 0, 10}), true);
        // Never thinner than a pixel.
        EQUALS((CustomItemRegistryTests::Fit(4096, 1) == std::vector<std::uint32_t> {64, 1, 0, 31}), true);
        EQUALS((CustomItemRegistryTests::Fit(1, 4096) == std::vector<std::uint32_t> {1, 64, 31, 0}), true);
    });

    IT("centres the scaled pixels on a transparent stock square", {
        const KCDC::Shared::Image::ImageFit wide         = KCDC::Shared::Image::FitImage(128, 64, KCDC::Shared::Game::kCustomItemIconSide);
        const std::vector<std::uint8_t> scaled           = CustomItemRegistryTests::Numbered(wide.width, wide.height);
        const std::vector<std::uint8_t> square           = KCDC::Shared::Image::CenterImage(scaled, wide, KCDC::Shared::Game::kCustomItemIconSide);
        EQUALS(square.size(), static_cast<std::size_t>(64 * 64 * 4));
        // The rows above and below the image stay fully transparent, and every
        // scaled row lands whole and in order from the fit's corner.
        bool clear  = true;
        bool placed = true;
        for (std::size_t row = 0; row < 64; ++row) {
            const bool inside = row >= wide.top && row < wide.top + wide.height;
            for (std::size_t byte = 0; byte < 64 * 4; ++byte) {
                clear = clear && (inside || square[row * 64 * 4 + byte] == 0);
            }
            for (std::size_t byte = 0; inside && byte < static_cast<std::size_t>(wide.width) * 4; ++byte) {
                placed = placed && square[(row * 64 + wide.left) * 4 + byte] == scaled[(row - wide.top) * wide.width * 4 + byte];
            }
        }
        EQUALS(clear, true);
        EQUALS(placed, true);

        // A tall image leaves the columns on either side transparent.
        const KCDC::Shared::Image::ImageFit tall         = KCDC::Shared::Image::FitImage(1, 4096, KCDC::Shared::Game::kCustomItemIconSide);
        const std::vector<std::uint8_t> column           = KCDC::Shared::Image::CenterImage(CustomItemRegistryTests::Numbered(tall.width, tall.height), tall, KCDC::Shared::Game::kCustomItemIconSide);
        bool onlyColumn                                  = true;
        for (std::size_t row = 0; row < 64; ++row) {
            for (std::size_t x = 0; x < 64; ++x) {
                onlyColumn = onlyColumn && column[(row * 64 + x) * 4 + 3] == (x == 31 ? 255 : 0);
            }
        }
        EQUALS(onlyColumn, true);
        // Row 5 of the column is pixel number 6.
        EQUALS(column[(5 * 64 + 31) * 4], static_cast<std::uint8_t>(6));
    });
});
