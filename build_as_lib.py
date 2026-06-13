from os.path import realpath

Import("env") # type: ignore
menv=env # type: ignore

src_filter = [
  '+<*.cpp>',
  '+<helpers/*.cpp>',
  '+<helpers/sensors>',
  '+<helpers/radiolib/*.cpp>',
  '+<helpers/ui/MomentaryButton.cpp>',
  '+<helpers/ui/buzzer.cpp>',
]

# add build and include dirs according to CPPDEFINES
for item in menv.get("CPPDEFINES", []):
 
    # PLATFORM HANDLING
    if item == "STM32_PLATFORM":
        src_filter.append("+<helpers/stm32/*>")
    elif item == "ESP32":
        src_filter.append("+<helpers/esp32/*>")
    elif item == "NRF52_PLATFORM":
        src_filter.append("+<helpers/nrf52/*>")
    elif item == "RP2040_PLATFORM":
        src_filter.append("+<helpers/rp2040/*>")
    
    # DISPLAY HANDLING
    elif isinstance(item, tuple) and item[0] == "DISPLAY_CLASS":
        display_class = item[1]
        src_filter.append(f"+<helpers/ui/{display_class}.cpp>")
        if (display_class == "ST7789Display") :
            src_filter.append(f"+<helpers/ui/OLEDDisplay.cpp>")
            src_filter.append(f"+<helpers/ui/OLEDDisplayFonts.cpp>")

    # VARIANTS HANDLING
    elif isinstance(item, tuple) and item[0] == "MC_VARIANT":
        variant_name = item[1]
        src_filter.append(f"+<../variants/{variant_name}>")
    
    # INCLUDE EXAMPLE CODE IN BUILD (to provide your own support files without touching the tree)
    elif isinstance(item, tuple) and item[0] == "BUILD_EXAMPLE":
        example_name = item[1]
        src_filter.append(f"+<../examples/{example_name}/*.cpp>")

    # EXCLUDE A SOURCE FILE FROM AN EXAMPLE (must be placed after example name or boom)
    elif isinstance(item, tuple) and item[0] == "EXCLUDE_FROM_EXAMPLE":
        exclude_name = item[1]
        if example_name is None:
            print("***** PLEASE DEFINE EXAMPLE FIRST *****")
            break
        src_filter.append(f"-<../examples/{example_name}/{exclude_name}>")

    # DEAL WITH UI VARIANT FOR AN EXAMPLE
    elif isinstance(item, tuple) and item[0] == "MC_UI_FLAVOR":
        ui_flavor = item[1]
        if example_name is None:
            print("***** PLEASE DEFINE EXAMPLE FIRST *****")
            break
        src_filter.append(f"+<../examples/{example_name}/{ui_flavor}/*.cpp>")
        
# When the consumer vendors the touch-app source itself (the wadamesh touch app
# builds meshcomod as its core lib_dep and ships these files in its own src/),
# drop them from the library so they aren't compiled twice -> duplicate symbols.
# This is opt-in (-DMC_VENDORED_TOUCH_APP); the monorepo's own builds are unaffected.
_defs = [d if isinstance(d, str) else d[0] for d in menv.get("CPPDEFINES", [])]
if "MC_VENDORED_TOUCH_APP" in _defs:
    src_filter += [
        '-<LvglPsramAlloc.cpp>',
        '-<helpers/TouchDiagTrace.cpp>',
        '-<helpers/esp32/MultiTransportCompanionInterface.cpp>',
        '-<helpers/esp32/SdNvsPrefs.cpp>',
        '-<helpers/esp32/TCPCompanionServer.cpp>',
        '-<helpers/esp32/TouchPrefsStore.cpp>',
        '-<helpers/esp32/WifiRuntimeStore.cpp>',
        '-<helpers/esp32/WebSocketCompanionServer.cpp>',
    ]

menv.Replace(SRC_FILTER=src_filter)

#print (menv.Dump())
