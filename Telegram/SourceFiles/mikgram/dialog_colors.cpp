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
#include "ui/painter.h"
#include "ui/rp_widget.h"
#include "ui/widgets/menu/menu_item_base.h"
#include "ui/widgets/popup_menu.h"
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
constexpr auto kPerRow = 5;

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

class SwatchesItem final : public Ui::Menu::ItemBase {
public:
	SwatchesItem(
		not_null<Ui::PopupMenu*> parent,
		const style::Menu &st,
		not_null<PeerData*> peer);

	not_null<QAction*> action() const override;
	bool isEnabled() const override;

protected:
	int contentHeight() const override;

private:
	const not_null<QAction*> _dummyAction;
	int _height = 0;

};

SwatchesItem::SwatchesItem(
	not_null<Ui::PopupMenu*> parent,
	const style::Menu &st,
	not_null<PeerData*> peer)
: ItemBase(parent->menu(), st)
, _dummyAction(Ui::CreateChild<QAction>(this)) {
	const auto padding = st.itemPadding;
	const auto swatches = Ui::CreateChild<Swatches>(
		this,
		peer,
		[=] { setClicked(); });
	const auto width = 2 * st::settingsAccentColorLine * 2
		+ kPerRow * st::settingsAccentColorSize
		+ (kPerRow - 1) * st::settingsAccentColorSkip * kSpacingRatio;
	swatches->resizeToWidth(width);
	swatches->move(padding.left(), padding.top());
	_height = padding.top() + swatches->height() + padding.bottom();
	setMinWidth(padding.left() + width + padding.right());
	fitToMenuWidth();
}

not_null<QAction*> SwatchesItem::action() const {
	return _dummyAction;
}

bool SwatchesItem::isEnabled() const {
	return true;
}

int SwatchesItem::contentHeight() const {
	return _height;
}

[[nodiscard]] QString DialogColorText() {
	static const auto map = base::flat_map<QString, QString>{
		{ u"de"_q, u"Hintergrundfarbe"_q },
		{ u"es"_q, u"Color de fondo"_q },
		{ u"fr"_q, u"Couleur de fond"_q },
		{ u"it"_q, u"Colore di sfondo"_q },
		{ u"nl"_q, u"Achtergrondkleur"_q },
		{ u"pl"_q, u"Kolor t\u0142a"_q },
		{ u"pt"_q, u"Cor de fundo"_q },
		{ u"ru"_q, u"\u0426\u0432\u0435\u0442 \u0444\u043e\u043d\u0430"_q },
		{ u"tr"_q, u"Arka plan rengi"_q },
		{ u"uk"_q, u"\u041a\u043e\u043b\u0456\u0440 \u0442\u043b\u0430"_q },
	};
	const auto i = map.find(Lang::Id().section('-', 0, 0));
	return (i != end(map)) ? i->second : u"Background color"_q;
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
		PeerData *peer,
		const Ui::Menu::MenuCallback &addAction) {
	if (!peer) {
		return;
	}
	addAction(Ui::Menu::MenuCallback::Args{
		.text = DialogColorText(),
		.handler = nullptr,
		.icon = &st::menuIconPalette,
		.fillSubmenu = [=](not_null<Ui::PopupMenu*> menu) {
			menu->addAction(base::make_unique_q<SwatchesItem>(
				menu,
				menu->st().menu,
				peer));
		},
	});
}

} // namespace MikGram
