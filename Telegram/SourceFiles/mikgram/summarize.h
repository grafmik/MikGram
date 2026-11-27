/*
This file is part of MikGram, a personal fork of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class HistoryItem;

namespace Ui::Text {
class String;
} // namespace Ui::Text

namespace MikGram {

// Summaries come from the Mac's on-device model (Apple Intelligence).
[[nodiscard]] TextWithEntities WithSummary(
	not_null<HistoryItem*> item,
	TextWithEntities transcription);

void SetSummaryLink(
	not_null<HistoryItem*> item,
	Ui::Text::String &text,
	const QString &transcription);

} // namespace MikGram
