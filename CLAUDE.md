# Claude Code Pointer

Read `AGENTS.md` and treat it as the canonical repository-wide instructions.

## MikGram : publication Mac

Le build Mac est en téléchargement sur https://9mp.org/mikgram/. La page pointe vers `releases/latest/download/MikGram-mac.zip` de `grafmik/MikGram` : republier, c'est seulement créer une nouvelle release, le site ne change pas.

Après chaque changement de MikGram poussé sur `dev` :
1. compiler par la recette Custom Rebase : `"/Applications/Custom Rebase.app/Contents/MacOS/CustomRebase" --update mikgram` (ne pas compiler depuis ce checkout) ;
2. lancer `./publish-mac.sh` : il zippe `~/app/ayugram-build/ayugram/out/Release/Telegram.app` en `MikGram.app`, crée la release `mac-<version>-<commit>` et vérifie que le lien stable sert le nouveau zip. Sans nouveau build, il ne fait rien.

Le build ne fait que l'Apple Silicon (arm64, macOS 12+). Il est signé ad hoc, sans notarisation : la page du site explique comment l'ouvrir la première fois.
