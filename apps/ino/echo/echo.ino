#include <string>

void setup() {
    Serial.begin(115200);
}

void loop() {
    if (Serial.available() > 0) {
        std::string line = Serial.readStringUntil('\n');
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        Serial.println(line);
    }
}
