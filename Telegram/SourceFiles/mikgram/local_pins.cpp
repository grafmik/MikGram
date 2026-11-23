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

// Below the chats pinned on Telegram (0xFFFFFFFF000000FF - index), above
// every chat sorted by date, even "unread on top" ones.
constexpr auto kLocalPinTop = 0xFFFFFFFE00000000ULL;

struct AccountPins {
	// The pins of this computer replace Telegram's ones.
	bool own = false;

	// Pinned chats, the top one first. Before "own": the chats pinned here
	// beyond Telegram's limit by the first version, below Telegram's pins.
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

[[nodiscard]] QJsonArray WritePeers(const std::vector<uint64> &peers) {
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

		// First version: an array of the chats pinned beyond the limit.
		const auto value = i.value();
		pins.own = !value.isArray();
		pins.order = ReadPeers(pins.own
			? value.toObject().value(u"order"_q)
			: value);
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
		if (pins.own) {
			object.insert(QString::number(account), QJsonObject{
				{ u"order"_q, WritePeers(pins.order) },
			});
		} else if (!pins.order.empty()) {
			object.insert(QString::number(account), WritePeers(pins.order));
		}
	}
	auto file = QSaveFile(StoragePath());
	if (file.open(QIODevice::WriteOnly)) {
		file.write(QJsonDocument(object).toJson(QJsonDocument::Compact));
		file.commit();
	}
}

// Null while Telegram's pins are shown as they are.
[[nodiscard]] AccountPins *Find(not_null<Main::Session*> session) {
	auto &all = All();
	const auto i = all.find(session->uniqueId());
	return (i != end(all) && (i->second.own || !i->second.order.empty()))
		? &i->second
		: nullptr;
}

// Only chats of the main list count: an archived chat keeps its place
// but is sorted by Telegram in the archive.
[[nodiscard]] const History *MainListHistory(
		not_null<const Dialogs::Entry*> entry,
		FilterId filterId) {
	const auto history = entry->asHistory();
	return (filterId || !history || history->folder()) ? nullptr : history;
}

[[nodiscard]] int IndexOf(const AccountPins &pins, PeerId peerId) {
	const auto i = ranges::find(pins.order, peerId.value);
	return (i != end(pins.order)) ? int(i - begin(pins.order)) : -1;
}

// Pinned chats of the main list as they are shown now, the top one first,
// then the pinned chats that are not shown (their chat is not loaded yet).
[[nodiscard]] std::vector<uint64> CurrentOrder(
		not_null<Main::Session*> session) {
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
	if (const auto pins = Find(session)) {
		for (const auto peerId : pins->order) {
			if (!ranges::contains(result, peerId)) {
				result.push_back(peerId);
			}
		}
	}
	return result;
}

void Save(
		not_null<Main::Session*> session,
		std::vector<uint64> order,
		std::vector<uint64> resort) {
	auto &pins = All()[session->uniqueId()];
	pins.own = true;
	pins.order = std::move(order);
	WritePins();

	// Every sort key of the order depends on the position.
	const auto owner = &session->data();
	for (const auto peerId : resort) {
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
	const auto history = MainListHistory(entry, filterId);
	const auto pins = history ? Find(&history->session()) : nullptr;
	if (!pins) {
		return entry->isPinnedDialog(filterId);
	}
	return (IndexOf(*pins, history->peer->id) >= 0)
		|| (!pins->own && history->isPinnedDialog(FilterId()));
}

uint64 ChatListSortKey(
		not_null<const Dialogs::Entry*> entry,
		FilterId filterId,
		uint64 telegramSortKey,
		uint64 sortKeyByDate) {
	const auto history = MainListHistory(entry, filterId);
	const auto pins = history ? Find(&history->session()) : nullptr;
	if (!pins) {
		return telegramSortKey;
	}
	const auto index = IndexOf(*pins, history->peer->id);
	if (pins->own) {
		return (index >= 0) ? (kLocalPinTop - index) : sortKeyByDate;
	}
	return (index >= 0 && !history->isPinnedDialog(FilterId()))
		? (kLocalPinTop - index)
		: telegramSortKey;
}

std::optional<bool> TogglePinLocally(not_null<Dialogs::Entry*> entry) {
	const auto history = entry->asHistory();
	if (!history || history->folder()) {
		return std::nullopt;
	}
	const auto session = &history->session();
	const auto peerId = history->peer->id.value;
	auto order = CurrentOrder(session);
	auto resort = order;
	const auto pinned = !ranges::contains(order, peerId);
	if (pinned) {
		order.insert(begin(order), peerId); // On top, like Telegram.
		resort.push_back(peerId);
	} else {
		order.erase(ranges::remove(order, peerId), end(order));
	}
	Save(session, std::move(order), std::move(resort));
	return pinned;
}

bool MovePinned(FilterId filterId, Dialogs::Key key1, Dialogs::Key key2) {
	const auto history = key1.history();
	if (filterId || !history || history->folder()) {
		return false;
	}
	const auto other = key2.history();
	if (!other || other->folder()) {
		return true; // The pinned archive stays above the chats.
	}
	const auto session = &history->session();
	auto order = CurrentOrder(session);
	const auto i = ranges::find(order, history->peer->id.value);
	const auto j = ranges::find(order, other->peer->id.value);
	if (i == end(order) || j == end(order)) {
		return true;
	}
	std::iter_swap(i, j);
	auto resort = order;
	Save(session, std::move(order), std::move(resort));
	return true;
}

} // namespace MikGram
