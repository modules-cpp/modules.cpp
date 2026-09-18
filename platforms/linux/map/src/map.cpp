// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <charconv>
#include <cerrno>
#include <cstdlib>
#include <fcntl.h>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>
#include <map>
#include <set>
#include <string>
#include <string_view>

module platform.linux.map;

namespace platform::linux {
namespace {

const Map* registered = nullptr;

std::string_view trim(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) text.remove_prefix(1);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t')) text.remove_suffix(1);
    return text;
}

bool number(std::string_view text, unsigned long& value) {
    text = trim(text);
    int base = 10;
    if (text.starts_with("0x")) {
        base = 16;
        text.remove_prefix(2);
    }
    if (text.empty()) return false;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value, base);
    return result.ec == std::errc{} && result.ptr == text.data() + text.size();
}

bool boolean(std::string_view text, bool& value) {
    text = trim(text);
    if (text == "yes") { value = true; return true; }
    if (text == "no") { value = false; return true; }
    return false;
}

bool quoted(std::string_view text, std::string& value) {
    text = trim(text);
    if (text.size() < 2 || text.front() != '"' || text.back() != '"') return false;
    value.clear();
    for (std::size_t i = 1; i + 1 < text.size(); ++i) {
        if (text[i] != '\\') { value += text[i]; continue; }
        if (++i + 1 > text.size()) return false;
        if (text[i] != '\\' && text[i] != '"') return false;
        value += text[i];
    }
    return true;
}

bool selector(std::string_view text, Selector& value) {
    text = trim(text);
    if (text == "auto") { value = {}; return true; }
    if (text.starts_with("index:")) {
        unsigned long index = 0;
        if (!number(text.substr(6), index) || index > 0xffffffffUL) return false;
        value = {SelectorKind::Index, static_cast<unsigned int>(index), {}};
        return true;
    }
    std::string name;
    if (!quoted(text, name)) return false;
    value = {SelectorKind::Name, 0, std::move(name)};
    return true;
}

bool indexed_key(std::string_view key, std::string_view facility,
                 std::size_t& index, std::string_view& field) {
    if (!key.starts_with(facility) || key.size() <= facility.size() ||
        key[facility.size()] != '.') return false;
    const auto rest = key.substr(facility.size() + 1);
    const auto dot = rest.find('.');
    if (dot == std::string_view::npos) return false;
    unsigned long parsed = 0;
    if (!number(rest.substr(0, dot), parsed) || parsed > 1024) return false;
    index = static_cast<std::size_t>(parsed);
    field = rest.substr(dot + 1);
    return !field.empty();
}

