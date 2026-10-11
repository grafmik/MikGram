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
