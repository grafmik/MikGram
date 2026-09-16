// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#pragma once

#include <QtGui/QColor>
#include <vector>

class PeerData;

namespace Window {
class SessionController;
} // namespace Window

namespace AyuUi {

// Predefined assortment of background tints for dialogs-list rows.
// The index into this vector is what gets stored per peer in AyuSettings.
[[nodiscard]] const std::vector<QColor> &DialogColorPalette();

// Opens a small box letting the user pick (or clear) the background color
// of a single conversation in the left dialogs list.
void ShowDialogColorBox(
	Window::SessionController *controller,
	PeerData *peer);

} // namespace AyuUi
