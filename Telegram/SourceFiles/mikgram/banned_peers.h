/*
This file is part of MikGram, a personal fork of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_peer_id.h"
#include "ui/widgets/menu/menu_add_action_callback.h"

class History;
class HistoryItem;
class PeerData;

namespace Data {
class Session;
} // namespace Data

namespace Window {
class SessionController;
} // namespace Window

namespace MikGram {

[[nodiscard]] bool HasBanned();
[[nodiscard]] bool IsBanned(PeerId peerId);
[[nodiscard]] bool IsBanned(const PeerData *peer);
[[nodiscard]] bool IsBannedIn(
	const PeerData *peer,
	not_null<const History*> history);
[[nodiscard]] QString BannedSuffix(
	const PeerData *peer,
	not_null<const History*> history);
[[nodiscard]] bool ReplyLeadsToBanned(not_null<const HistoryItem*> item);

template <typename Container, typename Proj>
void RemoveBanned(Container &list, Proj &&proj) {
	if (HasBanned()) {
		list.erase(
			ranges::remove_if(list, [&](const auto &value) {
				return IsBanned(proj(value));
			}),
			end(list));
	}
}

template <typename Container>
void RemoveBanned(Container &list) {
	RemoveBanned(list, [](const auto &peer) { return peer->id; });
}

struct SyncedBan {
	uint64 peerId = 0;
	QString title;
};
[[nodiscard]] std::vector<SyncedBan> BannedForSync();
void ApplySyncedBan(
	not_null<Data::Session*> owner,
	uint64 peerId,
	bool banned,
	const QString &title);

void AddBanAction(
	not_null<Window::SessionController*> controller,
	PeerData *peer,
	const Ui::Menu::MenuCallback &addAction);

void ShowBannedBox(not_null<Window::SessionController*> controller);

} // namespace MikGram
