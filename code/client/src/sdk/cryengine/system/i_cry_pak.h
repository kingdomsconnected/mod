#pragma once

namespace SDK {
    /** Native virtual filesystem interface; primary table 0x183A97328. */
    struct ICryPak {
        virtual ~ICryPak()        = default; // 0
        virtual void unk_fn0001() = 0;
        virtual void unk_fn0002() = 0;
        virtual void unk_fn0003() = 0;
        virtual void unk_fn0004() = 0;
        virtual void unk_fn0005() = 0;
        virtual void unk_fn0006() = 0;
        virtual void unk_fn0007() = 0;
        /**
         * Slot 8 (0x1804D92A8). Takes the path the pack was opened with.
         * False means no mounted pack matched, which a second close of the
         * same archive also reports.
         */
        virtual bool ClosePack(const char *name, unsigned int flags = 0x10000u) = 0;
        virtual void unk_fn0009()                                               = 0;
        virtual void unk_fn0010()                                               = 0;
        virtual void unk_fn0011()                                               = 0;
        virtual void unk_fn0012()                                               = 0;
        virtual void unk_fn0013()                                               = 0;
        virtual void unk_fn0014()                                               = 0;
        /**
         * Slot 15 (0x18241DC78). Flips FLAGS_DISABLE_PAK (0x800) on the mounted pack at `name`,
         * the bit `FindPakFileEntry` skips. Takes only the pak list's lock: it cannot block and
         * cannot fail on a mounted pack, unlike ClosePack, which refuses while anything holds one.
         */
        virtual bool SetPackAccessible(bool accessible, const char *name, unsigned int flags = 0x10000u) = 0;
        virtual void unk_fn0016()                                               = 0;
        virtual void unk_fn0017()                                               = 0;
        virtual void unk_fn0018()                                               = 0;
        virtual void unk_fn0019()                                               = 0;
        virtual void unk_fn0020()                                               = 0;
        virtual void unk_fn0021()                                               = 0;
        virtual void unk_fn0022()                                               = 0;
        virtual void unk_fn0023()                                               = 0;
        virtual void unk_fn0024()                                               = 0;
        virtual void unk_fn0025()                                               = 0;
        virtual void unk_fn0026()                                               = 0;
        virtual void unk_fn0027()                                               = 0;
        virtual void unk_fn0028()                                               = 0;
        virtual const char *GetGameFolder() const                               = 0; // 29

        static ICryPak *GetInstance();

        /**
         * Mount under the native game root. Path must be absolute.
         * Success can mean already mounted; it grants no separate ownership.
         * Call during setup, never from a movie callback.
         */
        bool MountArchive(const char *absolutePath);

        /**
         * Mounts with an explicit binding root and flags. The root is normalised the way the
         * public OpenPack normalises it; a pack is consulted only for lookups the root prefixes.
         * 0x400 (FLAGS_OVERRIDE_PAK) puts the pack above every non-override one, the level's
         * own paks included.
         */
        bool MountArchive(const char *bindRoot, const char *absolutePath, unsigned int flags);

        /** Undoes MountArchive for the same absolute path. */
        bool UnmountArchive(const char *absolutePath);
    };
    static_assert(sizeof(ICryPak) == 0x8);
} // namespace SDK
