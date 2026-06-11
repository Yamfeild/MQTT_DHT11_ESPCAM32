// ============================================
// Proyecto: UNL-Cloud-Connect
// Hardware: ESP32-CAM + Adaptador TTL + DHT11
// ============================================
#include <WiFi.h>
#include <PubSubClient.h>
#include <DHT.h>
#include <WiFiManager.h> // <-- LIBRERÍA NUEVA

// ---- Ya NO hay credenciales quemadas ----

// Variable para guardar la IP que el usuario escriba en el portal web
char mqtt_server[40] = ""; 
const int   MQTT_PORT     = 1884;

// El tópico exacto
const char* TOPIC_CLIMA   = "unl/clima/esp32"; 

// ---- Configuración DHT11 ----
#define DHTPIN    15       
#define DHTTYPE   DHT11    

DHT dht(DHTPIN, DHTTYPE);

// En la ESP32-CAM, el LED rojo pequeño interno está en el pin 33
#define LED_PIN   33        
#define INTERVALO 300000     

WiFiClient   espClient;
PubSubClient client(espClient);
unsigned long lastMsg = 0;

// ---- Reconectar a Mosquitto (Actualizado para IP dinámica) ----
void reconnect() {
  while (!client.connected()) {
    Serial.print("Conectando a Mosquitto en la IP: ");
    Serial.print(mqtt_server);
    Serial.print("...");
    
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
  digitalWrite(LED_PIN, HIGH); // Apagar LED al inicio
  
  dht.begin();                

  // ---- Inicia el WiFiManager ----
  WiFiManager wm;
  
  // DESCOMENTA LA SIGUIENTE LÍNEA si quieres borrar la memoria de la ESP32 
  // para forzar que el portal web aparezca siempre que la enciendas (útil para pruebas)
  wm.resetSettings(); 

  // Crea la caja de texto en la página web para pedir la IP
  WiFiManagerParameter custom_mqtt_server("server", "IP del Broker MQTT (PC destino)", mqtt_server, 40);
  wm.addParameter(&custom_mqtt_server);

  Serial.println("Buscando red conocida o levantando Portal Cautivo...");
  
  // Si no se puede conectar, levanta la red abierta "Sensor-UNL-Config"
  if (!wm.autoConnect("Sensor-UNL-Config")) {
    Serial.println("Fallo al conectar y timeout alcanzó");
    delay(3000);
    ESP.restart(); // Reinicia y vuelve a intentar
  }

  // Si pasa de esta línea, significa que ya se conectó a un Wi-Fi
  Serial.println("\n✓ WiFi conectado.");
  Serial.print("IP asignada a la placa: ");
  Serial.println(WiFi.localIP());

  // Lee el valor que el compañero haya escrito en el campo web y lo guarda
  strcpy(mqtt_server, custom_mqtt_server.getValue());
  
  Serial.print("IP del servidor guardada: ");
  Serial.println(mqtt_server);

  // Configura el cliente MQTT con la IP obtenida del portal web
  client.setServer(mqtt_server, MQTT_PORT);
}

// ---- Loop principal (SIN CAMBIOS EN LA LÓGICA) ----
void loop() {
  if (!client.connected()) reconnect();
  client.loop(); // Mantiene viva la conexión MQTT

  unsigned long now = millis();
  if (now - lastMsg > INTERVALO) {
    lastMsg = now;
    
    // 1. Leer DHT11
    float t = dht.readTemperature();
    float h = dht.readHumidity();

    if (isnan(t) || isnan(h)) {
      Serial.println("[ERROR] Fallo de lectura DHT11. Revisa los cables.");
      return; 
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

    if (!alerta_climatica) {
        mensaje_alerta = "Normal";
    }

    // 4. Armamos el JSON 
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

    digitalWrite(LED_PIN, LOW);  // Encender
    delay(100);
    digitalWrite(LED_PIN, HIGH); // Apagar
  }
}
