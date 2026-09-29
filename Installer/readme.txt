PG0 インストーラ
--

■ 概要
PG0 (Windows版) のインストーラと、インストーラを作成するツールです。
CLCL のインストーラ (CLCL/Installer) を PG0 用に移植したものです。

インストーラは C のみで記述しており、Windows の API 以外のライブラリは使用して
いません。ZIP の展開処理 (deflate) もインストーラ内に実装しているため、実行時に
必要なランタイムやライブラリはありません。

■ ファイル
  Installer.vcxproj インストーラのプロジェクト (PG0.sln に含まれる)
  build.ps1         インストーラを作成する PowerShell スクリプト
  prepare.ps1       ファイルの収集と ZIP の作成 (プロジェクトから呼ばれる)
  Installer.c       インストーラ本体
  Installer.h       インストーラの定義
  Installer.rc      リソース (ダイアログ / 各言語の文字列 / ZIP)
  resource.h        リソース ID
  unzip.c           ZIP の展開 (deflate と CRC32 の実装)
  unzip.h           ZIP の展開の定義
  res/manifest.xml  マニフェスト (管理者権限で実行)

■ インストーラの作成
Visual Studio 2017 以降 (C++ デスクトップ開発) と Windows SDK が必要です。
PowerShell で以下を実行すると、PG0 本体とライブラリの Win32 (x86) ビルドから
インストーラの作成までを一括で行います。

  PS> cd Installer
  PS> .\build.ps1

作成したインストーラは Installer\out に出力されます。
出力するファイル名は pg0<バージョン>.exe です。Ver 0.3.7 なら pg0037.exe に
なります。バージョンは PG0\pg0edit.rc の FILEVERSION から取得します。
インストーラに含まれるのは Release|Win32 構成でビルドした pg0.exe などです。

ビルド済みのファイルからインストーラだけを作成する場合は -SkipBuild を指定しま
す。

  PS> .\build.ps1 -SkipBuild

各プロジェクトはツールセットに v145 (Visual Studio 2026) を指定しています。
Visual Studio 2022 以前では -PlatformToolset でツールセットを指定してください。

  PS> .\build.ps1 -PlatformToolset v143

主なオプション
  -Configuration <構成>   ビルド構成 (既定: Release)
  -Version <バージョン>   バージョン (既定: pg0edit.rc の FILEVERSION)
  -SourceDir <フォルダ>   収集元のフォルダ (既定: <リポジトリ>\<ビルド構成>)
  -OutDir <フォルダ>      出力先のフォルダ (既定: Installer\out)
  -PlatformToolset <名前> プラットフォームツールセット (既定: プロジェクトの設定)
  -SkipBuild              PG0 本体のビルドを行わない

■ Visual Studio でのビルド
Installer.vcxproj は PG0.sln に含まれています (x86 のみ)。ソリューションを x86 で
ビルドすると、PG0 本体とライブラリのビルドの後にインストーラが作成されます。
Installer プロジェクトだけをビルドすることもできます。その場合は PG0 本体と
ライブラリを Release|Win32 でビルド済みにしておいてください。

ビルド前のイベントで prepare.ps1 がファイルの収集と ZIP の作成、バージョン情報の
instinfo.h の生成を行い、ビルド後のイベントで prepare.ps1 が Installer\out へ
pg0<バージョン>.exe として出力します。build.ps1 はこのプロジェクトを msbuild で
ビルドしているだけなので、どちらの方法でも結果は同じです。

Installer\out に出力するのは Release 構成のときだけです。Debug 構成でビルドした
インストーラは Installer\Debug\PG0Inst.exe になります。Debug 構成では Debug 構成
の pg0.exe などを収集するため、配布するインストーラと混ざらないようにしています。

プロジェクトの設定は msbuild のプロパティで変更できます。build.ps1 のオプション
はこれらに対応しています。フォルダは末尾に \ を付けないでください。
  Pg0Version    バージョン
  Pg0SourceDir  インストールするファイルの収集元
  Pg0OutDir     インストーラの出力先

インストーラのバージョン情報はビルド時に生成する instinfo.h で決まります。この
ファイルはリポジトリには含めていません。

