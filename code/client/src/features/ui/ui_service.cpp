#include "ui_service.h"

#include "core/application.h"

#include "shared/features/inventory/item_name_message.h"

#include <core_modules.h>
#include <networking/network_peer.h>

#include <cstring>
#include <string_view>
#include <utility>

namespace KCDC::Features::UI {
    std::size_t UiService::PublishText(std::string text) {
        std::scoped_lock lock(_publishedMutex);

        const std::size_t slot = _publishedCursor;
        _publishedCursor       = (_publishedCursor + 1) % kPublishedSlots;
        _published[slot]       = std::move(text);

        return slot;
    }

    void UiService::AssignPublishedText(std::size_t slot, SDK::CryStringT<char> &out) const {
        std::scoped_lock lock(_publishedMutex);
        out.Assign(_published[slot].c_str());
    }

    void UiService::Install() {
        auto *net = Framework::CoreModules::GetNetworkPeer();
        if (net == nullptr) {
            return;
        }

        net->RegisterRPC<KCDC::Shared::RPC::ItemNames>([](const KCDC::Shared::RPC::ItemNames &names, MafiaNet::Packet *) {
            if (!names.IsSane() || KCDC::Core::gApplication == nullptr) {
                return;
            }

            UiService &service = KCDC::Core::gApplication->GetUiService();
            std::scoped_lock lock(service._itemNamesMutex);
            if (names.replace) {
                service._itemNames.clear();
            }
            for (const auto &entry : names.entries) {
                if (entry.name.empty()) {
                    service._itemNames.erase(entry.key);
                }
                else {
                    service._itemNames.insert_or_assign(entry.key, entry.name);
                }
            }
        });
    }

    bool UiService::ResolveItemName(const char *label, SDK::CryStringT<char> &out) const {
        // Every '@' key the game shows passes through here, so the common
        // answer -- not ours -- is reached without touching the table.
        if (label[0] != '@') {
            return false;
        }

        const std::size_t length = std::strlen(label + 1);
        if (length == 0 || length > KCDC::Shared::RPC::ItemNames::kMaximumKeyBytes) {
            return false;
        }

        // The engine matches keys by their lowercase spelling, and so does
        // the server's table.
        char key[KCDC::Shared::RPC::ItemNames::kMaximumKeyBytes];
        for (std::size_t index = 0; index < length; ++index) {
            const char c = label[index + 1];
            key[index]   = c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
        }

        std::scoped_lock lock(_itemNamesMutex);
        if (_itemNames.empty()) {
            return false;
        }
        const auto found = _itemNames.find(std::string_view(key, length));
        if (found == _itemNames.end()) {
            return false;
        }
        out.Assign(found->second.c_str());
        return true;
    }
} // namespace KCDC::Features::UI
