const int buttonPin = 2;
volatile int buttonCount = 0;

void onButton() {
    buttonCount = buttonCount + 1;
}

void setup() {
    pinMode(buttonPin, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(buttonPin), onButton, FALLING);
    Serial.begin(115200);
    Serial.println("button");
}

void loop() {
    static int lastCount = 0;
    if (buttonCount != lastCount) {
        lastCount = buttonCount;
        Serial.print("count: ");
        Serial.println(lastCount);
    }
    delay(10);
}
