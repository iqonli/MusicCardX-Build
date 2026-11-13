import 'package:flutter/material.dart';

/// Paint a usable shell before awaiting native storage or asset initialization.
class DesktopStartup extends StatefulWidget {
  final Future<Widget> Function() load;
  const DesktopStartup({super.key, required this.load});
  @override
  State<DesktopStartup> createState() => _DesktopStartupState();
}
class _DesktopStartupState extends State<DesktopStartup> {
  late Future<Widget> loading = widget.load();
  @override
  Widget build(BuildContext context) => FutureBuilder<Widget>(
    future: loading,
    builder: (context, state) {
      if(state.hasData) return state.data!;
      return MaterialApp(home: Scaffold(body: Center(child: SizedBox(width: 600,
        child: Padding(padding: const EdgeInsets.all(24),child: Column(
          mainAxisSize: MainAxisSize.min, crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            const Text('MusicCardX',style: TextStyle(fontSize:28)),
            const SizedBox(height:20),
            if(state.hasError) ...[
              const Text('启动失败'), const SizedBox(height:12),
              SelectableText('${state.error}'), const SizedBox(height:12),
              TextButton(onPressed:()=>setState(()=>loading=widget.load()),child:const Text('重试')),
            ] else ...[const LinearProgressIndicator(), const SizedBox(height:12),const Text('正在加载配置…')],
          ],
        )),
      ))));
    },
  );
}
