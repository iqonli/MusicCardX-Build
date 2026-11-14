import 'dart:convert';
import 'dart:io';
import 'dart:ui';
import 'package:flutter/scheduler.dart';

/// Enabled only by the external release-package CI probe; no production polling.
class DesktopFrameMetrics {
  final String? report;
  final List<FrameTiming> _frames = [];
  DesktopFrameMetrics() : report = Platform.environment['MCX_SMOKE_REPORT'];
  void start() {
    if (report != null) SchedulerBinding.instance.addTimingsCallback(_record);
  }
  void _record(List<FrameTiming> frames) {
    _frames.addAll(frames);
    if (_frames.length > 600) _frames.removeRange(0, _frames.length - 600);
  }
  Future<void> finish() async {
    if (report == null) return;
    SchedulerBinding.instance.removeTimingsCallback(_record);
    final frames = _frames;
    Map<String, Object> stats(Iterable<Duration> values) {
      final ms = values.map((v) => v.inMicroseconds / 1000).toList()..sort();
      if (ms.isEmpty) return {'samples':0};
      return {'samples':ms.length,'p50Ms':ms[(ms.length*.50).floor()],
        'p95Ms':ms[((ms.length-1)*.95).round()],'maxMs':ms.last,
        'over16_67ms':ms.where((v)=>v>16.67).length};
    }
    await File('$report.frames.json').writeAsString(jsonEncode({
      'build':stats(frames.map((f)=>f.buildDuration)),
      'raster':stats(frames.map((f)=>f.rasterDuration)),
      'note':'Release frames during startup and scripted resize/wheel input; CI hardware is not a user PC.',
    }));
  }
}
