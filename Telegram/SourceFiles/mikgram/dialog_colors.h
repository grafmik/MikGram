/*
This file is part of MikGram, a personal fork of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_peer_id.h"
#include "ui/widgets/menu/menu_add_action_callback.h"

class PeerData;

namespace Window {
class SessionController;
} // namespace Window

namespace MikGram {

[[nodiscard]] const std::vector<QColor> &DialogColorPalette();

[[nodiscard]] int DialogColor(PeerId peerId);

void AddDialogColorAction(
	not_null<Window::SessionController*> controller,
	PeerData *peer,
	const Ui::Menu::MenuCallback &addAction);

} // namespace MikGram
