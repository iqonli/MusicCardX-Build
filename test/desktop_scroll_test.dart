import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:musiccardx/desktop/smooth_scroll.dart';

void main() {
  testWidgets('wheel moves linearly and stops at its target', (tester) async {
    await tester.pumpWidget(const MaterialApp(home:Scaffold(body:
      DesktopSmoothScroll(child:SizedBox(height:3000,child:Text('content'))))));
    final state=tester.state<ScrollableState>(find.byType(Scrollable).first);
    state.position.pointerScroll(240);
    await tester.pump();
    await tester.pump(const Duration(milliseconds:50));
    final first=state.position.pixels;
    await tester.pump(const Duration(milliseconds:50));
    final second=state.position.pixels;
    expect(first,greaterThan(0));
    expect(second-first,closeTo(first,2));
    await tester.pumpAndSettle();
    expect(state.position.pixels,closeTo(240,.5));
    await tester.pump(const Duration(milliseconds:500));
    expect(state.position.pixels,closeTo(240,.5));
  });
}
