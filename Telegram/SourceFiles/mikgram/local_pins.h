/*
This file is part of MikGram, a personal fork of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_types.h"

class History;

namespace Main {
class Session;
} // namespace Main

namespace Data {
class Folder;
} // namespace Data

namespace Dialogs {
class Entry;
class Key;
} // namespace Dialogs

namespace MikGram {

// Pinned chats of the main chats list, beyond Telegram's limit (5 chats,
// 10 with Premium, checked by the server) and in any order. Both are kept
// on this computer only: the chats pinned beyond the limit, and the order
// of every pinned chat once one is pinned that way. The order of the chats
// pinned on Telegram is still sent to Telegram after a reorder.

// Pinned on Telegram or locally: what the chats list and menus show.
[[nodiscard]] bool IsPinnedShown(
	not_null<const Dialogs::Entry*> entry,
	FilterId filterId);

// The sort key Telegram computed, unless this computer has an order for
// the chat: pinned chats unknown to that order stay on top.
[[nodiscard]] uint64 ChatListSortKey(
	not_null<const Dialogs::Entry*> entry,
	FilterId filterId,
	uint64 telegramSortKey);

// When Telegram refuses one more pinned chat in the main list.
bool PinLocally(not_null<History*> history);

// True when the chat was only pinned locally and nothing is left to do.
bool UnpinLocally(not_null<Dialogs::Entry*> entry);

// Dragging a pinned chat past its neighbour. False when Telegram does it.
bool MovePinned(FilterId filterId, Dialogs::Key key1, Dialogs::Key key2);

// Before Telegram saves the order of its pinned chats after a drag:
// the chats pinned on Telegram take the order they have here.
void SyncServerPinnedOrder(
	not_null<Main::Session*> session,
	Data::Folder *folder);

} // namespace MikGram