bool apply(Map& map, std::string_view key, std::string_view text) {
    if (key == "board.name") return quoted(text, map.board_name);
    if (key == "board.led.name") {
        std::string value; if (!quoted(text, value)) return false; map.led_name = std::move(value); return true;
    }
    if (key == "board.led.gpio") {
        unsigned long value = 0; if (!number(text, value) || value > 0xffffffffUL) return false;
        map.led_gpio = static_cast<unsigned int>(value); return true;
    }
    if (key == "board.led.active-high") return boolean(text, map.led_active_high);
    if (key == "rtc.path") return quoted(text, map.rtc.path);
    if (key == "rtc.allow-write") return boolean(text, map.rtc.allow_write);
    if (key == "rtc.convention") {
        text = trim(text);
        if (text == "utc") { map.rtc.convention = RtcConvention::Utc; return true; }
        if (text == "local") { map.rtc.convention = RtcConvention::Local; return true; }
        return false;
    }
    if (key == "display.card") return selector(text, map.display.card);
    if (key == "display.connector") return selector(text, map.display.connector);
    if (key == "display.mode") return selector(text, map.display.mode);
    if (key == "display.width" || key == "display.height") {
        unsigned long value = 0; if (!number(text, value) || value > 65535) return false;
        (key == "display.width" ? map.display.width : map.display.height) = static_cast<unsigned int>(value); return true;
    }
    if (key == "touch.device") return selector(text, map.touch.device);
    if (key == "touch.invert-x") return boolean(text, map.touch.invert_x);
    if (key == "touch.invert-y") return boolean(text, map.touch.invert_y);
    if (key == "touch.swap-axes") return boolean(text, map.touch.swap_axes);
    if (key == "imu.device") return selector(text, map.imu.device);
    if (key == "imu.trigger") return selector(text, map.imu.trigger);
    if (key == "imu.timeout-ms") return number(text, map.imu.timeout_ms) && map.imu.timeout_ms > 0;
    if (key == "adc.device") return selector(text, map.adc_device);

    std::size_t index = 0; std::string_view field;
    if (indexed_key(key, "gpio", index, field)) {
        if (map.gpios.size() <= index) map.gpios.resize(index + 1);
        auto& e = map.gpios[index];
        if (field == "chip") return quoted(text, e.chip);
        if (field == "name") return quoted(text, e.name);
        if (field == "offset") { unsigned long v=0; if (!number(text,v)||v>0xffffffffUL) return false; e.offset=static_cast<unsigned int>(v); return true; }
        return false;
    }
    if (indexed_key(key, "spi", index, field)) {
        if (map.spis.size() <= index) map.spis.resize(index + 1);
        auto& e = map.spis[index]; unsigned long v=0;
        if (field == "path") return quoted(text, e.path);
        if (field == "clock-gpio") { if(!number(text,v))return false;e.clock_gpio=v;return true; }
        if (field == "transmit-gpio") { if(!number(text,v))return false;e.transmit_gpio=v;return true; }
        if (field == "receive-gpio") { if(!number(text,v))return false;e.receive_gpio=v;return true; }
        if (field == "max-speed") { if(!number(text,v)||v==0)return false;e.max_speed=v;return true; }
        if (field == "speed-fixed") return boolean(text,e.speed_fixed);
        if (field == "mode") { text=trim(text); if(text.size()!=5||!text.starts_with("mode")||text[4]<'0'||text[4]>'3')return false;e.mode=text[4]-'0';return true; }
        if (field == "bit-order") { text=trim(text); if(text=="msb"){e.least_significant_first=false;return true;}if(text=="lsb"){e.least_significant_first=true;return true;}return false; }
        return false;
    }
    if (indexed_key(key, "i2c", index, field)) {
        if (map.i2cs.size() <= index) map.i2cs.resize(index + 1);
        auto& e = map.i2cs[index]; unsigned long v=0;
        if(field=="adapter"){if(!number(text,v)||v>0xffffffffUL)return false;e.adapter=v;return true;}
        if(field=="data-gpio"){if(!number(text,v))return false;e.data_gpio=v;return true;}
        if(field=="clock-gpio"){if(!number(text,v))return false;e.clock_gpio=v;return true;}
        if(field=="baud"){if(!number(text,v)||v==0)return false;e.baud=v;return true;}
        if(field=="baud-fixed")return boolean(text,e.baud_fixed);
        return false;
    }
    if (indexed_key(key, "adc", index, field)) {
        if (map.adcs.size() <= index) map.adcs.resize(index + 1);
        auto& e = map.adcs[index]; unsigned long v=0;
        if(field=="channel"){if(!number(text,v)||v>0xffffffffUL)return false;e.channel=static_cast<unsigned int>(v);return true;}
        if(field=="gpio"){if(!number(text,v)||v>0xffffffffUL)return false;e.gpio=static_cast<unsigned int>(v);return true;}
        if(field=="name")return quoted(text,e.name);
        if(field=="bits"){if(!number(text,v)||v<1||v>31)return false;e.bits=static_cast<unsigned int>(v);return true;}
        if(field=="reference-mv"){if(!number(text,v)||v>0xffffffffUL)return false;e.reference_millivolts=static_cast<unsigned int>(v);return true;}
        return false;
    }
    if (indexed_key(key, "pwm", index, field)) {
        if (map.pwms.size() <= index) map.pwms.resize(index + 1);
        auto& e = map.pwms[index]; unsigned long v=0;
        if(field=="chip"){if(!number(text,v)||v>0xffffffffUL)return false;e.chip=static_cast<unsigned int>(v);return true;}
        if(field=="channel"){if(!number(text,v)||v>0xffffffffUL)return false;e.channel=static_cast<unsigned int>(v);return true;}
        if(field=="gpio"){if(!number(text,v)||v>0xffffffffUL)return false;e.gpio=static_cast<unsigned int>(v);return true;}
        if(field=="name")return quoted(text,e.name);
        if(field=="group"){if(!number(text,v)||v>0xffffffffUL)return false;e.group=static_cast<unsigned int>(v);return true;}
        return false;
    }
    if (indexed_key(key, "uart", index, field)) {
        if (map.uarts.size() <= index) map.uarts.resize(index + 1);
        auto& e=map.uarts[index]; unsigned long v=0;
        if(field=="path")return quoted(text,e.path);
        if(field=="baud"){if(!number(text,v)||v==0)return false;e.baud=v;return true;}
        if(field=="data-bits"){if(!number(text,v)||v<5||v>8)return false;e.data_bits=v;return true;}
        if(field=="stop-bits"){if(!number(text,v)||(v!=1&&v!=2))return false;e.stop_bits=v;return true;}
        if(field=="write-deadline-ms"){if(!number(text,v)||v==0)return false;e.write_deadline_ms=v;return true;}
        if(field=="parity"){text=trim(text);if(text=="none")e.parity=0;else if(text=="even")e.parity=1;else if(text=="odd")e.parity=2;else return false;return true;}
        return false;
    }
    return false;
}

