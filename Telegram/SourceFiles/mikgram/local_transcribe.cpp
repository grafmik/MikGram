/*
This file is part of MikGram, a personal fork of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "mikgram/local_transcribe.h"

#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_file_origin.h"
#include "data/data_media_types.h"
#include "history/history_item.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "media/audio/media_audio_ffmpeg_loader.h"
#include "mikgram/lang.h"
#include "mikgram/local_transcribe_speech.h"
#include "ui/toast/toast.h"

namespace MikGram {
namespace {

struct Decoded {
	std::vector<float> samples;
	int frequency = 0;
};

base::flat_map<FullMsgId, rpl::lifetime> &Downloads() {
	static auto result = base::flat_map<FullMsgId, rpl::lifetime>();
	return result;
}

[[nodiscard]] Decoded Decode(
		const Core::FileLocation &location,
		const QByteArray &bytes) {
	auto loader = Media::FFMpegLoader(location, bytes, bytes::vector());
	if (!loader.open(0)) {
		return {};
	}
	const auto format = loader.format();
	const auto stereo = (format == AL_FORMAT_STEREO8)
		|| (format == AL_FORMAT_STEREO16);
	const auto wide = (format == AL_FORMAT_MONO16)
		|| (format == AL_FORMAT_STEREO16);
	const auto channels = stereo ? 2 : 1;
	auto result = Decoded{ .frequency = loader.samplesFrequency() };
	auto sum = 0.f;
	auto summed = 0;
	const auto push = [&](float sample) {
		sum += sample;
		if (++summed == channels) {
			result.samples.push_back(sum / channels);
			sum = 0.f;
			summed = 0;
		}
	};
	while (true) {
		const auto read = loader.readMore();
		if (v::is<Media::AudioPlayerLoader::ReadError>(read)) {
			if (v::get<Media::AudioPlayerLoader::ReadError>(read)
				== Media::AudioPlayerLoader::ReadError::Retry) {
				continue;
			}
			break;
		}
		const auto span = v::get<bytes::const_span>(read);
		if (wide) {
			const auto data = reinterpret_cast<const int16*>(span.data());
			for (auto i = 0, count = int(span.size() / 2); i != count; ++i) {
				push(data[i] / 32768.f);
			}
		} else {
			const auto data = reinterpret_cast<const uchar*>(span.data());
			for (auto i = 0, count = int(span.size()); i != count; ++i) {
				push((int(data[i]) - 0x80) / 128.f);
			}
		}
	}
	return result;
}

[[nodiscard]] QString Language() {
	const auto lang = Lang::Id().section('-', 0, 0).toLower();
	const auto system = QLocale::system().name().replace('_', '-');
	if (system.section('-', 0, 0).toLower() == lang
		&& system.contains('-')) {
		return system;
	}
	return (lang == u"en"_q)
		? u"en-US"_q
		: (lang + '-' + lang.toUpper());
}

void Fail(
		const Fn<void(QString, bool)> &done,
		const QString &message) {
	Ui::Toast::Show(message);
	done(QString(), true);
}

void Recognize(
		const Core::FileLocation &location,
		const QByteArray &bytes,
		Fn<void(QString, bool)> done) {
	const auto language = Language();
	crl::async([=] {
		auto decoded = Decode(location, bytes);
		crl::on_main([=, decoded = std::move(decoded)]() mutable {
			if (decoded.samples.empty() || decoded.frequency <= 0) {
				Fail(done, Text(Phrase::TranscribeFailed));
				return;
			}
			RecognizeSpeech(
				std::move(decoded.samples),
				decoded.frequency,
				language,
				[=](SpeechResult result) {
					switch (result.error) {
					case SpeechError::None:
						done(result.text, false);
						break;
					case SpeechError::Denied:
						Fail(done, Text(Phrase::TranscribeDenied));
						break;
					case SpeechError::Unavailable:
						Fail(done, Text(Phrase::TranscribeUnavailable));
						break;
					case SpeechError::Failed:
						Fail(done, result.details.isEmpty()
							? Text(Phrase::TranscribeFailed)
							: (Text(Phrase::TranscribeFailed)
								+ u" ("_q
								+ result.details
								+ ')'));
						break;
					}
				});
		});
	});
}

} // namespace

bool TranscribesLocally(not_null<Main::Session*> session) {
#ifdef Q_OS_MAC
	return !session->premium();
#else // Q_OS_MAC
	return false;
#endif // Q_OS_MAC
}

void TranscribeLocally(
		not_null<HistoryItem*> item,
		Fn<void(QString text, bool failed)> done) {
	const auto media = item->media();
	const auto document = media ? media->document() : nullptr;
	if (!document) {
		done(QString(), true);
		return;
	}
	const auto view = document->createMediaView();
	const auto start = [=] {
		Recognize(document->location(true), view->bytes(), done);
	};
	if (view->loaded()) {
		start();
		return;
	}
	const auto id = item->fullId();
	auto &lifetime = Downloads()[id];
	lifetime.destroy();
	document->save(id, QString());
	document->session().downloaderTaskFinished(
	) | rpl::filter([=] {
		return view->loaded() || document->cancelled();
	}) | rpl::take(1) | rpl::on_next([=] {
		const auto loaded = view->loaded();
		crl::on_main([=] {
			Downloads().remove(id);
		});
		if (loaded) {
			start();
		} else {
			Fail(done, Text(Phrase::TranscribeFailed));
		}
	}, lifetime);
}

#ifndef Q_OS_MAC
void RecognizeSpeech(
		std::vector<float> samples,
		int frequency,
		QString language,
		Fn<void(SpeechResult)> done) {
	done({ .error = SpeechError::Unavailable });
}
#endif // Q_OS_MAC

} // namespace MikGram
