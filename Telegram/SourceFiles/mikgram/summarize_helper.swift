/*
This file is part of MikGram, a personal fork of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/

import Foundation
import FoundationModels

// The model reads about 4096 tokens: longer texts are summarized in parts.
let partLength = 6000

let instructions = """
	You summarize a transcribed voice message for someone who has no time to \
	read it. Write in the language of the message. Do not address the sender \
	or the reader, and do not start with "The message" or "In this message": \
	go straight to the content. Keep names, places, dates, amounts and any \
	request or question. Three short sentences at most.
	"""

func fail(_ message: String, _ code: Int32) -> Never {
	FileHandle.standardError.write(Data((message + "\n").utf8))
	exit(code)
}

func parts(of text: String) -> [String] {
	var result: [String] = []
	var current = ""
	for sentence in text.split(separator: " ", omittingEmptySubsequences: true) {
		if current.count + sentence.count + 1 > partLength && !current.isEmpty {
			result.append(current)
			current = ""
		}
		current += (current.isEmpty ? "" : " ") + sentence
	}
	if !current.isEmpty {
		result.append(current)
	}
	return result
}

func summarize(_ text: String) async throws -> String {
	let session = LanguageModelSession(instructions: instructions)
	let options = GenerationOptions(temperature: 0.2)
	return try await session.respond(to: text, options: options).content
}

let model = SystemLanguageModel.default
switch model.availability {
case .available:
	break
case .unavailable(let reason):
	fail("unavailable: \(reason)", 2)
}
if CommandLine.arguments.contains("--check") {
	exit(0)
}

let input = String(
	decoding: FileHandle.standardInput.readDataToEndOfFile(),
	as: UTF8.self
).trimmingCharacters(in: .whitespacesAndNewlines)
if input.isEmpty {
	fail("empty text", 1)
}

do {
	let pieces = parts(of: input)
	let summary: String
	if pieces.count == 1 {
		summary = try await summarize(input)
	} else {
		var summaries: [String] = []
		for piece in pieces {
			summaries.append(try await summarize(piece))
		}
		summary = try await summarize(summaries.joined(separator: "\n"))
	}
	print(summary.trimmingCharacters(in: .whitespacesAndNewlines))
} catch {
	fail("\(error)", 1)
}
