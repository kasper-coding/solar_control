#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <LiquidCrystal_I2C.h>
#include <OneWire.h>
#include <DallasTemperature.h>

//Wifi Config
const char* SSID = "YOUR_SSUD";
const char* PSK = "YOUR_PSK";
const char* MQTT_BROKER = "YOUR_BROKER";

//Webserver
ESP8266WebServer server(80);

// Wifi init
WiFiClient espClient;

//MQTT über Wifi
PubSubClient client(espClient);

// LCD init
LiquidCrystal_I2C lcd(0x27,16,2);

// One Wire Init
const int oneWireBus = D4;
OneWire oneWire(oneWireBus);
DallasTemperature sensors(&oneWire);

// Darstellung des Grad Symbols
byte degree[] = {
  B00010,
  B00101,
  B00010,
  B00000,
  B00000,
  B00000,
  B00000,
  B00000
};

int numberOfDevices; //# of DS18B20 OneWire Sensors
DeviceAddress tempDeviceAddress; 

// Fixe Adressen der DS18B20
uint8_t sensor_pool[8] = { 0x28, 0x1B, 0xB5, 0x37, 0x0D, 0x00, 0x00 ,0xFE }; // <- CHANGE THIS TO YOUR SPECIFIC ADDRESS!
uint8_t sensor_solar[8] = { 0x28, 0x0A, 0x72, 0x51, 0x31, 0x21, 0x03, 0xFA }; // // <- CHANGE THIS TO YOUR SPECIFIC ADDRESS!

//Variablen Initialisierung
int pinRelais = D3 ;
int solar = 0;  // Solar IST Temperatur
int pool  = 0;  // Pool IST Temperatur
int soll  = 32; // Solltemperatur Pool
int offset = 5; // Differenz von Pool zu Solar
int rt = 300;  // Temperatur Refresh Time 

// Timer-Variablen
long myTimer = 0;
long myTimeout = 5000;

String ssolar = "Solar: ";
String spool = "Pool: ";
String soffset = "Offset: ";
String srt = "Refreshtime: ";
String pumppower = "";
String outputstring ="";

boolean relais_on = false;
boolean startup_mqtt = true;
boolean startup_timer = true;
boolean psim = false;
boolean ssim = false;


void setup() {
  char pchar[16];
  lcd.init();
  lcd.backlight();
  
  //Show Welcome Message
  lcd.setCursor(0,0);
  lcd.print("MK Pool Solar");
  lcd.setCursor(0,1);
  lcd.print("Controller V 1.0");
  lcd.createChar(0, degree);
  delay(2000);

  //Relais Pin initialisiern und setzen
  pinMode(pinRelais , OUTPUT);
  digitalWrite(pinRelais , HIGH);

  // Temp Sensoren Initialisieren
  sensors.begin();
  numberOfDevices = sensors.getDeviceCount();
  
  //Serielle Konsole öffenen
  Serial.begin(115200);
  Serial.print("Locating devices...");
  Serial.print("Found ");
  Serial.print(numberOfDevices, DEC);
  Serial.println(" devices.");

  // Sensor Info auf LCD ausgeben
  lcd.clear();
  lcd.setCursor(0,0);
  outputstring = itoa(numberOfDevices, pchar, 10);
  lcd.print("Found " + outputstring + " Sensors");
  if(numberOfDevices < 2){
    lcd.setCursor(0,1);  
    lcd.print("Error: S01");
  }else{
    lcd.setCursor(0,1);  
    lcd.print("OK!");
  }
  delay(1000);
  
 

   // Gefundene OneWire Sensoren inkl. Adressen ausgeben
  for(int i=0;i<numberOfDevices; i++){
    // Search the wire for address
    if(sensors.getAddress(tempDeviceAddress, i)){
      Serial.print("Found device ");
      Serial.print(i, DEC);
      Serial.print(" with address: ");
      printAddress(tempDeviceAddress);
      Serial.println();
    } else {
      Serial.print("Found ghost device at ");
      Serial.print(i, DEC);
      Serial.print(" but could not detect address. Check power and cabling");
    }
  }

  //Connect Wifi
  setup_wifi();

  //Start MQTT connection
  client.setServer(MQTT_BROKER, 1883);
  client.setCallback(callback);
}

//Webserver Connect Function
void handle_OnConnect() {
  server.send(200, "text/html", SendHTML(solar,pool,soll,offset,rt, relais_on)); 
}

//Webserver 404 Function
void handle_NotFound(){
  server.send(404, "text/plain", "Not found");
}

// Hilfsfunktion für OneWire Adressausgabe
void printAddress(DeviceAddress deviceAddress) {
  for (uint8_t i = 0; i < 8; i++){
    if (deviceAddress[i] < 16) Serial.print("0");
      Serial.print(deviceAddress[i], HEX);
  }
}

