# rm2-shingetsu: reMarkable 2 へ入れる手順

このバンドルは、rM2-stuff の端末アプリ `yaft` に Colemak キーマップ・日本語入力 (新月配列 + mozc)・CJK グリフを加えたものと、Vim、mozc 用の chroot を含みます。
**動作確認**: reMarkable 2 (OS 3.29.1.0) の実機で、`try.sh` による画面表示、Type Folio からの入力、新月配列と mozc による変換まで確認しています。以下の「未検証」の印がある箇所は、必ず自分で確かめてください。

## 先に読むこと

- 改造は自己責任です。実機を触る前に、起動しなくなったときの復旧手段 (例: [remarkable2-recovery](https://github.com/ddvk/remarkable2-recovery)) を用意してください。
- 上流 rM2-stuff の注意: データ暗号化には非対応です。暗号化を無効にしてから使います ([discussion #47](https://github.com/timower/rM2-stuff/discussions/47))。
- rm2fb が対応するファームウェアは 3.20、3.22.4、3.23.0.54 / 3.23.0.64 です。swtcon モード (下の「まず試す」で使うもの) は 3.28 beta までの動作報告があります ([rm2fb の README](https://github.com/timower/rM2-stuff/blob/main/libs/rm2fb/README.md))。
- 端末に SSH で入れる必要があります。USB 接続なら `root@10.11.99.1` です。root パスワードは端末の設定にある「著作権およびライセンス (Copyrights and licenses)」の画面に表示されます。自分で入力してください。

## まず試す (インストールなし)

`try.sh` は、このバンドルのディレクトリから直接 yaft を動かします。ルートパーティションには何も書き込まず、systemd のユニットも作りません。止まるのは xochitl (と、入っていれば rm2fb / rocket) だけで、`yaft` を `exit` で終えると元に戻ります。**再起動すればすべて元通りになります。**

PC 側:

```sh
scp rm2-shingetsu-<バージョン>.tar.gz root@10.11.99.1:
ssh root@10.11.99.1
```

端末側:

```sh
mkdir -p rm2-shingetsu
tar -xzf rm2-shingetsu-*.tar.gz -C rm2-shingetsu --strip-components=1
cd rm2-shingetsu
sh try.sh
```

`try.sh` がすること (上流の `nix/pkgs/nixos-launcher.sh` の起動手順に沿っています):

1. ファームウェアのバージョンを確認します。上流が対応を明記していないバージョンなら警告を出し、続けるか尋ねます (`--yes` で確認を省略)。
2. xochitl を止めます (rm2fb / rocket が動いていればそれも)。
3. `rm2fb_server_swtcon` を起動し、`/run/rm2fb.sock` ができるのを最大 30 秒待ちます。
4. `librm2fb_client_swtcon.so` を `LD_PRELOAD` して、同梱の `yaft` を起動します。設定ファイルは `~/.local/share/rm2-shingetsu-try/config` 以下に作る専用のもので、既存の `~/.config/yaft/config.toml` は変更しません。Vim は同梱のものが `PATH` の先頭に入ります。
5. yaft が終わる (または Ctrl-C / 回線切断) と、サーバを止めて xochitl を起動し直します。

日本語変換 (mozc、約 30 MB) がまだ入っていなければ、`~/.local/share/rm2-shingetsu-try/mozc-root` に展開してよいか尋ねます。断ると変換なし (かな入力だけ) で動きます。

**実機での確認**: OS 3.29.1.0 では、この手順で yaft が表示され、`exit` で xochitl に戻りました。それ以外のバージョンでは未検証です。`systemctl stop xochitl` で `/home` がアンマウントされる構成だと、バンドルのファイルが見えなくなるため、`try.sh` は停止後にファイルが残っているか確かめて、なければ元に戻して終了します。画面が戻らないときは電源ボタンを長押しして再起動してください。

## インストール (常用する)

バンドルを展開したディレクトリで、root で実行します。

```sh
sh install.sh
```

- Vim を `/home/root/.local/share/rm2-shingetsu/{bin,share}` に入れます。`~/.local/bin/vim` からリンクを張るので、`~/.local/bin` が `PATH` に入っている必要があります。
- mozc の chroot を `/home/root/.local/share/rm2-shingetsu/mozc-root` に展開します (`/home/mozc` を uid/gid 1000、`/tmp` を 1777 にします)。再実行しても mozc の学習履歴 (`home/mozc`) は残ります。
- `~/.config/yaft/config.toml` と `~/.vimrc` は、無いときだけ入れます。すでにあれば `config.toml.rm2-shingetsu` として横に置くので、キーマップと `[ime]` の部分を自分の設定に取り込んでください。
- 何度実行しても同じ結果になります。`PREFIX` 環境変数でインストール先を変えられます (`yaft` の `mozc-root` も合わせて変えてください)。

yaft 本体 (`ipk/yaft.ipk`) は、上流と同じく opkg で入れる前提です。

```sh
sh install.sh --with-ipk                       # ipk/yaft.ipk だけ
sh install.sh --ipk-pkgs "rm2display yaft rocket"   # 指定した ipk
```

`opkg` が無い場合は、入れられなかった ipk の一覧を表示して終わります。

### 上流パッケージ (ipk) と最近のファームウェア

調べた範囲でわかったこと (リンク先が一次情報です):

- 上流の ipk は toltec の `/opt`、`opkg`、`/opt/etc/xochitl.env.d/rm2fb-preload.env`、systemd の `rm2fb.socket` を前提にしています ([libs/rm2fb/CMakeLists.txt](https://github.com/timower/rM2-stuff/blob/main/libs/rm2fb/CMakeLists.txt)、[opkg/postinst](https://github.com/timower/rM2-stuff/blob/main/libs/rm2fb/opkg/postinst))。
- toltec 本家が対応する OS は 2.6.1.71〜3.3.2.1666 までです ([toltec-dev.org](https://toltec-dev.org))。リポジトリはアーカイブされ、新しい OS には vellum や reManager が勧められています ([toltec-dev/toltec](https://github.com/toltec-dev/toltec))。vellum は apk 形式のパッケージ管理で、ipk は扱えません ([vellum-dev/vellum](https://github.com/vellum-dev/vellum))。
- 上流の 3.20 向けの手順は [discussion #47](https://github.com/timower/rM2-stuff/discussions/47) にあります。`/etc/fstab` の編集 (`/home` を直接マウント)、toltec の testing 版 bootstrap、`opkg install rm2display.ipk yaft.ipk rocket.ipk` といった、起動しなくなるおそれのある手順です。同ページの手順 0 は、`opt/bin/rm2fb_server`・`opt/lib/librm2fb_client.so`・`opt/bin/yaft` を `/home/root` に置いて手で起動する、システムを変えない方法です。`try.sh` はこの方法を swtcon 版で自動化したものです。
- 上流にはルートパーティションを変えない NixOS 方式 (soft-reboot) もあります ([nix/README.md](https://github.com/timower/rM2-stuff/blob/main/nix/README.md))。

つまり、最近のファームウェアで `--with-ipk` を使うのは、上の discussion #47 の手順をすでに済ませた端末だけにしてください。それ以外では「まず試す」を使うのが安全です。

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

IME をオンにしている間の操作 (新月配列。`table = "colemak"` が既定で、QWERTY で打つ場合は `"qwerty"`):

| キー | 動作 |
| --- | --- |
| Space | 変換 (もう一度押すと次の候補) |
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

### Vim

標準の Vim のキーのままで、Colemak 用のリマップはしていません。`escape-off = true` なので、Vim でノーマルモードに戻る Esc は、IME がオンでも 1 回で IME オフ + Esc として Vim に届きます。文字コードは UTF-8 を基本に、`cp932` / `euc-jp` のファイルも自動判別します。

### 設定 (`~/.config/yaft/config.toml`)

このバンドルの `config/yaft-config.toml` が、yaft の既定の設定に `keymap = "rm-colemak"` と `[ime]` を足したものです。

| キー | 意味 |
| --- | --- |
| `layout` | 画面上のキーボード。`colemak` (既定) または `qwerty` |
| `keymap` | `rm-colemak` / `rm-qwerty` (Type Folio)、`colemak` / `qwerty` (USB)、`timower` (上流) |
| `[ime] enabled` | 日本語入力の機能を使うか (起動時の IME はオフで、キーでオンにする) |
| `[ime] table` | `"colemak"`、`"qwerty"` (新月配列を打つときの配列)、または hazkey 形式の TSV のパス |
| `[ime] escape-off` | 未入力のときの Esc で IME をオフにするか |
| `[ime] mozc-root` | mozc の chroot (既定は `/home/root/.local/share/rm2-shingetsu/mozc-root`) |
| `[ime] mozc-uid` | mozc を動かす uid (既定 1000。root では動きません) |
| `[ime] mozc-command` | 空でなければ、chroot の代わりにこのコマンドを実行する (主にテスト用) |

## 元に戻す

```sh
sh uninstall.sh            # Vim・mozc・リンクを消す。設定は残す
sh uninstall.sh --purge    # ~/.config/yaft/config.toml と ~/.vimrc も消す
```

`opkg` で入れた ipk は消えません。`opkg remove yaft` などで外してください。`try.sh` の作業用ディレクトリ (`~/.local/share/rm2-shingetsu-try`) も `uninstall.sh` が消します。

## うまくいかないとき

- 画面が戻らない、xochitl が起動しない: 再起動してください (`reboot`、または電源ボタン長押し)。`try.sh` はルートパーティションを書き換えません。
- `try.sh` のサーバのログ: `~/.local/share/rm2-shingetsu-try/server.log`
- 変換できない: mozc は root では動かないため、yaft は mount namespace を作って `proc` と `/dev` を chroot に載せ、uid 1000 で起動します。かな入力はできても漢字に変換されない場合は、`mozc-root` の場所と、`mozc-root/home/mozc` が uid 1000 の持ち物であること (`install.sh` が設定します) を確認してください。
- `install.sh` が「active mounts」で止まる: yaft がまだ動いています。終了してからやり直してください (mozc の chroot に残ったマウントごと消すのを避けるための確認です)。

## ライセンス

`LICENSES/` に、同梱物すべてのライセンス (GPL-3.0、Vim、ncurses、libiconv、mozc と Debian の著作権表示、BIZ UDGothic の OFL-1.1、新月配列の MIT) があります。ソースの入手先は `LICENSES/README.txt` と `vim/BUILDINFO` を見てください。
