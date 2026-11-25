/*
This file is part of MikGram, a personal fork of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Main {
class Session;
} // namespace Main

namespace Window {
class SessionController;
} // namespace Window

namespace MikGram {

// Format: https://github.com/grafmik/mikgram-sync/blob/main/PROTOCOL.md
void StartSync(not_null<Main::Session*> session);
void ShowSyncBox(not_null<Window::SessionController*> controller);

void NoteColorChanged(uint64 peerId);
void NoteBanChanged(uint64 peerId);
void NotePinsChanged(not_null<Main::Session*> session);

} // namespace MikGram
