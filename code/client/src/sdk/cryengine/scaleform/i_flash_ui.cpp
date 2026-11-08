#include "i_flash_ui.h"

#include "../../patterns.h"

namespace SDK {
    IFlashUI *IFlashUI::GetInstance() { return *reinterpret_cast<IFlashUI **>(gPatterns.IFlashUI__Instance); }
} // namespace SDK
