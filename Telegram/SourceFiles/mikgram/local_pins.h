/*
This file is part of MikGram, a personal fork of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_types.h"

class History;

namespace Dialogs {
class Entry;
} // namespace Dialogs

namespace MikGram {

// Chats pinned on top of the main chats list beyond Telegram's limit
// (5 chats, 10 with Premium, checked by the server). They are kept on this
// computer only, below the chats pinned on Telegram, the latest on top.

// Pinned on Telegram or locally: what the chats list and menus show.
[[nodiscard]] bool IsPinnedShown(
	not_null<const Dialogs::Entry*> entry,
	FilterId filterId);

// Sort key of a chat that is not pinned on Telegram.
[[nodiscard]] uint64 LocalPinSortKey(
	not_null<const Dialogs::Entry*> entry,
	FilterId filterId,
	uint64 sortKeyByDate);

// When Telegram refuses one more pinned chat in the main list.
bool PinLocally(not_null<History*> history);

// False when the chat was not pinned locally.
bool UnpinLocally(not_null<Dialogs::Entry*> entry);

} // namespace MikGram
