/*
This file is part of MikGram, a personal fork of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class HistoryItem;
class PeerData;

namespace MikGram {

// In the discussion group of a channel, the copy of a post looks like the
// post in the channel: when the channel shows author profiles, the author is
// the sender (avatar, name, click), read from the post in the channel since
// the copy only carries the channel. A plain signature becomes the sender
// name, and the channel title moves to the badge on the right.

// Author to display instead of the channel, nullptr while unknown.
[[nodiscard]] PeerData *DiscussionAuthor(not_null<const HistoryItem*> item);

[[nodiscard]] QString DiscussionName(
	not_null<const HistoryItem*> item,
	not_null<PeerData*> from);

// The badge on the right of the name, when the copy shows its author.
[[nodiscard]] QString DiscussionBadge(
	not_null<const HistoryItem*> item,
	const QString &badge);

// The signature next to the time is not needed when the name shows it.
[[nodiscard]] bool ShowsDiscussionAuthor(not_null<const HistoryItem*> item);

} // namespace MikGram
