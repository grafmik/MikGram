/*
This file is part of MikGram, a personal fork of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_types.h"

namespace Dialogs {
class Entry;
class Key;
} // namespace Dialogs

namespace MikGram {

// Pinned chats of the main chats list kept on this computer: no limit, any
// order, and nothing is sent to Telegram, neither the pinned chats nor their
// order. Until the first pin, unpin or move, the chats pinned on Telegram are
// shown; they become the starting list of this computer. The archive and the
// custom folders keep Telegram's pins.

// What the chats list and menus show as pinned.
[[nodiscard]] bool IsPinnedShown(
	not_null<const Dialogs::Entry*> entry,
	FilterId filterId);

// Telegram's sort key (pinned or by date) unless this computer decides.
[[nodiscard]] uint64 ChatListSortKey(
	not_null<const Dialogs::Entry*> entry,
	FilterId filterId,
	uint64 telegramSortKey,
	uint64 sortKeyByDate);

// Pin (on top) or unpin a chat of the main list: whether it is pinned now.
// Nothing for the other lists, Telegram does it.
std::optional<bool> TogglePinLocally(not_null<Dialogs::Entry*> entry);

// Dragging a pinned chat past its neighbour. False when Telegram does it.
bool MovePinned(FilterId filterId, Dialogs::Key key1, Dialogs::Key key2);

} // namespace MikGram
