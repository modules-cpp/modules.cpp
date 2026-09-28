// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module mm.audio;

namespace mm::audio {

namespace {

// The fallbacks are objects with nothing overridden, so an unserved end of the
// link answers Unsupported rather than failing to link or dereferencing a null
// pointer. A missing implementation is a runtime answer here, as in mm.touch.
Microphone unserved_microphone;
Microphone* current_microphone = &unserved_microphone;

Codec unserved_codec;
Codec* current_codec = &unserved_codec;

}  // namespace

void set_microphone(Microphone& microphone) { current_microphone = &microphone; }
Microphone& selected_microphone() { return *current_microphone; }

void set_codec(Codec& codec) { current_codec = &codec; }
Codec& selected_codec() { return *current_codec; }

}  // namespace mm::audio
