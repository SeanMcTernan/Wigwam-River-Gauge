unsigned int sensor_value = 0;

void setup() {
    Serial.begin(9600);
}

void loop() {
    sensor_value = analogRead(A0);
    delay(100);
    Serial.print("\t");
    Serial.print(sensor_value);
}
