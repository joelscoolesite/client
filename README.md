# Nova Client

Een DLL voor Minecraft Bedrock (Windows) die waypoints in je scherm tekent. Je typt coördinaten in, en je ziet een
marker met naam, afstand en een lichtstraal op die plek. Staat de waypoint buiten beeld, dan wijst een pijltje aan de
rand van je scherm de goede kant op.

## Downloaden

Elke push bouwt de DLL automatisch op GitHub:
**Actions** → nieuwste **Build DLL** run → onderaan bij **Artifacts** → `BedrockWaypoints` (zip met de `.dll`).

## Gebruiken

1. Start Minecraft en ga een wereld of server in.
2. Injecteer `BedrockWaypoints.dll` met je injector.
3. Druk op **Esc** (zodat je muis vrij is) en daarna op **F8** voor het menu.

Het menu heeft een zijbalk met **HUD**, **Visual**, **Utility**, **Waypoints** en **Settings**. Elke module heeft een
schakelaar, een eigen sneltoets en instellingen (klik op **Settings** in de kaart). Met de zoekbalk vind je snel een module.

| Module | Wat het doet |
| --- | --- |
| Coordinates | Je positie, Nether/Overworld-coördinaten en kijkrichting |
| Compass | Kompasbalk bovenin, met je waypoints als stipjes |
| FPS / CPS | Frames per seconde en clicks per seconde |
| Clock | Tijd en sessie-timer |
| Keystrokes | W A S D, muisknoppen (met CPS) en spatie |
| Chunk Borders | Randen van de chunk waar je in staat |
| Fullbright | Alles helder |
| Zoom | Houd **C** ingedrukt om in te zoomen |
| Toggle Sprint | Sprint/sneak één keer indrukken om aan te houden (experimenteel) |
| Chat Coordinates | Ziet coördinaten in chat en laat je er een waypoint van maken |
| Screenshot | **F6** kopieert het scherm naar je klembord |

**Settings**: accentkleur, HUD-achtergrond, notificaties, **Edit HUD layout** (HUD-elementen verslepen, scrollen
om groter/kleiner te maken) en **profielen** (instellingen opslaan en wisselen).

**Waypoints**: lijst (met **Edit** en **Copy** om in chat te plakken), **Add** en **Options**. **F7** zet een waypoint
waar je staat. Death waypoints, per wereld/dimensie, **Always load** en Nether-omrekening werken zoals voorheen.

Alles wordt opgeslagen in `%LOCALAPPDATA%\BedrockWaypoints\` (`waypoints.txt`, `settings.ini`, `profiles\`).

## Na een Minecraft update

De DLL leest de camera uit het geheugen van Minecraft. Waar dat precies staat (signature en offsets) verandert soms
bij een update. Ingebouwde profielen:

| Profiel | Bron |
| --- | --- |
| `1.26.5x` | [Latite](https://github.com/LatiteClient/Latite) |
| `1.26.0-1.26.3` | [Flarial](https://github.com/flarialmc/dll) |

De DLL probeert ze zelf en kiest de eerste die past. Bovenin het menu zie je je Minecraft-versie en welk profiel
gebruikt wordt. Werkt geen enkel profiel, dan zegt het menu *"... is not supported yet"*.

Nieuwe waarden kun je zonder opnieuw bouwen invullen in `%LOCALAPPDATA%\BedrockWaypoints\config.ini`: haal de `;`
weg voor een regel en pas de waarde aan. Verwijder `config.ini` om terug te gaan naar de standaardwaarden.
In `log.txt` in dezelfde map zie je wat er gebeurde.

In `config.ini` kun je ook andere toetsen kiezen: `menuKey` (menu) en `addWaypointKey` (snelle waypoint), bijv. `0x2D` voor Insert.

## Zelf bouwen

Nodig: Visual Studio 2022 met *Desktop development with C++* en CMake.

```bat
cmake -S . -B build -A x64
cmake --build build --config Release
```

De DLL staat dan in `build\Release\BedrockWaypoints.dll`. ImGui en MinHook worden automatisch gedownload.

## Hoe het werkt

- `src/render.cpp` hookt `IDXGISwapChain::Present` (DirectX 11 en 12) en tekent met ImGui over het spel heen.
- `src/game.cpp` hookt `ScreenView::setupAndRender` en leest daar de camerapositie en view/projection matrices uit.
  Alle reads zijn beveiligd, zodat verkeerde offsets het spel niet laten crashen.
- `src/ui.cpp` is het menu en rekent wereldcoördinaten om naar schermposities.

Let op: gebruik dit niet op servers die client-mods verbieden.
