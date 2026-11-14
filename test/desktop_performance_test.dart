import 'dart:convert';
import 'dart:io';
import 'dart:ui' as ui;
import 'package:flutter/foundation.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:musiccardx/fonts.dart';
import 'package:musiccardx/model.dart';
import 'package:musiccardx/renderer.dart';

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();
  test('system font index reuses metadata and recovers from corruption', () {
    final root = Directory.systemTemp.createTempSync('mcx-font-index');
    try {
      final file = File('${root.path}/index.json');
      final args = <String, Object?>{
        'files': [File('test/fixtures/fonts/Regular.ttf').absolute.path],
        'cache': file.path,
      };
      final first = inspectCachedSystemFonts(args);
      expect(first, isNotEmpty);
      expect(inspectCachedSystemFonts(args), first);
      file.writeAsStringSync('broken');
      expect(inspectCachedSystemFonts(args), first);
      expect(jsonDecode(file.readAsStringSync())['version'], 1);
    } finally { root.deleteSync(recursive:true); }
  });
  test('native field cache preserves exact output across edits and scale changes', () async {
    await CardDocument.initialize();
    final chinese = ChineseConverter();
    await chinese.load();
    final renderer = CardRenderer(chinese, FontRegistry());
    final doc = CardDocument.fresh();
    // Avoid external font repositories: this test exercises the same glyphs in both renders.
    for (final style in doc.styles.values) { style['font'] = ''; }
    doc.lyrics['font'] = '';
    final measurements = <String, Object?>{};
    try {
      for (final mode in ['fusionSoft','fusionHard','lightFlow','mist']) {
        doc.data['backgroundStyle'] = mode;
        doc.data['seed'] = '12345678';
        for (final scale in [1.0, 2.0]) {
          final first = await renderer.render(doc, scaleOverride:scale);
          final again = await renderer.render(doc, scaleOverride:scale);
          try {
            final a = (await first.image.toByteData(format:ui.ImageByteFormat.rawRgba))!.buffer.asUint8List();
            final b = (await again.image.toByteData(format:ui.ImageByteFormat.rawRgba))!.buffer.asUint8List();
            expect(b, orderedEquals(a), reason:'$mode at $scale');
            measurements['$mode/$scale'] = {'firstMs':first.milliseconds,'repeatMs':again.milliseconds};
          } finally { first.image.dispose(); again.image.dispose(); }
          doc.styles['titleValue']['opacity'] = 180;
          final edited = await renderer.render(doc, scaleOverride:scale);
          renderer.clearBackgroundCache();
          final uncached = await renderer.render(doc, scaleOverride:scale);
          try { expect(await edited.png(), orderedEquals(await uncached.png())); }
          finally { edited.image.dispose(); uncached.image.dispose(); }
        }
      }
      debugPrint('PERFORMANCE_RENDER ${jsonEncode(measurements)}');
    } finally { renderer.clearBackgroundCache(); }
  });
}
