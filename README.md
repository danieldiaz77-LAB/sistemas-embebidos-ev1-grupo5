# Monitor de temperatura con ESP32 · EV1

Proyecto del Grupo 5 para Programación de Sistemas Embebidos. La demo obtiene temperatura mediante I2C y la muestra por puerto serie y en una página web alojada en la ESP32. El panel incluye gráfico, estado de la lectura y exportación CSV.

## Hardware y conexiones

La placa reportada es una ESP32 DevKit de 38 pines, USB-C y puente CP2102, con chip ESP32-D0WD-V3 y 4 MB de flash. La identificación exacta del módulo debe comprobarse en su serigrafía. PlatformIO utiliza el perfil `esp32dev`.

| Sensor | ESP32 |
| --- | --- |
| VCC | 3V3 |
| GND | GND |
| SDA | GPIO21 |
| SCL | GPIO22 |

El firmware utiliza la dirección I2C `0x76` y admite los identificadores `0x58` (BMP280) y `0x60` (BME280). El identificador se imprime como `chip_id` al inicializar el sensor. Solo se utiliza la medición de temperatura. Las resistencias pull-up internas están desactivadas: el bus necesita pull-ups a 3,3 V, normalmente presentes en el módulo; comprobarlas antes de agregar otras.

## Entorno

Desarrollo en Ubuntu, nativo o mediante WSL 2, con PlatformIO y ESP-IDF. `platformio.ini` fija `espressif32@8.0.0`, placa `esp32dev`, framework `espidf`, flash de 4 MB y monitor a 115200 baudios. PlatformIO resuelve las herramientas y el framework de esa plataforma. No se utiliza Arduino.

Se necesita PlatformIO Core disponible como `pio` y un cable USB de datos. En Ubuntu debe existir acceso al puerto serie; en WSL también hay que adjuntar el dispositivo USB a la distribución. Ejecutar los comandos siguientes desde la carpeta que contiene `platformio.ini`.

## Compilar y cargar

```bash
pio --version
pio device list
pio run
pio run -t upload --upload-port /dev/ttyUSB0
pio device monitor --port /dev/ttyUSB0 --baud 115200
```

Cambiar `/dev/ttyUSB0` por el puerto detectado. Cerrar el monitor antes de cargar de nuevo. Para registrar las versiones efectivas puede usarse `pio pkg list`.

## Panel web

1. Encender la placa y comprobar el arranque en el monitor serie.
2. Conectarse a la red Wi-Fi `SBS-Grupo5`, con clave `ColectorG5!`.
3. Abrir la dirección HTTP indicada en el monitor; con la configuración de red predeterminada es `http://192.168.4.1`.

La clave corresponde exclusivamente al punto de acceso de demostración, no a una red doméstica. La ESP32 crea una red local sin acceso a Internet. El panel se sirve desde la placa y no usa servicios externos. `/api/temp` devuelve el estado y la temperatura en centésimas de grado.

El periodo objetivo de lectura es de 2 segundos. Una muestra de más de 6 segundos se considera desactualizada. El navegador conserva hasta 300 muestras; el historial no se guarda permanentemente en la placa.

## Organización

- `src/main.c`: tarea de adquisición, estado compartido, Wi-Fi y servidor HTTP.
- `src/bmp280.c` y `src/bmp280.h`: identificación, calibración y lectura del sensor.
- `dashboard.html`: interfaz web.
- `src/web_page.h`: interfaz incorporada al firmware.
- `embed_web.py`: generación del archivo anterior.
- `tests/`: pruebas de lógica y del driver con periféricos simulados.

Si se edita `dashboard.html`, ejecutar `python3 embed_web.py` antes de compilar. El archivo generado se incluye para permitir la compilación directa del proyecto.

## Pruebas

El equipo confirmó una prueba funcional de esta versión en su placa. No se adjuntan registros de esa sesión, por lo que no se atribuyen resultados específicos de precisión, duración ni recuperación ante fallos.

Pruebas aisladas en Linux, con Node.js y GCC:

```bash
node tests/logic.cjs
gcc -std=c11 -Wall -Wextra -Werror -Wshadow -Wformat=2 -fsanitize=undefined,address -Itests/stubs -Isrc tests/sensor_host.c src/bmp280.c -o /tmp/ev1-sensor-test
ASAN_OPTIONS=detect_leaks=0 /tmp/ev1-sensor-test
```

La prueba de JavaScript simula el DOM. La prueba de C simula I2C y comprueba compensación, errores y reintentos; no sustituye una prueba física. `detect_leaks=0` desactiva únicamente LeakSanitizer para entornos restringidos. `tests/dashboard.cjs` es una prueba adicional para Playwright con Chromium; requiere instalar esas herramientas y no forma parte de la compilación del firmware.

## Alcance y límites

Esta entrega mide y visualiza temperatura ambiente. No implementa todavía PID, control de RPM, calefactor ni máquina de estados de seguridad del sistema final. El sensor ambiental de la demo no sustituye la termocupla prevista en el anteproyecto. La exactitud necesita contrastarse con una referencia independiente.

## Referencias

- Bosch Sensortec: hojas de datos de BMP280 y BME280, registros y compensación de temperatura.
- Espressif: documentación de ESP-IDF para ESP32, I2C, Wi-Fi, HTTP y FreeRTOS.
- PlatformIO: plataforma Espressif32 y framework ESP-IDF.
