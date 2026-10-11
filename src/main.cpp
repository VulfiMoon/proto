/**
 * Adeon's Toaster Blaster
 * ---------------------------------------------------------------------------
 * This is an LED control program for protogen helmets to be used with MAX7219
 * LED matrixes and ESP32 dev boards. Keyframe animation system allows you
 * to create animations with ease. Facial expressions are controlled with an
 * inexpensive bluetooth controller (MOCUTE-052F), with more gamepads to be
 * supported in the future.
 * 
 * This is a pre-release with some bugs, unfinished code and 
 * lacking documentation. Use at your own risk.
 */

#include <Arduino.h>
#include <functional>
#include "main.h"

Matrix* matrix;
DisplayManager* displayManager;
SequencePlayer* sequencePlayer;
Controller* controller;
TaskHandle_t asyncLoopTask;
Timestamp lastTime;
AnimatedMouth* animatedMouth;

SettingsManager* settingsManager;
HeadsUpDisplay* hud;
TweenManager* displayTweenManager;
AnalogSensor_Boop* boopSensor;  // S-V-G analog TCRT5000 (no trimmer, software-calibrated)
EyeBlink* eyeBlink;
LEDStrip* ledStrip;
UptimeCounter* uptimeCounter;
PWMFan* fanControl;

void setup() {
    Serial.begin(115200);  // used for TOASTER_LOG output and the handleSerialCommand() remote control below
    TOASTER_LOG("\n\n\n\n\n\n%s %s\n", PROJECT_NAME, VERSION_NUMBER);
    TOASTER_LOG("Compiled %s\n", COMPILE_TIMESTAMP);
    TOASTER_LOG("Bitmaps compiled %s\n", Bitmaps::BITMAP_GENERATION_TIMESTAMP);
    TOASTER_LOG("%s%s\n\n", LINK_1, LINK_2);

    EEPROM.begin(SETTING_COUNT + 4);    // Settings checksum is stored at address `SETTING_COUNT`

    hud = new U8G2HUDWrapper(Config::Pins::HUD_SCL, Config::Pins::HUD_SDA);
    matrix = new Matrix_Max7219(Config::Pins::MATRIX_DIN, Config::Pins::MATRIX_CS, Config::Pins::MATRIX_CLK, SettingsManager::getSavedValue(SETTING_MATRIX_BRIGHTNESS));
    matrix->clear();
    displayManager = new DisplayManager(matrix, hud, SettingsManager::getSavedValue(SETTING_ENABLE_MATRIX));
    displayTweenManager = new TweenManager([](){displayManager->requestRedraw();});

    // Register displays (matrix position, number of rows (size), bitmask, hud preview position)
    // Simply comment out any displays that are not in use.
    using namespace Config::Displays;
    displayManager->addDisplays({
        new Display(POS_EYE_L,    16,   EYE_L,    {49, 0},    displayTweenManager),
        new Display(POS_EYE_R,    16,   EYE_R,    {1, 0},     displayTweenManager),
        new Display(POS_MOUTH_L,  32,   MOUTH_L,  {34, 8},    displayTweenManager),
        new Display(POS_MOUTH_R,  32,   MOUTH_R,  {0, 8},     displayTweenManager),
        new Display(POS_NOSE_L,   8,    NOSE_L,   {35, 0},    displayTweenManager),
        new Display(POS_NOSE_R,   8,    NOSE_R,   {23, 0},    displayTweenManager),
        new Display(POS_MOUTH_FRONT, 16, MOUTH_FRONT, {17, 8}, displayTweenManager),   // extra front-facing mouth panel, adjust HUD preview position to taste
    });

    // To add persistent rotation or other effects to your displays, add a global effect:
    //displayManager->addGlobalEffect(new Rotate180(ALL));

    // Front mouth (extra panel) doesn't have its own artwork - instead it's built every
    // frame from the columns of MOUTH_L/MOUTH_R that physically border it, so the
    // pattern flows continuously as one line from the left mouth, through the front,
    // to the right mouth. NOTE: this only looks right when MOUTH_L and MOUTH_R carry
    // matching/mirrored art - an expression that puts different art on each side (like
    // a one-sided tongue) will show a piece of that mismatch here. Fix that at the
    // source (make MOUTH_L/MOUTH_R match for that expression) rather than here.
    displayManager->addGlobalEffect(new LambdaEffect(MOUTH_FRONT, [](u8* buffer, Display* display) {
        Display* mouthL = displayManager->getDisplayByBitmask(MOUTH_L);
        Display* mouthR = displayManager->getDisplayByBitmask(MOUTH_R);
        if (!mouthL || !mouthR) return;
        // buffer[0..7]  borders MOUTH_R -> take MOUTH_R's front-facing 8 columns
        for (u8 k = 0; k < 8; k++) {
            buffer[k] = mouthR->matrixBuffer[k];
        }
        // buffer[8..15] borders MOUTH_L -> take MOUTH_L's front-facing 8 columns
        for (u8 k = 0; k < 8; k++) {
            buffer[15 - k] = mouthL->matrixBuffer[31 - k];
        }
    }));

    // initialize led strip
    //ledStrip = new LEDStrip(SettingsManager::getSavedValue(SETTING_ENABLE_LEDSTRIP));     // spinning color LED strip animation
    ledStrip = new LEDStrip_CD(SettingsManager::getSavedValue(SETTING_ENABLE_LEDSTRIP));     // optical disc LED strip animation

    // initialize bluetooth gamepad
    #ifdef BT_GAMEPAD
    Gamepad::init();
    createButtonMapping();
    #endif

    // initialize eye blink animation
    eyeBlink = new EyeBlink(
        displayManager,                     // displayManager instance
        &Overlays::EyeBlink::closing,       // closing overlay
        &Overlays::EyeBlink::closed,        // closed overlay
        &Overlays::EyeBlink::opening,       // opening overlay
        5 SECONDS,                          // minimum delay between auto blinks
        20 SECONDS,                         // maximum delay between auto blinks
        40 MILLIS,                          // minimum closed eyes duration
        200 MILLIS,                         // maximum closed eyes duration
        SettingsManager::getSavedValue(SETTING_ENABLE_AUTO_BLINK)       // enabled
    );

    // initialize sequencePlayer with startup sequence
    sequencePlayer = new SequencePlayer(displayManager, displayTweenManager, eyeBlink, &Sequences::startup, SettingsManager::getSavedValue(SETTING_RARE_TRANSITION_CHANCE), true);
    
    // set up common sequence transitions
    sequencePlayer->addCommonTransitions({
        &Transitions::blink,
        //&Transitions::crossfade
    });

    // set up rare sequence transitions
    sequencePlayer->addRareTransitions({
        &Transitions::drop,
        &Transitions::slide,
        &Transitions::losePower,
        &Transitions::glitch,
        &Transitions::explode,
        &Transitions::fizz,
        &Transitions::doomMelt,
        //&Transitions::earthquake,
        //&Transitions::expand,
        //&Transitions::shuffle,
        //&Transitions::pulse
        //&Transitions::modem
    });

    // initialize other components
    animatedMouth = new AnimatedMouth(displayManager, 33 MILLIS, 500, 500, SettingsManager::getSavedValue(SETTING_ENABLE_ANIMATED_MOUTH));

    // Analog boop sensor (3-pin S-V-G TCRT5000, no trimmer, sensitivity set in software via calibration).
    boopSensor = new AnalogSensor_Boop(
        displayManager, new OverlayPlayer(displayManager, &Overlays::Boop::boop, false, false), 
        &Overlays::Boop::boop,  // boop overlay
        &Transitions::glitch,   // end boop overlay
        Config::Pins::BOOP_ANALOG, Config::BOOP_TRIGGER_COUNT, Config::BOOP_TRIGGERS_MAX, 
        ((f32)SettingsManager::getSavedValue(SETTING_BOOP_TRIGGER_MULTIPLIER)) / 255.0f,  // trigger multiplier
        200,   // calibration readings
        false, // invert
        SettingsManager::getSavedValue(SETTING_ENABLE_BOOP_SENSOR)   // enable
    );

    /*
    // Digital-output boop sensor (3-pin module: VCC, GND, OUT, with an onboard trimmer
    // potentiometer for sensitivity - no software calibration for this type).
    // Switch to this (and change the `boopSensor` global's type to Sensor*) if you swap
    // to that kind of sensor instead.
    boopSensor = new DigitalSensor_Boop(
        displayManager, new OverlayPlayer(displayManager, &Overlays::Boop::boop, false, false), &Overlays::Boop::boop,
        &Transitions::glitch, Config::Pins::BOOP_DIGITAL, Config::BOOP_TRIGGER_COUNT, Config::BOOP_TRIGGERS_MAX,
        false, // invert
        SettingsManager::getSavedValue(SETTING_ENABLE_BOOP_SENSOR)
    );
    */

    fanControl = new PWMFan(Config::Pins::FAN_PWM, Config::FAN_PWM_CHANNEL, SettingsManager::getSavedValue(SETTING_FAN_PWM_SPEED));

    uptimeCounter = new UptimeCounter();

    settingsManager = new SettingsManager();
    createSettingsMenu();
    if(!settingsManager->init()) ESP.restart(); // restart microcontroller if settings checksum is invalid, settings have been reset

    

    // initialize default controller
    controller = new Controller_Startup(displayManager, 3 SECONDS, [](){changeController(new Controller_Settings(displayManager, settingsManager));});

    // create heads up display update task on other core
    xTaskCreatePinnedToCore(loopAsync, "loopAsync", 2048, NULL, 1, &asyncLoopTask, 0);
}

