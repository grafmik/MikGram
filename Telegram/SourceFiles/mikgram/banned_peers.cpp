/*
This file is part of MikGram, a personal fork of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "mikgram/banned_peers.h"

#include "data/data_peer.h"
#include "data/data_folder.h"
#include "data/data_session.h"
#include "dialogs/dialogs_indexed_list.h"
#include "dialogs/dialogs_main_list.h"
#include "dialogs/dialogs_row.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item_components.h"
#include "history/view/history_view_element.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "mikgram/lang.h"
#include "settings.h"
#include "ui/layers/generic_box.h"
#include "ui/rp_widget.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "window/window_session_controller.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_widgets.h"

#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QSaveFile>

namespace MikGram {
namespace {

struct Banned {
	QString title;
};

[[nodiscard]] QString StoragePath() {
	return cWorkingDir() + u"tdata/mikgram_banned_peers.json"_q;
}

[[nodiscard]] base::flat_map<uint64, Banned> ReadBanned() {
	auto result = base::flat_map<uint64, Banned>();
	auto file = QFile(StoragePath());
	if (!file.open(QIODevice::ReadOnly)) {
		return result;
	}
	const auto object = QJsonDocument::fromJson(file.readAll()).object();
	for (auto i = object.constBegin(); i != object.constEnd(); ++i) {
		const auto peerId = i.key().toULongLong();
		const auto data = i.value().toObject();
		if (peerId) {
			result.emplace(peerId, Banned{
				.title = data.value(u"title"_q).toString(),
			});
		}
	}
	return result;
}

[[nodiscard]] base::flat_map<uint64, Banned> &All() {
	static auto result = ReadBanned();
	return result;
}

void WriteBanned() {
	auto object = QJsonObject();
	for (const auto &[peerId, banned] : All()) {
		object.insert(QString::number(peerId), QJsonObject{
			{ u"title"_q, banned.title },
		});
	}
	auto file = QSaveFile(StoragePath());
	if (file.open(QIODevice::WriteOnly)) {
		file.write(QJsonDocument(object).toJson(QJsonDocument::Compact));
		file.commit();
	}
}

void ResizeViews(not_null<Dialogs::MainList*> list) {
	for (const auto &row : list->indexed()->all()) {
		if (const auto history = row->history()) {
			for (const auto &block : history->blocks) {
				for (const auto &view : block->messages) {
					history->owner().requestViewResize(view.get());
				}
			}
		}
	}
}

void Applied(not_null<Data::Session*> owner, PeerId peerId) {
	if (const auto history = owner->historyLoaded(peerId)) {
		history->updateChatListExistence();
	}
	ResizeViews(owner->chatsList());
	if (const auto folder = owner->folderLoaded(Data::Folder::kId)) {
		ResizeViews(folder->chatsList());
	}
}

void Ban(not_null<PeerData*> peer) {
	All()[peer->id.value] = Banned{ .title = peer->name() };
	WriteBanned();
	Applied(&peer->owner(), peer->id);
}

void Unban(not_null<Data::Session*> owner, PeerId peerId) {
	if (All().remove(peerId.value)) {
		WriteBanned();
		Applied(owner, peerId);
	}
}

} // namespace

bool HasBanned() {
	return !All().empty();
}

bool IsBanned(PeerId peerId) {
	return HasBanned() && All().contains(peerId.value);
}

bool IsBanned(const PeerData *peer) {
	return peer && IsBanned(peer->id);
}

bool IsBannedIn(const PeerData *peer, not_null<const History*> history) {
	return IsBanned(peer) && (history->peer->id != peer->id);
}

QString BannedSuffix(
		const PeerData *peer,
		not_null<const History*> history) {
	return IsBannedIn(peer, history)
		? (u" ("_q + Text(Phrase::Banned) + ')')
		: QString();
}

bool ReplyLeadsToBanned(not_null<const HistoryItem*> item) {
	const auto reply = HasBanned()
		? item->Get<HistoryMessageReply>()
		: nullptr;
	if (!reply) {
		return false;
	}
	const auto history = item->history();
	const auto replied = reply->resolvedMessage.get();
	const auto external = reply->externalPeerId();
	return (IsBanned(external) && history->peer->id != external)
		|| (replied && IsBannedIn(replied->history()->peer, history));
}

void AddBanAction(
		not_null<Window::SessionController*> controller,
		PeerData *peer,
		const Ui::Menu::MenuCallback &addAction) {
	const auto group = peer && (peer->isChat() || peer->isMegagroup());
	if (!peer || (!group && !peer->isBroadcast())) {
		return;
	}
	const auto banned = IsBanned(peer);
	addAction(
		Text(banned
			? (group ? Phrase::UnbanGroup : Phrase::UnbanChannel)
			: (group ? Phrase::BanGroup : Phrase::BanChannel)),
		[=] {
			if (banned) {
				Unban(&peer->owner(), peer->id);
			} else {
				Ban(peer);
			}
		},
		banned ? &st::menuIconUnblock : &st::menuIconBlock);
}

void ShowBannedBox(not_null<Window::SessionController*> controller) {
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(rpl::single(Text(Phrase::BannedChats)));
		box->addButton(tr::lng_close(), [=] { box->closeBox(); });
		if (!HasBanned()) {
			box->addRow(object_ptr<Ui::FlatLabel>(
				box,
				Text(Phrase::NoBannedChats),
				st::boxLabel));
			return;
		}
		for (const auto &[id, banned] : All()) {
			const auto peerId = PeerId(PeerIdHelper(id));
			const auto row = box->addRow(object_ptr<Ui::RpWidget>(box));
			const auto label = Ui::CreateChild<Ui::FlatLabel>(
				row,
				banned.title,
				st::boxLabel);
			const auto button = Ui::CreateChild<Ui::RoundButton>(
				row,
				rpl::single(Text(Phrase::Unban)),
				st::defaultLightButton);
			label->setAttribute(Qt::WA_TransparentForMouseEvents);
			row->resize(row->width(), button->height());
			row->widthValue() | rpl::on_next([=](int width) {
				const auto skip = st::boxRowPadding.left();
				label->resizeToWidth(width - button->width() - skip);
				label->moveToLeft(
					0,
					(button->height() - label->height()) / 2,
					width);
				button->moveToRight(0, 0, width);
			}, row->lifetime());
			button->setClickedCallback([=] {
				Unban(&controller->session().data(), peerId);
				crl::on_main(row, [=] { delete row; });
			});
		}
	}));
}

} // namespace MikGram
