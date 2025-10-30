/**
 * @file ESP32_BLE_SERVER.ino
 * @author M. Alejandro Sáchez R.
 * @brief Main firmware for a BLE Serial Message Repeater on ESP32 using FreeRTOS.
 * @version 0.1
 * @date 2025-10-29
 *
 * @copyright Copyright (c) 2025
 *
 * This project implements a 3-task FreeRTOS system to achieve a specific goal:
 * 1. Allow a user to type a message into the Serial Monitor.
 * 2. Continuously (on a timer) send that *last* message over a BLE notification.
 * 3. Update the message being sent whenever the user types a new one.
 *
 * Core Components:
 * - BLE Server: Advertises a service and characteristic.
 * - FreeRTOS Queue: A queue (`stringQueue`) decouples the BLE task from the data-sending task.
 * - FreeRTOS Mutex: A mutex (`g_messageMutex`) provides thread-safe access to a shared global
 * variable (`g_lastMessage`) that stores the latest string from the Serial Monitor.
 *
 * Task Architecture:
 * - bleTask (Core 0): Initializes the BLE server and characteristic. It spends its life
 * blocked, waiting for a message to arrive on `stringQueue`. When one does,
 * it sends it as a BLE notification.
 *
 * - serialReaderTask (Core 1): Polls the Serial Monitor (`Serial.available()`). When the user
 * presses Enter, this task takes the mutex, updates the `g_lastMessage`
 * variable, and releases the mutex.
 *
 * - dataSenderTask (Core 1): Runs on a simple timer (e.g., every 2 seconds). It wakes up,
 * takes the mutex, copies the `g_lastMessage` into a local buffer, releases
 * the mutex, and then sends that local copy to the `stringQueue`.
 */

// --- System & Library Headers ---
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h" // <-- Incluir para Mutex (Semaphore Handles)

// --- BLE Definitions ---
// Visita www.uuidgenerator.net para crear tus propios UUIDs
#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"

// --- FreeRTOS Definitions ---
#define MAX_STRING_LEN 50  // El tamaño máximo del string
#define QUEUE_LENGTH   5   // Cuántos mensajes puede almacenar la cola
#define SEND_INTERVAL_MS 2000 // Enviar el mensaje cada 2 segundos

// --- Global Handles & Flags ---
QueueHandle_t stringQueue;                   // Handle para la cola de FreeRTOS
BLECharacteristic *pCharacteristic;          // Puntero global a la característica
bool deviceConnected = false;                // Flag para el estado de la conexión

// --- Shared Global Variables ---
// Esta sección es crítica para la comunicación entre tareas.
// g_lastMessage es el recurso compartido.
char g_lastMessage[MAX_STRING_LEN] = "Esperando..."; // Almacena el último mensaje
SemaphoreHandle_t g_messageMutex;                    // Mutex para proteger g_lastMessage

// --- BLE Server Callbacks ---
/**
 * @brief Maneja los eventos de conexión y desconexión del cliente BLE.
 */
class MyServerCallbacks: public BLEServerCallbacks {
    /**
     * @brief Se llama cuando un cliente BLE se conecta.
     */
    void onConnect(BLEServer* pServer) {
      deviceConnected = true;
      Serial.println("Cliente Conectado");
    }

    /**
     * @brief Se llama cuando un cliente BLE se desconecta.
     */
    void onDisconnect(BLEServer* pServer) {
      deviceConnected = false;
      Serial.println("Cliente Desconectado");
      // Reiniciar la publicidad para que otros dispositivos puedan conectarse
      pServer->getAdvertising()->start();
    }
};

/**
 * @brief Tarea de FreeRTOS para gestionar el servidor BLE.
 * @param pvParameters Puntero a parámetros de la tarea (no se usa).
 * @note Esta tarea es "data-driven". Se bloquea indefinidamente en
 * `xQueueReceive` esperando que otra tarea le envíe datos.
 *
 * Inicializa la pila BLE, el servidor, el servicio y la característica.
 * Luego entra en un bucle infinito donde:
 * 1. Espera un string de `stringQueue`.
 * 2. Actualiza el valor de la característica BLE.
 * 3. Si hay un cliente conectado, le envía una notificación.
 */