// Webserver Ausgabe
String SendHTML(int solar,int pool,int soll,int offset, int rt , boolean heizen){
  String sol_image_b64 = "<img width=30px src='data:image/svg+xml;base64,PD94bWwgdmVyc2lvbj0iMS4wIiBlbmNvZGluZz0iaXNvLTg4NTktMSI/Pg0KPCEtLSBHZW5lcmF0b3I6IEFkb2JlIElsbHVzdHJhdG9yIDE4LjEuMSwgU1ZHIEV4cG9ydCBQbHVnLUluIC4gU1ZHIFZlcnNpb246IDYuMDAgQnVpbGQgMCkgIC0tPg0KPHN2ZyB2ZXJzaW9uPSIxLjEiIGlkPSJDYXBhXzEiIHhtbG5zPSJodHRwOi8vd3d3LnczLm9yZy8yMDAwL3N2ZyIgeG1sbnM6eGxpbms9Imh0dHA6Ly93d3cudzMub3JnLzE5OTkveGxpbmsiIHg9IjBweCIgeT0iMHB4Ig0KCSB2aWV3Qm94PSIwIDAgNjEyLjAwMiA2MTIuMDAyIiBzdHlsZT0iZW5hYmxlLWJhY2tncm91bmQ6bmV3IDAgMCA2MTIuMDAyIDYxMi4wMDI7IiB4bWw6c3BhY2U9InByZXNlcnZlIj4NCjxnPg0KCTxnPg0KCQk8cGF0aCBkPSJNNjExLjU2NywzNzAuMTcyYy0xNS40ODUtMTA0Ljc2My0zMC45NzctMjA5LjUyOC00Ni40NzItMzE0LjI4NmMtMy4yMzktMjEuOTM4LTIxLjYtMzguNDgzLTQyLjcxMS0zOC40ODNIODkuNjIxDQoJCQljLTIxLjEwNCwwLTM5LjQ2NywxNi41MzktNDIuNzE1LDM4LjQ3N2wtOC4xMTksNTQuOTFjLTEyLjc3OSw4Ni40Ni0yNS41NiwxNzIuOTIxLTM4LjM1MSwyNTkuMzc0DQoJCQljLTEuNjk4LDExLjQ2NSwxLjYxLDIzLDkuMDc1LDMxLjY1MWM3LjgwNiw5LjA0NiwxOS40NzgsMTQuMjMzLDMyLjAyNCwxNC4yMzNoODYuNDE2bDgxLjQ2Miw4NC41MjFoLTQ0LjkwMg0KCQkJYy02LjA3NSwwLTExLjM1LDQuNzA4LTEyLjI2NSwxMC45NDJsLTEwLjMwOSw2OS43Yy0wLjQ4OSwzLjM1NiwwLjQ5Miw2LjczNSwyLjY4Niw5LjI2OGMyLjI2NSwyLjYyLDUuNjI4LDQuMTIxLDkuMjI0LDQuMTIxDQoJCQloMzA0LjMwNWMzLjYwNSwwLDYuOTcxLTEuNTA2LDkuMjM4LTQuMTMzYzIuMTktMi41NCwzLjE2NS01LjkxOSwyLjY2OC05LjI3OWwtMTAuMy02OS42NTINCgkJCWMtMC45MDgtNi4yNS02LjE4MS0xMC45NjYtMTIuMjY1LTEwLjk2NmgtNDQuOTAzbDgxLjQ2LTg0LjUyMWg4Ni40MTVjMTIuNTQzLDAsMjQuMjE2LTUuMTg3LDMyLjAyMi0xNC4yMzINCgkJCUM2MDkuOTUzLDM5My4xNjYsNjEzLjI2MywzODEuNjMzLDYxMS41NjcsMzcwLjE3MnogTTMwOC44NjUsNTAwLjU2OWgtNS43MjJsLTgxLjQ2LTg0LjUyMWgxNjguNjM5TDMwOC44NjUsNTAwLjU2OXoNCgkJCSBNNTIuMzA1LDM2NS40MzVjMTIuMTkyLTgyLjQxMywyNC4zNzQtMTY0LjgyOCwzNi41NTYtMjQ3LjI0M2w3LjQxNC01MC4xNzVoNDE5LjQ1MmMxNC42Niw5OS4xMzksMjkuMzE4LDE5OC4yNzksNDMuOTc0LDI5Ny40MTgNCgkJCUg1Mi4zMDV6Ii8+DQoJCTxwb2x5Z29uIHBvaW50cz0iMzU2LjU2Niw5Ni44NDQgMjUyLjA2NSw5Ni44NDQgMjQ4LjQyLDIwMS45MTUgMzYwLjIxMSwyMDEuOTE1IAkJIi8+DQoJCTxwYXRoIGQ9Ik0zODguNTMyLDIyOS4yNjlsMy42NDMsMTA1LjA2NWgxMjMuMDM5Yy01LjE3OC0zNS4wMjMtMTAuMzYtNzAuMDQyLTE1LjUzNC0xMDUuMDY1TDM4OC41MzIsMjI5LjI2OUwzODguNTMyLDIyOS4yNjl6Ii8+DQoJCTxwb2x5Z29uIHBvaW50cz0iMjQ3LjQ3MiwyMjkuMjY5IDI0My44MzEsMzM0LjMzNSAzNjQuODA2LDMzNC4zMzUgMzYxLjE1OSwyMjkuMjY5IAkJIi8+DQoJCTxwYXRoIGQ9Ik00ODAuMTAxLDk2Ljg0NGgtOTYuMTY3bDMuNjQ3LDEwNS4wNzFoMTA4LjA1M0M0OTAuNDU5LDE2Ni44ODgsNDg1LjI3OSwxMzEuODY3LDQ4MC4xMDEsOTYuODQ0eiIvPg0KCQk8cGF0aCBkPSJNMTE5Ljk4NCwxNTQuNjUxYy0yLjMyNCwxNS43NTYtNC42NTUsMzEuNTEtNi45OCw0Ny4yNjRoMTA4LjA0OWwzLjY0NS0xMDUuMDcxaC05Ni4xNjJMMTE5Ljk4NCwxNTQuNjUxeiIvPg0KCQk8cGF0aCBkPSJNOTMuNDE4LDMzNC4zMzVoMTIzLjAzOWwzLjY0OC0xMDUuMDY1SDEwOC45NTJDMTAzLjc3OCwyNjQuMjkyLDk4LjYsMjk5LjMxMiw5My40MTgsMzM0LjMzNXoiLz4NCgk8L2c+DQo8L2c+DQo8Zz4NCjwvZz4NCjxnPg0KPC9nPg0KPGc+DQo8L2c+DQo8Zz4NCjwvZz4NCjxnPg0KPC9nPg0KPGc+DQo8L2c+DQo8Zz4NCjwvZz4NCjxnPg0KPC9nPg0KPGc+DQo8L2c+DQo8Zz4NCjwvZz4NCjxnPg0KPC9nPg0KPGc+DQo8L2c+DQo8Zz4NCjwvZz4NCjxnPg0KPC9nPg0KPGc+DQo8L2c+DQo8L3N2Zz4NCg==' />";
  String pool_image_b64 = "<img width=30px src='data:image/svg+xml;base64,PD94bWwgdmVyc2lvbj0iMS4wIiBlbmNvZGluZz0iaXNvLTg4NTktMSI/Pg0KPCEtLSBHZW5lcmF0b3I6IEFkb2JlIElsbHVzdHJhdG9yIDE4LjEuMSwgU1ZHIEV4cG9ydCBQbHVnLUluIC4gU1ZHIFZlcnNpb246IDYuMDAgQnVpbGQgMCkgIC0tPg0KPHN2ZyB2ZXJzaW9uPSIxLjEiIGlkPSJDYXBhXzEiIHhtbG5zPSJodHRwOi8vd3d3LnczLm9yZy8yMDAwL3N2ZyIgeG1sbnM6eGxpbms9Imh0dHA6Ly93d3cudzMub3JnLzE5OTkveGxpbmsiIHg9IjBweCIgeT0iMHB4Ig0KCSB2aWV3Qm94PSIwIDAgNjEyIDYxMiIgc3R5bGU9ImVuYWJsZS1iYWNrZ3JvdW5kOm5ldyAwIDAgNjEyIDYxMjsiIHhtbDpzcGFjZT0icHJlc2VydmUiPg0KPGc+DQoJPGc+DQoJCTxwYXRoIGQ9Ik0xNzUuMjA1LDIzOS42MmMwLjEyNy0xLjk2NS0wLjUzMy0zLjkwMi0xLjgzMy01LjM4MWwtNTguODQtNjYuOTQxYy0xLjMtMS40NzktMy4xMzUtMi4zODEtNS4xMDItMi41MDgNCgkJCWMtMS45NzUtMC4xMjYtMy45MDIsMC41MzMtNS4zODEsMS44MzNjLTI3LjAzNywyMy43NjYtNDkuNDc5LDUxLjc5NC02Ni43MDYsODMuMzA1Yy0wLjk0NCwxLjcyOS0xLjE2NSwzLjc2Mi0wLjYxMSw1LjY1MQ0KCQkJYzAuNTU0LDEuODksMS44MzYsMy40ODMsMy41NjUsNC40MjdsNzguMjA1LDQyLjc0OGMxLjEzMSwwLjYxOSwyLjM1MiwwLjkxMiwzLjU1NywwLjkxMmMyLjYyNywwLDUuMTc0LTEuMzk4LDYuNTIzLTMuODY2DQoJCQljMTEuMzg2LTIwLjgyOCwyNi4yMjktMzkuMzU5LDQ0LjExNC01NS4wOEMxNzQuMTc4LDI0My40MjIsMTc1LjA4LDI0MS41ODcsMTc1LjIwNSwyMzkuNjJ6Ii8+DQoJCTxwYXRoIGQ9Ik0yMDEuNDYyLDIxNC44MjljMS4zMzQsMi41MTUsMy45MDcsMy45NDgsNi41NjgsMy45NDhjMS4xNzQsMCwyLjM2NS0wLjI3OSwzLjQ3My0wLjg2Nw0KCQkJYzIwLjk2Mi0xMS4xMTcsNDMuNTEyLTE4LjM3MSw2Ny4wMjUtMjEuNTYxYzQuMDY0LTAuNTUxLDYuOTEzLTQuMjkzLDYuMzYyLTguMzU4bC0xMS45NzktODguMzE2DQoJCQljLTAuNTUxLTQuMDY0LTQuMzA0LTYuOTA5LTguMzU4LTYuMzYyYy0zNS43MDgsNC44NDMtNjkuOTQ5LDE1Ljg1Ny0xMDEuNzcyLDMyLjczNmMtMy42MjMsMS45MjItNS4wMDIsNi40MTYtMy4wODIsMTAuMDQxDQoJCQlMMjAxLjQ2MiwyMTQuODI5eiIvPg0KCQk8cGF0aCBkPSJNMTA1Ljc4NSwzMzQuMzQ1bC04Ni4wMTctMjMuMzM4Yy0xLjkwMS0wLjUxNC0zLjkyOS0wLjI1NS01LjYzOCwwLjcyNXMtMi45NTgsMi41OTgtMy40NzUsNC40OTkNCgkJCUMzLjU4NiwzNDIuMjk1LDAsMzY5LjMwOSwwLDM5Ni41MjNjMCw0LjY1NywwLjExMSw5LjMyOSwwLjM0MiwxNC4yODRjMC4xODUsMy45ODEsMy40NjgsNy4wODMsNy40MTQsNy4wODMNCgkJCWMwLjExNiwwLDAuMjM0LTAuMDAyLDAuMzUtMC4wMDhsODkuMDMxLTQuMTEzYzEuOTY3LTAuMDksMy44Mi0wLjk2LDUuMTQ1LTIuNDE1YzEuMzI3LTEuNDU1LDIuMDIyLTMuMzgsMS45My01LjM0Nw0KCQkJYy0wLjE1NS0zLjM0MS0wLjIzLTYuNDQ0LTAuMjMtOS40ODRjMC0xOC4wMiwyLjM2NS0zNS44NzMsNy4wMjktNTMuMDY2QzExMi4wODIsMzM5LjQ5OSwxMDkuNzQzLDMzNS40MiwxMDUuNzg1LDMzNC4zNDV6Ii8+DQoJCTxwYXRoIGQ9Ik00MzguNzMxLDEyMC43NDVjLTMyLjQxMS0xNS42MjUtNjcuMDQtMjUuMzA4LTEwMi45MjUtMjguNzg2Yy0xLjk3Mi0wLjE5OC0zLjkxOCwwLjQwOC01LjQzOSwxLjY1OQ0KCQkJYy0xLjUyMSwxLjI1Mi0yLjQ4MSwzLjA1Ni0yLjY3MSw1LjAxOGwtOC41OTMsODguNzEyYy0wLjM5Niw0LjA4MiwyLjU5NCw3LjcxMyw2LjY3Nyw4LjEwOA0KCQkJYzIzLjY1MiwyLjI5MSw0Ni40NjMsOC42NjksNjcuOCwxOC45NTRjMS4wMTUsMC40OSwyLjExOCwwLjczOCwzLjIyNSwwLjczOGMwLjgyNiwwLDEuNjU0LTAuMTM5LDIuNDUtMC40MTYNCgkJCWMxLjg1OS0wLjY0OSwzLjM4NS0yLjAxMiw0LjI0LTMuNzg2bDM4LjctODAuMjg3QzQ0My45NzgsMTI2Ljk2NSw0NDIuNDI3LDEyMi41MjUsNDM4LjczMSwxMjAuNzQ1eiIvPg0KCQk8cGF0aCBkPSJNNTY5LjY0MiwyNDUuMzM3YzAuNDgtMS45MTEsMC4xODQtMy45MzItMC44MjgtNS42MjRjLTE4LjQzMi0zMC44MzUtNDEuOTMzLTU3Ljk4My02OS44NDgtODAuNjg2DQoJCQljLTEuNTI5LTEuMjQyLTMuNDgtMS44MjQtNS40NDctMS42MjdjLTEuOTU5LDAuMjAzLTMuNzU4LDEuMTc0LTUsMi43MDJsLTU2LjIzNyw2OS4xNDRjLTEuMjQyLDEuNTI5LTEuODI4LDMuNDg4LTEuNjI1LDUuNDQ3DQoJCQljMC4yMDEsMS45NTksMS4xNzMsMy43NTgsMi43MDIsNS4wMDJjMTguNDcsMTUuMDE5LDM0LjAxNSwzMi45NzUsNDYuMjA1LDUzLjM2OWMxLjM5MiwyLjMyNiwzLjg1NSwzLjYxOCw2LjM4MywzLjYxOA0KCQkJYzEuMjk3LDAsMi42MS0wLjM0LDMuODAzLTEuMDU0bDc2LjUwMS00NS43MjhDNTY3Ljk0LDI0OC44ODksNTY5LjE2LDI0Ny4yNDgsNTY5LjY0MiwyNDUuMzM3eiIvPg0KCQk8cGF0aCBkPSJNNTk4LjA0NCwzMDQuOTM5Yy0xLjIyOC0zLjkxNS01LjM5Ny02LjA5Ni05LjMwOC00Ljg2N2wtODUuMDQ4LDI2LjY0OGMtMy45MTUsMS4yMjYtNi4wOTMsNS4zOTMtNC44NjcsOS4zMDYNCgkJCWM2LjEwNCwxOS40ODYsOS4xOTksMzkuODM5LDkuMTk5LDYwLjQ5NGMwLDMuMDQxLTAuMDc2LDYuMTQ0LTAuMjMsOS40ODRjLTAuMDkyLDEuOTY3LDAuNjAyLDMuODkyLDEuOTMsNS4zNDcNCgkJCWMxLjMyNywxLjQ1NiwzLjE3OCwyLjMyNSw1LjE0NSwyLjQxNWw4OS4wMzEsNC4xMTNjMC4xMTgsMC4wMDUsMC4yMzQsMC4wMDgsMC4zNSwwLjAwOGMzLjk0NCwwLDcuMjI4LTMuMTAzLDcuNDE0LTcuMDgzDQoJCQljMC4yMjktNC45NTUsMC4zNDItOS42MjcsMC4zNDItMTQuMjg0QzYxMiwzNjUuMzA2LDYwNy4zMDYsMzM0LjQ5NCw1OTguMDQ0LDMwNC45Mzl6Ii8+DQoJCTxwYXRoIGQ9Ik0zMDUuNzM3LDM4MC43NTVjLTEuMjgxLDAtMi41NTUsMC4wNDItMy44MjQsMC4xMWwtMTIwLjY1LTcxLjE4NWMtMi45NTMtMS43NDUtNi43MDItMS4zMDgtOS4xNzYsMS4wNjUNCgkJCWMtMi40NzYsMi4zNzEtMy4wNyw2LjA5OS0xLjQ1Niw5LjEyMWw2NS44MTUsMTIzLjM1NWMtMC4yNDIsMi4zNzYtMC4zNzEsNC43NzUtMC4zNzEsNy4xOTVjMCwxOC42MDgsNy4yNDYsMzYuMTAxLDIwLjQwMyw0OS4yNTgNCgkJCWMxMy4xNTgsMTMuMTU4LDMwLjY1MiwyMC40MDQsNDkuMjYsMjAuNDA0YzE4LjYwOCwwLDM2LjEwMS03LjI0OCw0OS4yNTgtMjAuNDA0YzEzLjE1OC0xMy4xNTcsMjAuNDAzLTMwLjY1LDIwLjQwMy00OS4yNTgNCgkJCWMwLTE4LjYwOC03LjI0Ni0zNi4xMDEtMjAuNDAzLTQ5LjI1OEMzNDEuODM5LDM4OC4wMDEsMzI0LjM0NCwzODAuNzU1LDMwNS43MzcsMzgwLjc1NXoiLz4NCgk8L2c+DQo8L2c+DQo8Zz4NCjwvZz4NCjxnPg0KPC9nPg0KPGc+DQo8L2c+DQo8Zz4NCjwvZz4NCjxnPg0KPC9nPg0KPGc+DQo8L2c+DQo8Zz4NCjwvZz4NCjxnPg0KPC9nPg0KPGc+DQo8L2c+DQo8Zz4NCjwvZz4NCjxnPg0KPC9nPg0KPGc+DQo8L2c+DQo8Zz4NCjwvZz4NCjxnPg0KPC9nPg0KPGc+DQo8L2c+DQo8L3N2Zz4NCg==' />";
  String offset_image_b64 = "<img width=30px src='data:image/svg+xml;base64,PD94bWwgdmVyc2lvbj0iMS4wIiBlbmNvZGluZz0idXRmLTgiPz4KCjwhRE9DVFlQRSBzdmcgUFVCTElDICItLy9XM0MvL0RURCBTVkcgMS4xLy9FTiIgImh0dHA6Ly93d3cudzMub3JnL0dyYXBoaWNzL1NWRy8xLjEvRFREL3N2ZzExLmR0ZCI+Cjxzdmcgd2lkdGg9IjE2cHgiIGhlaWdodD0iMTZweCIgdmlld0JveD0iMCAwIDE2IDE2IiB2ZXJzaW9uPSIxLjEiIHhtbG5zPSJodHRwOi8vd3d3LnczLm9yZy8yMDAwL3N2ZyIgeG1sbnM6eGxpbms9Imh0dHA6Ly93d3cudzMub3JnLzE5OTkveGxpbmsiPgo8cGF0aCBmaWxsPSIjNDQ0IiBkPSJNMTAgN2g2djJoLTZ2LTJ6Ij48L3BhdGg+CjxwYXRoIGZpbGw9IiM0NDQiIGQ9Ik00IDVoLTJ2MmgtMnYyaDJ2Mmgydi0yaDJ2LTJoLTJ6Ij48L3BhdGg+CjxwYXRoIGZpbGw9IiM0NDQiIGQ9Ik02IDJsMyAxMmgxbC0zLTEyeiI+PC9wYXRoPgo8L3N2Zz4K' />";
  String rt_image_b64 ="<img width=30px src='data:image/svg+xml;base64,PD94bWwgdmVyc2lvbj0iMS4wIiBlbmNvZGluZz0iaXNvLTg4NTktMSI/Pg0KPCEtLSBHZW5lcmF0b3I6IEFkb2JlIElsbHVzdHJhdG9yIDE4LjEuMSwgU1ZHIEV4cG9ydCBQbHVnLUluIC4gU1ZHIFZlcnNpb246IDYuMDAgQnVpbGQgMCkgIC0tPg0KPHN2ZyB2ZXJzaW9uPSIxLjEiIGlkPSJDYXBhXzEiIHhtbG5zPSJodHRwOi8vd3d3LnczLm9yZy8yMDAwL3N2ZyIgeG1sbnM6eGxpbms9Imh0dHA6Ly93d3cudzMub3JnLzE5OTkveGxpbmsiIHg9IjBweCIgeT0iMHB4Ig0KCSB2aWV3Qm94PSIwIDAgNjEyIDYxMiIgc3R5bGU9ImVuYWJsZS1iYWNrZ3JvdW5kOm5ldyAwIDAgNjEyIDYxMjsiIHhtbDpzcGFjZT0icHJlc2VydmUiPg0KPGc+DQoJPGc+DQoJCTxwYXRoIGQ9Ik0zMDYsODcuNzM1Yy0xMjAuMzUyLDAtMjE4LjI2NSw5Ny45MTQtMjE4LjI2NSwyMTguMjY2UzE4NS42NDgsNTI0LjI2NiwzMDYsNTI0LjI2NnMyMTguMjY2LTk3LjkxMiwyMTguMjY2LTIxOC4yNjUNCgkJCVM0MjYuMzUyLDg3LjczNSwzMDYsODcuNzM1eiBNNDIwLjgwMSwzMjguNjU4SDMwNmMtMTIuNTEzLDAtMjIuNjU4LTEwLjE0NS0yMi42NTgtMjIuNjU2VjE2Ni4zMDYNCgkJCWMwLTEyLjUxMywxMC4xNDMtMjIuNjU2LDIyLjY1OC0yMi42NTZjMTIuNTEzLDAsMjIuNjU2LDEwLjE0NSwyMi42NTYsMjIuNjU2djExNy4wMzdoOTIuMTQ1DQoJCQljMTIuNTEzLDAsMjIuNjU4LDEwLjE0NSwyMi42NTgsMjIuNjU4QzQ0My40NTcsMzE4LjUxMyw0MzMuMzE1LDMyOC42NTgsNDIwLjgwMSwzMjguNjU4eiIvPg0KCQk8cGF0aCBkPSJNMzA2LDBDMTM3LjI3MiwwLDAsMTM3LjI3MiwwLDMwNi4wMDJDMCw0NzQuNzMsMTM3LjI3Miw2MTIsMzA2LDYxMnMzMDYtMTM3LjI3LDMwNi0zMDUuOTk4QzYxMiwxMzcuMjcyLDQ3NC43MjgsMCwzMDYsMHoNCgkJCSBNMzA2LDU2NC45MjNDMTYzLjIzLDU2NC45MjMsNDcuMDc3LDQ0OC43Nyw0Ny4wNzcsMzA2QzQ3LjA3NywxNjMuMjI4LDE2My4yMjgsNDcuMDc0LDMwNiw0Ny4wNzQNCgkJCWMxNDIuNzcsMCwyNTguOTIzLDExNi4xNTQsMjU4LjkyMywyNTguOTI2QzU2NC45MjMsNDQ4Ljc3LDQ0OC43Nyw1NjQuOTIzLDMwNiw1NjQuOTIzeiIvPg0KCTwvZz4NCjwvZz4NCjxnPg0KPC9nPg0KPGc+DQo8L2c+DQo8Zz4NCjwvZz4NCjxnPg0KPC9nPg0KPGc+DQo8L2c+DQo8Zz4NCjwvZz4NCjxnPg0KPC9nPg0KPGc+DQo8L2c+DQo8Zz4NCjwvZz4NCjxnPg0KPC9nPg0KPGc+DQo8L2c+DQo8Zz4NCjwvZz4NCjxnPg0KPC9nPg0KPGc+DQo8L2c+DQo8Zz4NCjwvZz4NCjwvc3ZnPg0K' />";
  String heat_image_b64 = "<img width=50px src='data:image/svg+xml;base64,PHN2ZyB3aWR0aD0iNTEycHgiIGhlaWdodD0iNTEycHgiIHZpZXdCb3g9IjAgMCA1MTIgNTEyIiB4bWxucz0iaHR0cDovL3d3dy53My5vcmcvMjAwMC9zdmciPjxwYXRoIGZpbGw9IiMwMDAiIGQ9Ik0zMjguMDk0IDE2LjI4Yy00MTguNTQ3IDE4OS41OSA1OC4xMDggMjMwLjE0Ni04Ni4zMTMgNDczLjUzM0M1NjYuNjQ2IDI0Ny4wMzUgNTkuNzIzIDI1Ni44MzcgMzI4LjA5NSAxNi4yOHptMTAuODQ0IDMyLjQ0QzE1NC43MTQgMTg2LjEgNDc1LjIyNiAyNTMuNjQgMzY5LjcxNyA0MDkuMDYgNTYxLjQ4IDI1My4wMjggMjQ4LjIxNSAyMDMuNzY4IDMzOC45NCA0OC43MnpNMTQxIDEwMi4yNWMtMTc0LjI0NCAxMzUuMDI1IDEwNC4zMzIgMjE1Ljc1NCA2MS4wNjMgMzY3QzMwNy4wMyAyODUuNzcgNDIuODg3IDI2OC4zMSAxNDEgMTAyLjI1eiIvPjwvc3ZnPg==' />";
  String stb_image_b64 = "<img width=50px src='data:image/svg+xml;base64,PHN2ZyB3aWR0aD0iOHB4IiBoZWlnaHQ9IjhweCIgdmlld0JveD0iMCAwIDggOCIgeG1sbnM9Imh0dHA6Ly93d3cudzMub3JnLzIwMDAvc3ZnIj4KICA8cGF0aCBkPSJNMyAwdjRoMXYtNGgtMXptLTEuMjggMS40NGwtLjM4LjMxYy0uODEuNjQtMS4zNCAxLjY0LTEuMzQgMi43NSAwIDEuOTMgMS41NyAzLjUgMy41IDMuNXMzLjUtMS41NyAzLjUtMy41YzAtMS4xMS0uNTMtMi4xMS0xLjM0LTIuNzVsLS4zOC0uMzEtLjYzLjc4LjM4LjMxYy41OC40Ni45NyAxLjE3Ljk3IDEuOTcgMCAxLjM5LTEuMTEgMi41LTIuNSAyLjVzLTIuNS0xLjExLTIuNS0yLjVjMC0uOC4zNi0xLjUxLjk0LTEuOTdsLjQxLS4zMS0uNjMtLjc4eiIKICAvPgo8L3N2Zz4=' />";
  String ptr = "<!DOCTYPE html> <html>\n";
  ptr +="<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0, user-scalable=no\">\n";
  ptr +="<meta http-equiv='refresh' content='15'> \n ";
  ptr +="<title>Pool Solar Control</title>\n";
  ptr +="<style>html { font-family: Helvetica; display: inline-block; margin: 0px auto; text-align: center;}\n";
  ptr +="body{margin-top: 50px;} h1 {color: #444444;margin: 50px auto 30px;}\n";
  ptr +="p {font-size: 24px;color: #444444;margin-bottom: 10px;}\n";
  ptr +="</style>\n";
  ptr +="</head>\n";
  ptr +="<body>\n";
  ptr +="<div id=\"webpage\">\n";
  ptr +="<h1>Solar Pool Control</h1>\n <p>";
  ptr +="Betriebsmodus: <p>";
  if(heizen){
    ptr += heat_image_b64;
    ptr += "<p> HEIZEN <p>";  
  }else{
    ptr += stb_image_b64;
    ptr += "<p> STANDBY <p>";    
  }
  ptr += sol_image_b64;
  ptr +="   Solar Temperature: ";
  ptr +=solar;
  ptr +="&deg;C";
  if(ssim){
    ptr += " (simuliert!)";  
  }
  "</p>";
  ptr +="</p><p>";
  ptr += pool_image_b64;
  ptr += "   Pool: ";
  ptr +=pool;
  ptr +="&deg;C / ";
  ptr +=soll;
  ptr +="&deg;C";
  if(psim){
    ptr += " (simuliert!)";  
  }
  ptr += "</p><p>";
  ptr += offset_image_b64;
  ptr += "   Heiz Offset: ";
  ptr +=offset;
  ptr +="&deg;C</p><p>";
  ptr += rt_image_b64;
  ptr +="   Refresh Time: ";
  ptr +=rt;
  ptr +=" Sek</p>";
  ptr +="</p><p><p>";
  ptr +="Value Change via MQTT Topics: <p> ";
  ptr +="/garten/pool/solarcontrol/soll <p>";
  ptr +="/garten/pool/solarcontrol/offset <p>";
  ptr +="/garten/pool/solarcontrol/refreshtime <p>";
  ptr +="</div>\n";
  ptr +="</body>\n";
  ptr +="</html>\n";
  return ptr;
}


