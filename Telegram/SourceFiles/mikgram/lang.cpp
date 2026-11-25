/*
This file is part of MikGram, a personal fork of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "mikgram/lang.h"

#include "lang/lang_keys.h"

namespace MikGram {
namespace {

using Phrases = std::array<QString, int(Phrase::Count)>;

[[nodiscard]] const Phrases &English() {
	static const auto result = Phrases{
		u"Background color"_q,
		u"Ban channel"_q,
		u"Unban channel"_q,
		u"Ban group"_q,
		u"Unban group"_q,
		u"Unban"_q,
		u"Banned chats"_q,
		u"banned"_q,
		u"No banned chats."_q,
		u"macOS could not transcribe this message"_q,
		u"Allow MikGram in System Settings > Privacy & Security > Speech Recognition"_q,
		u"Speech recognition is unavailable for this language: turn on Dictation in System Settings > Keyboard"_q,
		u"Sync"_q,
		u"Keep background colors, pinned chats and banned chats in sync between your MikGram devices. They are encrypted on your devices: the server (mikgram.9mp.org) cannot read them."_q,
		u"Turn on sync"_q,
		u"This account is already synced on another device. Open Sync there, copy the key and paste it here."_q,
		u"The key was replaced on another device. Paste the new one."_q,
		u"Sync key"_q,
		u"Use this key"_q,
		u"Scan this QR code with MikGram on your phone (menu \u22ee \u203a Sync), or copy the key."_q,
		u"Copy the key"_q,
		u"Key copied"_q,
		u"Sync now"_q,
		u"New key"_q,
		u"Create a new key? Your other devices will have to scan the new QR code."_q,
		u"Delete my online data"_q,
		u"Delete the synced data from the server? What is on your devices stays."_q,
		u"Last sync:"_q,
		u"Offline: sync will resume later."_q,
		u"Sync failed"_q,
		u"This key is not for this account."_q,
		u"Syncing\u2026"_q,
	};
	return result;
}

[[nodiscard]] const base::flat_map<QString, Phrases> &Translations() {
	static const auto result = base::flat_map<QString, Phrases>{
		{ u"de"_q, Phrases{ u"Hintergrundfarbe"_q } },
		{ u"es"_q, Phrases{ u"Color de fondo"_q } },
		{ u"fr"_q, Phrases{
			u"Couleur de fond"_q,
			u"Bannir le canal"_q,
			u"Débannir le canal"_q,
			u"Bannir le groupe"_q,
			u"D\u00e9bannir le groupe"_q,
			u"D\u00e9bannir"_q,
			u"Discussions bannies"_q,
			u"banni"_q,
			u"Aucune discussion bannie."_q,
			u"macOS n'a pas pu transcrire ce message"_q,
			u"Autorisez MikGram dans R\u00e9glages Syst\u00e8me > Confidentialit\u00e9 et s\u00e9curit\u00e9 > Reconnaissance vocale"_q,
			u"Reconnaissance vocale indisponible pour cette langue : activez la Dict\u00e9e dans R\u00e9glages Syst\u00e8me > Clavier"_q,
			u"Synchro"_q,
			u"Retrouvez les couleurs de fond, les discussions \u00e9pingl\u00e9es et les discussions bannies sur tous vos appareils MikGram. Elles sont chiffr\u00e9es sur vos appareils : le serveur (mikgram.9mp.org) ne peut pas les lire."_q,
			u"Activer la synchro"_q,
			u"Ce compte est d\u00e9j\u00e0 synchronis\u00e9 sur un autre appareil. Ouvrez Synchro sur celui-ci, copiez la cl\u00e9 et collez-la ici."_q,
			u"La cl\u00e9 a \u00e9t\u00e9 remplac\u00e9e sur un autre appareil. Collez la nouvelle."_q,
			u"Cl\u00e9 de synchro"_q,
			u"Utiliser cette cl\u00e9"_q,
			u"Scannez ce QR code avec MikGram sur votre t\u00e9l\u00e9phone (menu \u22ee \u203a Synchro), ou copiez la cl\u00e9."_q,
			u"Copier la cl\u00e9"_q,
			u"Cl\u00e9 copi\u00e9e"_q,
			u"Synchroniser maintenant"_q,
			u"Nouvelle cl\u00e9"_q,
			u"Cr\u00e9er une nouvelle cl\u00e9 ? Vos autres appareils devront scanner le nouveau QR code."_q,
			u"Supprimer mes donn\u00e9es en ligne"_q,
			u"Supprimer les donn\u00e9es synchronis\u00e9es du serveur ? Ce qui est sur vos appareils reste."_q,
			u"Derni\u00e8re synchro :"_q,
			u"Hors ligne : la synchro reprendra plus tard."_q,
			u"\u00c9chec de la synchro"_q,
			u"Cette cl\u00e9 n'est pas pour ce compte."_q,
			u"Synchronisation\u2026"_q,
		} },
		{ u"it"_q, Phrases{ u"Colore di sfondo"_q } },
		{ u"nl"_q, Phrases{ u"Achtergrondkleur"_q } },
		{ u"pl"_q, Phrases{ u"Kolor tła"_q } },
		{ u"pt"_q, Phrases{ u"Cor de fundo"_q } },
		{ u"ru"_q, Phrases{ u"Цвет фона"_q } },
		{ u"tr"_q, Phrases{ u"Arka plan rengi"_q } },
		{ u"uk"_q, Phrases{ u"Колір тла"_q } },
	};
	return result;
}

} // namespace

QString Text(Phrase phrase) {
	const auto index = int(phrase);
	const auto &map = Translations();
	const auto i = map.find(Lang::Id().section('-', 0, 0));
	return (i != end(map) && !i->second[index].isEmpty())
		? i->second[index]
		: English()[index];
}

} // namespace MikGram
