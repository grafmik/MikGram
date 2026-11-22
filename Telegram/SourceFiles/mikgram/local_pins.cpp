/*
This file is part of MikGram, a personal fork of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "mikgram/local_pins.h"

#include "data/data_peer.h"
#include "data/data_session.h"
#include "dialogs/dialogs_entry.h"
#include "dialogs/dialogs_indexed_list.h"
#include "dialogs/dialogs_key.h"
#include "dialogs/dialogs_main_list.h"
#include "dialogs/dialogs_row.h"
#include "history/history.h"
#include "main/main_session.h"
#include "settings.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QSaveFile>

namespace MikGram {
namespace {

// Below the chats pinned on Telegram that this computer has no order for
// (0xFFFFFFFF000000FF - index), above every chat sorted by date, even
// "unread on top" ones.
constexpr auto kLocalPinTop = 0xFFFFFFFE00000000ULL;

struct AccountPins {
	// Pinned on this computer only.
	base::flat_set<uint64> local;

	// Every pinned chat of the main list, the top one first. It always
	// holds the local ones; it is empty when there are none.
	std::vector<uint64> order;
};

// Account (Main::Session::uniqueId) -> its pins.
using Pins = base::flat_map<uint64, AccountPins>;

[[nodiscard]] QString StoragePath() {
	return cWorkingDir() + u"tdata/mikgram_local_pins.json"_q;
}

[[nodiscard]] std::vector<uint64> ReadPeers(const QJsonValue &value) {
	auto result = std::vector<uint64>();
	for (const auto &peer : value.toArray()) {
		if (const auto peerId = peer.toString().toULongLong()) {
			result.push_back(peerId);
		}
	}
	return result;
}

[[nodiscard]] QJsonArray WritePeers(const auto &peers) {
	auto result = QJsonArray();
	for (const auto peerId : peers) {
		result.push_back(QString::number(peerId));
	}
	return result;
}

[[nodiscard]] Pins ReadPins() {
	auto result = Pins();
	auto file = QFile(StoragePath());
	if (!file.open(QIODevice::ReadOnly)) {
		return result;
	}
	const auto object = QJsonDocument::fromJson(file.readAll()).object();
	for (auto i = object.constBegin(); i != object.constEnd(); ++i) {
		auto &pins = result[i.key().toULongLong()];

		// First version: an array of the local pins, the top one first.
		const auto first = i.value().isArray();
		const auto account = i.value().toObject();
		pins.order = ReadPeers(first ? i.value() : account[u"order"_q]);
		const auto local = first
			? pins.order
			: ReadPeers(account[u"local"_q]);
		for (const auto peerId : local) {
			pins.local.emplace(peerId);
			if (!ranges::contains(pins.order, peerId)) {
				pins.order.push_back(peerId);
			}
		}
		if (pins.local.empty()) {
			pins.order.clear();
		}
	}
	return result;
}

[[nodiscard]] Pins &All() {
	static auto result = ReadPins();
	return result;
}

void WritePins() {
	auto object = QJsonObject();
	for (const auto &[account, pins] : All()) {
		if (pins.order.empty()) {
			continue;
		}
		object.insert(QString::number(account), QJsonObject{
			{ u"local"_q, WritePeers(pins.local) },
			{ u"order"_q, WritePeers(pins.order) },
		});
	}
	auto file = QSaveFile(StoragePath());
	if (file.open(QIODevice::WriteOnly)) {
		file.write(QJsonDocument(object).toJson(QJsonDocument::Compact));
		file.commit();
	}
}

// Null while the account has no local pin: Telegram does everything.
[[nodiscard]] AccountPins *Find(not_null<Main::Session*> session) {
	auto &all = All();
	const auto i = all.find(session->uniqueId());
	return (i != end(all) && !i->second.order.empty()) ? &i->second : nullptr;
}

// Only chats of the main list count: an archived chat keeps its place
// but is sorted by Telegram in the archive.
[[nodiscard]] const History *MainListHistory(
		not_null<const Dialogs::Entry*> entry,
		FilterId filterId) {
	const auto history = entry->asHistory();
	return (filterId || !history || history->folder()) ? nullptr : history;
}

[[nodiscard]] bool IsLocal(
		not_null<const Dialogs::Entry*> entry,
		FilterId filterId) {
	const auto history = MainListHistory(entry, filterId);
	const auto pins = history ? Find(&history->session()) : nullptr;
	return pins && pins->local.contains(history->peer->id.value);
}

// Position in the order of this computer, or -1.
[[nodiscard]] int OrderIndex(
		not_null<const Dialogs::Entry*> entry,
		FilterId filterId) {
	const auto history = MainListHistory(entry, filterId);
	const auto pins = history ? Find(&history->session()) : nullptr;
	if (!pins) {
		return -1;
	}
	const auto peerId = history->peer->id.value;
	if (!history->isPinnedDialog(FilterId())
		&& !pins->local.contains(peerId)) {
		return -1;
	}
	const auto i = ranges::find(pins->order, peerId);
	return (i != end(pins->order)) ? int(i - begin(pins->order)) : -1;
}

// Pinned chats of the main list as they are shown now, the top one first,
// then the local pins that are not shown (their chat is not loaded yet).
[[nodiscard]] std::vector<uint64> CurrentOrder(
		not_null<Main::Session*> session,
		const AccountPins &pins) {
	auto result = std::vector<uint64>();
	for (const auto &row : *session->data().chatsList()->indexed()) {
		const auto entry = row->entry();
		if (entry->fixedOnTopIndex()) {
			continue;
		} else if (!IsPinnedShown(entry, FilterId())) {
			break;
		} else if (const auto history = entry->asHistory()) {
			result.push_back(history->peer->id.value);
		}
	}
	for (const auto peerId : pins.order) {
		if (pins.local.contains(peerId)
			&& !ranges::contains(result, peerId)) {
			result.push_back(peerId);
		}
	}
	return result;
}

// Every sort key of the order depends on the position.
void Resort(
		not_null<Main::Session*> session,
		std::vector<uint64> peers) {
	const auto owner = &session->data();
	for (const auto peerId : peers) {
		if (const auto history = owner->historyLoaded(
				PeerId(PeerIdHelper(peerId)))) {
			history->updateChatListSortPosition();
		}
	}
}

} // namespace

bool IsPinnedShown(
		not_null<const Dialogs::Entry*> entry,
		FilterId filterId) {
	return entry->isPinnedDialog(filterId) || IsLocal(entry, filterId);
}

uint64 ChatListSortKey(
		not_null<const Dialogs::Entry*> entry,
		FilterId filterId,
		uint64 telegramSortKey) {
	const auto index = OrderIndex(entry, filterId);
	return (index >= 0) ? (kLocalPinTop - index) : telegramSortKey;
}

bool PinLocally(not_null<History*> history) {
	if (history->folder() || history->isPinnedDialog(FilterId())) {
		return false;
	}
	const auto session = &history->session();
	auto &pins = All()[session->uniqueId()];
	const auto peerId = history->peer->id.value;
	auto order = CurrentOrder(session, pins);
	order.erase(ranges::remove(order, peerId), end(order));

	// On top, like a chat pinned on Telegram.
	order.insert(begin(order), peerId);
	pins.local.emplace(peerId);
	pins.order = std::move(order);
	WritePins();
	Resort(session, pins.order);
	return true;
}

bool UnpinLocally(not_null<Dialogs::Entry*> entry) {
	const auto history = entry->asHistory();
	if (!history || history->folder()) {
		return false;
	}
	const auto session = &history->session();
	const auto pins = Find(session);
	if (!pins) {
		return false;
	}
	const auto peerId = history->peer->id.value;
	const auto local = pins->local.remove(peerId);
	if (!local && !ranges::contains(pins->order, peerId)) {
		return false;
	}
	auto resort = pins->order;
	pins->order.erase(ranges::remove(pins->order, peerId), end(pins->order));
	if (pins->local.empty()) {
		// No local pin left: the order Telegram has comes back, it was
		// sent there after each reorder.
		pins->order.clear();
	}
	WritePins();
	Resort(session, resort);

	// Pinned on Telegram too: unpin it there as well.
	return local && !history->isPinnedDialog(FilterId());
}

bool MovePinned(FilterId filterId, Dialogs::Key key1, Dialogs::Key key2) {
	const auto history = key1.history();
	if (filterId || !history || history->folder()) {
		return false;
	}
	const auto session = &history->session();
	const auto pins = Find(session);
	if (!pins) {
		return false;
	}
	const auto other = key2.history();
	if (!other || other->folder()) {
		return true; // The pinned archive stays above the chats.
	}
	auto order = CurrentOrder(session, *pins);
	const auto i = ranges::find(order, history->peer->id.value);
	const auto j = ranges::find(order, other->peer->id.value);
	if (i == end(order) || j == end(order)) {
		return true;
	}
	std::iter_swap(i, j);
	pins->order = std::move(order);
	WritePins();
	Resort(session, pins->order);
	return true;
}

void SyncServerPinnedOrder(
		not_null<Main::Session*> session,
		Data::Folder *folder) {
	const auto pins = folder ? nullptr : Find(session);
	if (!pins) {
		return;
	}
	const auto owner = &session->data();
	const auto mainList = static_cast<Data::Folder*>(nullptr);

	// The chats pinned on Telegram, in the order shown here.
	auto shown = std::vector<Dialogs::Key>();
	for (const auto peerId : pins->order) {
		const auto history = owner->historyLoaded(
			PeerId(PeerIdHelper(peerId)));
		if (history
			&& !history->folder()
			&& history->isPinnedDialog(FilterId())) {
			shown.push_back(history);
		}
	}

	// Telegram's list with its chats in that order, the archive in place.
	auto wanted = owner->pinnedChatsOrder(mainList);
	auto next = begin(shown);
	for (auto &key : wanted) {
		if (key.history()) {
			if (next == end(shown)) {
				return; // A chat pinned on Telegram is not ordered here.
			}
			key = *next++;
		}
	}
	if (next != end(shown)) {
		return;
	}
	for (auto i = 0; i != int(wanted.size()); ++i) {
		const auto key = owner->pinnedChatsOrder(mainList)[i];
		if (key != wanted[i]) {
			owner->reorderTwoPinnedChats(FilterId(), key, wanted[i]);
		}
	}
}

} // namespace MikGram