void bleTask(void *pvParameters) {
  Serial.println("Iniciando Tarea BLE... (Core 0)");

  // 1. Inicializar BLE
  BLEDevice::init("ESP32_FreeRTOS_Server");

  // 2. Crear el Servidor BLE
  BLEServer *pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks()); // Asignar callbacks

  // 3. Crear el Servicio
  BLEService *pService = pServer->createService(SERVICE_UUID);

  // 4. Crear la Característica
  pCharacteristic = pService->createCharacteristic(
                      CHARACTERISTIC_UUID,
                      BLECharacteristic::PROPERTY_READ   | // Permitir lectura
                      BLECharacteristic::PROPERTY_NOTIFY   // Permitir notificaciones
                    );

  // Se necesita un descriptor BLE2902 para que las notificaciones funcionen
  pCharacteristic->addDescriptor(new BLE2902());

  // 5. Iniciar el Servicio
  pService->start();

  // 6. Iniciar la Publicidad (Advertising)
  BLEAdvertising *pAdvertising = pServer->getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06); // Ayuda con problemas de conexión en iOS
  pAdvertising->setMinPreferred(0x12);
  pServer->getAdvertising()->start();
  
  Serial.println("Servidor BLE listo. Esperando conexiones...");

  char receivedString[MAX_STRING_LEN]; // Buffer local para recibir el string de la cola

  // 7. Bucle principal de la tarea
  for (;;) {
    // Esperar (bloquear) hasta que un item esté disponible en la cola
    if (xQueueReceive(stringQueue, &receivedString, portMAX_DELAY)) {
      
      Serial.print("Dato recibido de la cola: ");
      Serial.println(receivedString);

      // Actualizar el valor de la característica
      pCharacteristic->setValue(receivedString);

      // Si hay un cliente conectado, enviarle una notificación
      if (deviceConnected) {
        pCharacteristic->notify();
        Serial.println("Notificación enviada.");
      }
    }
  }
}

/**
 * @brief Tarea que lee del Monitor Serial y GUARDA el mensaje.
 * @param pvParameters Puntero a parámetros de la tarea (no se usa).
 * @note Esta tarea *escribe* en el recurso compartido `g_lastMessage`.
 * Utiliza `g_messageMutex` para garantizar que la escritura sea atómica y
 * segura (thread-safe) y no sea interrumpida por la tarea de lectura.
 *
 * Esta tarea sondea `Serial.available()` en un bucle.
 * Cuando detecta una nueva línea (el usuario presiona Enter):
 * 1. Toma el Mutex (`xSemaphoreTake`), bloqueando a otras tareas.
 * 2. Copia de forma segura el nuevo string en `g_lastMessage`.
 * 3. Libera el Mutex (`xSemaphoreGive`), permitiendo que otras tareas accedan.
 */
void serialReaderTask(void *pvParameters) {
  Serial.println("Iniciando Tarea de Lectura Serial... (Core 1)");
  Serial.println("Escribe un mensaje en el monitor serial y presiona Enter.");

  for (;;) {
    // Verificar si hay datos disponibles en el buffer serial
    if (Serial.available() > 0) {
      String input = Serial.readStringUntil('\n');
      
      // Quitar espacios en blanco al inicio y al final
      input.trim();

      if (input.length() > 0) {
        
        // --- INICIO DE SECCIÓN CRÍTICA ---
        // Tomar el Mutex para bloquear el acceso a g_lastMessage
        // Esperará indefinidamente (portMAX_DELAY) si está bloqueado.
        if (xSemaphoreTake(g_messageMutex, portMAX_DELAY) == pdTRUE) {
          
          // Copiar el contenido del objeto String al array de char global
          strncpy(g_lastMessage, input.c_str(), MAX_STRING_LEN);
          // Asegurarse de que el string termine en nulo, por si input era > MAX_STRING_LEN
          g_lastMessage[MAX_STRING_LEN - 1] = '\0'; 

          // Liberar el Mutex para que dataSenderTask pueda leer
          xSemaphoreGive(g_messageMutex);
          
          Serial.print("NUEVO MENSAJE GUARDADO: ");
          Serial.println(g_lastMessage);
        }
        // --- FIN DE SECCIÓN CRÍTICA ---
      }
    }
    // Pequeña pausa para ceder tiempo de CPU a otras tareas
    vTaskDelay(pdMS_TO_TICKS(100)); 
  }
}

/**
 * @brief Tarea que ENVÍA el último mensaje guardado en un temporizador.
 * @param pvParameters Puntero a parámetros de la tarea (no se usa).
 * @note Esta tarea *lee* del recurso compartido `g_lastMessage`.
 * Utiliza `g_messageMutex` para garantizar una lectura segura (thread-safe).
 *
 * Esta tarea se despierta cada `SEND_INTERVAL_MS` milisegundos.
 * 1. Toma el Mutex (`xSemaphoreTake`) para asegurar que `serialReaderTask` no
 * escriba en `g_lastMessage` mientras esta tarea lo lee.
 * 2. Copia el mensaje global a un *buffer local* (`messageToSend`).
 * 3. Libera el Mutex (`xSemaphoreGive`) INMEDIATAMENTE después de la copia.
 * 4. Envía la copia local (`messageToSend`) a `stringQueue`, donde `bleTask`
 * la recogerá.
 */
