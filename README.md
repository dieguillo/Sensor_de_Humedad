# Estación Meteorológica con ESP8266

## Descripción General
Este programa convierte un módulo ESP8266 en una estación meteorológica con capacidad de:
- Medir temperatura, humedad, punto de rocío y déficit de presión de vapor (VPD)
- Mostrar datos en una pantalla OLED
- Proporcionar acceso web local a los datos
- Configuración mediante portal cautivo
- Actualizaciones inalámbricas (OTA)
- Uso de EEPROM para guardar configuración

## Características Principales
1. **Sensores integrados**:
   - Sensor AHT10 para medición de temperatura y humedad
   - Cálculo de Punto de Rocío y VPD

2. **Interfaz de usuario**:
   - Pantalla OLED SH1106 de 128x64 píxeles
   - Servidor web integrado
   - Portal cautivo para configuración

3. **Conectividad**:
   - Conexión WiFi configurable
   - Modo Access Point para configuración inicial
   - API JSON para integración con otros sistemas

4. **Mantenimiento**:
   - Actualizaciones OTA (Over-The-Air)
   - Reset de configuración mediante botón físico

## Componentes Requeridos
- Módulo ESP8266
- Sensor AHT10
- Pantalla OLED SH1106 (128x64, I2C)
- Botón para reset (default botón FLASH)

## Configuración Inicial

### Primer Uso
1. Al encender por primera vez, el dispositivo creará una red WiFi con el nombre "ESP-[ID] WiFi Config"
2. Conéctese a esta red desde su smartphone o computadora
3. Abra un navegador web y visite cualquier página (será redirigido automáticamente)
4. Complete el formulario con:
   - Nombre personalizado para el dispositivo
   - Credenciales de su red WiFi
5. Guarde la configuración. El dispositivo se reiniciará y conectará a su red.

### Acceso a los Datos
Una vez configurado:
- **Pantalla OLED**: Muestra datos en tiempo real
- **Interfaz Web**: Acceda a `http://[IP-del-dispositivo]` desde cualquier navegador
- **API JSON**: Acceda a `http://[IP-del-dispositivo]/json` para obtener datos en formato JSON

## Funcionamiento Detallado

### Flujo Principal
1. **Inicialización**:
   - Verifica configuración almacenada en EEPROM
   - Intenta conectar a WiFi guardada
   - Si falla, inicia modo Access Point

2. **Medición**:
   - Lee sensor AHT10 (3 lecturas para promedio)
   - Calcula temperatura, humedad, punto de rocío y VPD

3. **Visualización**:
   - Actualiza pantalla OLED cada 500ms
   - Actualiza servidor web con los últimos datos

4. **Mantenimiento**:
   - Escucha actualizaciones OTA
   - Monitorea botón de reset

### Modos de Operación
1. **Modo Cliente WiFi**:
   - Conectado a red configurada
   - LED interno apagado
   - Datos accesibles via web

2. **Modo Access Point**:
   - Red propia cuando no hay configuración o falla conexión
   - LED parpadea indicando modo AP
   - Portal cautivo para configuración

### Características Avanzadas
1. **Cálculo de Punto de Rocío**:
   - Usa la fórmula de Magnus para calcular la temperatura a la que el aire se satura
   - Importante para prevenir condensación

2. **Cálculo de VPD (Vapor Pressure Deficit)**:
   - Indica el estrés hídrico de las plantas
   - Valores útiles para agricultura e invernaderos

3. **Media Móvil**:
   - Realiza 3 lecturas del sensor y promedia para mayor precisión

## Mantenimiento y Configuración

### Reset de Configuración
Mantenga presionado el botón FLASH (GPIO0) por 3 segundos para:
1. Borrar toda la configuración almacenada
2. Reiniciar el dispositivo
3. Volver al modo Access Point para nueva configuración

### Actualizaciones OTA
Para actualizar el firmware inalámbricamente:
1. Use Arduino IDE o plataforma compatible
2. Seleccione el dispositivo por su nombre (ESP-[ID])
3. Contraseña: misma que la red WiFi configurada

### Personalización
Puede modificar:
- Nombre del dispositivo (hasta 16 caracteres)
- Credenciales WiFi
- Intervalos de medición (modificando el código)

## Especificaciones Técnicas

### Pines Utilizados
| Función           | Pin ESP8266 | Pin NodeMCU |
|-------------------|-------------|-------------|
| Botón Reset       | GPIO0       | FLASH       |
| LED Interno       | GPIO2       | D4          |
| I2C (OLED y AHT10)| GPIO4 (SCL) | D1          |
|                   | GPIO5 (SDA) | D2          |

### Estructura de Datos
```cpp
struct EspConfig {
  char title[17];  // Nombre personalizado
  char ssid[33];   // SSID WiFi
  char passwd[33]; // Contraseña WiFi
};

struct S_AhtData {
    float T;    // Temperatura (°C)
    float RH;   // Humedad Relativa (%)
    float Td;   // Punto de Rocío (°C)
    float VPD;  // Déficit de Presión de Vapor (kPa)
};
```

### Endpoints Web
| Ruta               | Descripción                         | Formato  |
|--------------------|-------------------------------------|----------|
| `/`                | Interfaz principal                  | HTML     |
| `/json`            | Datos en formato JSON               | JSON     |
| `/save`            | Guardar configuración (POST)        | HTML     |
| `/generate_204`    | Captive Portal (Android)            | -        |
| `/hotspot-detect`  | Captive Portal (Apple)              | HTML     |
| `/ncsi.txt`        | Captive Portal (Windows)            | Texto    |


### Mensajes de Error Serial
- `Error inicializando AHT10`: Problema con el sensor de temperatura/humedad
- `Error inicializando display OLED`: Problema con la pantalla
- `EEPROM: SSID vacío`: Configuración no encontrada, inicia modo AP

## Consideraciones de Seguridad
- La contraseña WiFi se almacena en EEPROM
- Acceso OTA protegido con misma contraseña que WiFi

## Licencia y Uso
Este software puede ser modificado y distribuido libremente. Se recomienda su uso para proyectos personales, educativos y agrícolas.

## Links de interés
- [¿Qué es VPD (Déficit de Presión de Vapor)?](https://drygair.com/es/blog-es/vpd-vapor-pressure-deficit/)

--- 

Este manual cubre todas las funcionalidades principales del sistema.
Para personalizaciones avanzadas o integraciones, consulte el código fuente comentado.
