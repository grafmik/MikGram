/*
This file is part of MikGram, a personal fork of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "mikgram/discussion_author.h"

#include "apiwrap.h"
#include "data/data_channel.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item_components.h"
#include "main/main_session.h"
#include "mikgram/banned_peers.h"
#include "ui/text/text_utilities.h"
#include "styles/style_basic.h"

namespace MikGram {
namespace {

// Posts already asked from their channel: account, channel, post.
base::flat_set<std::tuple<uint64, PeerId, MsgId>> Requested;

[[nodiscard]] UserData *KnownAuthor(not_null<const HistoryItem*> item) {
	const auto forwarded = item->Get<HistoryMessageForwarded>();
	for (const auto peer : {
		forwarded->originalSender,
		forwarded->savedFromSender,
	}) {
		if (const auto user = peer ? peer->asUser() : nullptr) {
			return user;
		}
	}
	if (const auto original = item->lookupDiscussionPostOriginal()) {
		return original->from()->asUser();
	}
	return nullptr;
}

void RequestOriginal(
		not_null<const HistoryItem*> item,
		not_null<ChannelData*> channel) {
	const auto postId = item->Get<HistoryMessageForwarded>()->savedFromMsgId;
	const auto session = &item->history()->session();
	if (!postId
		|| !Requested.emplace(session->uniqueId(), channel->id, postId).second) {
		return;
	}
	const auto copyId = item->fullId();
	session->api().requestMessageData(channel, postId, crl::guard(session, [=] {
		if (const auto copy = session->data().message(copyId)) {
			copy->invalidateDisplayFrom();
			session->data().requestItemViewRefresh(copy);
		}
	}));
}

[[nodiscard]] QString Signature(not_null<const HistoryItem*> item) {
	const auto msgsigned = item->Get<HistoryMessageSigned>();
	return msgsigned
		? TextUtilities::SingleLine(msgsigned->author)
		: QString();
}

} // namespace

PeerData *DiscussionAuthor(not_null<const HistoryItem*> item) {
	const auto channel = item->discussionPostOriginalSender();
	if (!channel || !channel->signatureProfiles()) {
		return nullptr;
	} else if (const auto user = KnownAuthor(item)) {
		return user;
	}
	RequestOriginal(item, channel);
	return nullptr;
}

QString DiscussionName(
		not_null<const HistoryItem*> item,
		not_null<PeerData*> from) {
	if (from == item->discussionPostOriginalSender()) {
		const auto signature = Signature(item);
		if (!signature.isEmpty()) {
			return signature;
		}
	}
	return from->name();
}

QString DiscussionBadge(
		not_null<const HistoryItem*> item,
		const QString &badge) {
	if (!ShowsDiscussionAuthor(item)) {
		return badge;
	}
	const auto channel = item->discussionPostOriginalSender();
	return st::normalFont->elided(
		channel->name() + BannedSuffix(channel, item->history()),
		style::ConvertScale(140));
}

bool ShowsDiscussionAuthor(not_null<const HistoryItem*> item) {
	const auto channel = item->discussionPostOriginalSender();
	return channel
		&& ((item->displayFrom() != channel) || !Signature(item).isEmpty());
}

} // namespace MikGram