void dataSenderTask(void *pvParameters) {
  Serial.println("Iniciando Tarea de Envío de Datos... (Core 1)");
  char messageToSend[MAX_STRING_LEN]; // Buffer local para enviar a la cola

  for (;;) {
    // Esperar el intervalo definido (ej. 2000 ms)
    vTaskDelay(pdMS_TO_TICKS(SEND_INTERVAL_MS));

    // --- INICIO DE SECCIÓN CRÍTICA ---
    // Tomar el Mutex para copiar de forma segura g_lastMessage
    if (xSemaphoreTake(g_messageMutex, portMAX_DELAY) == pdTRUE) {
      
      // Copiar el mensaje global a nuestro buffer local
      strcpy(messageToSend, g_lastMessage); 

      // Liberar el Mutex tan pronto como sea posible.
      // No lo mantenemos mientras enviamos a la cola.
      xSemaphoreGive(g_messageMutex);
    }
    // --- FIN DE SECCIÓN CRÍTICA ---

    // Ahora, enviar la copia local a la cola (fuera de la sección crítica)
    // Esperará hasta 100ms si la cola está llena antes de rendirse
    if (xQueueSend(stringQueue, &messageToSend, pdMS_TO_TICKS(100)) == pdPASS) {
      // El print se hace en bleTask, no es necesario hacerlo aquí
      // Serial.print("Dato enviado a la cola: ");
      // Serial.println(messageToSend);
    } else {
      Serial.println("Error: La cola BLE está llena.");
    }
  }
}

// --- Función Setup de Arduino ---
/**
 * @brief Inicializa el sistema.
 *
 * Esta función realiza los siguientes pasos:
 * 1. Inicia la comunicación Serial para debugging.
 * 2. Crea el Mutex (`g_messageMutex`) para proteger la variable compartida.
 * 3. Crea la Cola (`stringQueue`) para comunicar `dataSenderTask` con `bleTask`.
 * 4. Crea y ancla (pin) todas las tareas a sus respectivos núcleos de CPU.
 * 5. Borra la tarea de `setup/loop` ya que no se usará.
 */
void setup() {
  Serial.begin(115200);
  Serial.println("Iniciando sistema...");

  // 1. Crear el Mutex
  g_messageMutex = xSemaphoreCreateMutex();
  if (g_messageMutex == NULL) {
    Serial.println("Error al crear el Mutex!");
    while(1); // Detener ejecución
  }

  // 2. Crear la cola
  // Almacenará items del tamaño de nuestro array 'char[MAX_STRING_LEN]'
  stringQueue = xQueueCreate(QUEUE_LENGTH, sizeof(char[MAX_STRING_LEN]));
  if (stringQueue == NULL) {
    Serial.println("Error al crear la cola!");
    while(1); // Detener ejecución
  }

  // 3. Crear las tareas
  
  // Tarea BLE (Core 0 - para todo lo relacionado con WiFi/Bluetooth)
  xTaskCreatePinnedToCore(
    bleTask,
    "BLE Task",
    8192, // BLE necesita un stack GRANDE
    NULL,
    1,    // Prioridad
    NULL,
    0     // Core 0
  );

  // Tarea de Lectura Serial (Core 1 - para lógica de aplicación)
  xTaskCreatePinnedToCore(
    serialReaderTask,
    "Serial Reader Task",
    2048, // Stack normal
    NULL,
    1,    // Prioridad
    NULL,
    1     // Core 1
  );

  // Tarea de Envío de Datos (Core 1 - para lógica de aplicación)
  xTaskCreatePinnedToCore(
    dataSenderTask,
    "Data Sender Task",
    2048, // Stack normal
    NULL,
    1,    // Prioridad
    NULL,
    1     // Core 1
  );
  
  Serial.println("Setup completo. 3 Tareas iniciadas.");
  
  // No necesitamos hacer nada en loop(), así que podemos borrar su tarea
  vTaskDelete(NULL); 
}

// --- Función Loop de Arduino ---
/**
 * @brief Bucle principal de Arduino - intencionalmente vacío.
 *
 * En este proyecto basado en FreeRTOS, toda la lógica se maneja en
 * Tareas (Tasks) independientes. La tarea que ejecuta `setup()` y `loop()`
 * se borra en `setup()` llamando a `vTaskDelete(NULL)`, por lo que esta
 * función `loop()` nunca se ejecuta.
 */
void loop() {
  // Vacío
}