void setup_wifi() {
    WiFi.begin(SSID, PSK);
 
    while (WiFi.status() != WL_CONNECTED) {
        delay(100);
    }
    
    Serial.println(WiFi.localIP());
    lcd.clear();
    lcd.setCursor(0,0);
    lcd.print("WIFI connected!");
    lcd.setCursor(0,1);
    lcd.print(WiFi.localIP());
    // Start Webserver
    server.on("/", handle_OnConnect);
    server.onNotFound(handle_NotFound);
    server.begin();
    Serial.println("HTTP server started");
    delay(1000);
}

// Lauschen auf MQTT Changes
void callback(char* topic, byte* payload, unsigned int length) {
    String msg;
    
    if (strcmp(topic,"/garten/pool/solarcontrol/soll")==0){
      for (byte i = 0; i < length; i++) {
          char tmp = char(payload[i]);
           msg += tmp;
      }
      Serial.print("MQTT Update Soll: ");
      Serial.println(msg);
      soll = msg.toInt();
      
    }

    if (strcmp(topic,"/garten/pool/solarcontrol/offset")==0){
      for (byte i = 0; i < length; i++) {
          char tmp = char(payload[i]);
           msg += tmp;
      }
      Serial.print("MQTT Update Offset: ");
      Serial.println(msg);
      offset = msg.toInt();
      
    }
    if (strcmp(topic,"/garten/pool/solarcontrol/refreshtime")==0){
      for (byte i = 0; i < length; i++) {
          char tmp = char(payload[i]);
           msg += tmp;
      }
      Serial.print("MQTT Update Refreshtime: ");
      Serial.println(msg);
      rt = msg.toInt();
      myTimeout = rt * 1000; 
      
    }
    
}

