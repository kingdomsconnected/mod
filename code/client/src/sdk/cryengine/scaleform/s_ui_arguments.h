#pragma once

#include "../../types.h"

#include <cstring>

namespace SDK {
    /** Borrowed view of KCD2 callback arguments; never owns native buffers. */
    class SUIArguments {
      public:
        SUIArguments()                                = delete;
        SUIArguments(const SUIArguments &)            = delete;
        SUIArguments &operator=(const SUIArguments &) = delete;

        class Argument {
          public:
            int GetDeclaredType() const { return m_declaredType; }
            int GetStoredType() const { return m_storedType; }
            bool ReadInt(int &value) const { return Read(0, value); }
            bool ReadFloat(float &value) const { return Read(1, value); }
            bool ReadUInt(unsigned int &value) const { return Read(2, value); }
            bool ReadString(const char *&value) const { return Read(4, value); }
            bool ReadWideString(const wchar_t *&value) const { return Read(5, value); }
            bool ReadBool(bool &value) const { return Read(6, value); }

          private:
            int m_declaredType;
            std::byte m_opaque0004[4];
            alignas(8) std::byte m_payload[16];
            signed char m_storedType;
            std::byte m_opaque0019[7];

            template <typename T>
            bool Read(int type, T &value) const {
                if (m_storedType != type) {
                    return false;
                }
                static_assert(sizeof(T) <= sizeof(m_payload));
                std::memcpy(&value, m_payload, sizeof(T));
                return true;
            }

            static void AssertLayout() {
                static_assert(offsetof(Argument, m_declaredType) == 0x0);
                static_assert(offsetof(Argument, m_payload) == 0x8);
                static_assert(offsetof(Argument, m_storedType) == 0x18);
            }
        };
        static_assert(sizeof(Argument) == 0x20);

        int GetArgCount() const { return GetHeader().count; }

        /** Returned record and any string payload expire with the callback. */
        const Argument *GetArgument(int index) const { return index >= 0 && index < GetArgCount() ? m_arguments + index : nullptr; }

      private:
        /** Native resize at 0x1803C2238 proves the two-word array prefix. */
        struct ArrayHeader {
            int capacity;
            int count;
        };
        static_assert(sizeof(ArrayHeader) == 8);
        static_assert(offsetof(ArrayHeader, capacity) == 0);
        static_assert(offsetof(ArrayHeader, count) == 4);

        const ArrayHeader &GetHeader() const { return *(reinterpret_cast<const ArrayHeader *>(m_arguments) - 1); }

        const Argument *m_arguments;
        CryStringT<char> m_stringCache;
        CryStringT<wchar_t> m_wideStringCache;
        const void *m_flashValueCache;
        CryStringT<char> m_delimiter;
        unsigned int m_dirtyFlags;
        std::byte m_opaque002C[4];

        static void AssertLayout() {
            static_assert(offsetof(SUIArguments, m_arguments) == 0x0);
            static_assert(offsetof(SUIArguments, m_stringCache) == 0x8);
            static_assert(offsetof(SUIArguments, m_wideStringCache) == 0x10);
            static_assert(offsetof(SUIArguments, m_flashValueCache) == 0x18);
            static_assert(offsetof(SUIArguments, m_delimiter) == 0x20);
            static_assert(offsetof(SUIArguments, m_dirtyFlags) == 0x28);
        }
    };
    static_assert(sizeof(SUIArguments) == 0x30);

    /**
     * An SUIArguments the mod owns, for a native that reads one. Built and
     * destroyed through the engine's own constructor (0x1807F7170) and
     * destructor (0x1807F71C8), and filled through its own append, so every
     * buffer inside it comes from and returns to the game's allocator; the
     * mod only provides the storage. The same arrangement as ScopedCryString.
     */
    class ScopedUIArguments final {
      public:
        ScopedUIArguments();
        ~ScopedUIArguments();

        ScopedUIArguments(const ScopedUIArguments &)            = delete;
        ScopedUIArguments &operator=(const ScopedUIArguments &) = delete;

        /** Appends one string argument, copied into the engine's own storage. */
        void AppendString(const char *value);

        /** Appends one unsigned integer (0x1803C1D4C), the type `Images` pages are declared with. */
        void AppendUInt(unsigned int value);

        const SUIArguments &Get() const {
            return *reinterpret_cast<const SUIArguments *>(m_storage);
        }

      private:
        /**
         * The string tag the game itself appends with (0x1805582D4). It is
         * Warhorse's numbering: stock CryEngine's EUIDataTypes counts from
         * bool and puts the string later.
         */
        static constexpr int kStringType = 4;

        alignas(8) std::byte m_storage[sizeof(SUIArguments)];
    };
} // namespace SDK
