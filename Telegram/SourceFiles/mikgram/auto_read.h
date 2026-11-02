/*
This file is part of MikGram, a personal fork of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class History;
class HistoryItem;
class PeerData;

namespace MikGram {

// Moderation bots (@MissRose_bot) whose messages are read automatically,
// as long as nobody else wrote something unread before them.
[[nodiscard]] bool IsAutoReadSender(const PeerData *peer);

// After a new incoming message was added to its chat.
void AutoReadAfterAdded(not_null<HistoryItem*> item);

// After the chat's unread state came from the server (chats list).
void AutoReadAfterDialog(not_null<History*> history);

} // namespace MikGram
