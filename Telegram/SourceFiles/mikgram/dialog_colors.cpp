/*
This file is part of MikGram, a personal fork of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "mikgram/dialog_colors.h"

#include "data/data_peer.h"
#include "data/data_session.h"
#include "history/history.h"
#include "lang/lang_keys.h"
#include "settings.h"
#include "ui/layers/generic_box.h"
#include "ui/painter.h"
#include "ui/rp_widget.h"
#include "window/window_session_controller.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"

#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QSaveFile>
#include <QtGui/QMouseEvent>

namespace MikGram {
namespace {

constexpr auto kNoColor = -1;
constexpr auto kSpacingRatio = 3;

[[nodiscard]] QString StoragePath() {
	return cWorkingDir() + u"tdata/mikgram_dialog_colors.json"_q;
}

[[nodiscard]] base::flat_map<uint64, int> ReadColors() {
	auto result = base::flat_map<uint64, int>();
	auto file = QFile(StoragePath());
	if (!file.open(QIODevice::ReadOnly)) {
		return result;
	}
	const auto object = QJsonDocument::fromJson(file.readAll()).object();
	const auto count = int(DialogColorPalette().size());
	for (auto i = object.constBegin(); i != object.constEnd(); ++i) {
		const auto peerId = i.key().toULongLong();
		const auto index = i.value().toInt(kNoColor);
		if (peerId && index >= 0 && index < count) {
			result.emplace(peerId, index);
		}
	}
	return result;
}

[[nodiscard]] base::flat_map<uint64, int> &Colors() {
	static auto result = ReadColors();
	return result;
}

void WriteColors() {
	auto object = QJsonObject();
	for (const auto &[peerId, index] : Colors()) {
		object.insert(QString::number(peerId), index);
	}
	auto file = QSaveFile(StoragePath());
	if (file.open(QIODevice::WriteOnly)) {
		file.write(QJsonDocument(object).toJson(QJsonDocument::Compact));
		file.commit();
	}
}

void SetDialogColor(not_null<PeerData*> peer, int index) {
	auto &colors = Colors();
	const auto key = peer->id.value;
	if (index < 0) {
		if (!colors.remove(key)) {
			return;
		}
	} else {
		const auto i = colors.find(key);
		if (i != end(colors) && i->second == index) {
			return;
		}
		colors[key] = index;
	}
	WriteColors();
	if (const auto history = peer->owner().historyLoaded(peer)) {
		history->updateChatListEntry();
	}
}

class Swatches final : public Ui::RpWidget {
public:
	Swatches(
		QWidget *parent,
		not_null<PeerData*> peer,
		Fn<void()> chosen);

protected:
	int resizeGetHeight(int newWidth) override;
	void paintEvent(QPaintEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;

private:
	const not_null<PeerData*> _peer;
	const Fn<void()> _chosen;
	std::vector<QRect> _rects;
	int _selected = kNoColor;

};

Swatches::Swatches(
	QWidget *parent,
	not_null<PeerData*> peer,
	Fn<void()> chosen)
: RpWidget(parent)
, _peer(peer)
, _chosen(std::move(chosen))
, _selected(DialogColor(peer->id)) {
	setCursor(style::cur_pointer);
}

int Swatches::resizeGetHeight(int newWidth) {
	const auto size = st::settingsAccentColorSize;
	const auto skip = st::settingsAccentColorSkip * kSpacingRatio;
	const auto line = st::settingsAccentColorLine * 2;
	const auto count = 1 + int(DialogColorPalette().size());
	const auto perRow = std::clamp(
		(newWidth - 2 * line + skip) / (size + skip),
		1,
		count);
	const auto rows = (count + perRow - 1) / perRow;
	_rects.clear();
	_rects.reserve(count);
	for (auto i = 0; i != count; ++i) {
		_rects.push_back(QRect(
			line + (i % perRow) * (size + skip),
			line + (i / perRow) * (size + skip),
			size,
			size));
	}
	return 2 * line + rows * size + (rows - 1) * skip;
}

void Swatches::paintEvent(QPaintEvent *e) {
	auto p = Painter(this);
	auto hq = PainterHighQualityEnabler(p);
	const auto &palette = DialogColorPalette();
	const auto line = st::settingsAccentColorLine;
	for (auto i = 0; i != int(_rects.size()); ++i) {
		const auto rect = _rects[i];
		const auto index = i - 1;
		if (index == _selected) {
			p.setPen(QPen(st::windowActiveTextFg, line));
			p.setBrush(Qt::NoBrush);
			p.drawEllipse(rect.marginsAdded({ line, line, line, line }));
		}
		if (index < 0) {
			const auto inset = rect.width() / 4;
			p.setPen(QPen(st::windowSubTextFg, st::lineWidth));
			p.setBrush(st::boxBg);
			p.drawEllipse(rect);
			p.drawLine(
				rect.topRight() + QPoint(-inset, inset),
				rect.bottomLeft() + QPoint(inset, -inset));
		} else {
			p.setPen(Qt::NoPen);
			p.setBrush(palette[index]);
			p.drawEllipse(rect);
		}
	}
}

void Swatches::mousePressEvent(QMouseEvent *e) {
	for (auto i = 0; i != int(_rects.size()); ++i) {
		if (_rects[i].contains(e->pos())) {
			_selected = i - 1;
			SetDialogColor(_peer, _selected);
			update();
			_chosen();
			return;
		}
	}
}

void ShowDialogColorBox(
		not_null<Window::SessionController*> controller,
		not_null<PeerData*> peer) {
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(rpl::single(u"Couleur de fond"_q));
		box->addRow(object_ptr<Swatches>(
			box,
			peer,
			[=] { box->closeBox(); }));
		box->addButton(tr::lng_close(), [=] { box->closeBox(); });
	}));
}

} // namespace

const std::vector<QColor> &DialogColorPalette() {
	static const auto result = std::vector<QColor>{
		QColor(0xE8, 0x74, 0x6B),
		QColor(0xE8, 0xA2, 0x4E),
		QColor(0xE3, 0xC7, 0x4B),
		QColor(0x6F, 0xB8, 0x6B),
		QColor(0x4F, 0xB0, 0xA5),
		QColor(0x5A, 0xA0, 0xE0),
		QColor(0x9B, 0x7B, 0xD6),
		QColor(0xDE, 0x7F, 0xB0),
	};
	return result;
}

int DialogColor(PeerId peerId) {
	const auto &colors = Colors();
	const auto i = colors.find(peerId.value);
	return (i != end(colors)) ? i->second : kNoColor;
}

void AddDialogColorAction(
		not_null<Window::SessionController*> controller,
		PeerData *peer,
		const Ui::Menu::MenuCallback &addAction) {
	if (!peer) {
		return;
	}
	addAction(
		u"Couleur de fond"_q,
		[=] { ShowDialogColorBox(controller, peer); },
		&st::menuIconPalette);
}

} // namespace MikGram
