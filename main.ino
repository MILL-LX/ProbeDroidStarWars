#include <WiFi.h>
#include <WebServer.h>
#include <HardwareSerial.h>
#include <DYPlayerArduino.h>
#include "Adafruit_VL53L1X.h"
#include <AccelStepper.h>

#include <Adafruit_NeoPixel.h>
#ifdef __AVR__
 #include <avr/power.h> // Required for 16 MHz Adafruit Trinket
#endif


#define PIN        D8  // No caso do seu pino
#define NUMPIXELS 96   // Tamanho da fita de LEDs


#define IRQ_PIN 2
#define XSHUT_PIN 3
#define STEP_PIN  4
#define DIR_PIN   5  



Adafruit_NeoPixel pixels(NUMPIXELS, PIN, NEO_RGB + NEO_KHZ800);

Adafruit_VL53L1X vl53 = Adafruit_VL53L1X(XSHUT_PIN, IRQ_PIN);
AccelStepper stepper(AccelStepper::DRIVER, STEP_PIN, DIR_PIN);

#define DELAYVAL 10  // Tempo entre cada mudança de intensidade
#define FADE_SPEED 5 // Velocidade do fade

// Configuração do Access Point
const char* ssid = "Probe Droid";  
const char* password = "12345678";  

bool GreenState = false;
bool BlueState = false;
bool RedState = false;

bool fadeGreenActive = false;
bool fadeRedActive = false;
bool fadeBlueActive = false;

bool blinkGreenActive = false;
bool blinkRedActive = false;
bool blinkBlueActive = false;

WebServer server(80);

// Serial para comunicação com o módulo MP3 (usando UART1 no ESP32-C3)
HardwareSerial mp3(1);

// Inicializar a porta serial
HardwareSerial SerialPort1(1);  // Usar UART1

// Inicializar o player de música
DY::Player player(&SerialPort1);