// Main program loop
void loop(){
    

    // Power-on glitch workaround: some MAX7219 modules can latch garbage into their pixel
    // registers during the brief power instability right as everything is plugged in. Normal
    // frame updates only resend columns that changed, so a garbled column can stay stuck
    // indefinitely. Force one full resend a couple seconds after boot, once power has settled.
    static bool didPostBootRedraw = false;
    if(!didPostBootRedraw && millis() > 2000) {
        displayManager->forceFinalize();
        didPostBootRedraw = true;
    }

    // calculate delta time
    deltaTime = (Timestamp)(micros() - lastTime);
    if(deltaTime < Config::FRAMETIME) return;       // intentionally limiting updates per second to avoid fixed point imprecision issues
    lastTime = micros();

    #ifdef BT_GAMEPAD
    Gamepad::update(safeMode);  // safeMode is a function that gets called when the gamepad disconnects
    #endif
    sequencePlayer->update();
    controller->update();
    displayTweenManager->update();
    boopSensor->update();
    eyeBlink->update();
    ledStrip->update();
    uptimeCounter->update();
    animatedMouth->update();
    displayManager->update();
    handleSerialCommand();
}

// Components to be updated independently on core 0
void loopAsync(void* pvParameters) {
    while(true) {
        hud->update();
    }
}

