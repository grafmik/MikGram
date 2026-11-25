/*
This file is part of MikGram, a personal fork of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace MikGram {

enum class SpeechError {
	None,
	Denied,
	Unavailable,
	Failed,
};

struct SpeechResult {
	QString text;
	SpeechError error = SpeechError::None;
	QString details;
};

// Mono samples in [-1, 1]. The callback runs on the main thread.
void RecognizeSpeech(
	std::vector<float> samples,
	int frequency,
	QStringList languages,
	Fn<void(SpeechResult)> done);

} // namespace MikGram
