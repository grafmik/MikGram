/*
This file is part of MikGram, a personal fork of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "mikgram/local_pins.h"

#include "data/data_peer.h"
#include "data/data_session.h"
#include "dialogs/dialogs_entry.h"
#include "history/history.h"
#include "main/main_session.h"
#include "settings.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QSaveFile>

namespace MikGram {
namespace {

// Below the chats pinned on Telegram (0xFFFFFFFF000000FF - index),
// above every chat sorted by date, even "unread on top" ones.
constexpr auto kLocalPinTop = 0xFFFFFFFE00000000ULL;

// Account (Main::Session::uniqueId) -> peers, the top one first.
using Pins = base::flat_map<uint64, std::vector<uint64>>;

[[nodiscard]] QString StoragePath() {
	return cWorkingDir() + u"tdata/mikgram_local_pins.json"_q;
}

[[nodiscard]] Pins ReadPins() {
	auto result = Pins();
	auto file = QFile(StoragePath());
	if (!file.open(QIODevice::ReadOnly)) {
		return result;
	}
	const auto object = QJsonDocument::fromJson(file.readAll()).object();
	for (auto i = object.constBegin(); i != object.constEnd(); ++i) {
		const auto account = i.key().toULongLong();
		auto &list = result[account];
		for (const auto &value : i.value().toArray()) {
			if (const auto peerId = value.toString().toULongLong()) {
				list.push_back(peerId);
			}
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
	for (const auto &[account, list] : All()) {
		if (list.empty()) {
			continue;
		}
		auto array = QJsonArray();
		for (const auto peerId : list) {
			array.push_back(QString::number(peerId));
		}
		object.insert(QString::number(account), array);
	}
	auto file = QSaveFile(StoragePath());
	if (file.open(QIODevice::WriteOnly)) {
		file.write(QJsonDocument(object).toJson(QJsonDocument::Compact));
		file.commit();
	}
}

[[nodiscard]] const std::vector<uint64> *ListFor(
		not_null<Main::Session*> session) {
	const auto &all = All();
	const auto i = all.find(session->uniqueId());
	return (i != end(all) && !i->second.empty()) ? &i->second : nullptr;
}

// Position in the local list, or -1. Only chats of the main list count:
// an archived chat keeps its place but is sorted by date in the archive.
[[nodiscard]] int LocalIndex(
		not_null<const Dialogs::Entry*> entry,
		FilterId filterId) {
	const auto history = entry->asHistory();
	if (filterId || !history || history->folder()) {
		return -1;
	}
	const auto list = ListFor(&history->session());
	if (!list) {
		return -1;
	}
	const auto i = ranges::find(*list, history->peer->id.value);
	return (i != end(*list)) ? int(i - begin(*list)) : -1;
}

// Every sort key of the local list depends on the position.
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
	return entry->isPinnedDialog(filterId)
		|| (LocalIndex(entry, filterId) >= 0);
}

uint64 LocalPinSortKey(
		not_null<const Dialogs::Entry*> entry,
		FilterId filterId,
		uint64 sortKeyByDate) {
	const auto index = LocalIndex(entry, filterId);
	return (index >= 0) ? (kLocalPinTop - index) : sortKeyByDate;
}

bool PinLocally(not_null<History*> history) {
	if (history->folder() || history->isPinnedDialog(FilterId())) {
		return false;
	}
	const auto session = &history->session();
	auto &list = All()[session->uniqueId()];
	const auto peerId = history->peer->id.value;
	list.erase(ranges::remove(list, peerId), end(list));
	list.insert(begin(list), peerId);
	WritePins();
	Resort(session, list);
	return true;
}

bool UnpinLocally(not_null<Dialogs::Entry*> entry) {
	if (LocalIndex(entry, FilterId()) < 0) {
		return false;
	}
	const auto history = entry->asHistory();
	const auto session = &history->session();
	auto &list = All()[session->uniqueId()];
	auto resort = list;
	list.erase(ranges::remove(list, history->peer->id.value), end(list));
	WritePins();
	Resort(session, resort);

	// Pinned on Telegram since (from another device): unpin it there too.
	return !history->isPinnedDialog(FilterId());
}

} // namespace MikGram
