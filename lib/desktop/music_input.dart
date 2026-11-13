// ignore_for_file: curly_braces_in_flow_control
import '../song_fields.dart';
/// Domain entry for a future bridge. No transport or automatic overwrite.
class MusicInput {
  final String source, event;
  final DateTime receivedAt;
  final Map<String,dynamic> fields;
  final String? lyrics, coverReference;
  MusicInput({required this.source, required this.event, required Map<String,dynamic> fields, this.lyrics, this.coverReference, DateTime? receivedAt}) : fields=normalizeSongFields(fields), receivedAt=receivedAt??DateTime.now();
}