// Página HTML com botões para controlar a música, volume e LEDs
const char html_page[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="pt-BR">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>ESP32 MP3 Player</title>
    <style>
        body { font-family: Arial, sans-serif; text-align: center; margin: 50px; }
        h1 { color: #007BFF; }
        button { padding: 10px 20px; font-size: 18px; cursor: pointer; margin: 10px; }
        .play { background: #28a745; color: white; border: none; border-radius: 5px; }
        .play:hover { background: #218838; }
        .stop { background: #dc3545; color: white; border: none; border-radius: 5px; }
        .stop:hover { background: #c82333; }
        .vol { background: #ffc107; color: black; border: none; border-radius: 5px; }
        .vol:hover { background: #e0a800; }
        .leds { background: #17a2b8; color: white; border: none; border-radius: 5px; }
        .leds:hover { background: #138496; }
        .submenu, .led-control { display: none; margin-top: 20px; }
        .led-btn, .control-btn { margin: 5px; padding: 10px 15px; font-size: 16px; cursor: pointer; border-radius: 5px; }
        .green { background-color: green; color: white; }
        .red { background-color: red; color: white; }
        .blue { background-color: blue; color: white; }
    </style>
</head>
<body>
    <h1>Probe Droid</h1>
    <p>Controle sua música e LEDs com os botões abaixo.</p>
    <button class="play" onclick="playMusic()">Tocar</button>
    <button class="stop" onclick="stopMusic()">Parar</button>
    <button class="vol" onclick="increaseVolume()">Aumentar Volume</button>
    <button class="vol" onclick="decreaseVolume()">Diminuir Volume</button>
    <button class="leds" onclick="toggleLEDMenu()">LEDs</button>

    <div id="ledMenu" class="submenu">
        <button class="led-btn green" onclick="controlLED('green', 'on')">Ligar Verde</button>
        <button class="led-btn green" onclick="controlLED('green', 'off')">Desligar Verde</button>
        <button class="led-btn green" onclick="controlLED('green', 'blink')">Piscar Verde</button>
        <button class="led-btn green" onclick="controlLED('green', 'fade')">Fade Verde</button>
        <button class="led-btn red" onclick="controlLED('red', 'on')">Ligar Vermelho</button>
        <button class="led-btn red" onclick="controlLED('red', 'off')">Desligar Vermelho</button>
        <button class="led-btn red" onclick="controlLED('red', 'blink')">Piscar Vermelho</button>
        <button class="led-btn red" onclick="controlLED('red', 'fade')">Fade Vermelho</button>
        <button class="led-btn blue" onclick="controlLED('blue', 'on')">Ligar Azul</button>
        <button class="led-btn blue" onclick="controlLED('blue', 'off')">Desligar Azul</button>
        <button class="led-btn blue" onclick="controlLED('blue', 'blink')">Piscar Azul</button>
        <button class="led-btn blue" onclick="controlLED('blue', 'fade')">Fade Azul</button>
    </div>

    <script>
        function playMusic() {
            fetch('/play').then(() => console.log('Tocando música')).catch(error => console.error('Erro:', error));
        }
        function stopMusic() {
            fetch('/stop').then(() => console.log('Parando música')).catch(error => console.error('Erro:', error));
        }
        function increaseVolume() {
            fetch('/volume/up').then(() => console.log('Aumentando volume')).catch(error => console.error('Erro:', error));
        }
        function decreaseVolume() {
            fetch('/volume/down').then(() => console.log('Diminuindo volume')).catch(error => console.error('Erro:', error));
        }
        function toggleLEDMenu() {
            const menu = document.getElementById('ledMenu');
            menu.style.display = (menu.style.display === 'none' || menu.style.display === '') ? 'block' : 'none';
        }
        function controlLED(color, action) {
            fetch(`/led/${color}/${action}`).then(() => console.log(`LED ${color} ${action} acionado`)).catch(error => console.error('Erro:', error));
        }
    </script>
</body>
</html>
           
)rawliteral";

int volume = 15; // Volume inicial
bool ledState[] = {false, false, false}; // Estados dos LEDs: verde, vermelho, azul

// Função para enviar comandos ao módulo MP3
void mp3_command(int8_t command, int16_t dat) {
    int8_t frame[8] = { 0x7e, 0xff, 0x06, command, 0x00, (int8_t)(dat >> 8), (int8_t)(dat), 0xef };
    for (uint8_t i = 0; i < 8; i++) {
        mp3.write(frame[i]);
    }
}

// Funções chamadas ao acessar as rotas do servidor
void handleRoot() {
    server.send(200, "text/html", html_page);
}

void handlePlay() {
    player.play();
    server.send(200, "text/plain", "Música tocando...");
}

void handleStop() {
    player.pause();
    server.send(200, "text/plain", "Música parada...");
}

void handleVolumeUp() {
    if (volume < 30) volume++;
    player.setVolume(volume);
    server.send(200, "text/plain", "Volume aumentado para " + String(volume));
}

void handleVolumeDown() {
    if (volume > 0) volume--;
    player.setVolume(volume);
    server.send(200, "text/plain", "Volume diminuído para " + String(volume));
}


#define GreenPinsCount 40

int GreenPins[] = {12, 13, 14, 15, 16, 17, 18, 19, 20 ,21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 64, 65, 66, 67, 68, 69, 70 ,71, 88, 89, 90, 91, 92, 93, 94, 95};

void handleLEDControlGreen() {
    GreenState = !GreenState;

    for (int i = 0; i < GreenPinsCount; i++) {
        int pin = GreenPins[i];
        pixels.setPixelColor(pin, GreenState ? pixels.Color(150, 0, 0) : pixels.Color(0, 0, 0));
    }

    pixels.show();

    String color = server.arg("color");
    server.send(200, "text/plain", color + " LED " + (GreenState ? "ligado" : "desligado"));
}



#define RedPinsCount 40

int RedPins[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 72, 73, 74, 75, 76, 77, 78, 79, 80, 81, 82, 83, 84, 85, 86, 87};

void handleLEDControlRed() {
    RedState = !RedState;

    for (int i = 0; i < RedPinsCount; i++) {
        int pin = RedPins[i];
        pixels.setPixelColor(pin, RedState ? pixels.Color(0, 150, 0) : pixels.Color(0, 0, 0));
    }

    pixels.show();

    String color = server.arg("color");
    server.send(200, "text/plain", color + " LED " + (RedState ? "ligado" : "desligado"));
}


#define BluePinsCount 16

int BluePins[] = {36, 37, 38, 39, 40, 41, 42, 43, 56, 57, 58, 59, 60, 61, 62, 63};

void handleLEDControlBlue() {
    BlueState = !BlueState;

    for (int i = 0; i < BluePinsCount; i++) {
        int pin = BluePins[i];
        pixels.setPixelColor(pin, BlueState ? pixels.Color(0, 0, 150) : pixels.Color(0, 0, 0));
    }

    pixels.show();

    String color = server.arg("color");
    server.send(200, "text/plain", color + " LED " + (BlueState ? "ligado" : "desligado"));
}



void setup() {

    #if defined(__AVR_ATtiny85__) && (F_CPU == 16000000)
    clock_prescale_set(clock_div_1);
    #endif
    
    Serial.begin(115200);

    pixels.begin(); // Inicializa os LEDs
    pixels.setBrightness(255);
    pixels.clear();
    SerialPort1.begin(9600, SERIAL_8N1, 20, 21);

    // Iniciar o Access Point
    WiFi.softAP(ssid, password);
    Serial.println("Access Point iniciado!");
    
    IPAddress IP = WiFi.softAPIP();
    Serial.print("Acesse a página em: http://");
    Serial.println(IP);


    Wire.begin();
    if (!vl53.begin(0x29, &Wire)) {
        Serial.print(F("Erro ao iniciar VL sensor: "));
        Serial.println(vl53.vl_status);
        while (1) delay(10);
    }
    Serial.println(F("VL53L1X sensor OK!"));
    
    if (!vl53.startRanging()) {
        Serial.print(F("Não foi possível iniciar a medição: "));
        Serial.println(vl53.vl_status);
        while (1) delay(10);
    }
    Serial.println(F("Medição iniciada"));
    vl53.setTimingBudget(50);
    
    // Configuração do motor de passo
    stepper.setMaxSpeed(3000);
    stepper.setAcceleration(1000);
    stepper.moveTo(8000);


    

    delay(100);
    player.begin();
    delay(100);
    player.setVolume(30); // 50% Volume
    delay(100);

    //player.setCycleMode(DY::PlayMode::Repeat);

    // Configurar rotas do servidor
    server.on("/", handleRoot);
    server.on("/play", handlePlay);
    server.on("/stop", handleStop);
    server.on("/volume/up", handleVolumeUp);
    server.on("/volume/down", handleVolumeDown);
    server.on("/led/blue/on", handleLEDControlBlue);
    server.on("/led/blue/blink", handleLEDControlBlueBlink);
    server.on("/led/blue/fade", handleLEDControlBlueFade);
    server.on("/led/blue/off", handleLEDControlBlueOff);
    server.on("/led/green/on", handleLEDControlGreen);
    server.on("/led/green/blink", handleLEDControlGreenBlink);
    server.on("/led/green/fade", handleLEDControlGreenFade);
    server.on("/led/green/off", handleLEDControlGreenOff);
    server.on("/led/red/on", handleLEDControlRed);
    server.on("/led/red/blink", handleLEDControlRedBlink);
    server.on("/led/red/fade", handleLEDControlRedFade);
    server.on("/led/red/off", handleLEDControlRedOff);
    
    

    server.begin();


    TesteBegin();


  }

void handleLEDControlGreenFade() {
    fadeGreenActive = !fadeGreenActive;
    server.send(200, "text/plain", fadeGreenActive ? "Fade Verde Ativado" : "Fade Verde Desativado");
}

void handleLEDControlRedFade() {
    fadeRedActive = !fadeRedActive;
    server.send(200, "text/plain", fadeRedActive ? "Fade Vermelho Ativado" : "Fade Vermelho Desativado");
}

void handleLEDControlBlueFade() {
    fadeBlueActive = !fadeBlueActive;
    server.send(200, "text/plain", fadeBlueActive ? "Fade Azul Ativado" : "Fade Azul Desativado");
}


void handleLEDControlGreenBlink() {
    blinkGreenActive = !blinkGreenActive;
    server.send(200, "text/plain", fadeGreenActive ? "Fade Verde Ativado" : "Fade Verde Desativado");
}

void handleLEDControlRedBlink() {
    blinkRedActive = !blinkRedActive;
    server.send(200, "text/plain", fadeRedActive ? "Fade Vermelho Ativado" : "Fade Vermelho Desativado");
}

void handleLEDControlBlueBlink() {
    blinkBlueActive = !blinkBlueActive;
    server.send(200, "text/plain", fadeBlueActive ? "Fade Azul Ativado" : "Fade Azul Desativado");
}


void handleLEDControlGreenOff() {
  
    blinkGreenActive = false;
    fadeGreenActive = false;

    for (int i = 0; i < GreenPinsCount; i++) {
        int pin = GreenPins[i];
        pixels.setPixelColor(pin, pixels.Color(0, 0, 0));
    }
    
    pixels.show();

    
    server.send(200, "text/plain", fadeGreenActive ? "Fade Verde Ativado" : "Fade Verde Desativado");
    
}

void handleLEDControlRedOff() {
  
    blinkRedActive = false;
    fadeRedActive = false;


    for (int i = 0; i < RedPinsCount; i++) {
        int pin = RedPins[i];
        pixels.setPixelColor(pin, pixels.Color(0, 0, 0));
    }
    
    pixels.show();

    
    server.send(200, "text/plain", fadeRedActive ? "Fade Vermelho Ativado" : "Fade Vermelho Desativado");
  
}

void handleLEDControlBlueOff() {
  
    blinkBlueActive = false;
    fadeBlueActive = false;



    for (int i = 0; i < BluePinsCount; i++) {
        int pin = BluePins[i];
        pixels.setPixelColor(pin, pixels.Color(0, 0, 0));
    }
    
    pixels.show();

    
    server.send(200, "text/plain", fadeBlueActive ? "Fade Azul Ativado" : "Fade Azul Desativado");

}






unsigned long lastActivationTime = 0; // Variável para armazenar o tempo da última ativação

  


void loop() {

    int16_t distance;


    if (vl53.dataReady()) {
        distance = vl53.distance();
        if (distance == -1) {
            Serial.print(F("Erro ao obter a distância: "));
            Serial.println(vl53.vl_status);
            return;
        }
        Serial.print(F("Distância: "));
        Serial.print(distance);
        Serial.println(" mm");
        
        vl53.clearInterrupt();
        
        // Se a distância for 0 mm, inicia o motor por 10 segundos
        if (distance == 0) {

            
            //player.play();
            //player.playSpecified(3);
            char path[] = "/00002.mp3";
            player.playSpecifiedDevicePath(DY::Device::Sd, path);

                        
            unsigned long startTime = millis();
            while (millis() - startTime < 20000) {
                if (stepper.distanceToGo() == 0) {
                    stepper.moveTo(-stepper.currentPosition());
                }
                stepper.run();
            }
            player.pause();
        
        }
    }
    
    
    server.handleClient(); // Mantém o servidor ativo

    if (fadeGreenActive) {
        fadeGreen();
    }
    if (fadeRedActive) {
        fadeRed();
    }
    if (fadeBlueActive) {
        fadeBlue();
    }
    if (blinkGreenActive) {
        blinkGreen();
    }
    if (blinkRedActive) {
        blinkRed();
    }
    if (blinkBlueActive) {
        blinkBlue();
    }



    if (millis() - lastActivationTime >= 60000) { // 60.000 ms = 1 minuto
        lastActivationTime = millis(); // Atualiza o tempo da última ativação
        
        Serial.println(F("Executando ação a cada 1 minuto"));


/*
        player.setCycleMode(DY::PlayMode::OneOff);   
        char path[] = "/00004.wav";
        player.playSpecifiedDevicePath(DY::Device::Sd, path);
*/

        int randomNumber = random(1, 6);

        
        char path[10];  // Declare path outside the switch statement


        Serial.println(randomNumber);
        switch (randomNumber) {
            case 1:
                player.setCycleMode(DY::PlayMode::OneOff);
                strcpy(path, "/00003.mp3");  // Use strcpy to assign value to path
                player.playSpecifiedDevicePath(DY::Device::Sd, path);
                break;
            case 2:
                player.setCycleMode(DY::PlayMode::OneOff);
                strcpy(path, "/00004.wav");
                player.playSpecifiedDevicePath(DY::Device::Sd, path);
                break;
            case 3:
                player.setCycleMode(DY::PlayMode::OneOff);
                strcpy(path, "/00005.wav");
                player.playSpecifiedDevicePath(DY::Device::Sd, path);
                break;
            case 4:
                player.setCycleMode(DY::PlayMode::OneOff);
                strcpy(path, "/00006.wav");
                player.playSpecifiedDevicePath(DY::Device::Sd, path);
                break;
            case 5:
                player.setCycleMode(DY::PlayMode::OneOff);
                strcpy(path, "/00007.wav");
                player.playSpecifiedDevicePath(DY::Device::Sd, path);
                break;
            case 6:
                player.setCycleMode(DY::PlayMode::OneOff);
                strcpy(path, "/00008.mp3");
                player.playSpecifiedDevicePath(DY::Device::Sd, path);
                break;
        }
    }



    
}



void fadeGreen() {
    for (int brightness = 0; brightness <= 255; brightness += FADE_SPEED) {
        if (!fadeGreenActive) return;  // Sai da função se o fade for desativado
        for (int i = 0; i < GreenPinsCount; i++) {
            int pin = GreenPins[i];
            pixels.setPixelColor(pin, pixels.Color(brightness, 0, 0));
        }
        pixels.show();
        delay(DELAYVAL);
    }

    for (int brightness = 255; brightness >= 0; brightness -= FADE_SPEED) {
        if (!fadeGreenActive) return;
        for (int i = 0; i < GreenPinsCount; i++) {
            int pin = GreenPins[i];
            pixels.setPixelColor(pin, pixels.Color(brightness, 0, 0));
        }
        pixels.show();
        delay(DELAYVAL);
    }
}

void fadeRed() {
    for (int brightness = 0; brightness <= 255; brightness += FADE_SPEED) {
        if (!fadeRedActive) return;
        for (int i = 0; i < RedPinsCount; i++) {
            int pin = RedPins[i];
            pixels.setPixelColor(pin, pixels.Color(0, brightness, 0));
        }
        pixels.show();
        delay(DELAYVAL);
    }

    for (int brightness = 255; brightness >= 0; brightness -= FADE_SPEED) {
        if (!fadeRedActive) return;
        for (int i = 0; i < RedPinsCount; i++) {
            int pin = RedPins[i];
            pixels.setPixelColor(pin, pixels.Color(0, brightness, 0));
        }
        pixels.show();
        delay(DELAYVAL);
    }
}

void fadeBlue() {
    for (int brightness = 0; brightness <= 255; brightness += FADE_SPEED) {
        if (!fadeBlueActive) return;
        for (int i = 0; i < BluePinsCount; i++) {
            int pin = BluePins[i];
            pixels.setPixelColor(pin, pixels.Color(0, 0, brightness));
        }
        pixels.show();
        delay(DELAYVAL);
    }

    for (int brightness = 255; brightness >= 0; brightness -= FADE_SPEED) {
        if (!fadeBlueActive) return;
        for (int i = 0; i < BluePinsCount; i++) {
            int pin = BluePins[i];
            pixels.setPixelColor(pin, pixels.Color(0, 0, brightness));
        }
        pixels.show();
        delay(DELAYVAL);
    }
}





void blinkGreen() {
    static bool state = false;
    if (!blinkGreenActive) return;

    for (int i = 0; i < GreenPinsCount; i++) {
        int pin = GreenPins[i];
        pixels.setPixelColor(pin, state ? pixels.Color(255, 0, 0) : pixels.Color(0, 0, 0));
    }
    pixels.show();
    state = !state;
    delay(100);
}

void blinkRed() {
    static bool state = false;
    if (!blinkRedActive) return;

    for (int i = 0; i < RedPinsCount; i++) {
        int pin = RedPins[i];
        pixels.setPixelColor(pin, state ? pixels.Color(0, 255, 0) : pixels.Color(0, 0, 0));
    }
    pixels.show();
    state = !state;
    delay(100);
}

void blinkBlue() {
    static bool state = false;
    if (!blinkBlueActive) return;

    for (int i = 0; i < BluePinsCount; i++) {
        int pin = BluePins[i];
        pixels.setPixelColor(pin, state ? pixels.Color(0, 0, 255) : pixels.Color(0, 0, 0));
    }
    pixels.show();
    state = !state;
    delay(100);
}




void TesteBegin(){

  GreenState = true;
  BlueState = true;
  RedState = true;

  for (int i = 0; i < RedPinsCount; i++) {
        int pin = RedPins[i];
        pixels.setPixelColor(pin, pixels.Color(0, 150, 0));
    }

  for (int i = 0; i < BluePinsCount; i++) {
        int pin = BluePins[i];
        pixels.setPixelColor(pin, pixels.Color(0, 0, 150));
    }

  for (int i = 0; i < GreenPinsCount; i++) {
        int pin = GreenPins[i];
        pixels.setPixelColor(pin, pixels.Color(150, 0, 0));
    }


  pixels.show();


  unsigned long startTime = millis();
            while (millis() - startTime < 10000) {
                if (stepper.distanceToGo() == 0) {
                    stepper.moveTo(-stepper.currentPosition());
                }
                stepper.run();
  }
    
  
}
