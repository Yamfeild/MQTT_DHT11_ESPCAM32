// ============================================
// Proyecto: UNL-Cloud-Connect
// Hardware: ESP32-CAM + Adaptador TTL + DHT11
// ============================================
#include <WiFi.h>
#include <PubSubClient.h>
#include <DHT.h>

// ---- Configuración de red ----
const char* WIFI_SSID     = "deivys";       // Pon el nombre de tu Wi-Fi
const char* WIFI_PASSWORD = "abcd1234";     // Pon tu contraseña

// ---- Configuración MQTT (Tu servidor FastAPI) ----
// Aquí debes poner la IP de tu laptop "Master-David" (búscala ejecutando 'ip a' en la terminal)
const char* MQTT_BROKER   = "10.168.240.187"; 
const int   MQTT_PORT     = 1884;

// El tópico exacto que programaste en app/mqtt/client.py
const char* TOPIC_CLIMA   = "unl/clima/esp32"; 

// ---- Configuración DHT11 ----
// Usaremos el GPIO 15, que está disponible en la ESP32-CAM (Pin U0R no se toca)
#define DHTPIN    15       
#define DHTTYPE   DHT11    

DHT dht(DHTPIN, DHTTYPE);

// En la ESP32-CAM, el LED rojo pequeño interno está en el pin 33 (lógica invertida)
#define LED_PIN   33        
// El sensor DHT11 es lento. 5 segundos es el tiempo ideal para no saturarlo
#define INTERVALO 300000     

WiFiClient   espClient;
PubSubClient client(espClient);
unsigned long lastMsg = 0;

// ---- Conectar WiFi ----
void setup_wifi() {
  Serial.print("Conectando a ");
  Serial.println(WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\n✓ WiFi conectado. IP: ");
  Serial.println(WiFi.localIP());
}

// ---- Reconectar a tu Mosquitto Local ----
void reconnect() {
  while (!client.connected()) {
    Serial.print("Conectando a Mosquitto Docker...");
    // Client ID aleatorio
    String clientId = "ESP32CAM-Sensor-";
    clientId += String(random(0xffff), HEX);
    
    if (client.connect(clientId.c_str())) {
      Serial.println(" ✓ Conectado");
    } else {
      Serial.print(" Falló. Código: ");
      Serial.print(client.state());
      Serial.println(" -> Reintentando en 5s");
      delay(5000);
    }
  }
}

// ---- Setup ----
void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH); // Apagar LED al inicio (HIGH lo apaga en el pin 33)
  
  dht.begin();                // Inicializar DHT11
  setup_wifi();
  client.setServer(MQTT_BROKER, MQTT_PORT);
}

// ---- Loop principal ----
void loop() {
  if (!client.connected()) reconnect();
  client.loop(); // Mantiene viva la conexión MQTT

  unsigned long now = millis();
  if (now - lastMsg > INTERVALO) {
    lastMsg = now;
    
    // 1. Leer DHT11
    float t = dht.readTemperature();
    float h = dht.readHumidity();

    // Verificar si la lectura falló (sensor desconectado o error de lectura)
    if (isnan(t) || isnan(h)) {
      Serial.println("[ERROR] Fallo de lectura DHT11. Revisa los cables.");
      return; // Aborta este ciclo y vuelve a intentar en 5 minutos
    }

    // 2. Inicializamos las variables de control en estado seguro
    bool alerta_climatica = false;
    String mensaje_alerta = "";

    // 3. Evaluamos individualmente cada umbral del RNF 04
    if (t < 12.0) {
        alerta_climatica = true;
        mensaje_alerta += "Temperatura Baja (<12C). ";
    }
    if (t > 27.0) {
        alerta_climatica = true;
        mensaje_alerta += "Temperatura Alta (>27C). ";
    }
    if (h > 85.0) {
        alerta_climatica = true;
        mensaje_alerta += "Humedad Alta (>85%). ";
    }

    // Si ninguna alerta se activó, el estado es completamente normal
    if (!alerta_climatica) {
        mensaje_alerta = "Normal";
    }

    // 4. Armamos el JSON con el detalle específico del problema
    String mensaje_json = "{";
    mensaje_json += "\"temperatura\": " + String(t, 1) + ", ";
    mensaje_json += "\"humedad\": " + String(h, 1) + ", ";
    mensaje_json += "\"alerta\": " + (alerta_climatica ? String("true") : String("false")) + ", ";
    mensaje_json += "\"detalles_alerta\": \"" + mensaje_alerta + "\"";
    mensaje_json += "}";
    // 5. Publicar en Mosquitto 
    client.publish(TOPIC_CLIMA, mensaje_json.c_str());

    Serial.print("[PUB] Enviado a FastAPI -> ");
    Serial.println(mensaje_json);

    // Parpadeo del LED para confirmar envío visualmente en la placa
    digitalWrite(LED_PIN, LOW);  // Encender
    delay(100);
    digitalWrite(LED_PIN, HIGH); // Apagar
  }
}