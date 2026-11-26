# More Save Slots (antes "Más Ranuras")

Mod nativo para REPENTOGON (`zhlMoreSaveSlots.dll`). Añade páginas de partidas al menú de selección: hasta 9 archivos con una misma cuenta de Steam.

- **Página 1 (FILE 1-3):** las ranuras de siempre, con Steam Cloud si está activado.
- **Páginas 2 y 3 (FILE 4-6 y 7-9):** solo locales. Nunca se escriben en la nube.

## Controles (menú de partidas)

| Acción | Resultado |
|---|---|
| ▶ en el archivo 3 | Página siguiente, con deslizamiento |
| ▶ en el archivo 3 de la última página | Selecciona la pestaña **+** |
| Confirmar en **+** | Añade una página (máximo 3) |
| ◀ en el archivo 1 | Página anterior |
| LB / RB | Página anterior / siguiente |

El menú recuerda la última página usada. La configuración está en `Repentogon/more_save_slots.ini`, dentro de la carpeta de partidas.

## Cómo funciona

El juego, REPENTOGON y los mods siempre ven las ranuras 1-3. Sus tablas internas y los archivos extra de REPENTOGON (`local_gamestateN`) solo admiten esos números, y usar el 4 directamente cerraba el juego.

Mientras está activa una ranura de las páginas 2-3, el DLL cambia solo el número del nombre del archivo en disco. Lo hace en las funciones de archivos de Windows (`CreateFile`, `DeleteFile`, `MoveFileEx`, `GetFileAttributes`…) de los módulos del juego: `isaac-ng.exe`, `ucrtbase`, `msvcp140` y `zhlREPENTOGON`. Steam y el sistema nunca se tocan.

Estos son los archivos que se redirigen:

- `rgon_persistentgamedataN.dat`, `rep+persistentgamedataN.dat` y sus copias de seguridad
- `gamestateN.dat`, `rep+rerunstateN.dat`
- `Repentogon/{EntitySaveStateManagement,ItemPoolManager,VirtualRoomSetManager}/*gamestateN*`
- `completionmarksN.json`, `achievementsN.json`
- `data/<mod>/saveN.dat`

En el menú, con la página 2 o 3 a la vista, "Delete file" borra la ranura de esa página. Al cerrar el juego se descarga la ranura activa antes de que REPENTOGON sincronice las ranuras 1-3 con las de Repentance+.

## Steam Cloud

Con la nube activada (`SteamCloud=1`), las ranuras 1-3 siguen en la nube y las 4-9 son siempre locales.

El motor decide si usa la nube leyendo directamente `OptionsConfig::_enableSteamCloud`. Por eso el mod apaga ese indicador en estos casos:

- mientras hay una ranura 4-9 activa;
- mientras el menú de partidas muestra la página 2 o 3, porque "Delete file" borra sin cargar la ranura.

Lo vuelve a encender al pasar a la página 1, a una ranura 1-3 o al cerrar. `options.ini` se guarda siempre con el valor real.

Esto se probó con una **nube simulada** (`tools/fake_cloud/`, `zhlFakeCloud.dll`, solo para la copia de pruebas). Dentro del proceso del juego, el DLL sustituye las funciones de archivos de `ISteamRemoteStorage` v016 por una carpeta local, `partidas_pruebas/fake_cloud/`, y apunta cada operación en `more_save_slots.log`. Ninguna operación llega a Steam.

```
tools\fake_cloud\build.bat instalar   (y SteamCloud=1 en options.ini y Repentogon\vanilla_options.ini del perfil de pruebas)
tools\fake_cloud\build.bat quitar
```

## Logros de online y daily runs

Las ranuras 4-9 solo existen con REPENTOGON, y REPENTOGON no deja jugar online. En Repentance+ las daily runs están dentro del modo online, así que estos 6 logros no se pueden conseguir en esas ranuras:

| ID | Logro | Cómo se consigue normalmente |
|---|---|---|
| 325 | Dedication | 31 daily runs |
| 336 | The Marathon (Cracked Crown) | Racha de 5 daily runs ganadas |
| 354 | Broken Modem | 7 daily runs |
| 638 | Play Online | Jugar online |
| 639 | Win Online | Ganar online |
| 640 | Win Online Daily | Ganar una daily online |

Dead God exige todos los logros, así que sin ellos el dibujo de Dead God y el banner de infinito serían imposibles. Por eso el mod los marca en cada ranura 4-9 al cargarla, lo que incluye las nuevas y las que se recrean tras "Delete file".

- Se marcan directamente en los datos, sin `TryUnlock`: no sale aviso en pantalla y no se toca Steam.
- Las ranuras 1-3 no se tocan.

El dibujo de Dead God y el banner de infinito (`infinity1-3`) no necesitan código. El juego los calcula con las 3 ranuras que carga el menú, y por eso cada página funciona como una tercia independiente.

## Compilar e instalar (copia de pruebas)

```
build.bat            compila bin\zhlMoreSaveSlots.dll
build.bat instalar   además lo copia, con sus gráficos, a isaac_repentogon\Repentogon\
```

Requisitos: Build Tools de VS 18 con MSVC x86, Windows SDK y CMake. Si falta `build\rgon\`, el script dice cómo clonar REPENTOGON en la etiqueta **1.1.3**, contra la que se compila (1.1.2g y 1.1.3a tienen la misma libzhl). El mod tiene que enlazarse contra la `libzhl` de esa misma versión.

Los gráficos (`assets/`) se generan a partir de los recursos del juego:

```
python tools\make_graphics.py <resources del juego> assets
```

## Límites conocidos

- Solo funciona con REPENTOGON (v1.9.7.12): versiones 1.1.2g, 1.1.3 y 1.1.3a (comparten las mismas estructuras de libzhl).
- Los rótulos "FILE 4-9" solo existen en inglés.
- "Solo local con la nube activada" está comprobado leyendo el código, no en el juego: el perfil de pruebas tiene la nube apagada a propósito.