boolean soll_erreicht(){
  if(pool >= soll){
    return true;
  }else{
    return false;
  }
  
}

boolean heizleistung_verfuegbar(){
  if(solar >= pool+offset){
    return true;
  }else{
    return false;
  }
  
}

void update_temps(){
   sensors.requestTemperatures(); 
   float temperatureC_pool = sensors.getTempC(sensor_pool);
   float temperatureC_solar = sensors.getTempC(sensor_solar);
   if(temperatureC_pool > -127){
      pool = temperatureC_pool;
      Serial.print("Pooltemperatur: ");
      Serial.print(pool);
      Serial.println("ºC");
      psim = false; 
   }else{
      pool= random(20,35);
      Serial.print("Pooltemperatur: ");
      Serial.print(pool);
      Serial.println("ºC (simulated)"); 
      psim = true;
   }
   if(temperatureC_solar > -127){
      solar = temperatureC_solar;
      Serial.print("Solartemperatur: ");
      Serial.print(solar);
      Serial.println("ºC"); 
      ssim = false;
   }else{
      solar= random(20,45);
      Serial.print("Solartemperatur: ");
      Serial.print(solar);
      Serial.println("ºC (simulated)"); 
      ssim = true;
   }
      Serial.print("Solltemperatur: ");
      Serial.print(soll);
      Serial.println("ºC"); 
      Serial.print("Offset: ");
      Serial.print(offset);
      Serial.println("ºC"); 
      Serial.print("Refreshtime: ");
      Serial.print(rt);
      Serial.println("s"); 
      Serial.println("*********");
      Serial.println("");  
}

