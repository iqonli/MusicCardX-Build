// ignore_for_file: curly_braces_in_flow_control
import 'dart:convert';
import 'dart:io';
import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:musiccardx/desktop/platform_io.dart';
import 'package:musiccardx/desktop/music_input.dart';

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();
  test('bridge input normalizes without transport or editor mutation', () {
    final fields = <String,dynamic>{'TITLE':'测试','tracknumber':'3'};
    final input=MusicInput(source:'unit',event:'manual',fields:fields);
    expect(input.source,'unit');
    expect(input.fields['title'],'测试');
    expect(fields.containsKey('TITLE'),true);
  });
  test('Windows state, presets, relative covers, migration and backup recovery', () async {
    final dir=await Directory.systemTemp.createTemp('MCX 中文路径 ');
    final exe=Directory('${dir.path}/绿色包')..createSync();
    final app=Directory('${dir.path}/用户')..createSync();
    TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger.setMockMethodCallHandler(PlatformIO.desktop,(call)async{
      final a=call.arguments as Map?;
      if(call.method=='locations')return {'exe':exe.path,'appData':app.path};
      if(call.method=='replaceFile') {
        final dest=File(a!['to'] as String); if(await dest.exists())await dest.delete();
        await File(a['from'] as String).rename(dest.path); return null;
      }
      throw MissingPluginException(call.method);
    });
    try {
      await PlatformIO.initialize();
      const io=PlatformIO();
      final root=await io.invokeMethod<String>('storageRoot');
      await io.invokeMethod<void>('saveState',{'json':jsonEncode({'fields':{'title':'中文'},'coverPath':'$root/covers/test.image'})});
      final stored=jsonDecode(await File('$root/state.json').readAsString());
      expect(stored['coverPath'],'covers/test.image');
      final loaded=jsonDecode((await io.invokeMethod<String>('loadState'))!);
      expect(loaded['coverPath'],'$root/covers/test.image');
      await io.invokeMethod<void>('savePreset',{'name':'配置一','json':'{"test":1}'});
      expect(await io.invokeListMethod<String>('listPresets'),contains('配置一'));
      await expectLater(io.invokeMethod<void>('savePreset',{'name':'../escape','json':'{}'}),throwsFormatException);
      final newRoot=await PlatformIO.migrate(true);
      expect(newRoot,contains('IQ Online Studio'));
      await PlatformIO.initialize();
      expect(await io.invokeMethod<String>('storageRoot'),newRoot);
      expect(await io.invokeMethod<String>('loadPreset',{'name':'配置一'}),'{"test":1}');
      await io.invokeMethod<void>('saveState',{'json':'{"fields":{"title":"新值"}}'});
      await File('$newRoot/state.json').writeAsString('broken');
      final recovered=jsonDecode((await io.invokeMethod<String>('loadState'))!);
      expect(recovered['fields']['title'],'中文');
    } finally {
      TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger.setMockMethodCallHandler(PlatformIO.desktop,null);
      await dir.delete(recursive:true);
    }
  },skip:!PlatformIO.windows);
}
