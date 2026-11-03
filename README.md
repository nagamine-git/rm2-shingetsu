rm2-shingetsu
=============

reMarkable 2 の端末アプリ `yaft` に、**Colemak キーマップ**、**新月配列の日本語入力** (かな漢字変換は mozc)、**CJK グリフ**を加え、**Vim** を同梱したフォークです。[timower/rM2-stuff](https://github.com/timower/rM2-stuff) (GPL-3.0) をもとにしています。

> **実機での動作は未検証です。** エミュレータ上の自動テスト (QEMU で動かした Vim と mozc、スタブを使った `try.sh` / `install.sh` の試運転) までは通していますが、実際の reMarkable 2 ではまだ動かしていません。「未検証」と書いた箇所は、自分の端末で確かめてください。

## これは何か、なぜ OS のフォークではないのか

reMarkable の OS (xochitl を含む) はプロプライエタリで、ソースコードがありません。そのためファームウェアそのもののフォークは作れません。このリポジトリは、**純正ファームウェアの上で動く**上流の仕組み (画面を横取りする `rm2fb`、端末アプリ `yaft`) の上に機能を足したものです。ルートパーティションは書き換えません。

追加したもの:

- **Colemak キーマップ**: rM Type Folio 用の `rm-colemak`、USB キーボード用の `colemak`。仮想キーボードにも Colemak 配列があります。
- **日本語入力 (IME)**: 新月配列 ([shingetsu-layout](https://github.com/nagamine-git/shingetsu-layout)) によるキー入力からかなへの変換と、mozc によるかな漢字変換。mozc は armhf の最小 chroot (Debian trixie のパッケージ) に入れて、非 root で動かします。
- **CJK グリフ**: BIZ UDGothic から作ったビットマップグリフを yaft に内蔵します。
- **Vim**: 静的リンクの armv7 バイナリ (huge、マルチバイト、cp932 / euc-jp 対応)。ncurses の terminfo は内蔵なので、端末側に terminfo データベースは要りません。

mozc の代わりに [hazkey](https://github.com/7ka-Hiira/hazkey) を使うことはできません。hazkey は Swift と azooKey を必要とし、armv7 向けの公式 Swift ツールチェーンがないためです。同梱の入力テーブルは hazkey のマッチング規則に合わせてあるので、hazkey と同じ挙動で入力できます。

## まず試す (インストールなし)

配布物 `rm2-shingetsu-<バージョン>.tar.gz` を端末に送って、展開したディレクトリの `try.sh` を実行します。何もインストールせず、ルートパーティションも書き換えません。yaft を `exit` で終えると xochitl が戻ります。**再起動すれば必ず元に戻ります。**

```sh
# PC 側 (USB 接続の端末は 10.11.99.1)
scp rm2-shingetsu-<バージョン>.tar.gz root@10.11.99.1:
ssh root@10.11.99.1
```

root のパスワードは、端末の設定の「著作権およびライセンス (Copyrights and licenses)」の画面にあります。ご自身で入力してください。

```sh
# 端末側
mkdir -p rm2-shingetsu
tar -xzf rm2-shingetsu-*.tar.gz -C rm2-shingetsu --strip-components=1
cd rm2-shingetsu
sh try.sh
```

`try.sh` は xochitl を止め、同梱の `rm2fb_server_swtcon` を起動して、`librm2fb_client_swtcon.so` を `LD_PRELOAD` した `yaft` を動かします (上流の [`nix/pkgs/nixos-launcher.sh`](nix/pkgs/nixos-launcher.sh) の起動手順と同じです)。ファームウェアのバージョンが上流の対応リスト (3.20、3.22.4、3.23.0.54 / 3.23.0.64。swtcon モードは 3.28 beta まで報告あり) にない場合は、警告して続けるか尋ねます。

詳しくはバンドルの [`README-device.ja.md`](shingetsu/bundle/README-device.ja.md) を見てください。**実機では未検証です。**

## インストール (常用する)

```sh
sh install.sh                # Vim と mozc の chroot、設定ファイル (無いときだけ)
sh install.sh --with-ipk     # さらに ipk/yaft.ipk を opkg で入れる
sh uninstall.sh [--purge]    # 元に戻す (設定は --purge のときだけ消す)
```

`install.sh` は root で実行し、何度実行しても同じ結果になります。Vim と mozc は `/home/root/.local/share/rm2-shingetsu` の下に入ります。既存の `~/.config/yaft/config.toml` と `~/.vimrc` は上書きせず、`*.rm2-shingetsu` として横に置きます。

yaft 本体の ipk は、上流と同じ前提 (toltec の `/opt`、`opkg`、systemd の `rm2fb.socket`) で作られています。調べた範囲では:

- toltec 本家が対応するのは OS 3.3.2.1666 までで ([toltec-dev.org](https://toltec-dev.org))、リポジトリはアーカイブされ、新しい OS には vellum / reManager が勧められています ([toltec-dev/toltec](https://github.com/toltec-dev/toltec))。vellum は apk 形式で、ipk は扱えません。
- 上流の 3.20 向けの手順は [discussion #47](https://github.com/timower/rM2-stuff/discussions/47) です。`/etc/fstab` の編集と toltec の testing 版 bootstrap を含み、起動しなくなるおそれがあります。復旧手段を用意し、暗号化を無効にしてから行ってください。
- そのため、最近のファームウェアでは `try.sh` で試すのが安全です。`--with-ipk` は、上の手順を済ませた端末向けです。

## 使い方

### キー操作

yaft は英数 (IME オフ) で起動します。日本語を打つには、右 Alt の単押し (`rm-colemak`) または Ctrl + Space で IME をオンにします。

rM Type Folio (`keymap = "rm-colemak"`):

| キー | 動作 |
| --- | --- |
| CapsLock | 単押し = Esc、長押し / 他のキーと同時押し = Ctrl |
| 左 Alt | 単押し = 英数 (IME オフ)、長押し / 同時押し = Alt |
| 右 Alt | 単押し = かな (IME オン)、長押し / 同時押し = Alt |
| Ctrl + Space | IME のオン / オフ |

USB キーボード (`keymap = "colemak"`):

| キー | 動作 |
| --- | --- |
| CapsLock | 単押し = Esc、長押し / 他のキーと同時押し = Ctrl |
| 左 Meta (Cmd / Win) | 単押し = IME オフ、長押し / 同時押し = Alt |
| 右 Meta | 単押し = IME オン、長押し / 同時押し = Alt |
| 無変換 / 英数 | IME オフ |
| 変換 / かな | IME オン |
| 半角/全角 | IME のオン / オフ |
| Ctrl + Space | IME のオン / オフ |

IME をオンにしている間の操作 (新月配列。`table = "colemak"` が既定、QWERTY で打つなら `"qwerty"`):

| キー | 動作 |
| --- | --- |
| Space | 変換 (もう一度で次の候補) |
| ↓ / Tab、↑ | 変換中に次の候補、前の候補 |
| ← / → | 文節の移動 |
| Shift + ← / → | 文節の長さを変える |
| 1〜9 | 変換中に候補の選択位置を動かすだけ (mozc と同じ)。確定は Enter |
| Enter | 確定 (変換前に押すと、かなのまま確定) |
| 文字キー | 変換中なら、いまの候補を確定して続けて入力 |
| Esc / Backspace (変換中) | 変換をやめて、かなの入力に戻る |
| Backspace (入力中) | 1 文字消す |
| Esc (入力中) | 入力中のかなを捨てる |
| 矢印など (入力中) | 入力中のかなを確定してから、そのキーをアプリに渡す |
| Esc (未入力のとき) | IME をオフにして、Esc をアプリに渡す (`escape-off`) |

Vim は標準のキーのままで、Colemak 用のリマップはありません。

### 設定

`~/.config/yaft/config.toml` (バンドルの `config/yaft-config.toml` が雛形):

```toml
layout = "colemak"        # 画面上のキーボード: colemak / qwerty
keymap = "rm-colemak"     # rm-colemak / rm-qwerty (Type Folio)、colemak / qwerty (USB)、timower (上流)

[ime]
enabled = true            # 日本語入力の機能を使うか (起動時の IME はオフ)
table = "colemak"         # "colemak"、"qwerty"、または hazkey 形式の TSV のパス
escape-off = true         # 未入力の Esc で IME をオフにする
mozc-root = "/home/root/.local/share/rm2-shingetsu/mozc-root"
mozc-uid = 1000           # mozc を動かす uid (root では動かない)
mozc-command = []         # 空でなければ chroot の代わりにこのコマンドを実行
```

その他のキー (`auto-rotate`、`rotation`、`auto-refresh`、`repeat-*`) は上流の [yaft](apps/yaft) と同じです。

## 制限

- 実機 (reMarkable 2) では未検証です。特に、`try.sh` が `systemctl stop xochitl` した後の挙動、実機の mozc 起動 (mount namespace + chroot + uid 1000)、実際のキー入力は、エミュレータ上のテストでは確かめられていません。
- 最近のファームウェアで上流の ipk を使うには、上流の手順 (fstab の編集など) が必要で、リスクがあります。上のとおり `try.sh` を先に使ってください。
- hazkey は動かせません (上記)。
- 変換候補の学習履歴は mozc の chroot (`mozc-root/home/mozc`) に保存されます。

## ビルド (開発者向け)

Docker (QEMU で armhf コンテナを動かせるもの) があれば、ホストに何も入れずにビルドとテストができます。

```sh
shingetsu/scripts/cross-build.sh .      # toltec 環境で ipk を作る (build/release-toltec)
shingetsu/vim/build-vim.sh              # out/vim (静的 Vim + ncurses + libiconv)
shingetsu/mozc/build-rootfs.sh          # out/mozc-root-armhf.tar.gz
shingetsu/test/qemu-vim-test.sh         # QEMU で Vim の動作確認
shingetsu/test/qemu-mozc-test.sh        # QEMU で mozc の変換確認 (私の名前)
shingetsu/scripts/make-bundle.sh \
  --cross-build-dir build/release-toltec --vim-dir out/vim \
  --mozc-tar out/mozc-root-armhf.tar.gz   # dist/rm2-shingetsu-<バージョン>.tar.gz
shingetsu/test/bundle-dryrun.sh dist/rm2-shingetsu-*.tar.gz
shingetsu/scripts/host-test.sh          # ホスト向けビルドと単体テスト
```

GitHub Actions (`.github/workflows/build.yml`) は、`build` (toltec クロスビルド + ipk)、`build-host` (単体テスト)、`extras` (Vim と mozc の chroot)、`bundle` を実行し、`v*` タグでは `release` がバンドルと ipk を (下書きの) リリースに添付します。Nix のビルドは `workflow_dispatch` のときだけ動きます。

## クレジットとライセンス

- [rM2-stuff](https://github.com/timower/rM2-stuff) (rm2fb、yaft、rocket ほか) — Timothy Werquin、GPL-3.0。yaft はもともと [uobikiemukot/yaft](https://uobikiemukot.github.io/yaft/) (haru) から派生しています。このフォークも GPL-3.0 です。
- [mozc](https://github.com/google/mozc) — BSD-3-Clause (辞書データには別のライセンスの部分があります)。Debian trixie のパッケージ (`mozc-server`、`emacs-mozc-bin`) と、それが依存する glibc / libstdc++ / protobuf / abseil / zlib を chroot に入れています。各パッケージの copyright ファイルは `LICENSES/mozc-root/` に入ります。
- [Vim](https://www.vim.org/) — Vim ライセンス (チャリティウェア)。[ncurses](https://invisible-island.net/ncurses/) — X11 系ライセンス。[GNU libiconv](https://www.gnu.org/software/libiconv/) — LGPL-2.1+。
- [BIZ UDGothic](https://github.com/googlefonts/morisawa-biz-ud-gothic) — SIL Open Font License 1.1 (`shingetsu/fonts/OFL.txt`)。
- 新月配列 [shingetsu-layout](https://github.com/nagamine-git/shingetsu-layout) — MIT (`shingetsu/tables/LICENSE`)。

配布物のすべてのライセンスは、バンドルの `LICENSES/` に入っています。

---

reMarkable Stuff
================
[![Build](https://github.com/timower/rM2-stuff/actions/workflows/build.yml/badge.svg)](https://github.com/timower/rM2-stuff/actions/workflows/build.yml)
[![codecov](https://codecov.io/github/timower/rM2-stuff/graph/badge.svg?token=SN0AG39539)](https://codecov.io/github/timower/rM2-stuff)

Collection of reMarkable related apps, utilities and libraries.

Projects
--------

### [rm2fb](libs/rm2fb)
[![2.15: supported](https://img.shields.io/badge/2.15-supported-brightgreen)](https://support.remarkable.com/s/article/Software-release-2-15-October-2022)
[![3.3: supported](https://img.shields.io/badge/3.3-supported-brightgreen)](https://support.remarkable.com/s/article/Software-release-3-3)
[![3.5: supported](https://img.shields.io/badge/3.5-supported-brightgreen)](https://support.remarkable.com/s/article/Software-release-3-5)
[![3.8: supported](https://img.shields.io/badge/3.8-supported-brightgreen)](https://support.remarkable.com/s/article/Software-release-3-8)
[![3.20: supported](https://img.shields.io/badge/3.20-supported-brightgreen)](https://support.remarkable.com/s/article/Software-release-3-20)
[![3.22: supported](https://img.shields.io/badge/3.22-supported-brightgreen)](https://support.remarkable.com/s/article/Software-release-3-22)
[![3.23: supported](https://img.shields.io/badge/3.23-supported-brightgreen)](https://support.remarkable.com/s/article/Software-release-3-23)
[![3.28: beta](https://img.shields.io/badge/3.28-beta-orange)](https://support.remarkable.com/s/article/Software-release-3-28)


Custom implementation for [reMarkable 2 framebuffer](https://github.com/ddvk/remarkable2-framebuffer), extended to be a full display manager.
Ensures only a single client can control the display, and also handles input capturing.

Use Rocket to switch between apps, or `rm2fbctl` on the commandline:
```
rm2fbctl list
rm2fbctl switch <pid>
```
This rm2fb implementation will not work with other launchers.

Also supports a 'swtcon' mode, which allows Xochitl to use its own swtcon to drive the display.
This decouples rm2fb from the xochitl version, as no address based hooks are used.
In this mode I tested up to 3.28 beta.

For a more supported display driver use [Oxide](https://github.com/Eeems-Org/oxide) or [qtfb](https://github.com/asivery/rm-appload).

### [Yaft](apps/yaft)

A fast framebuffer terminal emulator.

<img src="doc/yaft.png" width=500/>

To use simply execute `yaft` or `yaft <command..>`.
More usage information can be found in the yaft [Readme](apps/yaft).

### Rocket

Launcher that uses the power button to activate and switch apps.

<img src="doc/rocket.png" width=500/>

When pressing the power button, the app drawer will be shown with a timeout of 10 seconds.
After 10 seconds the device will go to sleep, unless an app is picked before that timeout.
You can also interrupt the timeout by pressing the `[x]` button.

This allows you to switch apps without relying on gestures.

### Tilem

A TI-84+ calculator emulator for the remarkable.

<img src="doc/tilem.png" width=500/>

To use simply execute `tilem`, a prompt for downloading a ROM file will be shown.
If you already have a ROM file, you can pass it as an argument on the command line.

### rMlib

Library for writing remarkable apps.
Includes an extensive declarative UI framework based on Flutter.

### [NixOS](nix/)

A [NixOS](https://nixos.org) module that allows soft-rebooting into NixOS. This
allows to declaratively manage your reMarkable 2 configuration.

### SWTCON

This library contains a reverse engineered software TCON, based on `libqsgepaper.so` version 3.23.0.54.
Used by rm2fb in the 'swtcon' mode, which requires no xochitl or libqsgepaper hooking.

Building
--------

Building for the remarkable can either use the [toltec toolchain](https://github.com/toltec-dev/toolchain)
or the reMarkable one:
```bash
# For toltec:
$ cmake --preset dev-toltec
# For remarkable:
$ cmake --preset dev

# To build everything:
$ cmake --build build/dev
# Or to build a specific app:
$ cmake --build build/dev --target yaft

# To create an ipk file:
$ cmake --build build/dev --target package
```

You can also build using Nix for the rm2 toolchain:
```bash
$ nix build .#dev-rm2-toolchain
```
See [NixOS](nix/) for more options.

Emulating
---------

For faster development an `EMULATE` mode is supported by rMlib. This allows
running most apps on a desktop using SDL to emulate the remarkable screen.
To enable it pass `-DEMULATE=ON` to the cmake configure command, without using
the reMarkable toolchain of course.
```bash
$ cmake --preset dev-host
$ cmake --build build/host --target yaft
$ ./build/host/apps/yaft/yaft # Should launch Yaft with an emulated screen in a separete window.
```

A full QEMU based emulator can also be built in [NixOS](nix/).
