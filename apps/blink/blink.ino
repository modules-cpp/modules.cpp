void setup() {
    pinMode(LED_BUILTIN, OUTPUT);
    Serial.begin(115200);
    Serial.println("blink");
}

void loop() {
    ledOn();
    delay(500);
    ledOff();
    delay(500);
}