MapStatus validate(Map& map, const std::string& path, ParseError& error) {
    std::set<std::string> names;
    for (std::size_t i=0;i<map.gpios.size();++i) {
        auto& gpio=map.gpios[i];
        if (gpio.name.empty()) gpio.name="gpio."+std::to_string(i);
        if (gpio.chip.empty() || !names.insert(gpio.name).second) {
            error={path,0,0,"gpio."+std::to_string(i),"missing chip or duplicate GPIO name"};
            return MapStatus::SyntaxError;
        }
    }
    if (map.led_gpio) {
        bool found=false;
        for (std::size_t i=0;i<map.gpios.size();++i)
            if (i==*map.led_gpio) found=true;
        if (!found) {
            error={path,0,0,"board.led.gpio","does not name a declared GPIO"};
            return MapStatus::SyntaxError;
        }
    }
    // An analog entry's pad must be a declared GPIO and attached once per
    // inventory, as the LED's must; its name defaults like a GPIO's.
    std::set<std::string> adc_names;
    std::set<unsigned int> adc_pins;
    for (std::size_t i=0;i<map.adcs.size();++i) {
        auto& adc=map.adcs[i];
        if (adc.name.empty()) adc.name="adc."+std::to_string(i);
        if (!adc_names.insert(adc.name).second) {
            error={path,0,0,"adc."+std::to_string(i),"duplicate ADC name"};
            return MapStatus::SyntaxError;
        }
        if (adc.gpio && (*adc.gpio>=map.gpios.size() || !adc_pins.insert(*adc.gpio).second)) {
            error={path,0,0,"adc."+std::to_string(i)+".gpio","does not name a declared GPIO once"};
            return MapStatus::SyntaxError;
        }
    }
    std::set<std::string> pwm_names;
    std::set<unsigned int> pwm_pins;
    for (std::size_t i=0;i<map.pwms.size();++i) {
        auto& pwm=map.pwms[i];
        if (pwm.name.empty()) pwm.name="pwm."+std::to_string(i);
        if (!pwm_names.insert(pwm.name).second) {
            error={path,0,0,"pwm."+std::to_string(i),"duplicate PWM name"};
            return MapStatus::SyntaxError;
        }
        if (pwm.gpio && (*pwm.gpio>=map.gpios.size() || !pwm_pins.insert(*pwm.gpio).second)) {
            error={path,0,0,"pwm."+std::to_string(i)+".gpio","does not name a declared GPIO once"};
            return MapStatus::SyntaxError;
        }
    }
    return MapStatus::Ok;
}

}  // namespace