// Controller related functions
void changeController(Controller* newController) {
    delete controller;
    switch(newController->getSequencePlayerState()){
        case SP_PLAY: sequencePlayer->start(); break;
        case SP_STOP: sequencePlayer->stop(); break;
    }
    controller = newController;
}

#ifdef BT_GAMEPAD
void createButtonMapping() {
    // joystick in GAME mode or left analog, mapped to controller action
    Gamepad::inputHandler.mapJoystick(
        0,                                                      // joystick id
        [](u8 x, u8 y){controller->handleInput(x, y);},         // filtered input
        [](u8 x, u8 y){controller->handleRawInput(x, y);}       // raw input
    );

    // joystick in KEY mode or right analog, mapped to controller action
    Gamepad::inputHandler.mapJoystick(
        1,                                                      // joystick id
        [](u8 x, u8 y){controller->handleInput(x, y);},         // filtered input
        [](u8 x, u8 y){controller->handleRawInput(x, y);}       // raw input
    );

    // joystick in KEY (dpad) mode or directional pad, mapped to joystick 0
    Gamepad::inputHandler.mapButtonsToJoystick(0, BTN_UP, BTN_UP_RIGHT, BTN_RIGHT, BTN_DOWN_RIGHT, BTN_DOWN, BTN_DOWN_LEFT, BTN_LEFT, BTN_UP_LEFT);

    // change button mapping depending on gamepad so that it makes more sense
    #ifdef GAMEPAD_DEBUG
        const Button 
            BTN_FACESET1 =              BTN_NONE,
            BTN_FACESET2 =              BTN_NONE,
            BTN_FACESET3 =              BTN_NONE,
            BTN_SETTINGS =              BTN_NONE,
            BTN_BLINK =                 BTN_NONE,
            BTN_CALIBRATE_BOOP =        BTN_NONE,
            BTN_AUTO_FACE_SWITCHER =    BTN_NONE;
    #endif
    #ifdef GAMEPAD_XBOX
        const Button 
            BTN_FACESET1 =              BTN_Y,
            BTN_FACESET2 =              BTN_X,
            BTN_FACESET3 =              BTN_B,
            BTN_SETTINGS =              BTN_A,
            BTN_BLINK =                 BTN_R1,
            BTN_CALIBRATE_BOOP =        BTN_SELECT,
            BTN_AUTO_FACE_SWITCHER =    BTN_START;
    #endif
    #ifdef GAMEPAD_MOCUTE052F
        const Button 
            BTN_FACESET1 =              BTN_X,
            BTN_FACESET2 =              BTN_SELECT,
            BTN_FACESET3 =              BTN_START,
            BTN_SETTINGS =              BTN_B,
            BTN_BLINK =                 BTN_R1,
            BTN_CALIBRATE_BOOP =        BTN_A,
            BTN_AUTO_FACE_SWITCHER =    BTN_Y;
    #endif

    // general button mapping
    Gamepad::inputHandler.mapButtons({
        // right bumper for eye blink
        {BTN_BLINK, BTN_STATE_PRESSED, [](){ 
            eyeBlink->handleInput(true); 
        }},
        {BTN_BLINK, BTN_STATE_RELEASED, [](){ 
            eyeBlink->handleInput(false); 
        }},

        // change face sets
        {BTN_FACESET1, BTN_STATE_PRESSED, [](){
            changeController( new Controller_FaceSwitcher( displayManager, sequencePlayer, &Sequences::joyBlush, &Sequences::spooked, &Sequences::neutral, &Sequences::blushing, &Sequences::joy, &Sequences::angry, &Sequences::angryHappy, &Sequences::annoyed ));
        }},
        {BTN_FACESET2, BTN_STATE_PRESSED, [](){ 
            changeController( new Controller_FaceSwitcher( displayManager, sequencePlayer, &Sequences::heartEyes, &Sequences::questioning, &Sequences::dizzy, &Sequences::exclamation, &Sequences::dead, &Sequences::squinting, &Sequences::powerOff, &Sequences::wink ));
        }},
        {BTN_FACESET3, BTN_STATE_PRESSED, [](){ 
            changeController( new Controller_FaceSwitcher( displayManager, sequencePlayer, &Sequences::crying, &Sequences::batteryCheck, &Sequences::uwu, &Sequences::randomize, &Sequences::nope, &Sequences::powerOff, &Sequences::owo, &Sequences::dizzy ));
        }},

        // settings
        {BTN_SETTINGS, BTN_STATE_PRESSED, [](){ 
            changeController(new Controller_Settings(displayManager, settingsManager)); 
        }},

        // calibrate boop sensor
        {BTN_CALIBRATE_BOOP, BTN_STATE_PRESSED, [](){ 
            boopSensor->calibrate(); 
        }},

        // automatic face switcher
        {BTN_AUTO_FACE_SWITCHER, BTN_STATE_PRESSED, [](){
            changeController( new Controller_AutomaticFaceSwitcher(
                displayManager, sequencePlayer, 5 SECONDS, 20 SECONDS, {
                    &Sequences::neutral,
                    &Sequences::joy,
                    &Sequences::joyBlush,
                    &Sequences::blushing,
                    &Sequences::angryHappy,
                    &Sequences::angry,
                    &Sequences::annoyed,
                    &Sequences::spooked,
                    &Sequences::questioning,
                    &Sequences::exclamation,
                    &Sequences::dead,
                    &Sequences::squinting,
                    &Sequences::dizzy,
                    &Sequences::heartEyes,
                    &Sequences::powerOff,
                    &Sequences::uwu,
                    &Sequences::owo,
                    &Sequences::crying,
                })
            );
        }},
    });
}
#endif

