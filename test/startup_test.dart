import 'dart:async';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:musiccardx/desktop/startup.dart';

void main() {
  testWidgets('startup paints before loading and replaces shell on success', (tester) async {
    final ready=Completer<Widget>();
    await tester.pumpWidget(DesktopStartup(load:()=>ready.future));
    expect(find.text('正在加载配置…'),findsOneWidget);
    ready.complete(const MaterialApp(home:Text('editor')));
    await tester.pumpAndSettle();
    expect(find.text('editor'),findsOneWidget);
  });
  testWidgets('startup failures remain visible and can be retried', (tester) async {
    var calls=0;
    await tester.pumpWidget(DesktopStartup(load:() async {
      if(calls++==0) throw StateError('storage unavailable');
      return const MaterialApp(home:Text('editor'));
    }));
    await tester.pumpAndSettle();
    expect(find.textContaining('storage unavailable'),findsOneWidget);
    await tester.tap(find.text('重试'));
    await tester.pumpAndSettle();
    expect(find.text('editor'),findsOneWidget);
  });
}
