/*
This file is part of MikGram, a personal fork of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class HistoryItem;

namespace Main {
class Session;
} // namespace Main

namespace MikGram {

// Voice and round video messages are transcribed on this computer, with the
// speech recognition of the system (the one of macOS Dictation), instead of
// the Premium transcription of Telegram's server. Nothing is sent to Telegram.
[[nodiscard]] bool TranscribesLocally(not_null<Main::Session*> session);

// Called on the main thread with the text, or with failed = true after the
// reason was shown in a toast.
void TranscribeLocally(
	not_null<HistoryItem*> item,
	Fn<void(QString text, bool failed)> done);

} // namespace MikGram
