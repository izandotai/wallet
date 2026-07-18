#pragma once

namespace izan::ui {

// One ledger line: a direction glyph (arrow into the tray for money
// received, out of it for money sent), the counterparty over the
// chain-and-moment note, the signed amount flush right — muted red
// glyph when the transaction failed on-chain. The whole row is a
// button; the caller usually opens the explorer. The row owns its
// tooltips: `hint` anywhere on the row, `note_hint` when the pointer
// rests on the note line (the moment, told in the user's own clock).
bool kit_tx_row(const char* id, bool incoming, const char* counterparty,
    const char* note, const char* amount, bool failed,
    const char* hint = nullptr, const char* note_hint = nullptr);

}
