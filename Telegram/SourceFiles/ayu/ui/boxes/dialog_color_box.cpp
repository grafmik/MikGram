// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "ayu/ui/boxes/dialog_color_box.h"

#include "ayu/ayu_settings.h"
#include "data/data_peer.h"
#include "lang_auto.h"
#include "ui/layers/generic_box.h"
#include "ui/painter.h"
#include "ui/rp_widget.h"
#include "styles/style_layers.h"
#include "window/window_session_controller.h"

#include <QtGui/QMouseEvent>

namespace AyuUi {
namespace {

constexpr auto kPadding = 12;
constexpr auto kDiameter = 36;
constexpr auto kSpacing = 16;

// A grid of tappable color swatches (plus a leading "none" swatch that clears
// the color). Tapping a swatch writes the choice to AyuSettings immediately and
// invokes the chosen-callback (used to close the containing box).
class SwatchesWidget final : public Ui::RpWidget {
public:
	SwatchesWidget(QWidget *parent, uint64 peerId, Fn<void()> chosen)
	: RpWidget(parent)
	, _peerId(peerId)
	, _chosen(std::move(chosen)) {
		_selected = AyuSettings::getInstance().dialogColor(peerId);
		setCursor(Qt::PointingHandCursor);
	}

protected:
	int resizeGetHeight(int newWidth) override {
		const auto count = 1 + int(DialogColorPalette().size());
		const auto avail = std::max(newWidth - 2 * kPadding, kDiameter);
		auto perRow = std::max(1, (avail + kSpacing) / (kDiameter + kSpacing));
		perRow = std::min(perRow, count);
		const auto rows = (count + perRow - 1) / perRow;
		_rects.clear();
		_rects.reserve(count);
		for (auto i = 0; i != count; ++i) {
			const auto r = i / perRow;
			const auto c = i % perRow;
			const auto x = kPadding + c * (kDiameter + kSpacing);
			const auto y = kPadding + r * (kDiameter + kSpacing);
			_rects.push_back(QRect(x, y, kDiameter, kDiameter));
		}
		return kPadding * 2 + rows * kDiameter + (rows - 1) * kSpacing;
	}

	void paintEvent(QPaintEvent *e) override {
		auto p = Painter(this);
		auto hq = PainterHighQualityEnabler(p);
		const auto &palette = DialogColorPalette();
		for (auto i = 0; i != int(_rects.size()); ++i) {
			const auto rect = _rects[i];
			const auto colorIndex = i - 1; // i == 0 -> "none" (-1)
			if (colorIndex == _selected) {
				auto pen = QPen(st::windowActiveTextFg);
				pen.setWidth(st::lineWidth * 2);
				p.setPen(pen);
				p.setBrush(Qt::NoBrush);
				p.drawEllipse(QRectF(rect).marginsAdded({ 3, 3, 3, 3 }));
			}
			if (colorIndex < 0) {
				p.setPen(QPen(st::windowSubTextFg, st::lineWidth));
				p.setBrush(st::boxBg);
				p.drawEllipse(rect);
				p.drawLine(
					rect.topRight() + QPoint(-8, 8),
					rect.bottomLeft() + QPoint(8, -8));
			} else {
				p.setPen(Qt::NoPen);
				p.setBrush(palette[colorIndex]);
				p.drawEllipse(rect);
			}
		}
	}

	void mousePressEvent(QMouseEvent *e) override {
		for (auto i = 0; i != int(_rects.size()); ++i) {
			if (_rects[i].contains(e->pos())) {
				const auto colorIndex = i - 1;
				AyuSettings::getInstance().setDialogColor(_peerId, colorIndex);
				_selected = colorIndex;
				update();
				if (_chosen) {
					_chosen();
				}
				return;
			}
		}
	}

private:
	uint64 _peerId = 0;
	int _selected = -1;
	std::vector<QRect> _rects;
	Fn<void()> _chosen;

};

} // namespace

const std::vector<QColor> &DialogColorPalette() {
	static const auto palette = std::vector<QColor>{
		QColor(0xE8, 0x74, 0x6B), // red
		QColor(0xE8, 0xA2, 0x4E), // orange
		QColor(0xE3, 0xC7, 0x4B), // yellow
		QColor(0x6F, 0xB8, 0x6B), // green
		QColor(0x4F, 0xB0, 0xA5), // teal
		QColor(0x5A, 0xA0, 0xE0), // blue
		QColor(0x9B, 0x7B, 0xD6), // purple
		QColor(0xDE, 0x7F, 0xB0), // pink
	};
	return palette;
}

void ShowDialogColorBox(
		Window::SessionController *controller,
		PeerData *peer) {
	if (!controller || !peer) {
		return;
	}
	const auto peerId = peer->id.value;
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(rpl::single(QString::fromUtf8("Couleur de fond")));
		box->addRow(object_ptr<SwatchesWidget>(
			box,
			peerId,
			[=] { box->closeBox(); }));
		box->addButton(tr::lng_close(), [=] { box->closeBox(); });
	}));
}

} // namespace AyuUi