■ インストールするファイル
prepare.ps1 が以下のファイルを収集して ZIP にまとめ、インストーラのリソース
(RCDATA) として登録します。インストーラは実行時にこの ZIP を展開して配置します。

  pg0.exe              Release フォルダから収集
  pg0cmd.exe           Release フォルダから収集
  lib\pg0_io.dll       Release\lib フォルダから収集 (入出力API)
  lib\pg0_math.dll     Release\lib フォルダから収集 (数値処理API)
  lib\pg0_string.dll   Release\lib フォルダから収集 (文字列API)
  lib\pg0_screen.dll   Release\lib フォルダから収集 (画面描画API)
  lib\*.pg0            pg0lib\lib から収集 (ライブラリの読み込み用スクリプト)
  pg0.txt              PG0 から収集 (PG0 の言語仕様)
  pg0.5.txt            PG0 から収集 (PG0.5 の言語仕様)
  pg0.5_lib.txt        PG0 から収集 (ライブラリ リファレンス)
  pg0.5_lib_eng.txt    PG0 から収集 (ライブラリ リファレンス 英語版)
  tutorial.html        PG0\tutorial から収集 (チュートリアル)
  image\*.png          PG0\tutorial\image から収集 (チュートリアルの画像)
  sample\*.pg0         PG0\sample から収集 (サンプルスクリプト)
  sample\lib\*.pg0     pg0lib\samples から収集 (ライブラリのサンプル)
  LICENSE.txt          リポジトリの LICENSE から収集

収集するファイルを変更する場合は prepare.ps1 の Get-TargetFiles を編集してくださ
い。

■ インストーラの動作
管理者権限で実行します (マニフェストで requireAdministrator を指定)。

・インストール先を選択できます。
  既定は %ProgramFiles%\pg0 (64bit 環境では %ProgramFiles(x86)%\pg0) です。
  既にインストールされている場合は、登録されているインストール先が既定になります。
・スタートメニュー、デスクトップへのショートカットの作成を選択できます。
  ショートカットはすべてのユーザ用の場所に作成します。
・コントロールパネルの「プログラムと機能」(アプリと機能) に登録します。
・既にインストールされている場合は更新します。
  登録済みのアプリの一覧のキーをそのまま使用するため、二重に登録されません。
・PG0 が起動中でファイルを上書きできない場合は、PG0 の終了を促します。
・インストール後に PG0 を起動するかどうかを確認します。起動する場合は管理者権
  限を引き継がないように、エクスプローラ経由で起動します。

■ アンインストール
コントロールパネルから実行できます。
インストーラに引数を指定して実行することもできます。

  pg0037.exe /uninstall

インストール先に配置される uninstall.exe も同じ動作です。

  "C:\Program Files (x86)\pg0\uninstall.exe" /uninstall

インストールしたファイルの一覧はインストール先の uninstall.dat に記録しており、
アンインストールではこの一覧のファイルとショートカットを削除します。
設定と保存した値のデータ (%LOCALAPPDATA%\pg0) を削除するかどうかは実行時に確認
します。

■ アプリの一覧への登録
以下のいずれかのキーの配下に、表示名が「PG0」のキー (「PG0 」で始まるものを含む)
があるかを検索し、見つかった場合はそのキーをそのまま使用します。

  HKEY_LOCAL_MACHINE\Software\Microsoft\Windows\CurrentVersion\Uninstall
    (32bit ビューと 64bit ビューの両方)
  HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Uninstall

見つからない場合は以下に新規に作成します。

  HKEY_LOCAL_MACHINE\Software\Microsoft\Windows\CurrentVersion\Uninstall\PG0
    (32bit のため、64bit 環境では WOW6432Node の配下になります)

これは以前のインストーラ (EXEpress) が使用しているキーと同じ場所のため、既存の
環境に上書きしても登録が二重になることはありません。複数見つかった場合は最初の
キーを使用し、残りは削除します。

登録する値
  DisplayName, DisplayVersion, DisplayIcon, Publisher, URLInfoAbout,
  InstallLocation, UninstallString, InstallDate, NoModify, NoRepair,
  EstimatedSize, VersionMajor, VersionMinor

値を設定する前にキーの値をすべて削除します。以前のインストーラが設定した値
(別のアンインストーラを指す UnInstallString など) が残ると、コントロールパネル
から古いアンインストーラが呼ばれてしまうためです。

以前のインストーラがインストール先に残すファイルの一覧 (install.DAT) がある場合
は、アンインストール時に削除するように記録します。インストール先のフォルダが残
らないようにするためです。

■ 多言語対応
日本語と英語のリソースを持っています。Windows の UI 言語に合わせて自動的に
切り替わります。リソースが無い言語の場合は英語で表示します。
翻訳を修正する場合は Installer.rc の STRINGTABLE を編集してください。

■ ライセンス
PG0 本体と同じライセンスです。LICENSE を参照してください。
