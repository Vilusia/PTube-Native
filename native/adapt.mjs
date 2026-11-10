// INITIAL IMPORT ONLY. Subsequent PTube safety fixes live in native/app and
// native/common; do not rerun this over those maintained files.
import {readFileSync, writeFileSync, readdirSync, mkdirSync} from 'node:fs';
import {fileURLToPath} from 'node:url';
import {join} from 'node:path';
const root = fileURLToPath(new URL('../', import.meta.url));
const original = join(root, 'upstream/komi-tube/native');
const destination = join(root, 'native');
const translations = new Map([
  ['Wi-Fiに接続しています…', 'Connecting to Wi-Fi...'],
  ['Wi-Fiに接続できませんでした', 'Could not connect to Wi-Fi'],
  ['Wi-Fiスイッチと、保存したネットワーク設定を確認してください', 'Check the WLAN switch and saved connection.'],
  ['マイリストから外しました', 'Removed from bookmarks'],
  ['♥ マイリストに追加しました', 'Added to bookmarks'],
  ['保存できませんでした', 'Could not save bookmarks'],
  ['一覧や再生中に ', 'Press '], [' で追加できます', ' in a list or player to save'],
  ['まだありません', 'No saved videos'], ['見つかりませんでした', 'No videos found'],
  ['関連動画を読み込めませんでした', 'Could not load related videos'],
  ['関連動画を読み込んでいます', 'Loading related videos'],
  ['次のページを読み込めませんでした', 'Could not load the next page'],
  ['検索語が空か長すぎます', 'Search is empty or too long'],
  ['検索できませんでした', 'Search failed'], ['これ以上ありません', 'No more pages'],
  ['%uページ目を読み込んでいます', 'Loading page %u'],
  ['「%s」を検索しています', 'Searching for "%s"'],
  ['「%s」 %uページ', '"%s" - page %u'], ['「%s」', '"%s"'],
  ['%s（%u本）', '%s (%u videos)'],
  ['検索（ひらがなで入力すると漢字の候補が出ます）', 'Search YouTube'],
  ['漢字に変換しています', 'Converting Japanese input'],
  ['入力のまま', 'Use as typed'], ['読み込み中...', 'Loading...'],
  ['再生できませんでした', 'Could not play this video'],
  ['マイリスト', 'Bookmarks'], ['検索する', 'Search YouTube'],
  ['関連：', 'Related: '], ['変換：', 'Convert: '],
  ['決定', 'Open'], ['検索中', 'Searching'], ['関連動画', 'Related videos'],
  ['関連', 'Related'], ['いいね', 'Save'], ['ページ', 'Page'], ['戻る', 'Back'],
  ['再生', 'Play'], ['削除', 'Remove'], ['検索', 'Search'], ['終了', 'Exit'],
  ['変換', 'Convert'], ['修正', 'Edit'], ['やめる', 'Cancel'], ['候補', 'Suggestion'],
  ['停止', 'Pause'], ['・', '.'],
]);
for (const folder of ['app', 'common']) mkdirSync(join(destination, folder), {recursive:true});
const files = readdirSync(join(original, 'app')).filter(name => /\.[ch]$/.test(name)).map(name => `app/${name}`);
files.push('common/komi_runtime.c');
for (const name of files) {
  let source = readFileSync(join(original, name), 'utf8').replace(/\r\n/g, '\n');
  if (name === 'app/main.c') {
    for (const [from, to] of translations) source = source.split(from).join(to.replaceAll('"', '\\"'));
    source = source.replaceAll('"komi-tube"', '"PTube Native"').replaceAll('"komi_app"', '"PTubeNative"');
    source = source.replaceAll('komi-app.cfg', 'ptube.cfg').replaceAll('komi-app.txt', 'ptube.log');
  }
  if (name === 'common/komi_runtime.c') source = source.replace('return "ja";', 'return "en";');
  source = source.replaceAll('komi-wifi.txt', 'ptube-wifi.txt').replaceAll('komi-mylist.txt', 'ptube-bookmarks.txt');
  writeFileSync(join(destination, name), '/* Adapted from KOMI Tube (MIT), copyright (c) 2026 komi-tube contributors.\n * PTube changes: see native/UPSTREAM.md. Licence: native/LICENSE-KOMI. */\n' + source);
}
console.log(`Adapted ${files.length} native source/header files.`);
