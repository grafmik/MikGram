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

struct Request {
	std::vector<float> samples;
	int frequency = 0;
	QString language;
	Fn<void(SpeechResult)> done;
};

struct Recognition {
	SFSpeechRecognizer *recognizer = nil;
	SFSpeechRecognitionTask *task = nil;
	Fn<void(SpeechResult)> done;
	bool finished = false;
};

void Finish(
		const std::shared_ptr<Recognition> &state,
		SpeechResult result) {
	if (state->finished) {
		return;
	}
	state->finished = true;
	crl::on_main([=, result = std::move(result)] {
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
			done(result);
		}
	});
}

[[nodiscard]] SFSpeechRecognizer *CreateRecognizer(const QString &language) {
	NSLocale *locale = [NSLocale localeWithLocaleIdentifier:
		language.toNSString()];
	SFSpeechRecognizer *result = [[SFSpeechRecognizer alloc]
		initWithLocale:locale];
	return result ? result : [[SFSpeechRecognizer alloc] init];
}

void Start(std::shared_ptr<Request> request) {
	const auto state = std::make_shared<Recognition>();
	state->done = std::move(request->done);
	state->recognizer = CreateRecognizer(request->language);
	if (!state->recognizer || !state->recognizer.isAvailable) {
		Finish(state, { .error = SpeechError::Unavailable });
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
		sampleRate:request->frequency
		channels:1
		interleaved:NO];
	const auto &samples = request->samples;
	const auto chunk = size_t(request->frequency);
	for (auto offset = size_t(0); offset < samples.size(); offset += chunk) {
		const auto count = std::min(chunk, samples.size() - offset);
		AVAudioPCMBuffer *buffer = [[AVAudioPCMBuffer alloc]
			initWithPCMFormat:format
			frameCapacity:AVAudioFrameCount(count)];
		buffer.frameLength = AVAudioFrameCount(count);
		memcpy(
			buffer.floatChannelData[0],
			samples.data() + offset,
			count * sizeof(float));
		[speech appendAudioPCMBuffer:buffer];
		[buffer release];
	}
	[format release];
	[speech endAudio];
	request->samples = {};

	state->task = [[state->recognizer
		recognitionTaskWithRequest:speech
		resultHandler:^(
				SFSpeechRecognitionResult *result,
				NSError *error) {
		if (result && result.isFinal) {
			Finish(state, {
				.text = QString::fromNSString(
					result.bestTranscription.formattedString),
			});
		} else if (error) {
			if (error.code == kNoSpeechDetected) {
				Finish(state, {});
			} else {
				Finish(state, {
					.error = SpeechError::Failed,
					.details = QString::fromNSString(error.localizedDescription),
				});
			}
		}
	}] retain];
	[speech release];
}

} // namespace

void RecognizeSpeech(
		std::vector<float> samples,
		int frequency,
		QString language,
		Fn<void(SpeechResult)> done) {
	auto request = std::make_shared<Request>(Request{
		.samples = std::move(samples),
		.frequency = frequency,
		.language = std::move(language),
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
