import 'package:flutter/material.dart';

/// Smooth mouse-wheel travel at a fixed speed, with no inertial tail.
class DesktopSmoothScroll extends StatefulWidget {
  final Widget? child;
  final List<Widget>? children;
  final EdgeInsetsGeometry padding;
  const DesktopSmoothScroll({super.key,this.child,this.children,this.padding=EdgeInsets.zero})
      : assert((child == null) != (children == null));
  @override
  State<DesktopSmoothScroll> createState()=>_DesktopSmoothScrollState();
}
class _WheelController extends ScrollController {
  @override
  ScrollPosition createScrollPosition(ScrollPhysics physics,ScrollContext context,ScrollPosition? oldPosition) =>
      _WheelPosition(physics:physics,context:context,oldPosition:oldPosition);
}
class _WheelPosition extends ScrollPositionWithSingleContext {
  _WheelPosition({required super.physics,required super.context,super.oldPosition});
  double? target;
  @override
  void pointerScroll(double delta) {
    if(delta==0 || !hasContentDimensions) return;
    final start=pixels;
    final pending = isScrollingNotifier.value ? (target ?? start) - start : 0.0;
    // Reverse immediately, and never queue seconds of travel behind fast input.
    final previous = pending * delta < 0 ? start : start + pending;
    final bounded = (previous + delta - start).clamp(-240.0, 240.0);
    final next=(start+bounded).clamp(minScrollExtent,maxScrollExtent);
    target=next;
    final distance=(next-start).abs();
    if(distance<.5) { jumpTo(next); return; }
    // Duration follows distance so repeated wheel events do not increase speed.
    final milliseconds=(distance/1.2).ceil().clamp(1,10000);
    animateTo(next,duration:Duration(milliseconds:milliseconds),curve:Curves.linear);
  }
}
class _DesktopSmoothScrollState extends State<DesktopSmoothScroll> {
  final controller=_WheelController();
  @override
  void dispose(){controller.dispose();super.dispose();}
  @override
  Widget build(BuildContext context)=>Listener(
    onPointerDown:(_){if(controller.hasClients)controller.jumpTo(controller.offset);},
    child:Scrollbar(controller:controller,child:widget.children != null
      ? ListView(controller:controller,physics:const ClampingScrollPhysics(),
          padding:widget.padding,cacheExtent:200,children:widget.children!)
      : SingleChildScrollView(controller:controller,physics:const ClampingScrollPhysics(),
          padding:widget.padding,child:RepaintBoundary(child:widget.child!))),
  );
}