void createSettingsMenu() {
    settingsManager->addSettings({
        new Setting_Lambda(
            "Matrix",       // category
            "Brightness",   // name
            SETTING_MATRIX_BRIGHTNESS,  // unique key defined in Settings/SettingKeys.h enum SettingKey
            SETTING_TYPE_PERCENTAGE,    // setting type, as defined in Settings/Setting.h enum SettingType
            8, 0, 15, 1,        // default, minimum, maximum, increment values
            []() -> u8 { return displayManager->getMatrixBrightness(); },       // getter returning u8
            [](u8 value) { displayManager->setMatrixBrightness(value); }        // setter using u8
        ),

        new Setting_ComponentToggle("Matrix", "Enable", SETTING_ENABLE_MATRIX, true, displayManager),        // category, name, unique key, default value, child of ToasterComponent

        new Setting_ComponentToggle("LED Strip", "Enable", SETTING_ENABLE_LEDSTRIP, true, ledStrip),

        new Setting_ComponentToggle("Auto blink", "Enable", SETTING_ENABLE_AUTO_BLINK, true, eyeBlink),

        new Setting_ComponentToggle("Boop sensor", "Enable", SETTING_ENABLE_BOOP_SENSOR, false, boopSensor),

        new Setting_Lambda(
            "Boop sensor",
            "Sensitivity",
            SETTING_BOOP_TRIGGER_MULTIPLIER,
            SETTING_TYPE_PERCENTAGE,
            127, 0, 255, 17,
            []() -> u8 { return boopSensor->getTriggerMultiplier(); }, 
            [](u8 value) { boopSensor->setTriggerMultiplier(value); }
        ),

        new Setting_Action("Boop sensor", "Calibrate", [](u8 value) { boopSensor->calibrate(); }),       // category, name, function to be called

        new Setting_ComponentToggle("Mouth anim.", "Enable", SETTING_ENABLE_ANIMATED_MOUTH, false, animatedMouth),

        new Setting_Action("Mouth anim.", "Calibrate", [](u8 value) { animatedMouth->reset(); }),

        new Setting_Lambda(
            "Rare transit.",
            "Chance",
            SETTING_RARE_TRANSITION_CHANCE,
            SETTING_TYPE_PERCENTAGE,
            25, 0, 255, 17,
            []() -> u8 { return sequencePlayer->getRareTransitionChance(); }, 
            [](u8 value) { sequencePlayer->setRareTransitionChance(value); }
        ),

        new Setting_Lambda(
            "Fan speed",
            "",
            SETTING_FAN_PWM_SPEED,
            SETTING_TYPE_PERCENTAGE,
            255, 0, 255, 51,
            []() -> u8 { return fanControl->getSpeed(); }, 
            [](u8 value) { fanControl->setSpeed(value); }
        ),

        new Setting_Action(
            "Misc.",
            "Snake game",
            [](u8 value) { 
                animatedMouth->stop(); 
                changeController(new Controller_SnakeGame(displayManager, MOUTH, EYES, 200 MILLIS)); 
            }
        ),

        /*
        new Setting_Action(
            "Misc.",
            "Analyzer",
            [](u8 value) { 
                animatedMouth->stop(); 
                changeController(new Controller_SpectrumAnalyzer(displayManager)); 
            }
        ),
        */

        // new Setting_Action("Misc.", "Strobe test", [](u8 value) { changeController(new Controller_Strobe(displayManager, sequencePlayer, displayTweenManager)); }),

        new Setting_Action("System", "Restart", [](u8 value) { ESP.restart(); }),

        new Setting_Action("System", "Save settings", [](u8 value) { settingsManager->saveSettings(); }),
    });
}