void set_map(const Map& defaults) { registered = &defaults; }

// The most an override may be: far beyond any map, small enough that reading
// it is not a wait.
constexpr std::size_t override_limit = 64 * 1024;

MapStatus apply_override(Map& map, const std::string& path, ParseError& error) {
    // The override is read by the first board query, which must return: the
    // path is opened without blocking, the descriptor -- not the name -- is
    // checked to be a regular file, and at most override_limit bytes are
    // read, so neither a FIFO put in the file's place nor a file that grows
    // while it is read can hold the query.
    const int fd = ::open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) { error={path,0,0,{},"cannot open override"}; return MapStatus::FileError; }
    struct stat about{};
    if (::fstat(fd, &about) != 0 || !S_ISREG(about.st_mode)) {
        ::close(fd);
        error={path,0,0,{},"override is not a regular file"};
        return MapStatus::FileError;
    }
    std::string text;
    while (text.size() <= override_limit) {
        char chunk[4096];
        const auto got = ::read(fd, chunk, sizeof chunk);
        if (got < 0 && errno == EINTR) continue;
        if (got < 0) { ::close(fd); error={path,0,0,{},"cannot read override"}; return MapStatus::FileError; }
        if (got == 0) break;
        text.append(chunk, static_cast<std::size_t>(got));
    }
    ::close(fd);
    if (text.size() > override_limit) {
        error={path,0,0,{},"override exceeds the size limit"};
        return MapStatus::FileError;
    }
    std::istringstream input(text);
    Map candidate = map;
    std::map<std::string,unsigned int,std::less<>> seen;
    std::string line;
    for (unsigned int line_number=1;std::getline(input,line);++line_number) {
        bool quote=false,escape=false; std::size_t comment=std::string::npos,equals=std::string::npos;
        for(std::size_t i=0;i<line.size();++i){const char c=line[i];if(escape){escape=false;continue;}if(quote&&c=='\\'){escape=true;continue;}if(c=='"'){quote=!quote;continue;}if(!quote&&c=='#'){comment=i;break;}if(!quote&&c=='='&&equals==std::string::npos)equals=i;}
        if(comment!=std::string::npos)line.resize(comment);
        const auto whole=trim(line); if(whole.empty())continue;
        if(quote||equals==std::string::npos){error={path,line_number,0,{},"expected one assignment"};return MapStatus::SyntaxError;}
        const auto key=trim(std::string_view(line).substr(0,equals));
        const auto value=trim(std::string_view(line).substr(equals+1));
        if(key.empty()||value.empty()){error={path,line_number,0,std::string(key),"empty key or value"};return MapStatus::SyntaxError;}
        if(const auto it=seen.find(key);it!=seen.end()){error={path,line_number,it->second,std::string(key),"duplicate key"};return MapStatus::SyntaxError;}
        seen.emplace(std::string(key),line_number);
        if(!apply(candidate,key,value)){error={path,line_number,0,std::string(key),"unknown key or invalid value"};return MapStatus::SyntaxError;}
    }
    const auto status = validate(candidate,path,error);
    if (status == MapStatus::Ok) map = std::move(candidate);
    return status;
}

const Resolution& resolve() {
    static Map resolved;
    static const Resolution result=[] {
        Resolution out;
        if (registered == nullptr) return out;
        resolved=*registered;
        out.status=MapStatus::Ok;
        out.map=&resolved;
        if(const char* path=std::getenv("MM_LINUX_DEVICE_MAP");path!=nullptr) {
            out.status=apply_override(resolved,path,out.error);
        } else {
            out.status=validate(resolved,"<defaults>",out.error);
        }
        return out;
    }();
    return result;
}

}
