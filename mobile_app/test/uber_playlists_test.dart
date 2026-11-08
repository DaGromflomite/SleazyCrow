import 'package:flutter_test/flutter_test.dart';
import 'package:sleazycrow/models/sub_playlist.dart';
import 'package:sleazycrow/services/uber_playlists.dart';

void main() {
  test('recognises Uber playlist files', () {
    expect(UberPlaylists.isPlaylistPath('subplaylist/Fans/Fans_on.txt'), isTrue);
    expect(UberPlaylists.isPlaylistPath(
        'Sub-GHz/Restaurant_Pagers/iBells_ZJ-68/ibells-pager_zj-68_playlist.txt'), isTrue);
    expect(UberPlaylists.isPlaylistPath('Sub-GHz/Garages/readme.txt'), isFalse);
    expect(UberPlaylists.isPlaylistPath('subplaylist/README.md'), isFalse);
  });

  test('names', () {
    expect(UberPlaylists.nameFor('subplaylist/Tesla/315/Tesla_Charge_Port_315.txt'),
        'Uber · Tesla / 315 / Tesla_Charge_Port_315');
    expect(UberPlaylists.nameFor('subplaylist/LPD_playlist.txt'), 'Uber · LPD_playlist');
    expect(UberPlaylists.nameFor('Sub-GHz/Restaurant_Pagers/x/pager_playlist.txt'),
        'Uber · pager_playlist');
  });

  test('parses sub: lines only, strips /ext/subghz', () {
    const text = '# comment\r\n'
        'sub: /ext/subghz/Vehicles/Tesla/a.sub\r\n'
        'SUB:/ext/subghz/Fans/b c.sub\n'
        '\n'
        'something else\n';
    expect(UberPlaylists.parse(text), ['Vehicles/Tesla/a.sub', 'Fans/b c.sub']);
  });

  test('matches case-insensitively and space/underscore tolerant', () {
    final idx = UberPlaylists.indexCatalog([
      'Misc/Sextoy/Egg_Vibrator/Egg_vib_power.sub',
      'Vehicles/Tesla/A.sub',
    ]);
    final m = UberPlaylists.match(
        'sub: /ext/subghz/Misc/Sextoy/Egg Vibrator/Egg_vib_power.sub\n'
        'sub: /ext/subghz/vehicles/tesla/a.sub\n'
        'sub: /ext/subghz/Nope/x.sub\n',
        idx);
    expect(m.files, ['Misc/Sextoy/Egg_Vibrator/Egg_vib_power.sub', 'Vehicles/Tesla/A.sub']);
    expect(m.missing, ['Nope/x.sub']);
  });

  test('build uses card paths, expands split pieces, skips missing', () {
    const pl = UberPlaylist(repoPath: 'subplaylist/Tesla_example_playlist.txt');
    const m = UberPlaylistMatch(['A/one.sub', 'A/big.sub', 'A/failed.sub'], ['gone.sub']);
    final r = UberPlaylists.build(pl, m, {
      'A/one.sub': ['A/one.sub'],
      'A/big.sub': ['A/big_01.sub', 'A/big_02.sub'],
    });
    expect(r.playlist.name, 'Uber · Tesla_example_playlist');
    expect(r.playlist.items.map((i) => i.path).toList(), [
      '/SUB Files/A/one.sub',
      '/SUB Files/A/big_01.sub',
      '/SUB Files/A/big_02.sub',
    ]);
    expect(r.playlist.items.every((i) => i.pathType == 5), isTrue);
    expect(r.skipped, 2);
  });

  test('merge replaces same-named playlists, keeps others', () {
    final a = SubPlaylist(name: 'Mine');
    final old = SubPlaylist(name: 'Uber · Fans / Fans_on', items: [PlaylistItem.pause(1000)]);
    final neu = SubPlaylist(name: 'Uber · Fans / Fans_on');
    final out = UberPlaylists.merge([a, old], [neu, SubPlaylist(name: 'Uber · LPD')]);
    expect(out.map((p) => p.name), ['Mine', 'Uber · Fans / Fans_on', 'Uber · LPD']);
    expect(out[1].items, isEmpty);
  });
}