// Shared list of face commands, used by the Serial remote control below.
struct FaceCommand { const char* name; Sequence* sequence; };
static const FaceCommand faces[] = {
    {"neutral",      &Sequences::neutral},
    {"joy",          &Sequences::joy},
    {"joyblush",     &Sequences::joyBlush},
    {"blushing",     &Sequences::blushing},
    {"angryhappy",   &Sequences::angryHappy},
    {"angry",        &Sequences::angry},
    {"spooked",      &Sequences::spooked},
    {"annoyed",      &Sequences::annoyed},
    {"questioning",  &Sequences::questioning},
    {"exclamation",  &Sequences::exclamation},
    {"dead",         &Sequences::dead},
    {"squinting",    &Sequences::squinting},
    {"uwu",          &Sequences::uwu},
    {"owo",          &Sequences::owo},
    {"nope",         &Sequences::nope},
    {"dizzy",        &Sequences::dizzy},
    {"hearteyes",    &Sequences::heartEyes},
    {"wink",         &Sequences::wink},
    {"poweroff",     &Sequences::powerOff},
    {"batterycheck", &Sequences::batteryCheck},
    {"crying",       &Sequences::crying},
    {"randomize",    &Sequences::randomize},
};

// Runs a text command (used by Serial and the web remote). Returns true if recognized.
bool executeCommand(String cmd) {
    cmd.trim();
    cmd.toLowerCase();
    if(cmd.length() == 0) return false;

    if(cmd == "redraw")     { displayManager->forceFinalize(); return true; }
    if(cmd == "boop")       { boopSensor->calibrate(); return true; }
    if(cmd == "settings")   { changeController(new Controller_Settings(displayManager, settingsManager)); return true; }
    if(cmd == "restart")    { ESP.restart(); return true; }
    if(cmd == "save")       { settingsManager->saveSettings(); return true; }
    if(cmd == "mouthcal")   { animatedMouth->reset(); return true; }
    if(cmd == "snake")      { animatedMouth->stop(); changeController(new Controller_SnakeGame(displayManager, MOUTH, EYES, 200 MILLIS)); return true; }
    if(cmd == "autoswitch") {
        changeController(new Controller_AutomaticFaceSwitcher(
            displayManager, sequencePlayer, 5 SECONDS, 20 SECONDS, {
                &Sequences::neutral,    &Sequences::joy,        &Sequences::joyBlush,
                &Sequences::blushing,   &Sequences::angryHappy, &Sequences::angry,
                &Sequences::annoyed,    &Sequences::spooked,    &Sequences::questioning,
                &Sequences::exclamation,&Sequences::dead,       &Sequences::squinting,
                &Sequences::dizzy,      &Sequences::heartEyes,  &Sequences::powerOff,
                &Sequences::uwu,        &Sequences::owo,        &Sequences::crying,
            }));
        return true;
    }
    if(cmd == "normal") {
        // leaves snake/auto-switcher/settings and goes back to plain button-driven faces
        changeController(new Controller_FaceSwitcher(displayManager, sequencePlayer, &Sequences::joyBlush, &Sequences::spooked, &Sequences::neutral, &Sequences::blushing, &Sequences::joy, &Sequences::angry, &Sequences::angryHappy, &Sequences::annoyed));
        return true;
    }

    for(const FaceCommand& f : faces) {
        if(cmd == f.name) {
            sequencePlayer->transitionSequence(f.sequence);
            TOASTER_LOG("-> %s\n", f.name);
            return true;
        }
    }
    return false;
}

// Temporary remote control via Serial Monitor (115200 baud) while no gamepad/soldering is available.
// Type a face name and press Enter, e.g.: neutral / joy / angry / dizzy / hearteyes ...
// Type "help" for the full list, "settings" to open the settings menu.
void handleSerialCommand() {
    if(!Serial.available()) return;

    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    cmd.toLowerCase();
    if(cmd.length() == 0) return;

    if(cmd == "help") {
        TOASTER_LOG("Faces: ");
        for(const FaceCommand& f : faces) TOASTER_LOG("%s ", f.name);
        TOASTER_LOG("\nOther commands: settings, redraw, boop, mouthcal, snake, autoswitch, normal, save, restart\n");
        return;
    }

    if(!executeCommand(cmd)) {
        TOASTER_LOG("Unknown command \"%s\", type \"help\" for the list\n", cmd.c_str());
    }
}

void safeMode() {
    if (controller->getType() != CONTROLLER_SETTINGS) {
        changeController(new Controller_Settings(displayManager, settingsManager));
    }
    Sequence* s = sequencePlayer->getSequence();
    if (s != &Sequences::neutral && s != &Sequences::startup) {
        sequencePlayer->transitionSequence(&Sequences::neutral);
    }
}