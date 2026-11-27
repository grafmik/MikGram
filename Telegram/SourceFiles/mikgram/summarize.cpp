/*
This file is part of MikGram, a personal fork of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "mikgram/summarize.h"

#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "mikgram/lang.h"
#include "ui/text/text.h"
#include "ui/text/text_utilities.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QFileInfo>
#include <QtCore/QOperatingSystemVersion>
#include <QtCore/QProcess>
#include <QtCore/QTimer>

namespace MikGram {
namespace {

constexpr auto kMinLength = 300;
constexpr auto kLinkIndex = 1;
constexpr auto kTimeout = 120 * crl::time(1000);

struct Summary {
	QString source;
	QString text;
	bool loading = false;
	bool failed = false;
};

using Key = std::pair<uint64, FullMsgId>;

[[nodiscard]] base::flat_map<Key, Summary> &Summaries() {
	static auto result = base::flat_map<Key, Summary>();
	return result;
}

[[nodiscard]] Key KeyFor(not_null<HistoryItem*> item) {
	return { item->history()->session().uniqueId(), item->fullId() };
}

[[nodiscard]] QString HelperPath() {
	return QCoreApplication::applicationDirPath()
		+ u"/../Helpers/MikGramSummarize"_q;
}

[[nodiscard]] bool Available() {
#ifdef Q_OS_MAC
	static const auto result = (QOperatingSystemVersion::current()
		>= QOperatingSystemVersion(QOperatingSystemVersion::MacOS, 26))
		&& QFileInfo(HelperPath()).isExecutable();
	return result;
#else // Q_OS_MAC
	return false;
#endif // Q_OS_MAC
}

void Refresh(base::weak_ptr<Main::Session> weak, FullMsgId id) {
	if (const auto session = weak.get()) {
		if (const auto item = session->data().message(id)) {
			session->data().requestItemResize(item);
		}
	}
}

void Start(not_null<HistoryItem*> item, QString source) {
	const auto key = KeyFor(item);
	auto &entry = Summaries()[key];
	if (entry.loading) {
		return;
	}
	entry = Summary{ .source = source, .loading = true };

	const auto weak = base::make_weak(&item->history()->session());
	const auto id = item->fullId();
	const auto process = new QProcess();
	const auto finish = [=](QString text) {
		auto &entry = Summaries()[key];
		entry.loading = false;
		entry.failed = text.isEmpty();
		entry.text = text;
		Refresh(weak, id);
	};
	QObject::connect(process, &QProcess::finished, [=](
			int code,
			QProcess::ExitStatus status) {
		const auto text = QString::fromUtf8(
			process->readAllStandardOutput()).trimmed();
		if (status != QProcess::NormalExit || code != 0) {
			LOG(("MikGram summary failed: %1 %2").arg(code).arg(
				QString::fromUtf8(process->readAllStandardError())));
		}
		process->deleteLater();
		finish((status == QProcess::NormalExit && !code) ? text : QString());
	});
	QObject::connect(process, &QProcess::errorOccurred, [=](
			QProcess::ProcessError error) {
		if (error == QProcess::FailedToStart) {
			LOG(("MikGram summary helper did not start."));
			process->deleteLater();
			finish(QString());
		}
	});
	QTimer::singleShot(kTimeout, process, [=] { process->kill(); });
	process->start(HelperPath(), QStringList());
	process->write(source.toUtf8());
	process->closeWriteChannel();
	Refresh(weak, id);
}

} // namespace

TextWithEntities WithSummary(
		not_null<HistoryItem*> item,
		TextWithEntities transcription) {
	if (transcription.text.size() < kMinLength || !Available()) {
		return transcription;
	}
	const auto &map = Summaries();
	const auto i = map.find(KeyFor(item));
	const auto entry = (i != end(map) && i->second.source == transcription.text)
		? &i->second
		: nullptr;
	if (entry && !entry->text.isEmpty()) {
		return tr::bold(Text(Phrase::SummaryTitle))
			.append(u": "_q)
			.append(tr::italic(entry->text))
			.append(u"\n\n"_q)
			.append(std::move(transcription));
	}
	transcription.append(u"\n"_q);
	if (entry && entry->loading) {
		return transcription.append(tr::italic(Text(Phrase::Summarizing)));
	}
	return transcription.append(Ui::Text::Link(
		Text((entry && entry->failed)
			? Phrase::SummaryFailed
			: Phrase::Summarize),
		kLinkIndex));
}

void SetSummaryLink(
		not_null<HistoryItem*> item,
		Ui::Text::String &text,
		const QString &transcription) {
	const auto weak = base::make_weak(&item->history()->session());
	const auto id = item->fullId();
	text.setLink(kLinkIndex, std::make_shared<LambdaClickHandler>([=] {
		if (const auto session = weak.get()) {
			if (const auto item = session->data().message(id)) {
				Start(item, transcription);
			}
		}
	}));
}

} // namespace MikGram
