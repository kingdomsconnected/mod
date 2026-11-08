#pragma once

#include <utility>

namespace SDK {
    class IEntity;

    /**
     * IEntityIt, vtable 0x183A772A8; CEntityItMap's implementation is at
     * 0x184077008. All seven slots are mapped in their KCD2 order.
     */
    class IEntityIt {
      public:
        virtual ~IEntityIt() = default; // +0x00
        virtual void AddRef() = 0;
        virtual void Release() = 0;
        virtual bool IsEnd() = 0;
        virtual IEntity *Next() = 0;
        virtual IEntity *This() = 0;
        virtual void MoveFirst() = 0;
    };

    class IEntityItPtr {
      public:
        IEntityItPtr() = default;

        explicit IEntityItPtr(IEntityIt *iterator): pIterator(iterator) {
            AddReference();
        }

        IEntityItPtr(const IEntityItPtr &other): pIterator(other.pIterator) {
            AddReference();
        }

        IEntityItPtr(IEntityItPtr &&other) noexcept: pIterator(other.pIterator) {
            other.pIterator = nullptr;
        }

        ~IEntityItPtr() {
            ReleaseReference();
        }

        IEntityItPtr &operator=(IEntityItPtr other) noexcept {
            Swap(other);
            return *this;
        }

        IEntityIt *Get() const { return pIterator; }
        IEntityIt *operator->() const { return pIterator; }
        explicit operator bool() const { return pIterator != nullptr; }

        void Swap(IEntityItPtr &other) noexcept {
            std::swap(pIterator, other.pIterator);
        }

      private:
        void AddReference() {
            if (pIterator) {
                pIterator->AddRef();
            }
        }

        void ReleaseReference() {
            if (pIterator) {
                pIterator->Release();
            }
        }

        IEntityIt *pIterator = nullptr;
    };

    static_assert(sizeof(IEntityItPtr) == 0x8, "IEntityItPtr size mismatch");
} // namespace SDK
