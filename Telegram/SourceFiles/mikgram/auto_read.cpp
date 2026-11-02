/*
This file is part of MikGram, a personal fork of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "mikgram/auto_read.h"

#include "apiwrap.h"
#include "data/data_forum_topic.h"
#include "data/data_histories.h"
#include "data/data_replies_list.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/view/history_view_element.h"
#include "main/main_session.h"

namespace MikGram {
namespace {

constexpr auto kAutoReadBots = std::array<std::string_view, 1>{
	"MissRose_bot",
};

struct Found {
	MsgId till = 0;
	std::vector<not_null<HistoryItem*>> items;
};

// Unread messages of the chat (rootId == 0) or of one topic, if they all
// come from an auto-read bot. Empty when someone else wrote one of them,
// or when they are not loaded and there is more than one.
[[nodiscard]] Found FindAutoReadable(
		not_null<History*> history,
		MsgId rootId,
		MsgId readTill,
		int unread,
		HistoryItem *last) {
	if (unread <= 0) {
		return {};
	}
	auto result = Found();
	for (const auto &block : ranges::views::reverse(history->blocks)) {
		for (const auto &view : ranges::views::reverse(block->messages)) {
			const auto item = view->data();
			if (!item->isRegular()
				|| item->out()
				|| (rootId && item->topicRootId() != rootId)) {
				continue;
			} else if (item->id <= readTill) {
				return (int(result.items.size()) == unread)
					? result
					: Found();
			} else if (!IsAutoReadSender(item->from())) {
				return {};
			}
			if (!result.till) {
				result.till = item->id;
			}
			result.items.push_back(item);
		}
	}
	if (int(result.items.size()) == unread) {
		return result;
	} else if (unread == 1
		&& last
		&& last->isRegular()
		&& !last->out()
		&& last->id > readTill
		&& (!rootId || last->topicRootId() == rootId)
		&& IsAutoReadSender(last->from())) {
		return { .till = last->id, .items = { not_null(last) } };
	}
	return {};
}

void MarkMentionsRead(const Found &found) {
	for (const auto &item : found.items) {
		if (item->isUnreadMention()) {
			item->history()->session().api().markContentsRead(item);
		}
	}
}

void CheckHistory(not_null<History*> history) {
	if (history->isForum()
		|| history->unreadMark()
		|| !history->unreadCountKnown()) {
		return;
	}
	const auto found = FindAutoReadable(
		history,
		MsgId(),
		history->inboxReadTillId(),
		history->unreadCount(),
		history->lastServerMessage());
	if (found.till) {
		MarkMentionsRead(found);
		history->owner().histories().readInboxTill(history, found.till);
	}
}

void CheckTopic(not_null<Data::ForumTopic*> topic) {
	const auto replies = topic->replies();
	if (!replies->unreadCountKnown()) {
		return;
	}
	const auto found = FindAutoReadable(
		topic->owningHistory(),
		topic->rootId(),
		replies->computeInboxReadTillFull(),
		replies->unreadCountCurrent(),
		topic->lastServerMessage());
	if (found.till) {
		MarkMentionsRead(found);
		replies->readTill(found.till);
	}
}

} // namespace

bool IsAutoReadSender(const PeerData *peer) {
	const auto user = peer ? peer->asUser() : nullptr;
	if (!user || !user->isBot()) {
		return false;
	}
	const auto username = user->username();
	return ranges::any_of(kAutoReadBots, [&](std::string_view bot) {
		return !username.compare(
			QLatin1String(bot.data(), int(bot.size())),
			Qt::CaseInsensitive);
	});
}

void AutoReadAfterAdded(not_null<HistoryItem*> item) {
	if (item->out()
		|| !item->isRegular()
		|| item->savedSublist()
		|| !IsAutoReadSender(item->from())) {
		return;
	}
	// Counters are updated by the rest of the new message handling.
	const auto session = &item->history()->session();
	const auto id = item->fullId();
	crl::on_main(session, [=] {
		const auto item = session->data().message(id);
		if (!item) {
			return;
		} else if (const auto topic = item->topic()) {
			CheckTopic(topic);
		} else {
			CheckHistory(item->history());
		}
	});
}

void AutoReadAfterDialog(not_null<History*> history) {
	const auto last = history->lastServerMessage();
	if (!last || last->out() || !IsAutoReadSender(last->from())) {
		return;
	}
	const auto session = &history->session();
	const auto peerId = history->peer->id;
	crl::on_main(session, [=] {
		if (const auto history = session->data().historyLoaded(peerId)) {
			CheckHistory(history);
		}
	});
}

} // namespace MikGram
