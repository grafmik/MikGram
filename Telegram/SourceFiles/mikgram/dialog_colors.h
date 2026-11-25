/*
This file is part of MikGram, a personal fork of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_peer_id.h"
#include "ui/widgets/menu/menu_add_action_callback.h"

class PeerData;

namespace Data {
class Session;
} // namespace Data

namespace MikGram {

[[nodiscard]] const std::vector<QColor> &DialogColorPalette();

[[nodiscard]] int DialogColor(PeerId peerId);
[[nodiscard]] std::optional<QColor> DialogTint(PeerId peerId);
[[nodiscard]] QColor DialogBg(PeerData *peer, QColor bg);

[[nodiscard]] std::vector<std::pair<uint64, int>> DialogColorsForSync();
void ApplySyncedDialogColor(
	not_null<Data::Session*> owner,
	uint64 peerId,
	int index);

void AddDialogColorAction(
	PeerData *peer,
	const Ui::Menu::MenuCallback &addAction);

} // namespace MikGram
