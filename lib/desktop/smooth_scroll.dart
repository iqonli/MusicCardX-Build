import 'package:flutter/gestures.dart';
import 'package:flutter/material.dart';

class DesktopSmoothScroll extends StatefulWidget {
  final Widget child;
  const DesktopSmoothScroll({super.key,required this.child});
  @override
  State<DesktopSmoothScroll> createState()=>_DesktopSmoothScrollState();
}
class _DesktopSmoothScrollState extends State<DesktopSmoothScroll> {
  final controller=ScrollController();
  double target=0;
  @override
  void dispose(){controller.dispose();super.dispose();}
  @override
  Widget build(BuildContext context)=>Listener(
    onPointerDown:(_){if(controller.hasClients){target=controller.offset;controller.jumpTo(target);}},
    onPointerSignal:(event){
      if(event is PointerScrollEvent && controller.hasClients){
        GestureBinding.instance.pointerSignalResolver.register(event,(_){
          if(!controller.position.isScrollingNotifier.value)target=controller.offset;
          target=(target+event.scrollDelta.dy).clamp(0,controller.position.maxScrollExtent);
          controller.animateTo(target,duration:const Duration(milliseconds:180),curve:Curves.easeOutCubic);
        });
      }
    },
    child:Scrollbar(controller:controller,child:SingleChildScrollView(controller:controller,child:widget.child)),
  );
}
