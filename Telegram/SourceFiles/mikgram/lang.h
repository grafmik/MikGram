/*
This file is part of MikGram, a personal fork of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace MikGram {

enum class Phrase {
	DialogColor,
	BanChannel,
	UnbanChannel,
	BanGroup,
	UnbanGroup,
	Unban,
	BannedChats,
	Banned,
	NoBannedChats,

	Count,
};

[[nodiscard]] QString Text(Phrase phrase);

} // namespace MikGram
