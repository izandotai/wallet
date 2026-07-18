#pragma once

// The kit: izan's own widget library. One component per translation
// unit, every one drawn in the active design language (design.hpp) over
// the active color theme. Pages compose these and never call raw imgui
// widgets for anything the kit covers. This header is the whole
// library; include a single component's header for a lean build edge.

#include "ui/widgets/address_field.hpp"
#include "ui/widgets/amount_field.hpp"
#include "ui/widgets/asset_row.hpp"
#include "ui/widgets/avatar.hpp"
#include "ui/widgets/button.hpp"
#include "ui/widgets/card.hpp"
#include "ui/widgets/choice_row.hpp"
#include "ui/widgets/copy_text.hpp"
#include "ui/widgets/design.hpp"
#include "ui/widgets/dialog.hpp"
#include "ui/widgets/empty_state.hpp"
#include "ui/widgets/hyperlink.hpp"
#include "ui/widgets/identity.hpp"
#include "ui/widgets/label.hpp"
#include "ui/widgets/list_row.hpp"
#include "ui/widgets/pill.hpp"
#include "ui/widgets/qr.hpp"
#include "ui/widgets/result_mark.hpp"
#include "ui/widgets/select.hpp"
#include "ui/widgets/spinner.hpp"
#include "ui/widgets/step_dots.hpp"
#include "ui/widgets/table.hpp"
#include "ui/widgets/text_field.hpp"
#include "ui/widgets/toggle.hpp"
#include "ui/widgets/tooltip.hpp"
