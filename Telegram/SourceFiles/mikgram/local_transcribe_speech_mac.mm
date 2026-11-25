/*
This file is part of MikGram, a personal fork of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "mikgram/local_transcribe_speech.h"

#import <AVFoundation/AVFoundation.h>
#import <Speech/Speech.h>

namespace MikGram {
namespace {

constexpr auto kNoSpeechDetected = 1110;
constexpr auto kMaxCandidates = 3;
constexpr auto kProbeSeconds = 20;

struct Request {
	std::vector<float> samples;
	int frequency = 0;
	QStringList languages;
	Fn<void(SpeechResult)> done;
};

struct Pass {
	QStringList parts;
	double confidence = 0.;
	int words = 0;
	SpeechError error = SpeechError::None;
	QString details;
};

struct Recognition {
	SFSpeechRecognizer *recognizer = nil;
	SFSpeechRecognitionTask *task = nil;
	Fn<void(Pass)> done;
	Pass pass;
	bool finished = false;
};

struct Candidates {
	~Candidates() {
		for (NSLocale *locale : locales) {
			[locale release];
		}
	}

	std::vector<NSLocale*> locales;
	std::vector<Pass> probes;
};

[[nodiscard]] QString Join(const QStringList &parts) {
	auto result = QString();
	for (auto part : parts) {
		part = part.trimmed();
		if (part.isEmpty()) {
			continue;
		}
		if (!result.isEmpty()) {
			const auto last = result.back();
			if (last != '.' && last != '!' && last != '?' && last != QChar(0x2026)) {
				result += '.';
			}
			result += ' ';
			part[0] = part[0].toUpper();
		}
		result += part;
	}
	return result;
}

[[nodiscard]] double Score(const Pass &pass) {
	return pass.words ? (pass.confidence / pass.words) : 0.;
}

[[nodiscard]] QString Normalized(NSString *identifier) {
	return QString::fromNSString(identifier).replace('_', '-').toLower();
}

[[nodiscard]] QString LanguageCode(NSString *identifier) {
	return Normalized(identifier).section('-', 0, 0);
}

[[nodiscard]] bool OnDevice(NSLocale *locale) {
	SFSpeechRecognizer *recognizer = [[SFSpeechRecognizer alloc]
		initWithLocale:locale];
	const auto result = recognizer && recognizer.supportsOnDeviceRecognition;
	[recognizer release];
	return result;
}

[[nodiscard]] NSLocale *SupportedLocale(const QString &language) {
	auto tags = QStringList();
	for (NSString *tag in [NSLocale preferredLanguages]) {
		if (LanguageCode(tag) == language) {
			tags.push_back(Normalized(tag));
		}
	}
	tags.push_back(QLocale(language).name().replace('_', '-').toLower());

	auto tagged = std::vector<NSLocale*>(tags.size(), nil);
	NSLocale *other = nil;
	for (NSLocale *locale in [SFSpeechRecognizer supportedLocales]) {
		if (LanguageCode(locale.localeIdentifier) != language) {
			continue;
		}
		const auto position = tags.indexOf(Normalized(locale.localeIdentifier));
		if (position >= 0) {
			tagged[position] = locale;
		} else if (!other) {
			other = locale;
		}
	}
	// On the Mac first: the audio stays here, and it is much faster.
	for (NSLocale *locale : tagged) {
		if (locale && OnDevice(locale)) {
			return locale;
		}
	}
	for (NSLocale *locale : tagged) {
		if (locale) {
			return locale;
		}
	}
	return other;
}

[[nodiscard]] std::vector<NSLocale*> Locales(const QStringList &languages) {
	auto codes = languages;
	for (NSString *tag in [NSLocale preferredLanguages]) {
		const auto code = LanguageCode(tag);
		if (!codes.contains(code)) {
			codes.push_back(code);
		}
	}
	auto result = std::vector<NSLocale*>();
	for (const auto &code : codes) {
		NSLocale *locale = SupportedLocale(code);
		const auto known = locale && ranges::any_of(result, [&](NSLocale *l) {
			return [l.localeIdentifier isEqualToString:locale.localeIdentifier];
		});
		if (locale && !known && int(result.size()) < kMaxCandidates) {
			result.push_back([locale retain]);
		}
	}
	return result;
}

void Finish(
		const std::shared_ptr<Recognition> &state,
		SpeechError error = SpeechError::None,
		QString details = QString()) {
	if (state->finished) {
		return;
	}
	state->finished = true;
	state->pass.error = error;
	state->pass.details = std::move(details);
	crl::on_main([=] {
		const auto done = base::take(state->done);
		if (state->task) {
			[state->task release];
			state->task = nil;
		}
		if (state->recognizer) {
			[state->recognizer release];
			state->recognizer = nil;
		}
		if (done) {
			done(base::take(state->pass));
		}
	});
}

void Recognize(
		NSLocale *locale,
		const std::vector<float> &samples,
		size_t count,
		int frequency,
		Fn<void(Pass)> done) {
	const auto state = std::make_shared<Recognition>();
	state->done = std::move(done);
	state->recognizer = locale
		? [[SFSpeechRecognizer alloc] initWithLocale:locale]
		: [[SFSpeechRecognizer alloc] init];
	if (!state->recognizer || !state->recognizer.isAvailable) {
		Finish(state, SpeechError::Unavailable);
		return;
	}
	SFSpeechAudioBufferRecognitionRequest *speech
		= [[SFSpeechAudioBufferRecognitionRequest alloc] init];
	speech.shouldReportPartialResults = NO;
	if (state->recognizer.supportsOnDeviceRecognition) {
		speech.requiresOnDeviceRecognition = YES;
	}
	if (@available(macOS 13.0, *)) {
		speech.addsPunctuation = YES;
	}
	AVAudioFormat *format = [[AVAudioFormat alloc]
		initWithCommonFormat:AVAudioPCMFormatFloat32
		sampleRate:frequency
		channels:1
		interleaved:NO];
	const auto chunk = size_t(frequency);
	for (auto offset = size_t(0); offset < count; offset += chunk) {
		const auto size = std::min(chunk, count - offset);
		AVAudioPCMBuffer *buffer = [[AVAudioPCMBuffer alloc]
			initWithPCMFormat:format
			frameCapacity:AVAudioFrameCount(size)];
		buffer.frameLength = AVAudioFrameCount(size);
		memcpy(
			buffer.floatChannelData[0],
			samples.data() + offset,
			size * sizeof(float));
		[speech appendAudioPCMBuffer:buffer];
		[buffer release];
	}
	[format release];
	[speech endAudio];

	state->task = [[state->recognizer
		recognitionTaskWithRequest:speech
		resultHandler:^(
				SFSpeechRecognitionResult *result,
				NSError *error) {
		// Long audio comes as one result per sentence: only the last one is final.
		if (result && (result.isFinal || result.speechRecognitionMetadata)) {
			state->pass.parts.push_back(QString::fromNSString(
				result.bestTranscription.formattedString));
			for (SFTranscriptionSegment *segment
					in result.bestTranscription.segments) {
				state->pass.confidence += segment.confidence;
				++state->pass.words;
			}
		}
		if (result && result.isFinal) {
			Finish(state);
		} else if (error) {
			if (!state->pass.parts.isEmpty()
				|| error.code == kNoSpeechDetected) {
				Finish(state);
			} else {
				Finish(
					state,
					SpeechError::Failed,
					QString::fromNSString(error.localizedDescription));
			}
		}
	}] retain];
	[speech release];
}

void Done(const std::shared_ptr<Request> &request, const Pass &pass) {
	const auto done = base::take(request->done);
	done({
		.text = Join(pass.parts),
		.error = pass.error,
		.details = pass.details,
	});
}

void Probe(
		std::shared_ptr<Request> request,
		std::shared_ptr<Candidates> candidates) {
	const auto index = candidates->probes.size();
	const auto count = std::min(
		request->samples.size(),
		size_t(request->frequency) * kProbeSeconds);
	if (index < candidates->locales.size()) {
		Recognize(
			candidates->locales[index],
			request->samples,
			count,
			request->frequency,
			[=](Pass pass) {
				candidates->probes.push_back(std::move(pass));
				Probe(request, candidates);
			});
		return;
	}
	auto best = 0;
	for (auto i = 1; i != int(candidates->probes.size()); ++i) {
		if (Score(candidates->probes[i]) > Score(candidates->probes[best])) {
			best = i;
		}
	}
	if (count == request->samples.size()) {
		Done(request, candidates->probes[best]);
		return;
	}
	Recognize(
		candidates->locales[best],
		request->samples,
		request->samples.size(),
		request->frequency,
		[=](Pass pass) { Done(request, pass); });
}

void Start(std::shared_ptr<Request> request) {
	auto candidates = std::make_shared<Candidates>();
	candidates->locales = Locales(request->languages);
	if (candidates->locales.size() > 1) {
		Probe(std::move(request), std::move(candidates));
		return;
	}
	const auto locale = candidates->locales.empty()
		? nil
		: candidates->locales.front();
	Recognize(
		locale,
		request->samples,
		request->samples.size(),
		request->frequency,
		[=](Pass pass) { Done(request, pass); });
}

} // namespace

void RecognizeSpeech(
		std::vector<float> samples,
		int frequency,
		QStringList languages,
		Fn<void(SpeechResult)> done) {
	auto request = std::make_shared<Request>(Request{
		.samples = std::move(samples),
		.frequency = frequency,
		.languages = std::move(languages),
		.done = std::move(done),
	});
	const auto status = [SFSpeechRecognizer authorizationStatus];
	if (status == SFSpeechRecognizerAuthorizationStatusAuthorized) {
		Start(std::move(request));
	} else if (status == SFSpeechRecognizerAuthorizationStatusNotDetermined) {
		[SFSpeechRecognizer requestAuthorization:^(
				SFSpeechRecognizerAuthorizationStatus status) {
			crl::on_main([=] {
				if (status == SFSpeechRecognizerAuthorizationStatusAuthorized) {
					Start(request);
				} else {
					request->done({ .error = SpeechError::Denied });
				}
			});
		}];
	} else {
		request->done({ .error = SpeechError::Denied });
	}
}

} // namespace MikGram