void loop() {
   
   char pchar[16];
   if (!client.connected()) {
        while (!client.connected()) {
            client.connect("PoolController");    
            if(startup_mqtt){        
              client.publish("/garten/pool/solarcontrol/soll", itoa(soll, pchar, 10));
              client.publish("/garten/pool/solarcontrol/refreshtime", itoa(rt, pchar, 10));
              client.publish("/garten/pool/solarcontrol/offset", itoa(offset, pchar, 10));
              startup_mqtt = false;
            }
            client.subscribe("/garten/pool/solarcontrol/soll");
            client.subscribe("/garten/pool/solarcontrol/offset");
            client.subscribe("/garten/pool/solarcontrol/refreshtime");
            delay(100);
            lcd.setCursor(0,0);
            lcd.print("MQTT link ready");
        }
    }
    client.loop();
    server.handleClient();
  
  if (millis() > myTimeout + myTimer ) {
    myTimer = millis();
  
    if(startup_timer){
      myTimeout = rt *1000;  
    }
  
    lcd.clear();
    lcd.setCursor(0,0);
    String out2 = srt + rt;
    lcd.print(out2);
    lcd.print("s");
    lcd.setCursor(0,1);
    out2 = soffset + offset;
    lcd.print(out2);
    lcd.write(0);
    lcd.print("C");
    delay(3000);
    
    // HIER Aktuelle Temperaturen lesen
    update_temps();

    //Neue Temperaturen per MQTT pushen
    client.publish("/garten/pool/solarcontrol/solar", itoa(solar, pchar, 10));
    client.publish("/garten/pool/solarcontrol/status", "online");
    client.publish("/garten/pool/solarcontrol/pool", itoa(pool, pchar, 10));
    
    lcd.clear();
    lcd.setCursor(0,0);
    String out = ssolar + solar;
    lcd.print(out);
    lcd.write(0);
    lcd.print("C");
    
    if(!soll_erreicht() && heizleistung_verfuegbar()){
       lcd.print(" HEAT");
       client.publish("/garten/pool/solarcontrol/ventil", "heizen");
       digitalWrite(pinRelais , LOW);
       relais_on = true;
    }
    // Solaranlage könnte heizen aber SOLL ist erreicht
    if(!soll_erreicht() && !heizleistung_verfuegbar()){      
      lcd.print(" LOW");
      client.publish("/garten/pool/solarcontrol/ventil", "bypass");
      digitalWrite(pinRelais , HIGH);
      relais_on = false;
    }
    if(soll_erreicht() && !heizleistung_verfuegbar()){      
      lcd.print(" STBY");
      client.publish("/garten/pool/solarcontrol/ventil", "bypass");
      digitalWrite(pinRelais , HIGH);
      relais_on = false;
    }
     if(soll_erreicht() && heizleistung_verfuegbar()){      
      lcd.print(" STBY");
      client.publish("/garten/pool/solarcontrol/ventil", "bypass");
      digitalWrite(pinRelais , HIGH);
      relais_on = false;
    }
    //2.Line
    out = spool + pool;
    lcd.setCursor(0,1);
    lcd.print(out);
    lcd.write(0);
    lcd.print("C");
    String dash = "/";
    out = dash + soll;
    lcd.print(out);
    lcd.write(0);
    lcd.print("C");    
  }
}
