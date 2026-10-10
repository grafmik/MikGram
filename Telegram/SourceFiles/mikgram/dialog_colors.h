/*
This file is part of MikGram, a personal fork of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_peer_id.h"
#include "ui/widgets/menu/menu_add_action_callback.h"

class PeerData;

namespace MikGram {

[[nodiscard]] const std::vector<QColor> &DialogColorPalette();

[[nodiscard]] int DialogColor(PeerId peerId);

void AddDialogColorAction(
	PeerData *peer,
	const Ui::Menu::MenuCallback &addAction);

} // namespace MikGram
