/*
This file is part of MikGram, a personal fork of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "mikgram/sync.h"

#include "apiwrap.h"
#include "base/timer.h"
#include "base/unixtime.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "mikgram/banned_peers.h"
#include "mikgram/dialog_colors.h"
#include "mikgram/lang.h"
#include "mikgram/local_pins.h"
#include "qr/qr_generate.h"
#include "settings.h"
#include "ui/boxes/confirm_box.h"
#include "ui/layers/generic_box.h"
#include "ui/painter.h"
#include "ui/rp_widget.h"
#include "ui/toast/toast.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"
#include "styles/style_layers.h"
#include "styles/style_widgets.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QSaveFile>
#include <QtCore/QUrlQuery>
#include <QtGui/QClipboard>
#include <QtGui/QGuiApplication>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>

#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/sha.h>

namespace MikGram {
namespace {

constexpr auto kServer = "https://mikgram.9mp.org"_cs;
constexpr auto kBot = "MikGramBot"_cs;
constexpr auto kShareScheme = "mikgram-sync"_cs;
constexpr auto kStartDelay = 5 * crl::time(1000);
constexpr auto kDebounce = 3 * crl::time(1000);
constexpr auto kPeriod = 15 * 60 * crl::time(1000);
constexpr auto kTombstoneMs = qint64(180) * 86400 * 1000;
constexpr auto kKeySize = 32;
constexpr auto kNonceSize = 12;
constexpr auto kTagSize = 16;
constexpr auto kMaxAttempts = 3;

enum class Status {
	Unknown,
	Disabled,
	Pairing,
	KeyReplaced,
	Working,
	Active,
	Offline,
	Failed,
};

struct ColorEntry {
	int color = -1;
	qint64 time = 0;
};

struct BanEntry {
	bool banned = false;
	QString title;
	qint64 time = 0;
};

struct PinsEntry {
	QStringList order;
	qint64 time = 0;
};

struct Payload {
	std::map<QString, ColorEntry> colors;
	std::map<QString, BanEntry> banned;
	std::optional<PinsEntry> pins;
};

struct AccountState {
	QByteArray key;
	QString token;
	qint64 pinsTime = 0;
};

struct State {
	base::flat_map<uint64, qint64> colorTimes;
	base::flat_map<uint64, qint64> banTimes;
	base::flat_map<uint64, AccountState> accounts;
};

[[nodiscard]] qint64 Now() {
	return QDateTime::currentMSecsSinceEpoch();
}

[[nodiscard]] QString StatePath() {
	return cWorkingDir() + u"tdata/mikgram_sync.json"_q;
}

[[nodiscard]] base::flat_map<uint64, qint64> ReadTimes(
		const QJsonObject &object) {
	auto result = base::flat_map<uint64, qint64>();
	for (auto i = object.constBegin(); i != object.constEnd(); ++i) {
		if (const auto id = i.key().toULongLong()) {
			result.emplace(id, qint64(i.value().toDouble()));
		}
	}
	return result;
}

[[nodiscard]] QJsonObject WriteTimes(
		const base::flat_map<uint64, qint64> &times) {
	auto result = QJsonObject();
	for (const auto &[id, time] : times) {
		result.insert(QString::number(id), double(time));
	}
	return result;
}

[[nodiscard]] State ReadState() {
	auto result = State();
	auto file = QFile(StatePath());
	if (!file.open(QIODevice::ReadOnly)) {
		return result;
	}
	const auto object = QJsonDocument::fromJson(file.readAll()).object();
	result.colorTimes = ReadTimes(object.value(u"colors"_q).toObject());
	result.banTimes = ReadTimes(object.value(u"banned"_q).toObject());
	const auto accounts = object.value(u"accounts"_q).toObject();
	for (auto i = accounts.constBegin(); i != accounts.constEnd(); ++i) {
		const auto data = i.value().toObject();
		if (const auto id = i.key().toULongLong()) {
			result.accounts.emplace(id, AccountState{
				.key = QByteArray::fromBase64(
					data.value(u"key"_q).toString().toLatin1()),
				.token = data.value(u"token"_q).toString(),
				.pinsTime = qint64(data.value(u"pins"_q).toDouble()),
			});
		}
	}
	return result;
}

[[nodiscard]] State &GlobalState() {
	static auto result = ReadState();
	return result;
}

void WriteState() {
	const auto &state = GlobalState();
	auto accounts = QJsonObject();
	for (const auto &[id, account] : state.accounts) {
		accounts.insert(QString::number(id), QJsonObject{
			{ u"key"_q, QString::fromLatin1(account.key.toBase64()) },
			{ u"token"_q, account.token },
			{ u"pins"_q, double(account.pinsTime) },
		});
	}
	auto file = QSaveFile(StatePath());
	if (file.open(QIODevice::WriteOnly)) {
		file.write(QJsonDocument(QJsonObject{
			{ u"colors"_q, WriteTimes(state.colorTimes) },
			{ u"banned"_q, WriteTimes(state.banTimes) },
			{ u"accounts"_q, accounts },
		}).toJson(QJsonDocument::Compact));
		file.commit();
	}
}

[[nodiscard]] QByteArray Sha256(const QByteArray &data) {
	auto result = QByteArray(SHA256_DIGEST_LENGTH, Qt::Uninitialized);
	SHA256(
		reinterpret_cast<const uchar*>(data.constData()),
		data.size(),
		reinterpret_cast<uchar*>(result.data()));
	return result;
}

[[nodiscard]] QByteArray RandomBytes(int count) {
	auto result = QByteArray(count, Qt::Uninitialized);
	RAND_bytes(reinterpret_cast<uchar*>(result.data()), count);
	return result;
}

[[nodiscard]] QString KeyId(const QByteArray &key) {
	return QString::fromLatin1(
		Sha256("mikgram-sync-key" + key).toHex().left(32));
}

[[nodiscard]] const uchar *Bytes(const QByteArray &data) {
	return reinterpret_cast<const uchar*>(data.constData());
}

[[nodiscard]] std::optional<QByteArray> Encrypt(
		const QByteArray &key,
		const QByteArray &aad,
		const QByteArray &plain) {
	const auto nonce = RandomBytes(kNonceSize);
	auto cipher = QByteArray(plain.size() + kTagSize, Qt::Uninitialized);
	const auto out = reinterpret_cast<uchar*>(cipher.data());
	const auto context = EVP_CIPHER_CTX_new();
	auto length = 0;
	auto finalLength = 0;
	const auto ok = context
		&& EVP_EncryptInit_ex(context, EVP_aes_256_gcm(), nullptr, nullptr, nullptr)
		&& EVP_CIPHER_CTX_ctrl(context, EVP_CTRL_GCM_SET_IVLEN, kNonceSize, nullptr)
		&& EVP_EncryptInit_ex(context, nullptr, nullptr, Bytes(key), Bytes(nonce))
		&& EVP_EncryptUpdate(context, nullptr, &length, Bytes(aad), aad.size())
		&& EVP_EncryptUpdate(context, out, &length, Bytes(plain), plain.size())
		&& EVP_EncryptFinal_ex(context, out + length, &finalLength)
		&& EVP_CIPHER_CTX_ctrl(
			context,
			EVP_CTRL_GCM_GET_TAG,
			kTagSize,
			out + plain.size());
	if (context) {
		EVP_CIPHER_CTX_free(context);
	}
	if (!ok) {
		return std::nullopt;
	}
	return "MGS1" + nonce + cipher;
}

[[nodiscard]] std::optional<QByteArray> Decrypt(
		const QByteArray &key,
		const QByteArray &aad,
		const QByteArray &blob) {
	if (blob.size() < 4 + kNonceSize + kTagSize || !blob.startsWith("MGS1")) {
		return std::nullopt;
	}
	const auto nonce = blob.mid(4, kNonceSize);
	const auto size = int(blob.size()) - 4 - kNonceSize - kTagSize;
	const auto cipher = blob.mid(4 + kNonceSize, size);
	auto tag = blob.right(kTagSize);
	auto plain = QByteArray(size, Qt::Uninitialized);
	const auto out = reinterpret_cast<uchar*>(plain.data());
	const auto context = EVP_CIPHER_CTX_new();
	auto length = 0;
	auto finalLength = 0;
	const auto ok = context
		&& EVP_DecryptInit_ex(context, EVP_aes_256_gcm(), nullptr, nullptr, nullptr)
		&& EVP_CIPHER_CTX_ctrl(context, EVP_CTRL_GCM_SET_IVLEN, kNonceSize, nullptr)
		&& EVP_DecryptInit_ex(context, nullptr, nullptr, Bytes(key), Bytes(nonce))
		&& EVP_DecryptUpdate(context, nullptr, &length, Bytes(aad), aad.size())
		&& EVP_DecryptUpdate(context, out, &length, Bytes(cipher), cipher.size())
		&& EVP_CIPHER_CTX_ctrl(context, EVP_CTRL_GCM_SET_TAG, kTagSize, tag.data())
		&& (EVP_DecryptFinal_ex(context, out + length, &finalLength) > 0);
	if (context) {
		EVP_CIPHER_CTX_free(context);
	}
	if (!ok) {
		return std::nullopt;
	}
	return plain;
}

[[nodiscard]] QString PeerKey(uint64 value) {
	const auto id = PeerId(PeerIdHelper(value));
	if (peerIsUser(id)) {
		return u"u"_q + QString::number(peerToUser(id).bare);
	} else if (peerIsChat(id)) {
		return u"g"_q + QString::number(peerToChat(id).bare);
	} else if (peerIsChannel(id)) {
		return u"g"_q + QString::number(peerToChannel(id).bare);
	}
	return QString();
}

[[nodiscard]] std::optional<uint64> PeerFromKey(
		not_null<Data::Session*> owner,
		const QString &key) {
	const auto bare = key.mid(1).toULongLong();
	if (!bare) {
		return std::nullopt;
	} else if (key.startsWith('u')) {
		return peerFromUser(UserId(bare)).value;
	} else if (!key.startsWith('g')) {
		return std::nullopt;
	} else if (owner->channelLoaded(ChannelId(bare))) {
		return peerFromChannel(ChannelId(bare)).value;
	} else if (owner->chatLoaded(ChatId(bare))) {
		return peerFromChat(ChatId(bare)).value;
	}
	return peerFromChannel(ChannelId(bare)).value;
}

[[nodiscard]] QJsonObject ToJson(const Payload &payload) {
	auto colors = QJsonObject();
	for (const auto &[key, entry] : payload.colors) {
		colors.insert(key, QJsonObject{
			{ u"c"_q, entry.color },
			{ u"t"_q, double(entry.time) },
		});
	}
	auto banned = QJsonObject();
	for (const auto &[key, entry] : payload.banned) {
		banned.insert(key, QJsonObject{
			{ u"b"_q, entry.banned },
			{ u"title"_q, entry.title },
			{ u"t"_q, double(entry.time) },
		});
	}
	auto result = QJsonObject{
		{ u"v"_q, 1 },
		{ u"colors"_q, colors },
		{ u"banned"_q, banned },
	};
	if (payload.pins) {
		result.insert(u"pins"_q, QJsonObject{
			{ u"order"_q, QJsonArray::fromStringList(payload.pins->order) },
			{ u"t"_q, double(payload.pins->time) },
		});
	}
	return result;
}

[[nodiscard]] Payload FromJson(const QJsonObject &object) {
	auto result = Payload();
	const auto colors = object.value(u"colors"_q).toObject();
	for (auto i = colors.constBegin(); i != colors.constEnd(); ++i) {
		const auto data = i.value().toObject();
		result.colors.emplace(i.key(), ColorEntry{
			.color = data.value(u"c"_q).toInt(-1),
			.time = qint64(data.value(u"t"_q).toDouble()),
		});
	}
	const auto banned = object.value(u"banned"_q).toObject();
	for (auto i = banned.constBegin(); i != banned.constEnd(); ++i) {
		const auto data = i.value().toObject();
		result.banned.emplace(i.key(), BanEntry{
			.banned = data.value(u"b"_q).toBool(),
			.title = data.value(u"title"_q).toString(),
			.time = qint64(data.value(u"t"_q).toDouble()),
		});
	}
	const auto pins = object.value(u"pins"_q);
	if (pins.isObject()) {
		auto entry = PinsEntry{
			.time = qint64(pins.toObject().value(u"t"_q).toDouble()),
		};
		for (const auto &value : pins.toObject().value(u"order"_q).toArray()) {
			entry.order.push_back(value.toString());
		}
		result.pins = std::move(entry);
	}
	return result;
}

[[nodiscard]] QByteArray Serialize(const Payload &payload) {
	return QJsonDocument(ToJson(payload)).toJson(QJsonDocument::Compact);
}

[[nodiscard]] Payload LocalPayload(not_null<Main::Session*> session) {
	auto &state = GlobalState();
	const auto now = Now();
	const auto expired = [&](qint64 time) {
		return now - time > kTombstoneMs;
	};
	auto result = Payload();

	auto colors = base::flat_map<uint64, int>();
	for (const auto &[peerId, color] : DialogColorsForSync()) {
		colors.emplace(peerId, color);
	}
	auto colorPeers = base::flat_set<uint64>();
	for (const auto &[peerId, color] : colors) {
		colorPeers.emplace(peerId);
	}
	for (const auto &[peerId, time] : state.colorTimes) {
		colorPeers.emplace(peerId);
	}
	for (const auto peerId : colorPeers) {
		const auto i = colors.find(peerId);
		const auto color = (i != end(colors)) ? i->second : -1;
		const auto j = state.colorTimes.find(peerId);
		const auto time = (j != end(state.colorTimes)) ? j->second : 0;
		const auto key = PeerKey(peerId);
		if (!key.isEmpty() && (color >= 0 || !expired(time))) {
			result.colors[key] = { .color = color, .time = time };
		}
	}

	auto banned = base::flat_map<uint64, QString>();
	for (const auto &entry : BannedForSync()) {
		banned.emplace(entry.peerId, entry.title);
	}
	auto banPeers = base::flat_set<uint64>();
	for (const auto &[peerId, title] : banned) {
		banPeers.emplace(peerId);
	}
	for (const auto &[peerId, time] : state.banTimes) {
		banPeers.emplace(peerId);
	}
	for (const auto peerId : banPeers) {
		const auto i = banned.find(peerId);
		const auto isBanned = (i != end(banned));
		const auto j = state.banTimes.find(peerId);
		const auto time = (j != end(state.banTimes)) ? j->second : 0;
		const auto key = PeerKey(peerId);
		if (!key.isEmpty() && (isBanned || !expired(time))) {
			result.banned[key] = {
				.banned = isBanned,
				.title = isBanned ? i->second : QString(),
				.time = time,
			};
		}
	}

	if (const auto order = LocalPinsForSync(session)) {
		auto entry = PinsEntry{
			.time = state.accounts[session->userId().bare].pinsTime,
		};
		for (const auto peerId : *order) {
			const auto key = PeerKey(peerId);
			if (!key.isEmpty()) {
				entry.order.push_back(key);
			}
		}
		result.pins = std::move(entry);
	}
	return result;
}

[[nodiscard]] Payload Merge(const Payload &local, const Payload &remote) {
	auto result = remote;
	for (const auto &[key, entry] : local.colors) {
		const auto i = result.colors.find(key);
		if (i == end(result.colors) || entry.time > i->second.time) {
			result.colors[key] = entry;
		}
	}
	for (const auto &[key, entry] : local.banned) {
		const auto i = result.banned.find(key);
		if (i == end(result.banned) || entry.time > i->second.time) {
			result.banned[key] = entry;
		}
	}
	if (local.pins
		&& (!result.pins || local.pins->time > result.pins->time)) {
		result.pins = local.pins;
	}
	const auto now = Now();
	for (auto i = begin(result.colors); i != end(result.colors);) {
		if (i->second.color < 0 && now - i->second.time > kTombstoneMs) {
			i = result.colors.erase(i);
		} else {
			++i;
		}
	}
	for (auto i = begin(result.banned); i != end(result.banned);) {
		if (!i->second.banned && now - i->second.time > kTombstoneMs) {
			i = result.banned.erase(i);
		} else {
			++i;
		}
	}
	return result;
}

void Apply(
		not_null<Main::Session*> session,
		const Payload &merged,
		const Payload &local) {
	auto &state = GlobalState();
	const auto owner = &session->data();
	for (const auto &[key, entry] : merged.colors) {
		const auto peerId = PeerFromKey(owner, key);
		if (!peerId) {
			continue;
		}
		const auto i = local.colors.find(key);
		const auto current = (i != end(local.colors)) ? i->second.color : -1;
		if (current != entry.color) {
			ApplySyncedDialogColor(owner, *peerId, entry.color);
		}
		auto &time = state.colorTimes[*peerId];
		time = std::max(time, entry.time);
	}
	for (const auto &[key, entry] : merged.banned) {
		const auto peerId = PeerFromKey(owner, key);
		if (!peerId) {
			continue;
		}
		const auto i = local.banned.find(key);
		const auto current = (i != end(local.banned)) && i->second.banned;
		if (current != entry.banned) {
			ApplySyncedBan(owner, *peerId, entry.banned, entry.title);
		}
		auto &time = state.banTimes[*peerId];
		time = std::max(time, entry.time);
	}
	if (merged.pins) {
		auto &account = state.accounts[session->userId().bare];
		const auto changed = !local.pins
			|| (local.pins->order != merged.pins->order);
		if (changed && merged.pins->time >= account.pinsTime) {
			auto order = std::vector<uint64>();
			for (const auto &key : merged.pins->order) {
				if (const auto peerId = PeerFromKey(owner, key)) {
					order.push_back(*peerId);
				}
			}
			ApplySyncedPins(session, std::move(order));
		}
		account.pinsTime = std::max(account.pinsTime, merged.pins->time);
	}
	WriteState();
}

[[nodiscard]] QNetworkAccessManager *Network() {
	static const auto result = new QNetworkAccessManager(
		QCoreApplication::instance());
	return result;
}

void Http(
		const QByteArray &method,
		const QString &path,
		const QString &token,
		std::optional<QJsonObject> body,
		Fn<void(int, QJsonObject)> done) {
	auto request = QNetworkRequest(QUrl(kServer.utf16() + path));
	request.setTransferTimeout(20000);
	request.setHeader(
		QNetworkRequest::ContentTypeHeader,
		u"application/json"_q);
	if (!token.isEmpty()) {
		request.setRawHeader("Authorization", "Bearer " + token.toUtf8());
	}
	const auto data = body
		? QJsonDocument(*body).toJson(QJsonDocument::Compact)
		: QByteArray();
	const auto reply = Network()->sendCustomRequest(request, method, data);
	QObject::connect(reply, &QNetworkReply::finished, [=] {
		reply->deleteLater();
		const auto status = reply->attribute(
			QNetworkRequest::HttpStatusCodeAttribute).toInt();
		done(status, QJsonDocument::fromJson(reply->readAll()).object());
	});
}

class Syncer final : public base::has_weak_ptr {
public:
	explicit Syncer(not_null<Main::Session*> session);

	[[nodiscard]] rpl::producer<> updates() const {
		return _updates.events();
	}
	[[nodiscard]] Status status() const {
		return _status;
	}
	[[nodiscard]] QString error() const {
		return _error;
	}
	[[nodiscard]] TimeId lastSync() const {
		return _lastSync;
	}
	[[nodiscard]] bool hasKey() {
		return !account().key.isEmpty();
	}
	[[nodiscard]] QString shareText();

	void schedule();
	void syncNow();
	void refresh();
	void enable();
	[[nodiscard]] bool pair(const QString &text);
	void resetKey();
	void removeRemote();

private:
	using Done = Fn<void(int, QJsonObject)>;

	[[nodiscard]] AccountState &account();
	[[nodiscard]] QByteArray aad() const;
	[[nodiscard]] bool start();
	void finish(Status status, QString error = QString());
	void fail(int status, const QString &details = QString());
	void forgetKey();
	void call(
		const QByteArray &method,
		const QString &path,
		std::optional<QJsonObject> body,
		Done done,
		bool retried = false);
	void authenticate(Fn<void()> then);
	void withBot(Fn<void(not_null<UserData*>)> then);
	void fetch(int attempt);
	void upload(const Payload &payload, int version, int attempt);
	[[nodiscard]] std::optional<QString> encrypt(const Payload &payload);

	const not_null<Main::Session*> _session;
	base::Timer _debounce;
	base::Timer _periodic;
	Status _status = Status::Unknown;
	QString _error;
	TimeId _lastSync = 0;
	bool _busy = false;
	bool _again = false;
	UserData *_bot = nullptr;
	rpl::event_stream<> _updates;

};

Syncer::Syncer(not_null<Main::Session*> session)
: _session(session)
, _debounce([=] { syncNow(); })
, _periodic([=] { syncNow(); }) {
	if (hasKey()) {
		_debounce.callOnce(kStartDelay);
	}
	_periodic.callEach(kPeriod);
}

AccountState &Syncer::account() {
	return GlobalState().accounts[_session->userId().bare];
}

QByteArray Syncer::aad() const {
	return "mikgram-sync:" + QByteArray::number(_session->userId().bare);
}

QString Syncer::shareText() {
	return kShareScheme.utf16()
		+ u":1?u="_q
		+ QString::number(_session->userId().bare)
		+ u"&k="_q
		+ QString::fromLatin1(account().key.toBase64(
			QByteArray::Base64UrlEncoding
			| QByteArray::OmitTrailingEquals));
}

void Syncer::schedule() {
	if (hasKey()) {
		_debounce.callOnce(kDebounce);
	}
}

void Syncer::syncNow() {
	if (hasKey()) {
		refresh();
	}
}

void Syncer::refresh() {
	if (start()) {
		fetch(0);
	}
}

bool Syncer::start() {
	if (_busy) {
		_again = true;
		return false;
	}
	_busy = true;
	_again = false;
	_status = Status::Working;
	_updates.fire({});
	return true;
}

void Syncer::finish(Status status, QString error) {
	_busy = false;
	_status = status;
	_error = std::move(error);
	if (status == Status::Active) {
		_lastSync = base::unixtime::now();
	}
	_updates.fire({});
	if (base::take(_again) && hasKey()) {
		_debounce.callOnce(kDebounce);
	}
}

void Syncer::fail(int status, const QString &details) {
	finish(
		status ? Status::Failed : Status::Offline,
		details.isEmpty() ? QString::number(status) : details);
}

void Syncer::forgetKey() {
	account().key = QByteArray();
	WriteState();
}

void Syncer::withBot(Fn<void(not_null<UserData*>)> then) {
	if (_bot) {
		then(_bot);
		return;
	}
	_session->api().request(MTPcontacts_ResolveUsername(
		MTP_flags(0),
		MTP_string(kBot.utf16()),
		MTP_string()
	)).done(crl::guard(this, [=](const MTPcontacts_ResolvedPeer &result) {
		const auto &data = result.data();
		_session->data().processUsers(data.vusers());
		_session->data().processChats(data.vchats());
		const auto peer = _session->data().peerLoaded(
			peerFromMTP(data.vpeer()));
		if (const auto user = peer ? peer->asUser() : nullptr) {
			_bot = user;
			then(user);
		} else {
			fail(-1, u"bot"_q);
		}
	})).fail(crl::guard(this, [=](const MTP::Error &error) {
		fail(-1, error.type());
	})).send();
}

void Syncer::authenticate(Fn<void()> then) {
	withBot([=](not_null<UserData*> bot) {
		// A simple web view would not get the signed user data.
		_session->api().request(MTPmessages_RequestMainWebView(
			MTP_flags(0),
			bot->input(),
			bot->inputUser(),
			MTPstring(),
			MTPDataJSON(),
			MTP_string("macos")
		)).done(crl::guard(this, [=](const MTPWebViewResult &result) {
			const auto url = QUrl(qs(result.data().vurl()));
			const auto initData = QUrlQuery(
				url.fragment(QUrl::FullyEncoded)
			).queryItemValue(u"tgWebAppData"_q, QUrl::FullyDecoded);
			if (initData.isEmpty()) {
				fail(-1, u"webview"_q);
				return;
			}
			Http("POST", u"/v1/auth"_q, QString(), QJsonObject{
				{ u"initData"_q, initData },
			}, crl::guard(this, [=](int status, QJsonObject object) {
				const auto token = object.value(u"token"_q).toString();
				if (status == 200 && !token.isEmpty()) {
					account().token = token;
					WriteState();
					then();
				} else {
					fail(status, u"auth"_q);
				}
			}));
		})).fail(crl::guard(this, [=](const MTP::Error &error) {
			fail(-1, error.type());
		})).send();
	});
}

void Syncer::call(
		const QByteArray &method,
		const QString &path,
		std::optional<QJsonObject> body,
		Done done,
		bool retried) {
	const auto send = [=] {
		Http(method, path, account().token, body, crl::guard(this, [=](
				int status,
				QJsonObject object) {
			if (status == 401 && !retried) {
				account().token = QString();
				WriteState();
				call(method, path, body, done, true);
			} else {
				done(status, object);
			}
		}));
	};
	if (account().token.isEmpty()) {
		authenticate(send);
	} else {
		send();
	}
}

std::optional<QString> Syncer::encrypt(const Payload &payload) {
	const auto blob = Encrypt(account().key, aad(), Serialize(payload));
	return blob
		? std::make_optional(QString::fromLatin1(blob->toBase64()))
		: std::nullopt;
}

void Syncer::fetch(int attempt) {
	call("GET", u"/v1/space"_q, std::nullopt, [=](
			int status,
			QJsonObject object) {
		if (status != 200) {
			fail(status);
			return;
		}
		if (!object.value(u"exists"_q).toBool()) {
			if (hasKey()) {
				forgetKey();
			}
			finish(Status::Disabled);
			return;
		} else if (!hasKey()) {
			finish(Status::Pairing);
			return;
		} else if (object.value(u"keyId"_q).toString()
				!= KeyId(account().key)) {
			forgetKey();
			finish(Status::KeyReplaced);
			return;
		}
		const auto plain = Decrypt(
			account().key,
			aad(),
			QByteArray::fromBase64(
				object.value(u"blob"_q).toString().toLatin1()));
		if (!plain) {
			finish(Status::Failed, u"decrypt"_q);
			return;
		}
		const auto remote = FromJson(QJsonDocument::fromJson(*plain).object());
		const auto local = LocalPayload(_session);
		const auto merged = Merge(local, remote);
		Apply(_session, merged, local);
		if (Serialize(merged) == Serialize(remote)) {
			finish(Status::Active);
		} else {
			upload(merged, object.value(u"version"_q).toInt(), attempt);
		}
	});
}

void Syncer::upload(const Payload &payload, int version, int attempt) {
	const auto blob = encrypt(payload);
	if (!blob) {
		finish(Status::Failed, u"encrypt"_q);
		return;
	}
	call("PUT", u"/v1/space"_q, QJsonObject{
		{ u"keyId"_q, KeyId(account().key) },
		{ u"version"_q, version },
		{ u"blob"_q, *blob },
	}, [=](int status, QJsonObject object) {
		const auto error = object.value(u"error"_q).toString();
		if (status == 200) {
			finish(Status::Active);
		} else if (status == 409 && !version) {
			forgetKey();
			finish(Status::Pairing);
		} else if (status == 409 && error == u"key"_q) {
			forgetKey();
			finish(Status::KeyReplaced);
		} else if (status == 409 && attempt + 1 < kMaxAttempts) {
			fetch(attempt + 1);
		} else {
			fail(status);
		}
	});
}

void Syncer::enable() {
	if (!start()) {
		return;
	}
	account().key = RandomBytes(kKeySize);
	WriteState();
	upload(LocalPayload(_session), 0, 0);
}

bool Syncer::pair(const QString &text) {
	const auto url = QUrl(text.trimmed());
	const auto query = QUrlQuery(url.query());
	const auto key = QByteArray::fromBase64(
		query.queryItemValue(u"k"_q).toLatin1(),
		QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
	const auto user = query.queryItemValue(u"u"_q).toULongLong();
	if (url.scheme() != kShareScheme.utf16()
		|| key.size() != kKeySize
		|| user != _session->userId().bare) {
		return false;
	}
	account().key = key;
	WriteState();
	refresh();
	return true;
}

void Syncer::resetKey() {
	if (!start()) {
		return;
	}
	account().key = RandomBytes(kKeySize);
	WriteState();
	const auto blob = encrypt(LocalPayload(_session));
	if (!blob) {
		finish(Status::Failed, u"encrypt"_q);
		return;
	}
	call("POST", u"/v1/space/reset"_q, QJsonObject{
		{ u"keyId"_q, KeyId(account().key) },
		{ u"blob"_q, *blob },
	}, [=](int status, QJsonObject object) {
		if (status == 200) {
			finish(Status::Active);
		} else {
			fail(status);
		}
	});
}

void Syncer::removeRemote() {
	if (!start()) {
		return;
	}
	call("DELETE", u"/v1/space"_q, std::nullopt, [=](
			int status,
			QJsonObject object) {
		if (status == 200) {
			forgetKey();
			finish(Status::Disabled);
		} else {
			fail(status);
		}
	});
}

[[nodiscard]] base::flat_map<
	not_null<Main::Session*>,
	std::unique_ptr<Syncer>> &Syncers() {
	static auto result = base::flat_map<
		not_null<Main::Session*>,
		std::unique_ptr<Syncer>>();
	return result;
}

void ScheduleAll() {
	for (const auto &[session, syncer] : Syncers()) {
		syncer->schedule();
	}
}

void AddLabel(not_null<Ui::VerticalLayout*> content, const QString &text) {
	content->add(
		object_ptr<Ui::FlatLabel>(content, text, st::boxLabel),
		st::boxRowPadding + QMargins(0, 0, 0, st::boxLittleSkip));
}

void AddButton(
		not_null<Ui::VerticalLayout*> content,
		const QString &text,
		Fn<void()> callback) {
	const auto row = content->add(
		object_ptr<Ui::RpWidget>(content),
		st::boxRowPadding + QMargins(0, 0, 0, st::boxLittleSkip));
	const auto button = Ui::CreateChild<Ui::RoundButton>(
		row,
		rpl::single(text),
		st::defaultLightButton);
	row->resize(row->width(), button->height());
	row->widthValue() | rpl::on_next([=](int width) {
		button->moveToLeft((width - button->width()) / 2, 0, width);
	}, row->lifetime());
	button->setClickedCallback(std::move(callback));
	button->show();
}

void AddQr(not_null<Ui::VerticalLayout*> content, const QString &text) {
	const auto data = Qr::Encode(text);
	const auto ratio = style::DevicePixelRatio();
	const auto quiet = 2;
	const auto pixel = std::max(1, (st::boxWideWidth / 2) / (data.size + 2 * quiet));
	auto image = Qr::Generate(data, pixel * ratio, Qt::black, Qt::white);
	image.setDevicePixelRatio(ratio);
	const auto side = (data.size + 2 * quiet) * pixel;
	const auto row = content->add(
		object_ptr<Ui::RpWidget>(content),
		st::boxRowPadding + QMargins(0, 0, 0, st::boxLittleSkip));
	row->resize(row->width(), side);
	row->paintRequest() | rpl::on_next([=] {
		auto p = QPainter(row);
		const auto left = (row->width() - side) / 2;
		p.fillRect(left, 0, side, side, Qt::white);
		p.drawImage(left + quiet * pixel, quiet * pixel, image);
	}, row->lifetime());
}

void AddPairing(
		not_null<Ui::VerticalLayout*> content,
		not_null<Syncer*> syncer) {
	const auto field = content->add(
		object_ptr<Ui::InputField>(
			content,
			st::defaultInputField,
			rpl::single(Text(Phrase::SyncKeyPlaceholder))),
		st::boxRowPadding + QMargins(0, 0, 0, st::boxLittleSkip));
	AddButton(content, Text(Phrase::SyncValidate), [=] {
		if (!syncer->pair(field->getLastText())) {
			field->showError();
			Ui::Toast::Show(Text(Phrase::SyncWrongKey));
		}
	});
}

void FillBox(
		not_null<Ui::VerticalLayout*> content,
		not_null<Window::SessionController*> controller,
		not_null<Syncer*> syncer) {
	const auto status = syncer->status();
	if (syncer->hasKey()) {
		AddQr(content, syncer->shareText());
		AddLabel(content, Text(Phrase::SyncScanAbout));
		AddButton(content, Text(Phrase::SyncCopyKey), [=] {
			QGuiApplication::clipboard()->setText(syncer->shareText());
			Ui::Toast::Show(Text(Phrase::SyncCopied));
		});
		AddButton(content, Text(Phrase::SyncNow), [=] {
			syncer->refresh();
		});
		AddButton(content, Text(Phrase::SyncNewKey), [=] {
			controller->show(Ui::MakeConfirmBox({
				.text = Text(Phrase::SyncNewKeyConfirm),
				.confirmed = [=](Fn<void()> close) {
					syncer->resetKey();
					close();
				},
			}));
		});
		AddButton(content, Text(Phrase::SyncDelete), [=] {
			controller->show(Ui::MakeConfirmBox({
				.text = Text(Phrase::SyncDeleteConfirm),
				.confirmed = [=](Fn<void()> close) {
					syncer->removeRemote();
					close();
				},
			}));
		});
	} else if (status == Status::Pairing || status == Status::KeyReplaced) {
		AddLabel(content, Text(status == Status::Pairing
			? Phrase::SyncPairingAbout
			: Phrase::SyncKeyReplaced));
		AddPairing(content, syncer);
	} else {
		AddLabel(content, Text(Phrase::SyncAbout));
		if (status != Status::Working && status != Status::Unknown) {
			AddButton(content, Text(Phrase::SyncEnable), [=] {
				syncer->enable();
			});
		}
	}
	auto line = QString();
	if (status == Status::Working || status == Status::Unknown) {
		line = Text(Phrase::SyncWorking);
	} else if (status == Status::Offline) {
		line = Text(Phrase::SyncOffline);
	} else if (status == Status::Failed) {
		line = Text(Phrase::SyncError) + u" ("_q + syncer->error() + ')';
	} else if (status == Status::Active && syncer->lastSync()) {
		line = Text(Phrase::SyncLast) + ' ' + QLocale().toString(
			base::unixtime::parse(syncer->lastSync()).time(),
			QLocale::ShortFormat);
	}
	if (!line.isEmpty()) {
		AddLabel(content, line);
	}
}

} // namespace

void StartSync(not_null<Main::Session*> session) {
	Syncers().emplace(session, std::make_unique<Syncer>(session));
	session->lifetime().add([=] {
		Syncers().remove(session);
	});
}

void ShowSyncBox(not_null<Window::SessionController*> controller) {
	const auto i = Syncers().find(&controller->session());
	if (i == end(Syncers())) {
		return;
	}
	const auto syncer = i->second.get();
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(rpl::single(Text(Phrase::SyncTitle)));
		box->setWidth(st::boxWideWidth);
		box->addButton(tr::lng_close(), [=] { box->closeBox(); });
		const auto content = box->addRow(
			object_ptr<Ui::VerticalLayout>(box),
			QMargins());
		const auto rebuild = [=] {
			content->clear();
			FillBox(content, controller, syncer);
			content->resizeToWidth(st::boxWideWidth);
		};
		syncer->updates() | rpl::on_next([=] {
			crl::on_main(box, rebuild);
		}, box->lifetime());
		rebuild();
		if (!syncer->hasKey()) {
			syncer->refresh();
		}
	}));
}

void NoteColorChanged(uint64 peerId) {
	GlobalState().colorTimes[peerId] = Now();
	WriteState();
	ScheduleAll();
}

void NoteBanChanged(uint64 peerId) {
	GlobalState().banTimes[peerId] = Now();
	WriteState();
	ScheduleAll();
}

void NotePinsChanged(not_null<Main::Session*> session) {
	GlobalState().accounts[session->userId().bare].pinsTime = Now();
	WriteState();
	const auto i = Syncers().find(session);
	if (i != end(Syncers())) {
		i->second->schedule();
	}
}

} // namespace MikGram
