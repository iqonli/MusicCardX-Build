// ignore_for_file: curly_braces_in_flow_control
import 'dart:convert';
import 'dart:io';
import 'package:flutter/services.dart';

/// One platform boundary; the Android method channel remains wire-compatible.
class PlatformIO extends MethodChannel {
  const PlatformIO() : super('com.iqonli.musiccardx/platform');
  static const native = MethodChannel('com.iqonli.musiccardx/platform');
  static const desktop = MethodChannel('com.iqonli.musiccardx/windows');
  static bool get windows => Platform.isWindows && !const bool.fromEnvironment('MCX_MOBILE_TEST');
  static String systemUiFont = 'Segoe UI';
  static Directory? _root;
  static String? startupNotice;
  static final List<Map<String, dynamic>> pending = [];
  static Future<void> initialize() async {
    if (!windows) return;
    final locations = Map<String, dynamic>.from((await desktop.invokeMapMethod<String, dynamic>('locations'))!);
    systemUiFont = '${locations['uiFontFamily'] ?? 'Segoe UI'}';
    final portable = Directory('${locations['exe']}/Config');
    final app = Directory('${locations['appData']}/IQ Online Studio/MusicCardX');
    final marker = File('${app.path}/location.json');
    var mode = 'portable';
    if (await marker.exists()) { try { mode = '${jsonDecode(await marker.readAsString())['mode']}'; } catch (_) {} }
    _root = mode == 'appData' ? app : portable;
    try { await _root!.create(recursive:true); final probe=File('${_root!.path}/.probe'); await probe.writeAsString(''); await probe.delete(); }
    catch (_) { startupNotice='程序目录不可写，已使用LocalAppData保存配置，可在设置中切换。'; _root = app; await app.create(recursive:true); await atomic(marker, jsonEncode({'mode':'appData'})); }
    final state = File('${_root!.path}/state.json');
    for(final f in [state,File('${state.path}.bak')]) {
      if(!await f.exists()) continue;
      try {
        final data=jsonDecode(await f.readAsString()) as Map<String,dynamic>;
        final cover=data['coverPath'];
        if(cover is String && cover.startsWith('covers/')) data['coverPath']='${_root!.path}/$cover';
        if(f.path!=state.path) await atomic(state,jsonEncode(data));
        break;
      } catch (_) { /* loadState will report a usable backup or return null. */ }
    }
  }
  static Future<void> _writes=Future.value();
  static Future<void> atomic(File file,String content) {
    final next=_writes.then((_)=>_atomic(file,content));
    _writes=next.catchError((Object _) {}); return next;
  }
  static Future<void> _atomic(File file,String content) async {
    jsonDecode(content);
    await file.parent.create(recursive:true);
    final part=File('${file.path}.tmp'); await part.writeAsString(content,flush:true);
    jsonDecode(await part.readAsString());
    if(await file.exists()) await file.copy('${file.path}.bak');
    // Windows rename does not replace an existing target. Native MoveFileEx is atomic.
    await desktop.invokeMethod('replaceFile',{'from':part.path,'to':file.path});
  }
  static Future<String> migrate(bool appData) async {
    final locations=(await desktop.invokeMapMethod<String,dynamic>('locations'))!;
    final app=Directory('${locations['appData']}/IQ Online Studio/MusicCardX');
    final target=appData?app:Directory('${locations['exe']}/Config');
    final old=_root!;
    final staged=<File,File>{};
    if(target.path==old.path) return target.path;
    await target.create(recursive:true);

    final staging=await Directory('${target.path}/.migration-${DateTime.now().microsecondsSinceEpoch}').create();
    try {
      await for(final e in old.list(recursive:true,followLinks:false)) {
        final normalized=e.path.replaceAll('\\','/');
        if(e is! File || normalized.endsWith('location.json') || normalized.contains('/cache/') || normalized.contains('/.migration-')) continue;
        final relative=e.path.substring(old.path.length+1);
        final copy=File('${staging.path}/$relative');
        await copy.parent.create(recursive:true); await e.copy(copy.path);
        final a=await e.readAsBytes(),b=await copy.readAsBytes();
        if(a.length!=b.length) throw const FileSystemException('迁移校验失败');
        for(var i=0;i<a.length;i++) { if(a[i]!=b[i]) throw const FileSystemException('迁移校验失败'); }
        staged[copy]=File('${target.path}/$relative');
      }
      for(final entry in staged.entries) {
        await entry.value.parent.create(recursive:true);
        if(await entry.value.exists()) await entry.value.copy('${entry.value.path}.pre-migration');
        await desktop.invokeMethod('replaceFile',{'from':entry.key.path,'to':entry.value.path});
      }
    } finally { if(await staging.exists()) await staging.delete(recursive:true); }
    final state=File('${target.path}/state.json');
    if(await state.exists()) {
      final data=jsonDecode(await state.readAsString()) as Map<String,dynamic>;
      final c=data['coverPath']; if(c is String && c.startsWith(old.path)) data['coverPath']=c.replaceFirst(old.path,target.path);
      await atomic(state,jsonEncode(data));
    }
    await atomic(File('${app.path}/location.json'),jsonEncode({'mode':appData?'appData':'portable'}));
    _root=target; return target.path;
  }
  @override
  Future<T?> invokeMethod<T>(String method,[dynamic arguments]) async {
    if(!windows) return native.invokeMethod<T>(method,arguments);
    final a=arguments is Map ? arguments : <String,dynamic>{};
    dynamic result;
    final root=_root!;
    final state=File('${root.path}/state.json');
    String preset(dynamic name) { final n='$name'; if(n.isEmpty || n.contains(RegExp(r'[\\/:*?"<>|]')) || n=='.' || n=='..') throw const FormatException('配置名称无效'); return '${root.path}/presets/$n.json'; }
    switch(method) {
      case 'storageRoot': result=root.path;
      case 'loadState':
        for(final f in [state,File('${state.path}.bak')]) { if(await f.exists()) { try { final s=await f.readAsString(); final data=jsonDecode(s) as Map<String,dynamic>; final c=data['coverPath']; if(c is String && c.startsWith('covers/')) data['coverPath']='${root.path}/$c'; result=jsonEncode(data); break; } catch (_) {} } }
      case 'saveState':
        final data=jsonDecode(a['json'] as String) as Map<String,dynamic>;
        final c=data['coverPath']; if(c is String && c.startsWith('${root.path}/covers/')) data['coverPath']=c.substring(root.path.length+1);
        await atomic(state,jsonEncode(data));
      case 'listPresets':
        final d=Directory('${root.path}/presets'); await d.create(recursive:true);
        result=await d.list().where((e)=>e is File && e.path.endsWith('.json')).map((e)=>e.uri.pathSegments.last.replaceFirst(RegExp(r'\.json$'),'')).toList();
      case 'savePreset': await atomic(File(preset(a['name'])),a['json'] as String);
      case 'loadPreset': result=await File(preset(a['name'])).readAsString();
      case 'deletePreset': await File(preset(a['name'])).delete();
      case 'renamePreset': final target=File(preset(a['newName'])); if(await target.exists()) throw const FormatException('配置已存在'); await File(preset(a['name'])).rename(target.path);
      case 'takeIncoming': result=pending.isEmpty?null:pending.removeAt(0);
      case 'requestStorage': result=true;
      case 'maintainStorage':
        final cache=Directory('${root.path}/cache');
        if(await cache.exists()) { await for(final file in cache.list(recursive:true,followLinks:false)) { if(file is File && DateTime.now().difference(await file.lastModified()).inMinutes>15) await file.delete(); } }
      case 'scanImage': case 'releaseIncoming': break;
      default: result=await desktop.invokeMethod<dynamic>(method,arguments);
    }
    return result as T?;
  }
  @override
  Future<List<T>?> invokeListMethod<T>(String method,[dynamic arguments]) async => (await invokeMethod<List<dynamic>>(method,arguments))?.cast<T>();
  @override
  Future<Map<K,V>?> invokeMapMethod<K,V>(String method,[dynamic arguments]) async => (await invokeMethod<Map<dynamic,dynamic>>(method,arguments))?.cast<K,V>();
  @override
  void setMethodCallHandler(Future<dynamic> Function(MethodCall)? handler) {
    native.setMethodCallHandler(handler);
    if(windows) desktop.setMethodCallHandler(handler);
  }
}
