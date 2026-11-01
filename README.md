# AfinaVST – afinador automático (VST3) con JUCE

Plugin de corrección de tono monofónico para voz, en la línea de MetaTune: eliges las notas
permitidas (tonalidad + escala, o nota a nota) y el plugin lleva la voz a la nota más cercana.

## Qué hace

- **Detección de pitch**: YIN (sobre una copia diezmada de la señal para ahorrar CPU), con filtro de mediana.
- **Corrección**: nota permitida más cercana con histéresis; *Retune Speed* (0 ms = efecto duro tipo T-Pain),
  *Humanize*, *Amount*, *Reference A*.
- **Cambio de tono**: TD-PSOLA en tiempo real (mantiene los formantes, suena natural en correcciones pequeñas).
- **Visor**: traza del pitch detectado (gris) y corregido (verde) sobre las notas de la escala.
- **Mix** dry/wet con compensación de latencia (el plugin informa su latencia al DAW).
- Mono o estéreo. Formatos: **VST3** y **Standalone** (para probar sin DAW).

## Compilar en Windows

Necesitas (todo gratuito):

1. **Visual Studio 2022 Community** con la carga de trabajo *Desarrollo de escritorio con C++*.
2. **CMake** (3.22 o superior) y **Git**, ambos en el PATH.
3. Conexión a internet la primera vez (CMake descarga JUCE solo, no hace falta instalarlo).

En una terminal, dentro de esta carpeta:

```
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --target AfinaVST_VST3
cmake --build build --config Release --target AfinaVST_Standalone
```

La primera vez tarda unos 5-10 minutos. Resultado:

- `build\AfinaVST_artefacts\Release\VST3\AfinaVST.vst3`  → cópialo a `C:\Program Files\Common Files\VST3\`
- `build\AfinaVST_artefacts\Release\Standalone\AfinaVST.exe` → para probarlo directamente con tu micro.

Después, reescanea plugins en tu DAW.

## Forma más fácil en tu PC: build.bat

1. Instala **Visual Studio Community** (gratis) marcando la carga de trabajo *Desarrollo para el escritorio con C++*.
2. Instala **Git for Windows** (git-scm.com, todo por defecto).
3. Abre el menú Inicio, busca **Developer Command Prompt for VS**, ve a esta carpeta (`cd` + ruta) y ejecuta `build.bat`.
4. Al terminar se abre la carpeta `dist` con `AfinaVST.vst3` y `AfinaVST.exe`.

## Alternativa sin instalar nada: que GitHub lo compile por ti (gratis)

1. Crea una cuenta en github.com y un repositorio nuevo (puede ser privado).
2. Sube el contenido de esta carpeta (botón *Add file > Upload files*; incluye la carpeta oculta `.github`).
3. Ve a la pestaña **Actions** > *Build Windows* > *Run workflow*. Tarda unos 10 minutos.
4. Cuando termine, descarga el artefacto **AfinaVST-Windows**: contiene `AfinaVST.vst3` y `AfinaVST.exe`.

## Tests del DSP (opcional, no necesitan JUCE)

```
g++ -O2 -std=c++17 -ISource/dsp Tests/dsp_test.cpp -o dsp_test && ./dsp_test
```

(En Windows con Visual Studio: `cmake -B build -DAFINA_BUILD_TESTS=ON` y compila el objetivo `dsp_test`.)

## Notas importantes

- **Latencia**: depende del selector *Voice Range* del plugin: ~37 ms (voz grave, desde 90 Hz),
  ~26 ms (media, desde 130 Hz, por defecto) o ~19 ms (aguda, desde 180 Hz). Elige el rango más alto que
  cubra tu voz: si cantas por debajo del límite, el afinador se equivocará de octava.
  Al cambiar el rango se reinicia el motor un instante (puede sonar un clic): hazlo antes de grabar.

## Grabar en tiempo real (Windows)

- **Lo más cómodo y seguro**: grabar la voz *sin* el plugin (monitorización directa de la interfaz de audio,
  sin latencia) y poner AfinaVST en la pista para oír el resultado. Los DAW compensan la latencia del plugin
  al reproducir, así que queda perfectamente alineado.
- **Si quieres oírte ya afinado mientras cantas**: pon el plugin en la pista y usa *Voice Range* adecuado.
  La latencia total será la del plugin + la de tu interfaz. Usa los drivers **ASIO** de tu interfaz
  (o ASIO4ALL) con un búfer de 64-128 muestras; con los drivers genéricos de Windows se añade mucha más.
  Consejo: baja *Retune Speed* a 0 solo si quieres el efecto robótico; con valores de 20-60 ms suena más natural.
- El `.exe` Standalone usa WASAPI por defecto, que tiene latencia alta: sirve para probar, no para grabar.
  Para grabar usa el VST3 dentro de un DAW.

- **Nombre y códigos**: en `CMakeLists.txt` cambia `COMPANY_NAME`, `PLUGIN_MANUFACTURER_CODE`,
  `PLUGIN_CODE` y `PRODUCT_NAME` por los tuyos. No uses el nombre ni el diseño de otros productos.
- **Licencia de JUCE**: gratuita para uso personal / ingresos bajos (con pantalla "Made with JUCE").
  Revisa las condiciones actuales en juce.com antes de distribuir o vender el plugin.
- **Estado**: el motor de audio está probado con señales sintéticas y el proyecto compila con JUCE 8.0.8
  (comprobado en Linux). **No se ha probado todavía en Windows ni en un DAW** con voz real: es una primera
  versión para iterar.

## Estructura

```
CMakeLists.txt
Source/
  PluginProcessor.*    parámetros, estado, hilo de audio
  PluginEditor.*       interfaz y visor de pitch
  dsp/
    YinDetector.h      detección de pitch
    PitchCorrector.h   nota objetivo, histéresis, velocidad de retune
    PsolaShifter.h     cambio de tono TD-PSOLA en streaming
    AutotuneEngine.h   cadena completa + dry/wet
Tests/dsp_test.cpp
```

## Ideas para una v2

Entrada de notas por MIDI, instancias enlazadas, modo de baja latencia (rango de voz configurable),
detección más robusta con voz ruidosa, y un visor tipo mapa de calor.
