#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

// String encoding and control-flow primitives for the shipped client DLL.
//
// KCDC_HIDE("literal") stores a per-string encoded blob in .rdata and
// decodes it into a stack buffer at the call site. Each expansion draws a
// distinct key from __COUNTER__ and a per-build seed. The result converts to
// std::string_view and exposes c_str() for pattern scanning and logging.
//
// KCDC_OPAQUE_TRUE / KCDC_JUNK use a volatile seed to prevent constant folding.
// The predicate always returns true and the junk statement changes no external
// state. These operations do not affect the CRT-startup boot path.
//
// Everything here is a no-op passthrough unless KCDC_OBFUSCATE is defined.
// The flag is set by the client CMake for Release only.

namespace KCDC::Obf {
    constexpr std::uint32_t FnvHash(const char *s) {
        std::uint32_t h = 0x811C9DC5u;
        while (*s) {
            h = (h ^ static_cast<std::uint8_t>(*s)) * 0x01000193u;
            ++s;
        }
        return h;
    }

    // Seeded from the build clock so the same source yields different keystreams across builds.
    inline constexpr std::uint32_t kBuildSeed = FnvHash(__DATE__) ^ (FnvHash(__TIME__) * 0x9E3779B9u);

    constexpr std::uint8_t Keystream(std::uint32_t key, std::size_t index) {
        std::uint32_t x = key + static_cast<std::uint32_t>(index) * 0x9E3779B9u;
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        return static_cast<std::uint8_t>(x);
    }

    // Consumed only in constant evaluation, so the plaintext argument is never emitted into the image.
    template <std::size_t N>
    constexpr std::array<char, N> Encrypt(const char (&plain)[N + 1], std::uint32_t key) {
        std::array<char, N> out{};
        for (std::size_t i = 0; i < N; ++i) {
            out[i] = static_cast<char>(static_cast<std::uint8_t>(plain[i]) ^ Keystream(key, i));
        }
        return out;
    }

    template <std::size_t N>
    struct Decryptor final {
        std::array<char, N + 1> buffer{};

        Decryptor(const char *cipher, std::uint32_t key) {
            for (std::size_t i = 0; i < N; ++i) {
                buffer[i] = static_cast<char>(static_cast<std::uint8_t>(cipher[i]) ^ Keystream(key, i));
            }
            buffer[N] = '\0';
        }

        operator std::string_view() const {
            return std::string_view{buffer.data(), N};
        }
        const char *c_str() const {
            return buffer.data();
        }
    };

    // Read through a volatile so the opaque predicate below cannot be constant-folded by the optimizer.
    inline volatile std::uint32_t gOpaqueSeed = 0x1u;

    // Opaque identity: returns `value` unchanged -- the mask is always zero, because the low bit of
    // n*(n+1) is always clear -- but through a volatile-fed computation the optimizer cannot fold. Use
    // it for an integer or bool result that must retain this runtime computation.
    template <typename T>
    inline T OpaqueValue(T value) {
        return static_cast<T>(value ^ static_cast<T>((gOpaqueSeed * (gOpaqueSeed + 1u)) & 1u));
    }
} // namespace KCDC::Obf

#if defined(KCDC_OBFUSCATE)
    #define KCDC_HIDE(str) ([]() -> ::KCDC::Obf::Decryptor<sizeof(str) - 1> { constexpr std::uint32_t kwKey = (static_cast<std::uint32_t>(__COUNTER__) * 2654435761u + 0x51ED270Bu) ^ ::KCDC::Obf::kBuildSeed; static constexpr auto kwCipher = ::KCDC::Obf::Encrypt<sizeof(str) - 1>(str, kwKey); return ::KCDC::Obf::Decryptor<sizeof(str) - 1>{kwCipher.data(), kwKey}; }())
    #define KCDC_OPAQUE_TRUE ((::KCDC::Obf::gOpaqueSeed * (::KCDC::Obf::gOpaqueSeed + 1u)) % 2u == 0u)
    #define KCDC_JUNK do { volatile std::uint32_t kwJunk = ::KCDC::Obf::gOpaqueSeed; kwJunk = (kwJunk << 7) ^ (kwJunk >> 3); (void)kwJunk; } while (0)
    #define KCDC_OBFI(x) (::KCDC::Obf::OpaqueValue(x))
#else
    #define KCDC_HIDE(str) (::std::string_view{str})
    #define KCDC_OPAQUE_TRUE (true)
    #define KCDC_JUNK do {} while (0)
    #define KCDC_OBFI(x) (x)
#endif
