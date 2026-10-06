# Bedrock Waypoints

Een DLL voor Minecraft Bedrock (Windows) die waypoints in je scherm tekent. Je typt coördinaten in, en je ziet een
marker met naam, afstand en een lichtstraal op die plek. Staat de waypoint buiten beeld, dan wijst een pijltje aan de
rand van je scherm de goede kant op.

## Downloaden

Elke push bouwt de DLL automatisch op GitHub:
**Actions** → nieuwste **Build DLL** run → onderaan bij **Artifacts** → `BedrockWaypoints` (zip met de `.dll`).

## Gebruiken

1. Start Minecraft en ga een wereld of server in.
2. Injecteer `BedrockWaypoints.dll` met je injector.
3. Druk op **Esc** (zodat je muis vrij is) en daarna op **F8** om het menu te openen.

Het menu heeft drie tabbladen:

- **Waypoints**: al je waypoints. Met **Edit** pas je naam, coördinaten, kleur, dimensie en wereld aan.
- **Add**: nieuwe waypoint maken. Vul **Name** en **X Y Z** in (of klik **Use my position**) en klik **Add**.
  Met de knoppen **Overworld -> Nether** en **Nether -> Overworld** reken je de coördinaten om.
- **Settings**: straal, afstand, Nether-omrekening en death waypoints aan/uit.

Extra functies:

- **F7** zet meteen een waypoint op de plek waar je staat.
- **Death waypoints**: als je doodgaat komt er automatisch een rode `Death` waypoint. Alleen de laatste 3 worden
  bewaard (aan te passen in Settings).
- **Per wereld en dimensie**: een waypoint verschijnt alleen in de wereld en dimensie waarin je hem maakte.
- **Always load (every world)**: vink dit aan en de waypoint is er altijd, in elke wereld en op elke server,
  elke keer dat je injecteert.
- **Nether-omrekening**: overworld-waypoints zie je in de Nether op /8 en Nether-waypoints in de overworld op x8.

Alles wordt opgeslagen in `%LOCALAPPDATA%\BedrockWaypoints\` (`waypoints.txt` en `settings.ini`).

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
