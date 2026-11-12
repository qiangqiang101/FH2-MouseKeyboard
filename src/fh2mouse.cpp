// FH2 Mouse & Keyboard: a mouse cursor for Forza Horizon 2's menus, and keys to drive with.
//
// The game's UI library (AVUI, a C++ take on WPF) has WPF's mouse code, but the Xbox build never
// feeds it and its hit testing finds nothing. Once a frame, on the UI thread, this mod:
//   - finds the control under the cursor itself, from the active window's visual tree,
//   - moves the keyboard focus to it (what the d-pad would do),
//   - turns clicks into the keys the controller sends the menus: left = A, right = B,
//     wheel = d-pad up/down, back/forward side buttons = LB/RB.
// Pages then behave exactly as they do with a controller.
#include "fh2mod.h"
#include "key_art.h"
#include "lang.h"

#include <windows.h>
#include <wincodec.h>

#include <algorithm>
#include <array>
#include <xinput.h>
#ifdef _MSC_VER
#include <intrin.h>
#define RETURN_ADDRESS() _ReturnAddress()
#else
#define RETURN_ADDRESS() __builtin_return_address(0)
#endif
#include <cctype>
#include <atomic>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <cwchar>
#include <mutex>
#include <string>
#include <map>
#include <vector>

namespace {

const FH2ModApi* g_api = nullptr;
FH2Mod* g_mod          = nullptr;
uint8_t* g_base        = nullptr;

// ---- the game (24 November 2015 build), offsets from its base -------------------------------

constexpr uintptr_t RVA_UI_UPDATE_SLOT = 0x2ECFBF8;   // UserInterface vtable slot 15: Update(this, float dt)
constexpr uintptr_t RVA_UI_UPDATE      = 0x142C5E0;   // ... which is this; it runs the pad-to-key pump
constexpr uintptr_t RVA_APPLICATION    = 0x36B0418;   // AVUI::Application*; render size floats at +0x50/+0x54
constexpr uintptr_t RVA_GET_WM         = 0x157D110;   // WindowManager* (): Application vtable +0x78
constexpr uintptr_t RVA_IS_TYPE        = 0x154F960;   // bool Type::IsTypeOf(type, base)
constexpr uintptr_t RVA_FOCUS          = 0x14D01D0;   // Keyboard::Focus(Ptr* out, IInputElement*)
constexpr uintptr_t RVA_FOCUSED        = 0x1519FD0;   // IInputElement* Keyboard::FocusedElement()
constexpr uintptr_t RVA_KEY_CODE       = 0x1514530;   // the key code the window manager takes, from a WPF Key
constexpr uintptr_t RVA_OBJECT_FREE    = 0x0257730;   // frees an AVUI object whose refcount (+0x10) hit 0
constexpr uintptr_t RVA_TO_ANCESTOR    = 0x1576BB0;   // bool Visual::TransformToAncestor(v, ancestor, inverse, GeneralTransform**, Matrix*)
constexpr uintptr_t RVA_TRANSFORM_PT   = 0x146E570;   // Point* Matrix::Transform(Matrix*, Point* out, const Point* in)
constexpr uintptr_t RVA_GET_VALUE      = 0x1548310;   // boxed value of a property in an object's value store
constexpr uintptr_t RVA_UNSET_VALUE    = 0x160C340;   // DependencyProperty::UnsetValue
constexpr uintptr_t RVA_OPACITY_STORED = 0x36A61D0;   // bool: Opacity's index (next dword) is known
constexpr uintptr_t RVA_OPACITY_DEFAULT = 0x36A61C8;  // boxed default Opacity
constexpr uintptr_t RVA_DP_GET_VALUE   = 0x15E0F10;   // DependencyObject::GetValue(obj, boxed* out, property)
constexpr uintptr_t RVA_IS_FOCUSED_DP  = 0x36A48B8;   // UIElement::IsFocusedProperty
constexpr uintptr_t RVA_FOCUSABLE_DP   = 0x36A76F8;   // UIElement::FocusableProperty
constexpr uintptr_t RVA_DATA_CONTEXT_DP = 0x36C7128;  // FrameworkElement::DataContextProperty
constexpr uintptr_t RVA_SELECTED_ITEM_DP = 0x36CF118; // Selector::SelectedItemProperty

// The map: MapScene (the page) and the MapViewControllerInteractive behind it, which pans the
// map under a cursor fixed in the middle. The map is seen from straight above, `zoom` world
// units from the middle to the top and bottom edges.
constexpr uintptr_t VT_MAP_SCENE       = 0x2DFED48;
constexpr uintptr_t VT_MAP_CONTROLLER  = 0x2D9CD48;
constexpr uintptr_t MAP_CONTROLLER     = 0x410;   // MapScene: its controller
constexpr uintptr_t MAP_POS_X          = 0x2C0;   // controller: the map cursor, in the world (x, y, z)
constexpr uintptr_t MAP_POS_Z          = 0x2C8;
constexpr uintptr_t MAP_TARGET_X       = 0x2D0;   // controller: where the camera is going (x, y, z)
constexpr uintptr_t MAP_TARGET_Z       = 0x2D8;
constexpr uintptr_t MAP_CAM_X          = 0x27C;   // controller: where it is (eased x, then z)
constexpr uintptr_t MAP_CAM_Z          = 0x290;
constexpr uintptr_t MAP_ZOOM           = 0x2E0;   // controller: zoom, 0..1
constexpr uintptr_t MAP_ZOOM_SHOWN     = 0x2E4;   // ... as the distance from the middle to the top edge
constexpr uintptr_t MAP_LEASH          = 0x3B0;   // controller: how far the cursor goes before the camera follows
constexpr uintptr_t MAP_ZOOM_MIN       = 0x384;
constexpr uintptr_t MAP_ZOOM_MAX       = 0x388;

constexpr uintptr_t WM_ACTIVE_WINDOW = 0x68;   // WindowManager: the active window (a UIWindow)
constexpr uintptr_t WM_KEY_SLOT      = 0xD8;   // WindowManager vtable: key(wm, up, code, 0, 0, 0)
constexpr uintptr_t OBJ_TYPE         = 0x18;   // AVUI object: its Type, once asked for (else vtable +0x18)
constexpr uintptr_t VISUAL_PARENT    = 0x70;   // Visual: its visual parent (the "Parent" property's getter)
constexpr uintptr_t UIELEMENT_INPUT  = 0xB8;   // UIElement: its IInputElement
constexpr uintptr_t VISUAL_FLAGS     = 0xA8;   // Visual: bit 3 Opacity is set; bits 5/6 laid out
constexpr uintptr_t RENDER_SIZE      = 0x134;  // UIElement: RenderSize (two floats), when laid out
constexpr uintptr_t VT_CHILD_COUNT   = 0x98;   // Visual vtable: int VisualChildrenCount()
constexpr uintptr_t VT_CHILD         = 0xA0;   // Visual vtable: Visual* GetVisualChild(int)
constexpr uintptr_t VT_GT_TRANSFORM  = 0xE0;   // GeneralTransform vtable: bool TryTransform(Point* in, Point* out)

// AVUI's static Type objects.
constexpr uintptr_t TYPE_VISUAL        = 0x36A25B0;
constexpr uintptr_t TYPE_UIELEMENT     = 0x36A3520;
constexpr uintptr_t TYPE_BUTTON_BASE   = 0x36ABE70;
constexpr uintptr_t TYPE_REPEAT_BUTTON = 0x36D0F10;
constexpr uintptr_t TYPE_LIST_BOX_ITEM = 0x3689AE0;
constexpr uintptr_t TYPE_COMBO_ITEM    = 0x36B79D0;
constexpr uintptr_t TYPE_TAB_ITEM      = 0x36DABC0;
constexpr uintptr_t TYPE_TREE_ITEM     = 0x36DB0C0;
constexpr uintptr_t TYPE_MENU_ITEM     = 0x3686B00;
constexpr uintptr_t TYPE_SLIDER        = 0x36D0070;
constexpr uintptr_t TYPE_SELECTOR      = 0x36CE8F0;   // ListBox, the game's ItemsSelector, ...
constexpr uintptr_t TYPE_SPINNER       = 0x3681F40;   // the game's SpinnerControl
constexpr uintptr_t TYPE_HELP_BUTTON   = 0x34E3D00;   // the game's ControllerButtonControl (the help bar's "(A) Select")
constexpr uintptr_t RVA_HELP_BUTTON_DP = 0x34E35F0;   // ControllerButtonControl::ButtonProperty (a ControllerButtonType)

// WPF Key values, as the pad sends them (game-notes.md, "The pad in the menus").
constexpr int KEY_A = 18;          // Space
constexpr int KEY_B = 45;          // B
constexpr int KEY_UP = 24, KEY_DOWN = 26;
constexpr int KEY_LB = 19, KEY_RB = 20;   // PageUp, PageDown
constexpr int KEY_X = 67, KEY_Y = 68;
constexpr int KEY_RS_CLICK = 119;   // RightCtrl: what the pad's right stick click sends
constexpr int KEY_LS_CLICK = 118;   // LeftCtrl: the left stick's
constexpr int KEY_VIEW = 13;        // the View button's
constexpr int KEY_LT = 116, KEY_RT = 117;   // the triggers' (the shifts)
constexpr int KEY_LEFT = 23, KEY_RIGHT = 25;

struct Expect {
    uintptr_t rva;
    uint8_t bytes[12];
    uint8_t len = sizeof bytes;   // how many of them to compare
};
const Expect kExpected[] = {
    // Not the address of its 1/30 s frame cap: frame rate patches point that at their own value.
    {RVA_UI_UPDATE, {0x40, 0x53, 0x48, 0x83, 0xEC, 0x30, 0xC5, 0xFA, 0x10, 0x05}, 10},
    {RVA_IS_TYPE, {0x40, 0x57, 0x48, 0x83, 0xEC, 0x50, 0x48, 0xC7, 0x44, 0x24, 0x20, 0xFE}},
    {RVA_FOCUS, {0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x30, 0x48, 0x8B}},
    {RVA_FOCUSED, {0x48, 0x83, 0xEC, 0x28, 0xE8, 0x37, 0x8E, 0xFB, 0xFF, 0x48, 0x8B, 0x88}},
    {RVA_KEY_CODE, {0xFF, 0xC9, 0x81, 0xF9, 0xAA, 0x00, 0x00, 0x00, 0x0F, 0x87, 0x0C, 0x04}},
    {RVA_GET_WM, {0x48, 0x8B, 0x0D, 0x01, 0x33, 0x13, 0x02, 0x48, 0x85, 0xC9, 0x74, 0x07}},
    {RVA_OBJECT_FREE, {0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xD9, 0xE8, 0xB2, 0xA2}},
    {RVA_TO_ANCESTOR, {0x4C, 0x89, 0x4C, 0x24, 0x20, 0x44, 0x88, 0x44, 0x24, 0x18, 0x48, 0x89}},
    {RVA_TRANSFORM_PT, {0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x49, 0x8B, 0x00, 0x4C, 0x8D, 0x42}},
    {RVA_GET_VALUE, {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48}},
    {RVA_UNSET_VALUE, {0x48, 0x8D, 0x05, 0xB1, 0xE1, 0x0A, 0x02, 0xC3, 0xCC, 0xCC, 0xCC, 0xCC}},
    {RVA_DP_GET_VALUE, {0x48, 0x8B, 0xC4, 0x48, 0x89, 0x50, 0x10, 0x55, 0x56, 0x57, 0x48, 0x83}},
};

using UpdateFn  = void (*)(void* self, float dt);
using GetWmFn   = uint8_t* (*)();
using IsTypeFn  = bool (*)(void* type, void* base);
using FocusFn   = void (*)(void** out, void* element);
using FocusedFn = void** (*)();
using KeyCodeFn = int (*)(int key);
using KeyFn     = uint8_t (*)(void* wm, uint8_t up, int code, int scan, uint8_t a, uint8_t b);
using FreeFn    = void (*)(void* obj);
using TypeOfFn  = void* (*)(void* obj);
using ToAncestorFn  = bool (*)(void* v, void* ancestor, uint8_t inverse, void** gt, float* matrix);
using TransformPtFn = float* (*)(const float* matrix, float* out, const float* in);
using GetValueFn    = uint8_t* (*)(void* obj, int index);
using UnsetFn       = uint8_t* (*)();
using CountFn       = int (*)(void* v);
using ChildFn       = uint8_t* (*)(void* v, int i);
using GtFn          = bool (*)(void* gt, const float* in, float* out);
using DpGetValueFn  = void* (*)(void* obj, uint8_t** out, void* property);

UpdateFn o_update = nullptr;

template <class F> F game(uintptr_t rva) { return reinterpret_cast<F>(g_base + rva); }
template <class T> T at(const void* p, uintptr_t off) { return *reinterpret_cast<T const*>(static_cast<const uint8_t*>(p) + off); }
template <class T> T& at_ref(void* p, uintptr_t off) { return *reinterpret_cast<T*>(static_cast<uint8_t*>(p) + off); }

// ---- settings ----------------------------------------------------------------------------------

struct Settings {
    bool enabled      = true;
    bool hover_focus  = true;
    DWORD hide_ms     = 3000;   // the cursor hides this long after the mouse last moved (0: never)
    bool debug        = false;  // log what's under the cursor
    bool free_look    = true;   // the mouse looks around while driving
    std::atomic<int> look_percent{50};    // ... this fast: 0 to 100, 50 is kLookPerPixel (100 twice that)
    int frame_rate    = 0;      // the game's frame rate, if measuring it goes wrong (0: measure)
} g_cfg;

std::wstring g_dir;

// Keys for the car and the menus. The car's actions answer the game's own input queries
// (driving is context 0, "the keyboard" below), and only count while driving. The menu's press
// the pad's buttons, as clicks do, and only count in the menus. So one key can do both: Esc
// pauses while driving and goes back in the menus, the arrows steer or move the selection.
constexpr int KEY_PAUSE = 7;   // what the pad's Start sends (the menus' Pause)
struct Action {
    const char* name;    // in the ini
    const char* label;   // on the Mods page
    const char* keys;    // the default
    int control;         // the driving control it pushes, or -1
    float sign;          // which way, for the steering axis
    int key;             // the menu key it presses, or 0
    bool menu;           // counts in the menus (else while driving)
    uint64_t pad = 0;    // the pad's own buttons it holds, for what doesn't ask by control
    DWORD min_ms = 0;    // a press holds them at least this long, for what wants them held
    const char* extra = nullptr;   // Extra Options' binding it presses (see "Extra Options' keys")
};
const Action kActions[] = {
    {"Gas", "Gas", "W", 0, 1, 0, false},
    {"Brake", "Brake / reverse", "S", 1, 1, 0, false},
    {"SteerLeft", "Steer left", "Left", 2, -1, 0, false},
    {"SteerRight", "Steer right", "Right", 2, 1, 0, false},
    {"Handbrake", "Handbrake", "Space", 3, 1, 0, false},
    {"ShiftUp", "Shift up", "A", 4, 1, 0, false},
    {"ShiftDown", "Shift down", "Z", 5, 1, 0, false},
    {"Clutch", "Clutch", "LAlt", 6, 1, 0, false},
    {"Camera", "Change camera", "V", 17, 1, 0, false},   // C is FH2 ANNA's (as FH3's)
    {"LookBack", "Look back", "RCtrl", 10, 1, 0, false},
    {"LookLeft", "Look left", "None", 11, 1, 0, false},
    {"LookRight", "Look right", "None", 12, 1, 0, false},
    {"Rewind", "Rewind", "R", 20, 1, 0, false},
    {"Map", "Map", "M", 22, 1, 0, false},
    {"PhotoMode", "Photo mode", "P", 15, 1, 0, false},
    // The radio reads the d-pad's buttons itself: left / right, with the bits the pad sets with them.
    {"RadioPrevious", "Previous station", "Comma", -1, 0, 0, false, 0xC0000004ull},
    {"RadioNext", "Next station", "Period", -1, 0, 0, false, 0xC0000008ull},
    // Telemetry is the d-pad's down, and its pages its left / right: the radio's keys. It takes
    // the d-pad only once it's been down a moment (a 120 ms press doesn't do, 250 ms does).
    {"Telemetry", "Telemetry", "T", -1, 0, 0, false, 0x140000002ull, 300},
    // The HUD's prompts ("Enter garage"): control 21, on X with an automatic gearbox and on Back
    // with a manual one, asked by the HUD at 0x78B745.
    {"Interact", "Enter garage (prompts)", "Enter", 21, 1, 0, false},
    // FnFNitro's nitro (its own mod, which reads the pad's nitro button itself): see "FnFNitro".
    {"Nitro", "Nitro (FnFNitro)", "LCtrl", -1, 0, 0, false},
    // HonkHonk's horn (its own mod too, reading a stick's click itself): see "HonkHonk".
    {"Horn", "Horn (HonkHonk)", "E", -1, 0, 0, false},
    // Extra Options' blinkers and lights, on whatever pad buttons its ini binds them to. The
    // indicators want a 0.5 s hold (shorter is the radio), so a tap holds them 0.65 s.
    {"SignalLeft", "Left indicator", "LBracket", -1, 0, 0, false, 0, 650, "SignalLeft"},
    {"SignalRight", "Right indicator", "RBracket", -1, 0, 0, false, 0, 650, "SignalRight"},
    {"Hazards", "Hazard lights", "G", -1, 0, 0, false, 0, 0, "Hazards"},
    {"Headlights", "Headlights", "L", -1, 0, 0, false, 0, 0, "Headlights"},
    // FH2 ANNA's keys (FH3's): they press its d-pad buttons, see "FH2 ANNA" (anna_pad_bits).
    {"AnnaOpen", "ANNA: open / close", "C", -1, 0, 0, false},
    {"AnnaUp", "ANNA: up", "I", -1, 0, 0, false},
    {"AnnaDown", "ANNA: down", "K", -1, 0, 0, false},
    {"AnnaBack", "ANNA: back", "J", -1, 0, 0, false},
    {"AnnaChoose", "ANNA: choose", "L", -1, 0, 0, false},
    // While ANNA's autodrive is on: holds the button ANNA turns its cinematic camera on and off with
    // (its ini's, LB unless it says another), see "FH2 ANNA".
    {"AnnaCinematic", "ANNA: cinematic camera (autodrive)", "LShift", -1, 0, 0, false},
    // ANNA Drone Mode's keys: only while the drone flies, when they're all that counts (with the
    // radio's keys), see "Drone Mode".
    {"DroneForward", "Drone: forward", "W", -1, 0, 0, false},
    {"DroneBack", "Drone: back", "S", -1, 0, 0, false},
    {"DroneLeft", "Drone: left", "A", -1, 0, 0, false},
    {"DroneRight", "Drone: right", "D", -1, 0, 0, false},
    {"DroneUp", "Drone: up", "E", -1, 0, 0, false},
    {"DroneDown", "Drone: down", "Q", -1, 0, 0, false},
    {"DroneFast", "Drone: faster", "LShift", -1, 0, 0, false},
    {"DroneToCar", "Drone: back to the car", "R", -1, 0, 0, false},
    {"DroneLookAtCar", "Drone: look at the car", "F", -1, 0, 0, false},
    {"DroneHelp", "Drone: hide / show the help", "Tab", -1, 0, 0, false},
    {"DroneExit", "Drone: exit", "Escape", -1, 0, 0, false},
    // The menu's keys hold the pad's buttons too (Start, A, B, the d-pad), so every menu takes them
    // as it takes the pad's; the key is what's pressed instead if that can't be done.
    {"Pause", "Pause", "Escape", -1, 0, KEY_PAUSE, false, 0x10ull},
    {"Accept", "Menu: select", "Enter", -1, 0, KEY_A, true, 0x1000ull},
    {"Back", "Menu: back", "Escape", -1, 0, KEY_B, true, 0x2000ull},
    {"Up", "Menu: up", "Up", -1, 0, KEY_UP, true, 0x140000001ull},
    {"Down", "Menu: down", "Down", -1, 0, KEY_DOWN, true, 0x140000002ull},
    {"Left", "Menu: left", "Left", -1, 0, KEY_LEFT, true, 0xC0000004ull},
    {"Right", "Menu: right", "Right", -1, 0, KEY_RIGHT, true, 0xC0000008ull},
    {"PreviousTab", "Menu: previous tab", "PageUp", -1, 0, KEY_LB, true, 0x100ull},
    {"NextTab", "Menu: next tab", "PageDown", -1, 0, KEY_RB, true, 0x200ull},
    // X and Y, for what the help bar offers on them ("(Y) Stamp").
    {"ButtonX", "Menu: X button", "X", -1, 0, KEY_X, true, 0x4000ull},
    {"ButtonY", "Menu: Y button", "Y", -1, 0, KEY_Y, true, 0x8000ull},
    // And the rest the help bar offers: View ("Setup manager", "Hide UI"), the triggers ("Select /
    // Unselect Layer") and the stick clicks ("Advanced camera"). The triggers aren't buttons, so
    // they're pressed, not held.
    {"ButtonView", "Menu: View button", "Tab", -1, 0, KEY_VIEW, true, 0x20ull},
    {"ButtonLT", "Menu: left trigger", "Z", -1, 0, KEY_LT, true},
    {"ButtonRT", "Menu: right trigger", "C", -1, 0, KEY_RT, true},
    {"ButtonLS", "Menu: left stick click", "F", -1, 0, KEY_LS_CLICK, true, 0x40ull},
    {"ButtonRS", "Menu: right stick click", "V", -1, 0, KEY_RS_CLICK, true, 0x80ull},
};
constexpr int kActionCount = sizeof kActions / sizeof kActions[0];
constexpr int kMaxKeys     = 4;
bool g_keyboard            = true;
bool g_key_icons_on        = true;   // the help bar shows the keys while they're used (see "Key icons")
// FH2 ANNA's interface (its include/fh2anna.h): ANNA, on the HUD, works with the pad only; its
// keys are ours, pressing its d-pad buttons (see "FH2 ANNA").
struct FH2AnnaApi {
    uint32_t size;
    int (*is_open)(void);
    int (*uses_key)(uint32_t vk);   // always 0 now
    void* add_entry;                // version 2 (not ours)
    void* hold_input;               // version 5 (not ours)
    uint16_t (*cinematic_button)(void);   // version 6: the button to hold for the cinematic key, while it drives
};
const FH2AnnaApi* g_anna = nullptr;
bool is_anna_action(const char* name) { return !std::strncmp(name, "Anna", 4); }
// ANNA Drone Mode's interface (its include/dronemode.h): while the drone flies, it asks for the
// keys' and the mouse's input each frame (see "Drone Mode").
struct DroneModeInput {
    uint32_t size;
    float forward, right, up;   // -1..1
    float yaw, pitch;           // radians now, + right / up
    float zoom;                 // the zoom times exp(zoom)
    int fast;
    uint32_t pressed;           // kDrone*
};
struct DroneModePicture {
    int kind;   // kDronePicture*
    int width, height;
    const uint32_t* pixels;   // premultiplied BGRA
};
struct DroneModeHelpKeys {
    uint32_t size;
    uint32_t (*shown)(void);   // 0: the pad's buttons; else changes with the pictures
    int (*pictures)(int item, DroneModePicture* out, int max);
};
struct DroneModeApi {
    uint32_t size;
    int (*is_flying)(void);
    void (*set_input)(void (*fn)(DroneModeInput* in, float dt));
    void (*set_help_keys)(const DroneModeHelpKeys* keys);
};
// Its button help's entries (DRONEMODE_HELP_*), and the pictures it draws (DRONEMODE_PICTURE_*).
enum { kDroneHelpMove, kDroneHelpLook, kDroneHelpDownUp, kDroneHelpFaster, kDroneHelpZoom, kDroneHelpRadio, kDroneHelpToCar,
       kDroneHelpLookAtCar, kDroneHelpHide, kDroneHelpExit };
enum { kDronePictureKey, kDronePictureMouse, kDronePictureWheel, kDronePictureLeft, kDronePictureRight };
std::atomic<uint32_t> g_keys_version{1};   // changes with any action's keys (for the drone's button help)
constexpr uint32_t kDroneExit = 0x01, kDroneToCar = 0x02, kDroneLookAtCar = 0x04, kDroneHelp = 0x08, kDroneRadioPrev = 0x10,
                   kDroneRadioNext = 0x20;
const DroneModeApi* g_drone = nullptr;
bool is_drone_action(const char* name) { return !std::strncmp(name, "Drone", 5); }
bool drone_flying() { return g_drone && g_drone->is_flying(); }
std::atomic<uint32_t> g_drone_pressed{0};                 // kDrone* pressed, for its next frame
std::atomic<float> g_drone_look_x{0}, g_drone_look_y{0};   // the mouse's move, in pixels
std::atomic<float> g_drone_zoom{0};                        // the wheel's
// What a key's press does to the drone (kDrone*), or 0.
uint32_t drone_press(const char* action) {
    static const struct { const char* name; uint32_t bit; } presses[] = {
        {"DroneExit", kDroneExit}, {"DroneToCar", kDroneToCar}, {"DroneLookAtCar", kDroneLookAtCar}, {"DroneHelp", kDroneHelp},
        {"RadioPrevious", kDroneRadioPrev}, {"RadioNext", kDroneRadioNext},
    };
    for (const auto& p : presses)
        if (!std::strcmp(action, p.name)) return p.bit;
    return 0;
}

float take(std::atomic<float>& a) { return a.exchange(0.f); }

void add(std::atomic<float>& a, float v) {
    float cur = a.load();
    while (!a.compare_exchange_weak(cur, cur + v)) {
    }
}
bool g_extra_options       = false;  // Extra Options' ini is there (see "Extra Options' keys")
bool g_kbm_rows            = false;  // the rows are on the Mods page (API 9), for Settings > Keyboard & Mouse
std::atomic<bool> g_icons_stale{false};   // the help bar on screen is to be shown again
std::array<std::array<int, kMaxKeys>, kActionCount> g_action_keys{};   // virtual keys, 0 = none
std::array<std::string, kActionCount> g_action_text;                   // as in the ini
std::wstring g_ini;

std::wstring widen(const std::string& s) { return std::wstring(s.begin(), s.end()); }   // ASCII

int vk_of(std::string k) {
    for (char& c : k) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (k.size() == 1 && ((k[0] >= 'a' && k[0] <= 'z') || (k[0] >= '0' && k[0] <= '9'))) return std::toupper(k[0]);
    if (k.size() > 2 && k[0] == '0' && k[1] == 'x') return static_cast<int>(std::strtol(k.c_str() + 2, nullptr, 16)) & 0xFF;
    if (k.size() == 4 && !k.compare(0, 3, "num") && k[3] >= '0' && k[3] <= '9') return VK_NUMPAD0 + (k[3] - '0');
    if (k.size() >= 2 && k.size() <= 3 && k[0] == 'f') {
        const int n = std::atoi(k.c_str() + 1);
        if (n >= 1 && n <= 24) return VK_F1 + n - 1;
    }
    static const struct { const char* name; int vk; } names[] = {
        {"left", VK_LEFT}, {"right", VK_RIGHT}, {"up", VK_UP}, {"down", VK_DOWN}, {"space", VK_SPACE},
        {"enter", VK_RETURN}, {"escape", VK_ESCAPE}, {"esc", VK_ESCAPE}, {"tab", VK_TAB}, {"backspace", VK_BACK},
        {"insert", VK_INSERT}, {"delete", VK_DELETE}, {"home", VK_HOME}, {"end", VK_END}, {"pageup", VK_PRIOR},
        {"pagedown", VK_NEXT}, {"shift", VK_SHIFT}, {"lshift", VK_LSHIFT}, {"rshift", VK_RSHIFT},
        {"ctrl", VK_CONTROL}, {"lctrl", VK_LCONTROL}, {"rctrl", VK_RCONTROL}, {"alt", VK_MENU},
        {"lalt", VK_LMENU}, {"ralt", VK_RMENU}, {"comma", VK_OEM_COMMA}, {"period", VK_OEM_PERIOD},
        {"minus", VK_OEM_MINUS}, {"equals", VK_OEM_PLUS}, {"semicolon", VK_OEM_1}, {"slash", VK_OEM_2},
        {"grave", VK_OEM_3}, {"lbracket", VK_OEM_4}, {"backslash", VK_OEM_5}, {"rbracket", VK_OEM_6},
        {"quote", VK_OEM_7},
    };
    for (const auto& n : names)
        if (k == n.name) return n.vk;
    return 0;
}

// An action's keys from their text ("Up I"); unknown names are logged and left out.
void set_action_keys(int i, const std::string& text) {
    std::array<int, kMaxKeys> keys{};
    int n = 0;
    std::size_t p = 0;
    while (n < kMaxKeys && (p = text.find_first_not_of(" ,", p)) != std::string::npos) {
        const std::size_t e = text.find_first_of(" ,", p);
        const std::string t = text.substr(p, e == std::string::npos ? std::string::npos : e - p);
        p = e;
        if (!_stricmp(t.c_str(), "none")) continue;
        if (const int vk = vk_of(t)) keys[n++] = vk;
        else g_api->log(g_mod, ("unknown key in [Keyboard]: " + t).c_str());
    }
    g_action_keys[i] = keys;
    ++g_keys_version;
    g_action_text[i] = n ? text : "None";
}

// Whether an Alt key is bound: then a tap of it mustn't open the window's system menu.
bool alt_bound() {
    for (const auto& keys : g_action_keys)
        for (int vk : keys)
            if (vk == VK_MENU || vk == VK_LMENU || vk == VK_RMENU) return true;
    return false;
}

void load_keys(const std::wstring& ini) {
    g_ini = ini;
    // Inis from before the keyboard get its section, so the keys can be found.
    wchar_t probe[4];
    if (!GetPrivateProfileSectionW(L"Keyboard", probe, 4, ini.c_str())) {
        std::string text = "\r\n; Keys for driving and the menus (also on Pause > Settings > Keyboard & Mouse). One or more keys\r\n"
                           "; each, separated by spaces: A-Z, 0-9, F1-F24, Num0-Num9 (with Num Lock on), Left, Right,\r\n"
                           "; Up, Down, Space, Enter, Escape, Tab, Backspace, Insert, Delete, Home, End, PageUp,\r\n"
                           "; PageDown, Shift, LShift, RShift, Ctrl, LCtrl, RCtrl, Alt, LAlt, RAlt, Comma, Period,\r\n"
                           "; Minus, Equals, Semicolon, Slash, Grave, LBracket, Backslash, RBracket, Quote, or a\r\n"
                           "; virtual-key code (0xNN). None leaves an action without a key. The driving keys only\r\n"
                           "; work while driving and the menu ones only in the menus, so a key can be one of each.\r\n"
                           "; Enabled=0 turns the keys off. KeyIcons=1: while you play with the keyboard, the\r\n"
                           "; help bar shows the keys instead of the controller's buttons.\r\n"
                           "[Keyboard]\r\nEnabled=1\r\nKeyIcons=1\r\n";
        for (const Action& a : kActions) text += std::string(a.name) + "=" + a.keys + "\r\n";
        if (HANDLE f = CreateFileW(ini.c_str(), FILE_APPEND_DATA, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            f != INVALID_HANDLE_VALUE) {
            DWORD n = 0;
            WriteFile(f, text.data(), static_cast<DWORD>(text.size()), &n, nullptr);
            CloseHandle(f);
        }
    }
    g_keyboard     = GetPrivateProfileIntW(L"Keyboard", L"Enabled", 1, ini.c_str()) != 0;
    g_key_icons_on = GetPrivateProfileIntW(L"Keyboard", L"KeyIcons", 1, ini.c_str()) != 0;
    for (int i = 0; i < kActionCount; ++i) {
        wchar_t buf[128] = {};
        GetPrivateProfileStringW(L"Keyboard", widen(kActions[i].name).c_str(), widen(kActions[i].keys).c_str(), buf, 128,
                                 ini.c_str());
        std::string text;
        for (const wchar_t* c = buf; *c; ++c) text += *c < 128 ? static_cast<char>(*c) : '?';
        set_action_keys(i, text);
    }
}

// ---- the keys on the Keyboard & Mouse page (API 9) ----------------------------------------------
// A choice row for each action: its key, from a list of the usual ones, or what the ini has if
// that's something else. A change takes effect at once and goes to the ini. The rows are the
// platform's Mods page rows, shown on a page of their own: see "Pause > Settings > Keyboard & Mouse".
// The platform's choice rows take up to 32 choices, so each row has these 28, then its default,
// the keys the presets give it, and what the ini has.
const char* const kKeyChoices[] = {
    "None", "W", "A", "S", "D", "Q", "E", "R", "F", "C", "Z", "X", "M", "P", "Space", "Enter", "Escape", "Left",
    "Right", "Up", "Down", "LShift", "RShift", "LCtrl", "RCtrl", "Tab", "Comma", "Period",
};
constexpr std::size_t kMaxChoices = 32;
std::array<std::vector<std::string>, kActionCount> g_row_choices;   // what each row offers
std::vector<std::pair<std::string, std::string>> g_row_names;       // our rows: label (as the XAML has it), key

// Text for the pages' XAML (as the platform escapes our labels).
std::string xml_escape(const std::string& s) {
    std::string o;
    for (char c : s) switch (c) {
            case '&': o += "&amp;"; break;
            case '<': o += "&lt;"; break;
            case '>': o += "&gt;"; break;
            case '"': o += "&quot;"; break;
            default: o += c;
        }
    return o;
}

// Presets: every action's keys at once. Each gives the keys that differ from kActions' defaults
// (which are the second, the mod's own layout); picking one sets all of them. They're all
// among kKeyChoices, so every row can show them. The QWERTZ and AZERTY ones keep the keys where
// they are on a QWERTY keyboard (the left trigger on the key left of X, AZERTY's Z Q S D).
struct PresetKey {
    const char* action;
    const char* keys;
};
struct Preset {
    const char* name;
    std::vector<PresetKey> keys;
};
const Preset kPresets[] = {
    {"WASD drive",
     {{"Gas", "W"}, {"Brake", "S"}, {"SteerLeft", "A"}, {"SteerRight", "D"}, {"ShiftUp", "E"}, {"ShiftDown", "Q"},
      {"Clutch", "LShift"}, {"Horn", "H"}}},
    {"WS drive, arrow steer", {}},
    {"Arrow drive",
     {{"Gas", "Up"}, {"Brake", "Down"}, {"SteerLeft", "Left"}, {"SteerRight", "Right"}, {"ShiftUp", "E"}, {"ShiftDown", "Q"},
      {"Clutch", "LShift"}, {"Horn", "H"}}},
    {"AZERTY",
     {{"Gas", "Z"}, {"Brake", "S"}, {"SteerLeft", "Q"}, {"SteerRight", "D"}, {"ShiftUp", "E"}, {"ShiftDown", "A"},
      {"Clutch", "LShift"}, {"Horn", "H"}, {"ButtonLT", "W"}, {"DroneForward", "Z"}, {"DroneLeft", "Q"},
      {"DroneDown", "A"}}},
    {"QWERTZ",
     {{"Gas", "W"}, {"Brake", "S"}, {"SteerLeft", "A"}, {"SteerRight", "D"}, {"ShiftUp", "E"}, {"ShiftDown", "Q"},
      {"Clutch", "LShift"}, {"Horn", "H"}, {"ButtonLT", "Y"}, {"ButtonY", "Z"}}},
};
constexpr int kPresetCount = sizeof kPresets / sizeof kPresets[0];   // and one more: "Custom"
int g_preset = kPresetCount;                                        // the row's value (UI thread)

const char* preset_keys(int p, int action) {
    for (const PresetKey& k : kPresets[p].keys)
        if (!std::strcmp(k.action, kActions[action].name)) return k.keys;
    return kActions[action].keys;
}

// The preset the keys are, or kPresetCount (Custom). `prefer` first, when it's one.
int matching_preset(int prefer) {
    auto is = [](int p) {
        for (int i = 0; i < kActionCount; ++i)
            if (_stricmp(preset_keys(p, i), g_action_text[i].c_str())) return false;
        return true;
    };
    if (prefer >= 0 && prefer < kPresetCount && is(prefer)) return prefer;
    for (int p = 0; p < kPresetCount; ++p)
        if (is(p)) return p;
    return kPresetCount;
}

void refresh_row(const char* key);   // the open page shows the row's new value (see "Keyboard & Mouse")

// A key row's value: its keys' place in its choices, or -1.
int row_value(int i) {
    for (std::size_t c = 0; c < g_row_choices[i].size(); ++c)
        if (!_stricmp(g_row_choices[i][c].c_str(), g_action_text[i].c_str())) return static_cast<int>(c);
    return -1;
}

void apply_preset(int p) {
    for (int i = 0; i < kActionCount; ++i) {
        if (!_stricmp(preset_keys(p, i), g_action_text[i].c_str())) continue;
        set_action_keys(i, preset_keys(p, i));
        WritePrivateProfileStringW(L"Keyboard", widen(kActions[i].name).c_str(), widen(g_action_text[i]).c_str(), g_ini.c_str());
        const int v = row_value(i);
        if (v < 0) continue;   // no row (Nitro, Horn without their mods)
        const std::string key = std::string("Key_") + kActions[i].name;
        g_api->set_setting(g_mod, key.c_str(), v);
        refresh_row(key.c_str());
    }
    g_api->log(g_mod, (std::string("keys set to the ") + kPresets[p].name + " preset").c_str());
}

// The free look sensitivity slider: a change takes effect at once and goes to the ini.
void look_speed_changed(void*, const char*, float value) {
    const int percent = std::max(0, std::min(100, static_cast<int>(std::lround(value))));
    g_cfg.look_percent = percent;
    WritePrivateProfileStringW(L"Mouse", L"FreeLookSensitivity", std::to_wstring(percent).c_str(), g_ini.c_str());
}

void keyboard_row_changed(void*, const char* key, int value) {
    g_icons_stale = true;
    if (!std::strcmp(key, "Keyboard")) {
        g_keyboard = value != 0;
        WritePrivateProfileStringW(L"Keyboard", L"Enabled", value ? L"1" : L"0", g_ini.c_str());
        return;
    }
    if (!std::strcmp(key, "KeyIcons")) {
        g_key_icons_on = value != 0;
        WritePrivateProfileStringW(L"Keyboard", L"KeyIcons", value ? L"1" : L"0", g_ini.c_str());
        return;
    }
    if (!std::strcmp(key, "HoverSelects") || !std::strcmp(key, "FreeLook")) {
        const bool hover = key[0] == 'H';
        (hover ? g_cfg.hover_focus : g_cfg.free_look) = value != 0;
        WritePrivateProfileStringW(L"Mouse", hover ? L"HoverSelects" : L"FreeLook", value ? L"1" : L"0", g_ini.c_str());
        return;
    }
    if (!std::strcmp(key, "Preset")) {
        g_preset = value;
        if (value >= 0 && value < kPresetCount) apply_preset(value);   // Custom changes nothing
        return;
    }
    for (int i = 0; i < kActionCount; ++i) {
        if (std::strcmp(key, (std::string("Key_") + kActions[i].name).c_str())) continue;
        if (value < 0 || value >= static_cast<int>(g_row_choices[i].size())) return;
        set_action_keys(i, g_row_choices[i][value]);
        WritePrivateProfileStringW(L"Keyboard", widen(kActions[i].name).c_str(), widen(g_action_text[i]).c_str(),
                                   g_ini.c_str());
        if (const int p = matching_preset(g_preset); p != g_preset) {   // Custom now, or another preset
            g_preset = p;
            g_api->set_setting(g_mod, "Preset", p);
            refresh_row("Preset");
        }
        return;
    }
}

void add_keyboard_rows() {
    if (g_api->api_version < 9) return;
    auto add_row = [](const FH2Setting& s) {
        if (g_api->add_setting(g_mod, &s)) g_row_names.emplace_back(xml_escape(s.label), s.key);   // as the page has it
    };
    FH2Setting on{};
    on.size        = sizeof on;
    on.key         = "Keyboard";
    on.label       = lang::tr("Keyboard controls");
    on.description = lang::tr("Drive with the keyboard and move around the menus with it. The rows below set the keys.");
    on.value       = g_keyboard;
    on.on_change   = keyboard_row_changed;
    add_row(on);
    FH2Setting icons{};
    icons.size        = sizeof icons;
    icons.key         = "KeyIcons";
    icons.label       = lang::tr("Keyboard icons");
    icons.description = lang::tr("While you play with the keyboard, the help bar shows its keys instead of the controller's buttons. A button on the controller brings the buttons back.");
    icons.value       = g_key_icons_on;
    icons.on_change   = keyboard_row_changed;
    add_row(icons);
    FH2Setting hover{};
    hover.size        = sizeof hover;
    hover.key         = "HoverSelects";
    hover.label       = lang::tr("Mouse hover selects");
    hover.description = lang::tr("Moving the mouse over a button, list row or slider selects it. Off, only a click does.");
    hover.value       = g_cfg.hover_focus;
    hover.on_change   = keyboard_row_changed;
    add_row(hover);
    FH2Setting look{};
    look.size        = sizeof look;
    look.key         = "FreeLook";
    look.label       = lang::tr("Mouse free look");
    look.description = lang::tr("While driving, moving the mouse looks around. The cursor stays in the game's window: Alt+Tab to leave it.");
    look.value       = g_cfg.free_look;
    look.on_change   = keyboard_row_changed;
    add_row(look);
    FH2Setting speed{};
    speed.size         = sizeof speed;
    speed.key          = "FreeLookSensitivity";
    speed.label        = lang::tr("Mouse free look sensitivity");
    speed.description  = lang::tr("How far the camera turns for the mouse's movement while you look around. 50 is the usual, 100 twice as far, 0 not at all.");
    speed.kind         = FH2_SETTING_SLIDER;
    speed.min          = 0;
    speed.max          = 100;
    speed.step         = 5;
    speed.slider_value = static_cast<float>(g_cfg.look_percent.load());
    speed.decimals     = 0;
    speed.on_slide     = look_speed_changed;
    add_row(speed);
    g_kbm_rows = true;
    static std::vector<const char*> preset_names;
    for (const Preset& p : kPresets) preset_names.push_back(lang::tr(p.name));
    preset_names.push_back(lang::tr("Custom"));
    g_preset = matching_preset(-1);
    FH2Setting preset{};
    preset.size         = sizeof preset;
    preset.key          = "Preset";
    preset.label        = lang::tr("Keyboard preset");
    preset.description  = lang::tr("Sets every key below at once. WS drive, arrow steer is the mod's own layout; AZERTY and QWERTZ put the keys where WASD's are on those keyboards. Changing a key makes it Custom.");
    preset.kind         = FH2_SETTING_CHOICE;
    preset.choices      = preset_names.data();
    preset.choice_count = static_cast<uint32_t>(preset_names.size());
    preset.value        = g_preset;
    preset.on_change    = keyboard_row_changed;
    add_row(preset);
    static std::string keys[kActionCount], labels[kActionCount];
    const bool nitro = GetFileAttributesW((std::wstring(g_api->azimuth_dir) + L"\\fh2-mods\\FnFNitro.dll").c_str()) !=
                       INVALID_FILE_ATTRIBUTES;
    const bool horn = GetFileAttributesW((std::wstring(g_api->azimuth_dir) + L"\\fh2-mods\\HonkHonk.dll").c_str()) !=
                      INVALID_FILE_ATTRIBUTES;
    const bool anna = GetFileAttributesW((std::wstring(g_api->azimuth_dir) + L"\\fh2-mods\\FH2ANNA.dll").c_str()) !=
                      INVALID_FILE_ATTRIBUTES;
    const bool drone = anna && GetFileAttributesW((std::wstring(g_api->azimuth_dir) + L"\\fh2-mods\\ANNADroneMode.dll").c_str()) !=
                                   INVALID_FILE_ATTRIBUTES;
    for (int i = 0; i < kActionCount; ++i) {
        const Action& a = kActions[i];
        if (!std::strcmp(a.name, "Nitro") && !nitro) continue;
        if (!std::strcmp(a.name, "Horn") && !horn) continue;
        if (a.extra && !g_extra_options) continue;
        if (is_anna_action(a.name) && !anna) continue;
        if (is_drone_action(a.name) && !drone) continue;
        auto& choices = g_row_choices[i];
        auto offer    = [&](const std::string& c) {
            if (choices.size() < kMaxChoices &&
                std::none_of(choices.begin(), choices.end(), [&](const std::string& o) { return !_stricmp(o.c_str(), c.c_str()); }))
                choices.push_back(c);
        };
        for (const char* c : kKeyChoices) offer(c);
        offer(a.keys);
        offer(g_action_text[i]);   // the ini's own
        for (int p = 0; p < kPresetCount; ++p) offer(preset_keys(p, i));
        const int value = row_value(i);
        std::vector<const char*> names;
        for (const std::string& c : choices) names.push_back(c.c_str());
        keys[i]   = std::string("Key_") + a.name;
        labels[i] = lang::tr(a.label);
        FH2Setting s{};
        s.size         = sizeof s;
        s.key          = keys[i].c_str();
        s.label        = labels[i].c_str();
        s.description  = lang::tr(is_drone_action(a.name)
                             ? "ANNA Drone Mode's key while flying the drone. While it flies only its keys (and the radio's) count, so it can be a driving or menu key too."
                         : !std::strcmp(a.name, "AnnaCinematic")
                             ? "FH2 ANNA's key while its autodrive drives: turns the cinematic camera on and off, holding the controller button ANNA's ini gives it (LB). It does nothing otherwise, so it can be a driving key too."
                         : is_anna_action(a.name)
                             ? "FH2 ANNA's key while driving, pressing the controller's d-pad for it. The open key is ANNA's whenever you drive, the others only while ANNA's menu is open: a driving key on the same key waits till then."
                         : a.menu  ? "The key for this in the menus. It does nothing while driving, so it can be a driving key too."
                         : a.extra ? "The key for this while driving: Extra Options' own, pressed on the controller button its ini gives it. It does nothing in the menus, so it can be a menu key too."
                                   : "The key for this while driving. It does nothing in the menus, so it can be a menu key too.");
        s.kind         = FH2_SETTING_CHOICE;
        s.choices      = names.data();
        s.choice_count = static_cast<uint32_t>(names.size());
        s.value        = value;
        s.on_change    = keyboard_row_changed;
        add_row(s);
    }
}

void load_settings() {
    g_dir = std::wstring(g_api->azimuth_dir) + L"\\FH2Mouse";
    CreateDirectoryW(g_dir.c_str(), nullptr);
    const std::wstring ini = g_dir + L"\\FH2Mouse.ini";
    if (GetFileAttributesW(ini.c_str()) == INVALID_FILE_ATTRIBUTES) {
        const char text[] =
            "; FH2 Mouse & Keyboard: a mouse cursor for Forza Horizon 2's menus, and keys to drive with. 0 = off, 1 = on.\r\n"
            "; Left click = A (or select what's clicked), right click = B, wheel = up/down,\r\n"
            "; mouse side buttons = LB/RB, middle click = right stick click. Help bar buttons\r\n"
            "; are clickable. Map: drag to pan, click to select or set a waypoint, wheel to zoom.\r\n"
            "; Garage: drag empty space to turn the camera, wheel to zoom; W A S D move a decal.\r\n"
            "; Photo mode: drag to turn, W A S D to move, Q / E down / up, wheel to zoom.\r\n"
            "; Driving: move the mouse to look around.\r\n"
            "[Mouse]\r\n"
            "Enabled=1\r\n"
            "\r\n"
            "; Moving the mouse over a button, list row or slider selects it.\r\n"
            "HoverSelects=1\r\n"
            "\r\n"
            "; Hide the cursor after this many seconds without moving the mouse (0 = never).\r\n"
            "HideAfterSeconds=3\r\n"
            "\r\n"
            "; The game's frame rate. 0 = measure it (the log says what it found). Set it (30, 60,\r\n"
            "; 120, 144...) only if photo mode or the garage camera turns too fast or too slow.\r\n"
            "FrameRate=0\r\n"
            "\r\n"
            "; While driving, the mouse looks around (it stays in the window: Alt+Tab to leave it).\r\n"
            "FreeLook=1\r\n"
            "\r\n"
            "; How fast free look turns, 0 to 100: 50 is the usual, 100 twice as fast.\r\n"
            "FreeLookSensitivity=50\r\n"
            "\r\n"
            "; Log what is under the cursor to fh2-mods\\platform.log.\r\n"
            "Debug=0\r\n";
        HANDLE f = CreateFileW(ini.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (f != INVALID_HANDLE_VALUE) {
            DWORD n = 0;
            WriteFile(f, text, sizeof text - 1, &n, nullptr);
            CloseHandle(f);
        }
    }
    g_cfg.enabled     = GetPrivateProfileIntW(L"Mouse", L"Enabled", 1, ini.c_str()) != 0;
    g_cfg.hover_focus = GetPrivateProfileIntW(L"Mouse", L"HoverSelects", 1, ini.c_str()) != 0;
    g_cfg.hide_ms     = GetPrivateProfileIntW(L"Mouse", L"HideAfterSeconds", 3, ini.c_str()) * 1000u;
    g_cfg.debug       = GetPrivateProfileIntW(L"Mouse", L"Debug", 0, ini.c_str()) != 0;
    g_cfg.free_look   = GetPrivateProfileIntW(L"Mouse", L"FreeLook", 1, ini.c_str()) != 0;
    g_cfg.look_percent = std::max(0, std::min(100, static_cast<int>(GetPrivateProfileIntW(L"Mouse", L"FreeLookSensitivity", 50, ini.c_str()))));
    g_cfg.frame_rate  = std::min(1000, static_cast<int>(GetPrivateProfileIntW(L"Mouse", L"FrameRate", 0, ini.c_str())));
    load_keys(ini);
}

void logf(const char* fmt, ...) {
    char line[512];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    g_api->log(g_mod, line);
}

// ---- input from the window (its own thread) ----------------------------------------------------

enum class Ev { Move, LDown, LUp, RDown, RUp, Wheel, X1Down, X1Up, X2Down, X2Up, MDown, MUp };
struct Event {
    Ev type;
    int a;   // wheel delta
};

std::mutex g_mx;
std::vector<Event> g_events;   // guarded by g_mx
std::atomic<HWND> g_hwnd{nullptr};
WNDPROC g_orig_proc = nullptr;
std::atomic<int> g_cx{-1}, g_cy{-1};       // the cursor, in client pixels
std::atomic<DWORD> g_last_move{0};          // GetTickCount of the last real movement
std::atomic<DWORD> g_last_frame{0};         // GetTickCount of the last UI frame
std::atomic<bool> g_framed{false};          // a UI frame has run (the game is up)
float g_ui_dt = 1.f / 30;                   // the last UI frame's length, in seconds (UI thread)
std::atomic<bool> g_shown{false};           // what the window was last told
std::atomic<bool> g_looking{false};         // the mouse turns the driving camera (see "free look")
HCURSOR g_arrow = nullptr;

bool game_active() { return g_framed.load() && GetTickCount() - g_last_frame.load() < 1000; }

// The game runs at whatever frame rate Azimuth or a patch sets. What's done each frame (mouse
// movement as stick values, waits for the menus) was tuned at 120, and is scaled to the frame's
// real length.
constexpr float kTunedDt = 1.f / 120;
constexpr float frames(int n) { return n * kTunedDt; }   // n frames at 120 a second, in seconds

// The time since the last call, in seconds (kept within 1/1000 .. 1/10 s), or one frame at
// FrameRate when that's set.
struct FrameClock {
    LARGE_INTEGER last{};
    float tick() {
        LARGE_INTEGER now, f;
        QueryPerformanceCounter(&now);
        QueryPerformanceFrequency(&f);
        const float s = last.QuadPart ? static_cast<float>(now.QuadPart - last.QuadPart) / f.QuadPart : 1.f / 30;
        last = now;
        if (g_cfg.frame_rate > 0) return 1.f / g_cfg.frame_rate;
        return std::max(0.001f, std::min(0.1f, s));
    }
};

bool cursor_wanted() {
    if (!g_cfg.enabled || !game_active() || !g_last_move.load() || g_looking.load()) return false;
    return g_cfg.hide_ms == 0 || GetTickCount() - g_last_move.load() < g_cfg.hide_ms;
}

void push(Ev type, int a = 0) {
    std::lock_guard<std::mutex> lock(g_mx);
    if (g_events.size() < 256) g_events.push_back({type, a});
}

// The cursor's shape: the window is asked on every mouse move. Azimuth hides it there.
LRESULT CALLBACK wndproc(HWND h, UINT msg, WPARAM w, LPARAM l) {
    // Alt alone opens the window's menu, which stalls the game: not while Alt is a key of ours.
    if (msg == WM_SYSCOMMAND && (w & 0xFFF0) == SC_KEYMENU && l == 0 && g_keyboard && alt_bound()) return 0;
    if (msg == WM_SETCURSOR && LOWORD(l) == HTCLIENT && g_cfg.enabled && game_active()) {
        const bool show = cursor_wanted();
        g_shown = show;
        SetCursor(show ? g_arrow : nullptr);
        return TRUE;
    }
    return CallWindowProcW(g_orig_proc, h, msg, w, l);
}

// Mouse input. Azimuth keeps the usual mouse messages from its window, so the cursor and the
// buttons are read on the UI thread each frame (poll_mouse), while the game window is in front
// and the cursor is over it. Only the wheel, which can't be read that way, comes from a
// low-level mouse hook; Windows drops such a hook when it's slow to answer, so it's set again
// every few seconds, on a thread of its own that does nothing else. Nothing is swallowed.
LRESULT CALLBACK mouse_hook(int code, WPARAM w, LPARAM l) {
    if (code == HC_ACTION && w == WM_MOUSEWHEEL) {
        HWND h = g_hwnd.load();
        if (h && g_cfg.enabled && game_active() && GetForegroundWindow() == h) {
            const auto* m = reinterpret_cast<const MSLLHOOKSTRUCT*>(l);
            g_last_move = GetTickCount();
            push(Ev::Wheel, static_cast<short>(HIWORD(m->mouseData)));
        }
    }
    return CallNextHookEx(nullptr, code, w, l);
}

HMODULE g_self = nullptr;

DWORD WINAPI hook_thread(LPVOID) {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
    HHOOK hook = nullptr;
    for (;;) {
        if (hook) UnhookWindowsHookEx(hook);
        hook = SetWindowsHookExW(WH_MOUSE_LL, mouse_hook, g_self, 0);
        if (!hook) {
            logf("couldn't set the mouse hook (error %lu)", GetLastError());
            return 0;
        }
        const DWORD until = GetTickCount() + 3000;
        for (DWORD now = GetTickCount(); now < until; now = GetTickCount()) {
            MsgWaitForMultipleObjects(0, nullptr, FALSE, until - now, QS_ALLINPUT);
            MSG msg;
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) DispatchMessageW(&msg);
        }
    }
}

// The cursor and the buttons, once a frame on the UI thread.
void poll_mouse() {
    static bool held[5] = {};
    static const int vk[5] = {VK_LBUTTON, VK_RBUTTON, VK_XBUTTON1, VK_XBUTTON2, VK_MBUTTON};
    static const Ev press[5] = {Ev::LDown, Ev::RDown, Ev::X1Down, Ev::X2Down, Ev::MDown};
    static const Ev lift[5]  = {Ev::LUp, Ev::RUp, Ev::X1Up, Ev::X2Up, Ev::MUp};
    HWND h = g_hwnd.load();
    const bool front = h && GetForegroundWindow() == h;
    POINT p{};
    RECT r{};
    bool inside = false;
    if (front && GetCursorPos(&p) && ScreenToClient(h, &p) && GetClientRect(h, &r)) inside = PtInRect(&r, p) != 0;
    // While the mouse looks around (free look), its moves are the camera's, not the cursor's.
    if (inside && !g_looking.load() && (p.x != g_cx.load() || p.y != g_cy.load())) {
        g_cx        = p.x;
        g_cy        = p.y;
        g_last_move = GetTickCount();
        push(Ev::Move);
    }
    for (int i = 0; i < 5; ++i) {
        const bool down = front && (GetAsyncKeyState(vk[i]) & 0x8000);
        if (down == held[i]) continue;
        if (down && !inside) continue;   // a press elsewhere isn't ours
        held[i] = down;
        if (down) g_last_move = GetTickCount();
        push(down ? press[i] : lift[i]);
    }
}

// The game window: Azimuth's, of this process.
HWND find_game_window() {
    for (HWND h = FindWindowExW(nullptr, nullptr, L"Azimuth.Game.Win32", nullptr); h;
         h = FindWindowExW(nullptr, h, L"Azimuth.Game.Win32", nullptr)) {
        DWORD pid = 0;
        GetWindowThreadProcessId(h, &pid);
        if (pid == GetCurrentProcessId()) return h;
    }
    return nullptr;
}

DWORD WINAPI window_thread(LPVOID) {
    for (;;) {
        Sleep(250);
        HWND h = find_game_window();
        if (!h || h == g_hwnd.load()) {
            // Tell the window when the cursor should hide (or show) without the mouse moving.
            HWND cur = g_hwnd.load();
            if (cur && cursor_wanted() != g_shown.load()) {
                POINT p;
                GetCursorPos(&p);
                if (WindowFromPoint(p) == cur) PostMessageW(cur, WM_SETCURSOR, reinterpret_cast<WPARAM>(cur), MAKELPARAM(HTCLIENT, WM_MOUSEMOVE));
            }
            continue;
        }
        HWND old = g_hwnd.exchange(h);
        if (old && IsWindow(old) && g_orig_proc) SetWindowLongPtrW(old, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(g_orig_proc));
        g_orig_proc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(h, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&wndproc)));
        RECT r{};
        GetClientRect(h, &r);
        logf("watching the game window (%ldx%ld)", r.right, r.bottom);
    }
}

// ---- the game's UI (UI thread only) --------------------------------------------------------------

void* type_of(uint8_t* obj) {
    void* t = at<void*>(obj, OBJ_TYPE);
    if (!t) t = reinterpret_cast<TypeOfFn>((*reinterpret_cast<void***>(obj))[3])(obj);
    return t;
}

bool is(void* type, uintptr_t base_rva) { return type && game<IsTypeFn>(RVA_IS_TYPE)(type, g_base + base_rva); }

const char* rtti_name(const void* obj) {
    auto* vt  = *reinterpret_cast<uint8_t* const*>(obj);
    auto* col = *reinterpret_cast<uint8_t* const*>(vt - 8);
    if (col < g_base || col >= g_base + 0x3A18000) return "?";
    return reinterpret_cast<const char*>(g_base + at<uint32_t>(col, 12) + 0x10);
}

void release(uint8_t* obj) {
    if (obj && InterlockedDecrement(reinterpret_cast<volatile LONG*>(obj + 0x10)) == 0) game<FreeFn>(RVA_OBJECT_FREE)(obj);
}

uint8_t* window_manager() { return game<GetWmFn>(RVA_GET_WM)(); }

uint8_t* focused_object() {
    void** iface = game<FocusedFn>(RVA_FOCUSED)();
    if (!iface) return nullptr;
    return reinterpret_cast<uint8_t* (*)(void*)>((*reinterpret_cast<void***>(iface))[0])(iface);
}

void focus(uint8_t* el) {
    void** iface = reinterpret_cast<void**>(el + UIELEMENT_INPUT);
    reinterpret_cast<void (*)(void*)>((*reinterpret_cast<void***>(iface))[3])(iface);   // the reference Focus takes
    void* out[4] = {};
    game<FocusFn>(RVA_FOCUS)(out, iface);
    release(static_cast<uint8_t*>(out[1]));
}

void send_key_now(int key, bool up) {
    uint8_t* wm = window_manager();
    if (!wm) return;
    const int code = game<KeyCodeFn>(RVA_KEY_CODE)(key);
    auto fn = *reinterpret_cast<KeyFn*>(*reinterpret_cast<uint8_t**>(wm) + WM_KEY_SLOT);
    fn(wm, up ? 1 : 0, code, 0, 0, 0);
}

// Key events go to the window manager one per frame, the way the pad's do: menus that look at
// what's held each frame miss a press and release in the same frame.
struct KeyEvent {
    int key;
    bool up;
    float wait;   // seconds to wait before this one (at least a frame, if any)
};
std::vector<KeyEvent> g_keys;   // UI thread only

void send_key(int key, bool up, float wait = 0) {
    if (g_keys.size() < 64) g_keys.push_back({key, up, wait});
}

void flush_one_key() {
    if (g_keys.empty()) return;
    if (g_keys.front().wait > 0) {
        g_keys.front().wait -= g_ui_dt;
        return;
    }
    const KeyEvent k = g_keys.front();
    g_keys.erase(g_keys.begin());
    if (g_cfg.debug) logf("key %d %s", k.key, k.up ? "up" : "down");
    send_key_now(k.key, k.up);
}

uint8_t* g_hovered = nullptr;   // compared only, never followed: it may be gone by now
bool g_a_held = false, g_b_held = false, g_lb_held = false, g_rb_held = false;

template <class F> F vfn(void* obj, uintptr_t off) { return *reinterpret_cast<F*>(*static_cast<uint8_t**>(obj) + off); }

// The cursor in the UI's own pixels (the render size, which the window may stretch).
bool cursor_ui(float pt[2]) {
    HWND h       = g_hwnd.load();
    uint8_t* app = at<uint8_t*>(g_base, RVA_APPLICATION);
    if (!h || !app) return false;
    RECT r{};
    GetClientRect(h, &r);
    if (r.right <= 0 || r.bottom <= 0) return false;
    float w = at<float>(app, 0x50), hgt = at<float>(app, 0x54);
    if (w <= 0 || hgt <= 0) w = static_cast<float>(r.right), hgt = static_cast<float>(r.bottom);
    pt[0] = g_cx.load() * w / r.right;
    pt[1] = g_cy.load() * hgt / r.bottom;
    return true;
}

float opacity(uint8_t* v) {
    if (!((at<uint32_t>(v, VISUAL_FLAGS) >> 3) & 1)) return 1.f;
    uint8_t* boxed = at<uint8_t*>(g_base, RVA_OPACITY_DEFAULT);
    if (at<uint8_t>(g_base, RVA_OPACITY_STORED)) {
        uint8_t* o = game<GetValueFn>(RVA_GET_VALUE)(v, at<int>(g_base, RVA_OPACITY_STORED + 4));
        if (o && o != game<UnsetFn>(RVA_UNSET_VALUE)()) boxed = o;
    }
    return boxed ? at<float>(boxed, 0x28) : 1.f;
}

// Where `v` (a laid-out UIElement) is laid out, in `root`'s pixels.
bool layout_bounds(uint8_t* v, uint8_t* root, float out[4]) {
    const uint32_t flags = at<uint32_t>(v, VISUAL_FLAGS);
    if (!((flags >> 5) & 1) && !((flags >> 6) & 1)) return false;
    const float w = at<float>(v, RENDER_SIZE), h = at<float>(v, RENDER_SIZE + 4);
    if (!(w > 0) || !(h > 0)) return false;
    alignas(16) float matrix[16] = {};
    void* gt          = nullptr;
    const bool simple = game<ToAncestorFn>(RVA_TO_ANCESTOR)(v, root, 0, &gt, matrix);
    const float corners[4][2] = {{0, 0}, {w, 0}, {0, h}, {w, h}};
    bool ok = simple || gt;
    out[0] = out[1] = 1e30f;
    out[2] = out[3] = -1e30f;
    for (int i = 0; ok && i < 4; ++i) {
        float p[2] = {corners[i][0], corners[i][1]};
        if (simple) game<TransformPtFn>(RVA_TRANSFORM_PT)(matrix, p, corners[i]);
        else ok = vfn<GtFn>(gt, VT_GT_TRANSFORM)(gt, corners[i], p);
        out[0] = std::min(out[0], p[0]), out[1] = std::min(out[1], p[1]);
        out[2] = std::max(out[2], p[0]), out[3] = std::max(out[3], p[1]);
    }
    release(static_cast<uint8_t*>(gt));
    if (ok) return true;
    // An animated transform on the way up: the layout offsets alone (VisualOffset, +0x90).
    float x = 0, y = 0;
    for (uint8_t* e = v; e && e != root; e = at<uint8_t*>(e, VISUAL_PARENT)) {
        if (!is(type_of(e), TYPE_VISUAL)) return false;
        x += at<float>(e, 0x90), y += at<float>(e, 0x94);
    }
    out[0] = x, out[1] = y, out[2] = x + w, out[3] = y + h;
    return true;
}

// Many menus are drawn tilted (the "screen tilt": a Grid with ZPanel.Transform, rotated 9 degrees
// back about its own bottom edge), seen in perspective from the middle of the screen. The layout
// doesn't know; this maps a laid-out point to where it's drawn. kTiltDistance was measured on
// the pause menu and the tuning page (in 1080p UI pixels).
constexpr uintptr_t RVA_ZPANEL_TRANSFORM_DP = 0x36A5460;   // ZPanel::TransformProperty (attached)
constexpr float kTiltDistance = 2184.f;
constexpr float kTiltSin = 0.15643447f, kTiltCos = 0.98768834f;   // 9 degrees

float ui_height();
float ui_width();

// The bottom edge of the nearest tilted ancestor of `v`, in root pixels; false if none.
bool tilt_pivot(uint8_t* v, uint8_t* root, float* bottom) {
    void* dp = at<void*>(g_base, RVA_ZPANEL_TRANSFORM_DP);
    if (!dp) return false;
    uint8_t* unset = game<UnsetFn>(RVA_UNSET_VALUE)();
    for (uint8_t* e = v; e && e != root; e = at<uint8_t*>(e, VISUAL_PARENT)) {
        if (!is(type_of(e), TYPE_UIELEMENT)) break;
        uint8_t* boxed = nullptr;
        game<DpGetValueFn>(RVA_DP_GET_VALUE)(e, &boxed, dp);
        const bool tilted = boxed && boxed != unset;
        release(boxed);
        if (!tilted) continue;
        float r[4];
        if (!layout_bounds(e, root, r)) return false;
        *bottom = r[3];
        return true;
    }
    return false;
}

void tilt_point(float bottom, float p[2]) {
    const float scale = ui_height() / 1080.f;
    const float h = bottom - p[1];
    const float s = kTiltDistance * scale / (kTiltDistance * scale + h * kTiltSin);
    const float y = bottom - h * kTiltCos;
    p[0] = ui_width() / 2 + (p[0] - ui_width() / 2) * s;
    p[1] = ui_height() / 2 + (y - ui_height() / 2) * s;
}

// Where `v` is drawn, in `root`'s pixels.
bool bounds(uint8_t* v, uint8_t* root, float out[4]) {
    if (!layout_bounds(v, root, out)) return false;
    float bottom;
    if (!tilt_pivot(v, root, &bottom)) return true;
    float a[2] = {out[0], out[1]}, b[2] = {out[2], out[3]}, c[2] = {out[0], out[3]}, d[2] = {out[2], out[1]};
    for (float* q : {a, b, c, d}) tilt_point(bottom, q);
    out[0] = std::min(a[0], c[0]), out[2] = std::max(b[0], d[0]);
    out[1] = std::min(a[1], d[1]), out[3] = std::max(b[1], c[1]);
    return true;
}

constexpr uintptr_t kItemTypes[] = {TYPE_LIST_BOX_ITEM, TYPE_COMBO_ITEM, TYPE_TAB_ITEM, TYPE_TREE_ITEM,
                                    TYPE_MENU_ITEM, TYPE_SLIDER, TYPE_SPINNER};

// 0: not a control the pad selects; 1: a RepeatButton (a slider's or spinner's arrow); 2: one.
int control_kind(void* t) {
    if (is(t, TYPE_HELP_BUTTON)) return 2;
    for (uintptr_t k : kItemTypes)
        if (is(t, k)) return 2;
    if (is(t, TYPE_BUTTON_BASE)) return is(t, TYPE_REPEAT_BUTTON) ? 1 : 2;
    return 0;
}

// The control the pad would select, at or above `el` in the visual tree.
uint8_t* control_at(uint8_t* el) {
    uint8_t* repeat = nullptr;
    for (int depth = 0; el && depth < 64; ++depth) {
        void* t = type_of(el);
        if (!is(t, TYPE_VISUAL)) break;
        const int kind = control_kind(t);
        if (kind == 2) return el;
        if (kind == 1 && !repeat) repeat = el;
        el = at<uint8_t*>(el, VISUAL_PARENT);
    }
    return repeat;
}

// A menu is up when the focus is on something the pad selects.
bool menu_active() {
    uint8_t* f = focused_object();
    return f && control_at(f) != nullptr;
}

// The topmost control under the cursor: the visual tree in drawing order, so later wins.
// Inside a control that's hit, a control in it wins too, except its RepeatButtons.
// Failing that, the nearest within kNear pixels: the menus are drawn slightly tilted, so what's
// on screen can stand a few pixels off the layout.
constexpr float kNear = 16.f;
bool bool_property(uint8_t* el, uintptr_t dp_rva);
bool is_adjustable(uint8_t* el);
struct Search {
    uint8_t* root;
    float pt[2];
    uint8_t* best;
    int visited;
    uint8_t* near_el = nullptr;
    float near_dist = kNear;
    uint8_t* focusable = nullptr;   // the topmost hit that can take the focus (under a tuning row's
                                    // slider lies one that can't: its history)
};

void search(Search& s, uint8_t* v, int depth, bool inside_control) {
    if (!v || depth > 80 || ++s.visited > 20000) return;
    void* t = type_of(v);
    if (!is(t, TYPE_VISUAL) || opacity(v) <= 0.01f) return;
    if (is(t, TYPE_UIELEMENT)) {
        const int kind = control_kind(t);
        if (kind && !(kind == 1 && inside_control)) {
            float r[4];
            if (bounds(v, s.root, r)) {
                if (s.pt[0] >= r[0] && s.pt[0] < r[2] && s.pt[1] >= r[1] && s.pt[1] < r[3]) {
                    s.best         = v;
                    inside_control = true;
                    if (bool_property(v, RVA_FOCUSABLE_DP)) s.focusable = v;
                } else {
                    const float dx = std::max({r[0] - s.pt[0], 0.f, s.pt[0] - r[2]});
                    const float dy = std::max({r[1] - s.pt[1], 0.f, s.pt[1] - r[3]});
                    if (std::max(dx, dy) < s.near_dist) s.near_el = v, s.near_dist = std::max(dx, dy);
                }
            }
        }
    }
    const int n = vfn<CountFn>(v, VT_CHILD_COUNT)(v);
    for (int i = 0; i < n && i < 4096; ++i) search(s, vfn<ChildFn>(v, VT_CHILD)(v, i), depth + 1, inside_control);
}

uint8_t* active_window() {
    uint8_t* wm = window_manager();
    return wm ? at<uint8_t*>(wm, WM_ACTIVE_WINDOW) : nullptr;
}

uint8_t* hovered_control() {
    Search s{active_window(), {}, nullptr, 0, nullptr, kNear};
    if (!s.root || !cursor_ui(s.pt)) return nullptr;
    search(s, s.root, 0, false);
    if (s.focusable && s.best && !bool_property(s.best, RVA_FOCUSABLE_DP) && is_adjustable(s.best)) s.best = s.focusable;
    if (!s.best) s.best = s.near_el;
    if (g_cfg.debug) {
        static DWORD last = 0;
        if (GetTickCount() - last > 2000) {
            last = GetTickCount();
            logf("hit test at (%.0f,%.0f): %d visuals, found %s", s.pt[0], s.pt[1], s.visited, s.best ? rtti_name(s.best) : "none");
        }
    }
    return s.best;
}

// Debug: the topmost element of any kind under the cursor, and its ancestors.
void deepest(Search& s, uint8_t* v, int depth) {
    if (!v || depth > 80 || ++s.visited > 20000) return;
    void* t = type_of(v);
    if (!is(t, TYPE_VISUAL) || opacity(v) <= 0.01f) return;
    float r[4];
    if (is(t, TYPE_UIELEMENT) && bounds(v, s.root, r) && s.pt[0] >= r[0] && s.pt[0] < r[2] && s.pt[1] >= r[1] &&
        s.pt[1] < r[3] && std::strcmp(rtti_name(v), ".?AVWindowFrame@AVUI@@"))
        s.best = v;
    const int n = vfn<CountFn>(v, VT_CHILD_COUNT)(v);
    for (int i = 0; i < n && i < 4096; ++i) deepest(s, vfn<ChildFn>(v, VT_CHILD)(v, i), depth + 1);
}

std::string chain_of(uint8_t* el) {
    std::string chain;
    for (int d = 0; el && d < 40; ++d, el = at<uint8_t*>(el, VISUAL_PARENT)) {
        if (!is(type_of(el), TYPE_VISUAL)) break;
        const char* n = rtti_name(el);
        chain += (chain.empty() ? "" : " < ") + std::string(n + (std::strncmp(n, ".?AV", 4) ? 0 : 4));
    }
    return chain;
}

void dump_under_cursor() {
    Search s{active_window(), {}, nullptr, 0, nullptr, kNear};
    if (!s.root || !cursor_ui(s.pt)) return;
    deepest(s, s.root, 0);
    logf("under the cursor (%.0f,%.0f): %s", s.pt[0], s.pt[1], s.best ? chain_of(s.best).c_str() : "nothing");
    if (uint8_t* f = focused_object()) logf("focus: %s", chain_of(f).c_str());
}

// ---- the keyboard -------------------------------------------------------------------------------
// The game asks its input for controls by (context, control): WasPressed (0x1072510), GetAnalog
// (0x106F040) and GetDigital (0x106E3E0), each first checking that the context is on for the
// player (IsContextActive, 0x1072890). Driving is context 0: 0 gas, 1 brake, 2 steering (-1..1),
// 3 handbrake, 4 / 5 shift up / down, 6 clutch, 9-12 the right stick's look up / back / left /
// right, 15 photo mode, 17 camera, 20 rewind, 21 the HUD's prompts ("Enter garage"), 22 map. The car's keys answer those while context
// 0 is on (so not in photo mode or the menus), as a pad would: held is 1, a press is "pressed"
// once to each place that asks.
constexpr uintptr_t RVA_CONTEXT_ACTIVE = 0x1072890;
constexpr uintptr_t RVA_GET_DIGITAL    = 0x106E3E0;
constexpr int kDriving = 0, kControls = 32;
using ContextActiveFn = bool (*)(void* input, int context);
using GetDigitalFn    = float (*)(void* input, int context, int control);
GetDigitalFn o_get_digital = nullptr;   // hooked: the driving keys are on

std::array<std::atomic<bool>, kActionCount> g_held{};   // set once a UI frame

struct Edge {
    uint32_t id = 0;   // counts presses
    DWORD at    = 0;   // GetTickCount of the last
    uint32_t used = 0;   // the press the game took (Consume), which no one else sees
    const void* from[8] = {};
    uint32_t told[8]    = {};
    uint32_t frame[8]   = {};   // the game frame it was told in
};
Edge g_edges[kControls];   // by control, guarded by g_edge_mx
std::mutex g_edge_mx;

void key_press(int control) {
    std::lock_guard<std::mutex> lock(g_edge_mx);
    ++g_edges[control].id;
    g_edges[control].at = GetTickCount();
}

std::atomic<bool> g_in_menu{true};       // a menu has the focus (the UI thread's in_menu, each frame)
std::atomic<uint8_t*> g_player{nullptr};  // the input that last asked about a control: the player's

bool driving(void* input, int context, int control) {
    if (!g_keyboard || !o_get_digital || g_in_menu.load() || context != kDriving || control < 0 || control >= kControls ||
        !game<ContextActiveFn>(RVA_CONTEXT_ACTIVE)(input, kDriving))
        return false;
    return true;
}

// The pad's own state: each frame the game's input update (0x1075490, once a frame from the main
// loop) reads every player's pad into its input (+0x70, the buttons a bitmask at +0x78, the
// pad's XInput bits and some of the game's own; last frame's at +0x128). What reads the buttons
// themselves, like the radio's d-pad and the menus' tile grids, gets the keys' bits from there,
// put in after the update, so to the game they're the pad's buttons, held, pressed and released.
constexpr uintptr_t RVA_INPUT_UPDATE = 0x1075490;
constexpr uintptr_t RVA_INPUTS       = 0x365D7C8;   // the players' inputs, 0x1300 apart from +0x10
constexpr uintptr_t INPUTS_DEVICE    = 0x70;        // in each: its pad, or null
constexpr uintptr_t INPUTS_CURRENT   = 0xAB28;      // the current player's (one of them)
constexpr uintptr_t INPUT_CONNECTED  = 0x4;    // bool: its pad is there
constexpr uintptr_t INPUT_BUTTONS    = 0x78;
// A pad's state is at +0x70 (this frame) and +0x128 (the last, for what was just pressed):
constexpr uintptr_t INPUT_STATE      = 0x70;
constexpr uintptr_t INPUT_LAST_STATE = 0x128;
constexpr uintptr_t STATE_BUTTONS    = 0x8;    // the buttons' bits
constexpr uintptr_t STATE_DPAD       = 0x10;   // the d-pad as a direction: {dir, angle, length, x, y}
// And the pad's own XINPUT_STATE, as read (+0x4C: dwPacketNumber, +0x50: wButtons). Extra Options'
// plugins (Weather Control's "Change time") read the buttons there, not the game's.
constexpr uintptr_t STATE_XINPUT_BUTTONS = 0x50;
// The buttons of the last frames they were held in (seen with a controller, not in the code): 10 entries of 8 bytes
// (XInput's bits), then the next one's index and how many frames they've been held; all 0 once let go. Some screens
// go by it (the race's start screen moves up and down by it).
constexpr uintptr_t STATE_HISTORY = 0x60, STATE_HISTORY_INDEX = 0xB0, STATE_HISTORY_COUNT = 0xB4;
constexpr int kHistory = 10;

// The d-pad's direction, as the pad gives it with its bits (what the tile grids go by): 0 up, 2
// left, 4 down, 6 right (counterclockwise, 8 none), the angle from up, the length, x and y.
void set_dpad(uint8_t* state, uint64_t bits) {
    struct Dir { uint64_t bit; int dir; float angle, x, y; };
    static const Dir dirs[] = {{1, 0, 0, 0, 1}, {4, 2, 1.5707964f, -1, 0}, {2, 4, 3.1415927f, 0, -1}, {8, 6, -1.5707964f, 1, 0}};
    int* d = reinterpret_cast<int*>(state + STATE_DPAD);
    if (*d != 8) return;   // the pad's own
    for (const Dir& r : dirs) {
        if (!(bits & r.bit)) continue;
        float* f = reinterpret_cast<float*>(d + 1);
        *d = r.dir, f[0] = r.angle, f[1] = 1, f[2] = r.x, f[3] = r.y;
        return;
    }
}
using InputUpdateFn = uintptr_t (*)(void* inputs, float dt);
InputUpdateFn o_input_update = nullptr;
std::atomic<uint32_t> g_input_frame{0};   // counts the game's input updates (0: not hooked)
std::atomic<uint64_t> g_click_bits{0};    // a help bar click's pad buttons (see help_click), held
std::atomic<DWORD> g_click_until{0};      // ... till then

// ---- Extra Options' keys ----------------------------------------------------------------------
// Extra Options (winmm.dll, FH2_ExtraOptions.ini next to azimuth.exe) works the blinkers, the
// hazards and the headlights from the pad's buttons as XInput reads them (the current player's
// input, +0xC0: see plugin_buttons) or from keys of its own, both bound in its [Bindings]
// section ("SignalLeftXbox=DPadLeft"), read when the game starts. FH2Mouse's keys for them hold
// those buttons there, and only there: the game's own buttons (+0x78) don't get them, so the
// left indicator's d-pad left doesn't change the radio as well.
std::array<uint16_t, kActionCount> g_extra_bits{};  // the pad buttons each of our keys holds for it

uint16_t xbox_bits(const std::string& text) {
    static const struct { const char* name; uint16_t bit; } names[] = {
        {"DPadUp", 0x1}, {"DPadDown", 0x2}, {"DPadLeft", 0x4}, {"DPadRight", 0x8}, {"Menu", 0x10}, {"Start", 0x10},
        {"View", 0x20}, {"Back", 0x20}, {"LeftStick", 0x40}, {"LS", 0x40}, {"RightStick", 0x80}, {"RS", 0x80},
        {"LB", 0x100}, {"RB", 0x200}, {"A", 0x1000}, {"B", 0x2000}, {"X", 0x4000}, {"Y", 0x8000},
    };
    uint16_t bits = 0;
    std::size_t p = 0;
    while (p <= text.size()) {
        std::size_t e = text.find('+', p);
        if (e == std::string::npos) e = text.size();
        std::string t = text.substr(p, e - p);
        t.erase(0, t.find_first_not_of(" \t"));
        t.erase(t.find_last_not_of(" \t") + 1);
        bool known = false;
        for (const auto& n : names)
            if (!_stricmp(t.c_str(), n.name)) bits |= n.bit, known = true;
        if (!known) return 0;   // None, a trigger (LT / RT: not a button), or something unknown
        p = e + 1;
    }
    return bits;
}

void load_extra_bindings() {
    const std::wstring ini = std::wstring(g_api->azimuth_dir) + L"\\FH2_ExtraOptions.ini";
    g_extra_options        = GetFileAttributesW(ini.c_str()) != INVALID_FILE_ATTRIBUTES;
    if (!g_extra_options) return;
    for (int i = 0; i < kActionCount; ++i) {
        if (!kActions[i].extra) continue;
        wchar_t buf[64] = {};
        GetPrivateProfileStringW(L"Bindings", (widen(kActions[i].extra) + L"Xbox").c_str(), L"", buf, 64, ini.c_str());
        std::string text;
        for (const wchar_t* c = buf; *c; ++c) text += *c < 128 ? static_cast<char>(*c) : '?';
        g_extra_bits[i] = xbox_bits(text);
        if (!g_extra_bits[i])
            g_api->log(g_mod, (std::string("Extra Options' ") + kActions[i].extra + "Xbox is \"" + text +
                               "\", which isn't pad buttons: the " + kActions[i].label + " key does nothing")
                                  .c_str());
    }
}

// The pad buttons the keys hold for Extra Options.
uint16_t extra_pad_bits() {
    uint16_t bits = 0;
    for (int i = 0; i < kActionCount; ++i)
        if (g_extra_bits[i] && g_held[i].load()) bits |= g_extra_bits[i];
    return bits;
}

// ---- FH2 ANNA ----------------------------------------------------------------------------------
// FH2 ANNA (fh2-mods\FH2ANNA.dll) reads only the pad, from the game's input after its update, as
// we hold buttons in it: its keys hold its d-pad buttons, as the pad would press them.
//  - Open: a tap of D-pad down (ANNA's; held 250 ms it's the telemetry's), 100 ms however long
//    the key is held. Open, it backs out instead: D-pad left, tapped till ANNA has closed.
//  - Up, down, back, choose: the d-pad's up, down, left, right, held with the key, only while
//    ANNA is open (else they'd be the radio's and the telemetry's).
// While driving, the open key is ANNA's, and the others while it's open: the driving keys on the
// same keys don't count then (anna_key).
constexpr int kAnnaKeyCount = 5;
const char* const kAnnaActions[kAnnaKeyCount] = {"AnnaOpen", "AnnaUp", "AnnaDown", "AnnaBack", "AnnaChoose"};
constexpr uint64_t kPadUp = 0x140000001ull, kPadDown = 0x140000002ull, kPadLeft = 0xC0000004ull, kPadRight = 0xC0000008ull;

int anna_action(int a) {
    static int index[kAnnaKeyCount] = {-2, -2, -2, -2, -2};
    if (index[a] == -2) {
        index[a] = -1;
        for (int i = 0; i < kActionCount; ++i)
            if (!std::strcmp(kActions[i].name, kAnnaActions[a])) index[a] = i;
    }
    return index[a];
}

bool anna_open() {
    const FH2AnnaApi* anna = g_anna;
    return anna && anna->is_open();
}

int action_index(const char* name);

// The pad button the cinematic key holds now: ANNA's (version 6) while it drives, else none.
uint16_t anna_cinematic_bit() {
    const FH2AnnaApi* anna = g_anna;
    if (!anna || anna->size < offsetof(FH2AnnaApi, cinematic_button) + sizeof(void*) || !anna->cinematic_button) return 0;
    static int index = -2;
    if (index == -2) index = action_index("AnnaCinematic");
    return index >= 0 && g_held[index].load() ? anna->cinematic_button() : 0;
}

// The key is ANNA's now (for the driving keys).
bool anna_key(int vk) {
    if (!g_anna || !vk) return false;
    const bool open = anna_open();
    for (int a = 0; a < kAnnaKeyCount; ++a) {
        const int i = anna_action(a);
        if (i < 0 || (a > 0 && !open)) continue;
        for (int k : g_action_keys[i])
            if (k == vk) return true;
    }
    return false;
}

// The d-pad buttons ANNA's keys hold this frame (the input update's thread).
uint64_t anna_pad_bits() {
    if (!g_anna) return 0;
    static bool was[kAnnaKeyCount] = {};
    static DWORD open_until = 0, close_until = 0;
    static int close_frame  = 0;
    bool now[kAnnaKeyCount];
    for (int a = 0; a < kAnnaKeyCount; ++a) {
        const int i = anna_action(a);
        now[a]      = i >= 0 && g_held[i].load();
    }
    const DWORD t   = GetTickCount();
    const bool open = anna_open();
    if (now[0] && !was[0]) {
        if (open) close_until = t + 1500, close_frame = 0;
        else open_until = t + 100;
    }
    for (int a = 0; a < kAnnaKeyCount; ++a) was[a] = now[a];
    uint64_t bits = 0;
    if (open_until) {
        if (static_cast<LONG>(open_until - t) > 0) bits |= kPadDown;
        else open_until = 0;
    }
    if (close_until) {
        if (!open || static_cast<LONG>(close_until - t) <= 0) close_until = 0;
        else if ((close_frame++ / 3) % 2 == 0) bits |= kPadLeft;   // 3 frames down, 3 up: a tap each
        return bits;
    }
    if (open) {
        if (now[1]) bits |= kPadUp;
        if (now[2]) bits |= kPadDown;
        if (now[3]) bits |= kPadLeft;
        if (now[4]) bits |= kPadRight;
    }
    return bits | anna_cinematic_bit();
}

// The pad's buttons the keys (and a help bar click) hold.
uint64_t keys_pad_bits() {
    uint64_t bits = anna_pad_bits();
    for (int i = 0; i < kActionCount; ++i)
        if (kActions[i].pad && g_held[i].load()) bits |= kActions[i].pad;
    if (const uint64_t click = g_click_bits.load()) {
        if (static_cast<LONG>(g_click_until.load() - GetTickCount()) > 0) bits |= click;
        else g_click_bits = 0;
    }
    return bits;
}

// Each input the keys' buttons go into, with what they held there last frame and its history as they left it.
struct FedInput {
    uint8_t* input     = nullptr;
    uint64_t last_bits = 0;
    int history_count  = 0;
    bool history_ours  = false;
    DWORD seen         = 0;
};
FedInput g_fed[8];                     // the input update's thread only
std::atomic<uint64_t> g_fed_bits{0};   // what the keys hold this frame (for was_pressed)

FedInput& fed_for(uint8_t* in) {
    FedInput* slot = &g_fed[0];
    for (FedInput& f : g_fed) {
        if (f.input == in) return f;
        if (f.seen < slot->seen) slot = &f;
    }
    *slot       = FedInput{};
    slot->input = in;
    return *slot;
}

// The keys' buttons into the state's history, as a controller's go in: each frame they're held, the next entry and
// one more frame; let go, it's cleared. If the controller wrote this frame's entry, the keys' join it.
void history(uint8_t* state, uint16_t buttons, FedInput& f) {
    auto* entries = reinterpret_cast<uint64_t*>(state + STATE_HISTORY);
    int& index    = *reinterpret_cast<int*>(state + STATE_HISTORY_INDEX);
    int& count    = *reinterpret_cast<int*>(state + STATE_HISTORY_COUNT);
    if (index < 0 || index >= kHistory) return;   // not as expected: left alone
    const bool game_wrote = count != f.history_count;
    if (buttons) {
        if (game_wrote && count > 0) entries[(index + kHistory - 1) % kHistory] |= buttons;
        else entries[index] = buttons, index = (index + 1) % kHistory, ++count;
        f.history_ours = true;
    } else if (f.history_ours) {
        f.history_ours = false;
        if (!game_wrote) {   // the keys' alone: let go, as a controller's are
            for (int i = 0; i < kHistory; ++i) entries[i] = 0;
            index = count = 0;
        }
    }
    f.history_count = count;
}

void feed(uint8_t* in, uint64_t bits) {
    FedInput& f = fed_for(in);
    f.seen      = GetTickCount();
    // The keys are its pad now: when the pad goes, the car (0x2FD9E3) stops reading an input that isn't connected
    // and the HUD pauses the game for it, so it stays connected.
    in[INPUT_CONNECTED] = 1;
    *reinterpret_cast<uint64_t*>(in + INPUT_BUTTONS) |= bits;
    if (bits & 0xF) set_dpad(in + INPUT_STATE, bits);
    // Last frame's state is the pad's alone, so what the keys held then goes in there too: else a held key is a new
    // press every frame (a tap ran down a whole list).
    *reinterpret_cast<uint64_t*>(in + INPUT_LAST_STATE + STATE_BUTTONS) |= f.last_bits;
    if (f.last_bits & 0xF) set_dpad(in + INPUT_LAST_STATE, f.last_bits);
    f.last_bits = bits;
    // As a controller's: its raw XInput buttons too, and the history of the frames they've been held.
    if (bits & 0xFFFF) *reinterpret_cast<uint16_t*>(in + INPUT_STATE + STATE_XINPUT_BUTTONS) |= static_cast<uint16_t>(bits);
    history(in + INPUT_STATE, static_cast<uint16_t>(bits & 0xFFFF), f);
}

uintptr_t input_update(void* inputs, float dt) {
    const uintptr_t r = o_input_update(inputs, dt);
    ++g_input_frame;
    uint8_t* in = g_player.load();
    if (!in) return r;
    if (g_cfg.debug) {
        static uint64_t last   = 0;
        const uint64_t buttons = *reinterpret_cast<uint64_t*>(in + INPUT_BUTTONS);
        if (buttons != last) logf("pad buttons %llx", static_cast<unsigned long long>(buttons));
        last = buttons;
    }
    if (!g_keyboard) return r;
    const uint64_t bits = keys_pad_bits();
    g_fed_bits          = bits;
    // Into the input that last asked and the current player's (both handed over by the game each frame). Not a
    // kept pointer to the UI's input: the game swaps it (a restart, a replay), and the old one may be gone.
    uint8_t* all = at<uint8_t*>(g_base, RVA_INPUTS);
    uint8_t* cur = all ? at<uint8_t*>(all, INPUTS_CURRENT) : nullptr;
    feed(in, bits);
    if (cur && cur != in) feed(cur, bits);
    // Extra Options reads the buttons as read (see plugin_buttons) in the simulation too, after this.
    if (const uint16_t extra = extra_pad_bits())
        if (cur) *reinterpret_cast<uint16_t*>(cur + INPUT_STATE + STATE_XINPUT_BUTTONS) |= extra;
    return r;
}

// A screen's own d-pad checks (menu controls 121 up, 123 down, 125 / 127 left / right: the race's start screen moves
// by them). The game decides "pressed" per button, and the first control that takes a press consumes it for the
// others: with a pad, the screen's checks come first; with the keys, the menus' key pump's (92...) does, and the
// screen never sees the press. So for a d-pad press of the keys', each is told "pressed" once if the game didn't say
// so itself, from when the direction is to be seen in the input asked (FH2 ANNA keeps a d-pad-down tap from the
// game a while, as its open button).
constexpr int kDpadControls[4]  = {121, 123, 125, 127};
constexpr uint64_t kDpadBits[4] = {1, 2, 4, 8};
DWORD g_dpad_at[4]              = {};
bool g_dpad_told[4]             = {};
bool g_dpad_visible[4]          = {};

int dpad_index(int control) {
    for (int i = 0; i < 4; ++i)
        if (kDpadControls[i] == control) return i;
    return -1;
}

bool dpad_visible(void* input, int i) {
    return input && (*reinterpret_cast<uint64_t*>(static_cast<uint8_t*>(input) + INPUT_BUTTONS) & kDpadBits[i]);
}

void dpad_told(void* input, int control) {
    const int i = dpad_index(control);
    if (i < 0) return;
    g_dpad_told[i] = true, g_dpad_at[i] = GetTickCount(), g_dpad_visible[i] = dpad_visible(input, i);
}

bool dpad_pressed(void* input, int control) {
    const int i = dpad_index(control);
    if (i < 0) return false;
    const bool keys    = (g_fed_bits.load() & kDpadBits[i]) != 0;
    const bool visible = dpad_visible(input, i);
    if (keys && visible && !g_dpad_visible[i]) g_dpad_at[i] = GetTickCount(), g_dpad_told[i] = false;
    g_dpad_visible[i] = visible;
    if (!keys || !visible || g_dpad_told[i] || GetTickCount() - g_dpad_at[i] > 250) return false;
    g_dpad_told[i] = true;
    return true;
}

// Extra Options' plugins (Weather Control's "(Y) Change time") read the pad's buttons as read, in
// the current player's input (the inputs' +0xAB28), while it's connected, from the UI's update.
// Signed in without a pad, that isn't the input the menus ask (the inputs' +0x9810, nobody's),
// and without a pad its state is cleared before the UI's update. So, just before it, the keys'
// buttons go there, and it's connected while the keys play.
void plugin_buttons() {
    if (!g_keyboard || !o_input_update) return;
    uint8_t* inputs = at<uint8_t*>(g_base, RVA_INPUTS);
    uint8_t* cur    = inputs ? at<uint8_t*>(inputs, INPUTS_CURRENT) : nullptr;
    if (!cur) return;
    cur[INPUT_CONNECTED] = 1;
    *reinterpret_cast<uint16_t*>(cur + INPUT_STATE + STATE_XINPUT_BUTTONS) |= static_cast<uint16_t>(keys_pad_bits()) | extra_pad_bits();
}

// What the keys add to a driving control's value (0 if none is held).
float key_value(void* input, int context, int control) {
    if (!driving(input, context, control)) return 0;
    float v = 0;
    for (int i = 0; i < kActionCount; ++i)
        if (kActions[i].control == control && g_held[i].load()) v += kActions[i].sign;
    return v;
}

float with_keys(float v, void* input, int context, int control) {
    const float k = key_value(input, context, control);
    return k ? std::max(-1.f, std::min(1.f, v + k)) : v;
}

// ---- free look ----------------------------------------------------------------------------------
// The driving cameras look where the right stick points (0x103F9D0): the angle of (right - left,
// up - back), from controls 12 / 11 / 9 / 10 read with GetDigital, and straight ahead again once
// the stick is let go. While a driving camera asks for them (and the game window is in front),
// the mouse is held in the middle of the window, hidden, and moving it sideways turns the camera
// as far as it's moved, all the way round (straight back after pi / kLookPerPixel pixels). A moment after the mouse
// stops, the camera turns back ahead, as when the stick is let go.
// While ANNA Drone Mode's drone flies, the mouse is held in the middle the same way, and its moves
// (up and down too) turn the drone (see "Drone Mode"), whatever FreeLook says, as in photo mode.
constexpr float kPi = 3.14159265f, kLookPerPixel = 0.006f;
constexpr DWORD kLookHoldMs = 1000;   // how long a look stays before it turns back
std::atomic<float> g_look_yaw{0};     // radians, + to the right
std::atomic<DWORD> g_look_asked{0};   // when a driving camera last asked for the right stick

float look_value(void* input, int context, int control) {
    if (context != kDriving || control < 9 || control > 12 || !game<ContextActiveFn>(RVA_CONTEXT_ACTIVE)(input, kDriving))
        return 0;
    g_look_asked = GetTickCount();
    // Looking ahead is the stick let go, not pushed up: else the pad's stick couldn't turn it.
    const float a = g_look_yaw.load();
    if (!g_looking.load() || g_in_menu.load() || a == 0) return 0;
    const float c = std::cos(a), s = std::sin(a);
    switch (control) {
        case 9: return std::max(c, 0.f);    // ahead
        case 10: return std::max(-c, 0.f);  // back
        case 11: return std::max(-s, 0.f);  // left
        default: return std::max(s, 0.f);   // right
    }
}

// Once a UI frame.
void look_frame() {
    static DWORD moved = 0;
    HWND h           = g_hwnd.load();
    const bool front = h && GetForegroundWindow() == h;
    const bool drone = front && drone_flying();
    const bool on    = drone || (g_cfg.free_look && front && o_get_digital && !g_in_menu.load() &&
                                 GetTickCount() - g_look_asked.load() < 250);
    RECT r{};
    POINT mid{}, p{};
    if (on) GetClientRect(h, &r), mid = {r.right / 2, r.bottom / 2};
    if (!on || !ClientToScreen(h, &mid) || !GetCursorPos(&p)) {
        g_looking  = false;
        g_look_yaw = 0;
        return;
    }
    if (!g_looking.load()) {   // from where the cursor was: nothing to turn by yet
        g_looking = true, moved = 0;
        SetCursorPos(mid.x, mid.y);
        return;
    }
    if (drone) {   // the drone's, as far as it moved
        g_look_yaw = 0;
        if (p.x != mid.x || p.y != mid.y) {
            add(g_drone_look_x, static_cast<float>(p.x - mid.x)), add(g_drone_look_y, static_cast<float>(p.y - mid.y));
            SetCursorPos(mid.x, mid.y);
        }
        return;
    }
    float yaw = g_look_yaw.load();
    if (p.x != mid.x || p.y != mid.y) {
        yaw   = std::remainder(yaw + (p.x - mid.x) * kLookPerPixel * g_cfg.look_percent.load() / 50.f, 2 * kPi);   // round and round
        if (yaw == 0) yaw = 1e-6f;   // 0 is let go
        moved = GetTickCount();
        SetCursorPos(mid.x, mid.y);
    } else if (yaw && GetTickCount() - moved > kLookHoldMs) {
        yaw *= std::exp(-6 * g_ui_dt);
        if (std::abs(yaw) < 0.01f) yaw = 0;
    }
    g_look_yaw = yaw;
}

// A key's press, to each place in the game's code that asks within a moment of it, once: for
// the game frame that place is first told in, as a pad's press is pressed for a frame. One place
// can ask for many: every HUD module's prompts (0x78B6E0, HUDModule's slot 5: the garage's
// "Enter garage", the notifications' "Join session") ask from 0x78B74A, one after another, and
// the one that takes the press consumes it (Consume(input, context, control), 0x106D640), so the
// rest don't see it, as with the pad.
constexpr uintptr_t RVA_CONSUME = 0x106D640;
using ConsumeFn = void (*)(void* input, int context, int control);
ConsumeFn o_consume = nullptr;

bool key_pressed(void* input, int context, int control, const void* from) {
    if (!driving(input, context, control)) return false;
    std::lock_guard<std::mutex> lock(g_edge_mx);
    Edge& e = g_edges[control];
    if (!e.id || e.used == e.id || GetTickCount() - e.at > 250) return false;
    const uint32_t frame = g_input_frame.load();
    for (int i = 0; i < 8; ++i) {
        if (e.from[i] && e.from[i] != from) continue;
        e.from[i] = from;
        if (e.told[i] == e.id) return frame && o_consume && e.frame[i] == frame;
        e.told[i]  = e.id;
        e.frame[i] = frame;
        return true;
    }
    return false;
}

void consume(void* input, int context, int control) {
    if (context == kDriving && control >= 0 && control < kControls) {
        std::lock_guard<std::mutex> lock(g_edge_mx);
        g_edges[control].used = g_edges[control].id;
    }
    o_consume(input, context, control);
}

// ---- the help bar ---------------------------------------------------------------------------------
// "(A) Select (B) Back ...": ControllerButtonControls, each naming a pad button. A click on one is
// that button's key, as the pad would send it.

bool is_help_button(uint8_t* el) { return el && is(type_of(el), TYPE_HELP_BUTTON); }

// The key for a ControllerButtonType (in the order the help bar template lists them), or 0.
// Help bar clicks press the pad button itself, as far as the menus can tell: the game's "was
// this pressed" check (WasPressed(input, context, control), context 7 being the menus) says yes
// to the button's control for one UI frame, then to its release for the next. The pad-to-key
// pump turns that into the key as for a real press, and pages that ask about the button
// directly (the pause menu's Y, Change time) see it too. The controls come from the pump's table.
constexpr uintptr_t RVA_WAS_PRESSED = 0x1072510;
constexpr uintptr_t RVA_PAD_KEYS    = 0x2254390;   // 24 x {pressed control, released control, key}
using WasPressedFn = bool (*)(void* input, int context, int control, uint8_t a);
WasPressedFn o_was_pressed = nullptr;

std::atomic<int> g_press_ctl{-1}, g_release_ctl{-1}, g_press_phase{0};   // 1: pressed, 2: released
std::atomic<DWORD> g_press_until{0};   // when the phase ends (0 while held: the middle button)
std::atomic<bool> g_press_held{false};
std::atomic<bool> g_press_told{false}, g_release_told{false};

bool was_pressed(void* input, int context, int control, uint8_t a) {
    g_player = static_cast<uint8_t*>(input);
    if (o_was_pressed(input, context, control, a)) {
        if (context == 7) dpad_told(input, control);
        if (g_cfg.debug) {
            static int last_ctx = -1, last_ctl = -1;
            static DWORD last_t = 0;
            if (context != last_ctx || control != last_ctl || GetTickCount() - last_t > 500)
                logf("pad pressed: context %d control %d (a %d), asked from game+0x%llX", context, control, a,
                     static_cast<unsigned long long>(static_cast<uint8_t*>(RETURN_ADDRESS()) - g_base));
            last_ctx = context, last_ctl = control, last_t = GetTickCount();
        }
        return true;
    }
    if (key_pressed(input, context, control, RETURN_ADDRESS())) return true;
    if (context != 7) return false;
    if (g_keyboard && dpad_pressed(input, control)) return true;
    // Like a real press: "pressed" to the first asker once, "released" once (later).
    const int phase = g_press_phase.load();
    bool r = false;
    if (phase == 1 && control == g_press_ctl.load()) r = !g_press_told.exchange(true);
    else if (phase == 2 && control == g_release_ctl.load()) r = !g_release_told.exchange(true);
    if (phase && g_cfg.debug && (control == g_press_ctl.load() || control == g_release_ctl.load()))
        logf("fake press: control %d asked from game+0x%llX in phase %d: %d", control,
             static_cast<unsigned long long>(static_cast<uint8_t*>(RETURN_ADDRESS()) - g_base), phase, r);
    return r;
}

// Once a frame: the press is down as long as it's held (at least 80 ms), then released for 40 ms,
// in real time: some of what asks runs outside the UI's update.
void press_frame() {
    const int phase = g_press_phase.load();
    if (!phase || g_press_held.load() || GetTickCount() < g_press_until.load()) return;
    if (phase == 1) {
        g_press_phase = 2;
        g_press_until = GetTickCount() + 40;
    } else {
        g_press_phase = 0;
    }
}

// Presses the pad button that sends `key` to the menus; false if there's none (or no hook).
bool press_pad(int key, bool hold = false) {
    if (!o_was_pressed || g_press_phase.load()) return false;
    for (int i = 0; i < 24; ++i) {
        const uint8_t* e = g_base + RVA_PAD_KEYS + 12 * i;
        if (at<int>(e, 8) != key) continue;
        g_press_ctl     = at<int>(e, 0);
        g_release_ctl   = at<int>(e, 4);
        g_press_held    = hold;
        g_press_told    = false;
        g_release_told  = false;
        g_press_until   = GetTickCount() + 80;
        g_press_phase   = 1;
        if (g_cfg.debug) logf("pad press: control %d / %d (key %d)", at<int>(e, 0), at<int>(e, 4), key);
        return true;
    }
    return false;
}

// Its ControllerButtonType, or -1.
int help_button_type(uint8_t* el) {
    void* dp = at<void*>(g_base, RVA_HELP_BUTTON_DP);
    if (!dp) return -1;
    uint8_t* boxed = nullptr;
    game<DpGetValueFn>(RVA_DP_GET_VALUE)(el, &boxed, dp);
    const int type = boxed && boxed != game<UnsetFn>(RVA_UNSET_VALUE)() ? at<int>(boxed, 0x28) : -1;
    release(boxed);
    return type;
}

int help_button_key(uint8_t* el) {
    const int type = help_button_type(el);
    // ControllerButtonType, in the help bar template's order, to the key the pump sends for it.
    // Start/View/triggers/sticks: Pause, Escape, the shifts and the controls, by the table's order.
    static const int keys[] = {
        KEY_A, KEY_B, KEY_X, KEY_Y,   // A B X Y
        7, 13,                        // Start (Menu), Back (View)
        KEY_LB, KEY_RB,               // bumpers
        116, 117,                     // triggers
        KEY_LS_CLICK, KEY_RS_CLICK,   // stick clicks (L3, R3)
        0, 0,                         // d-pad left/right, up/down (both ways: nothing to press)
        KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT,
        118, 0, 119, 119,             // left stick, left stick left/right, right stick, ... (0xB61C20's table)
    };
    const int key = type >= 0 && type < static_cast<int>(sizeof keys / sizeof keys[0]) ? keys[type] : 0;
    if (g_cfg.debug) logf("help bar button %d: key %d", type, key);
    return key;
}

// Better still, the pad's button itself held a moment, as its key holds it: some pages look at
// the button being down too (the Autoshow's "(View) Take photo"), and Extra Options' plugins
// read only that ("(Y) Change time"). False if no key holds that button (or no input update).
bool click_pad(int key) {
    if (!g_keyboard || !o_input_update || !g_player.load()) return false;
    for (const Action& a : kActions) {
        if (a.key != key || !a.pad) continue;
        g_click_until = GetTickCount() + 100;
        g_click_bits  = a.pad;
        if (g_cfg.debug) logf("help bar click: pad buttons %llx held", static_cast<unsigned long long>(a.pad));
        return true;
    }
    return false;
}

bool help_click(uint8_t* ctl) {
    if (!is_help_button(ctl)) return false;
    if (const int key = help_button_key(ctl)) {
        if (!click_pad(key) && !press_pad(key)) {
            send_key(key, false);
            send_key(key, true);
        }
    }
    return true;
}

// ---- key icons -----------------------------------------------------------------------------------
// While the keyboard (or the mouse) was used last, the pad's buttons in the help bar and the HUD's
// prompts show the keys that press them instead: "(Enter) Select", "(Esc) Back". A button on the
// pad brings the pad's back.
//
// A ControllerButtonControl shows its button with 0xB61C20(control, ControllerButtonType): from a
// table of names (built at 0x34E5920) it looks up the resource "A_BitmapImage_ControllerButton_A"
// with FindResource(element, String* name) (0x164F840, a borrowed reference or null), and sets it
// as the ImageSource of its icon (+0x1D8) and the icon's shadow (+0x1E0). An ActivateButtonControl
// (the HUD's "(X) Enter Garage", an event's "(X) Start", notifications) does the same in
// 0xB6A560(control), for the button the game's activate control is on (X, or View with a manual
// gearbox), with a size after the name ("_Large"): it's always the Interact key's, wherever the
// focus is. Those three FindResource calls are hooked: for a button a key presses, they get a
// BitmapImage of ours instead, of a texture made here (FH2MouseKeys\<key>.swatchbin, written
// through the platform when the mod loads). Which key presses a help bar's button depends on
// where it is: X is ButtonX in the menus and Interact while driving.
//
// The images are made as XAML makes them from a path: AVUI's BitmapImageTypeConverter
// (vtable 0x2D5F628, +8 ConvertFrom(this, Ptr* out, context, value), which uses no state of its
// own) given an AVUI String (0x1663220 builds one: 0x40 bytes, its UTF-16 text at +0x28 and
// length at +0x30). Pictures that pages name in their XAML themselves keep the pad's buttons.
//
// The textures are uncompressed, as the game's own 32 x 32 buttons (format 0xAD): BGRA with the
// colour premultiplied, in 8 x 8 tiles in rows, the pixels in each in Morton order (x in the even
// bits of the index, y in the odd ones), after a 0x53-byte header. The keys are Forza Horizon 6's
// keyboard art (key_art.h, made by tools/make_key_art.py), the key's name written on with GDI.
constexpr uintptr_t RVA_FIND_RESOURCE   = 0x164F840;
constexpr uintptr_t RVA_ICON_UPDATE     = 0xB61C20;
constexpr uintptr_t RVA_ACTIVATE_UPDATE = 0xB6A560;
constexpr uintptr_t RVA_ICON_FINDS[]    = {0xB61FFE, 0xB62055};   // the help bar's calls to FindResource
constexpr uintptr_t RVA_ACTIVATE_FIND   = 0xB6B054;               // the activate button's
constexpr uintptr_t ACTIVATE_SHOWN      = 0x1F8;   // ActivateButtonControl: the control it shows (same: nothing to do)
constexpr uintptr_t RVA_BITMAP_CONVERTER = 0x2D5F628;   // BitmapImageTypeConverter's vtable
constexpr uintptr_t RVA_CAN_CONVERT      = 0x1532C00;   // ... +0: CanConvertFrom (a String)
constexpr uintptr_t RVA_CONVERT_FROM     = 0x15999F0;   // ... +8: ConvertFrom
constexpr uintptr_t RVA_AVUI_STRING      = 0x1663220;   // String* (String* out, const char*, int length or -1)
using FindResourceFn = uint8_t* (*)(void* element, uint8_t* name);
using IconUpdateFn   = void (*)(void* control, int type);
using ActivateUpdateFn = void (*)(void* control);
using ConvertFromFn  = void** (*)(void* self, uint8_t** out, void* context, void* value);
using AvuiStringFn   = void* (*)(void* out, const char* text, int length);
FindResourceFn o_find_resource = nullptr;
bool g_icons_hooked = false;

std::atomic<bool> g_keys_last{false};  // the keyboard or the mouse was used last, not a pad
std::vector<std::string> g_key_files;  // the textures written, by name (Key_0D...)
int g_custom_keys = 0;                 // how many of them are pictures from FH2Mouse\KeyIcons

bool keys_shown() { return g_icons_hooked && g_key_icons_on && g_keyboard && g_keys_last.load(); }

constexpr int kKeySize = 128, kKeySmall = 32;

// A key's name, as written on it ("" for an arrow, which has its picture).
std::wstring key_label(int vk) {
    static const struct { int vk; const wchar_t* text; } names[] = {
        {VK_RETURN, L"Enter"}, {VK_ESCAPE, L"Esc"}, {VK_PRIOR, L"PgUp"}, {VK_NEXT, L"PgDn"}, {VK_SPACE, L"Space"},
        {VK_TAB, L"Tab"}, {VK_BACK, L"Bksp"}, {VK_INSERT, L"Ins"}, {VK_DELETE, L"Del"}, {VK_HOME, L"Home"},
        {VK_END, L"End"}, {VK_SHIFT, L"Shift"}, {VK_LSHIFT, L"Shift"}, {VK_RSHIFT, L"Shift"}, {VK_CONTROL, L"Ctrl"},
        {VK_LCONTROL, L"Ctrl"}, {VK_RCONTROL, L"Ctrl"}, {VK_MENU, L"Alt"}, {VK_LMENU, L"Alt"}, {VK_RMENU, L"Alt"},
        {VK_OEM_COMMA, L","}, {VK_OEM_PERIOD, L"."}, {VK_OEM_MINUS, L"-"}, {VK_OEM_PLUS, L"="}, {VK_OEM_1, L";"},
        {VK_OEM_2, L"/"}, {VK_OEM_3, L"`"}, {VK_OEM_4, L"["}, {VK_OEM_5, L"\\"}, {VK_OEM_6, L"]"}, {VK_OEM_7, L"'"},
        {VK_UP, L""}, {VK_DOWN, L""}, {VK_LEFT, L""}, {VK_RIGHT, L""},
    };
    for (const auto& n : names)
        if (n.vk == vk) return n.text;
    if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) return std::wstring(1, static_cast<wchar_t>(vk));
    if (vk >= VK_F1 && vk <= VK_F24) return L"F" + std::to_wstring(vk - VK_F1 + 1);
    if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) return L"Num" + std::to_wstring(vk - VK_NUMPAD0);
    wchar_t name[32] = {};
    if (GetKeyNameTextW(static_cast<LONG>(MapVirtualKeyW(vk, MAPVK_VK_TO_VSC) << 16), name, 32) > 0) return name;
    return L"?";
}

int arrow_of(int vk) {
    switch (vk) {
        case VK_UP: return 0;
        case VK_DOWN: return 1;
        case VK_LEFT: return 2;
        case VK_RIGHT: return 3;
        default: return -1;
    }
}

// Adds a piece of art to `cov` (128 x 128), scaled about the key's middle and moved by dx, dy.
void draw_art(const KeyArt& a, uint8_t* cov, float scale, float dx, float dy) {
    for (int y = 0; y < kKeySize; ++y)
        for (int x = 0; x < kKeySize; ++x) {
            const float sx = (x + 0.5f - 64 - dx) / scale + 64 - 0.5f - a.x;
            const float sy = (y + 0.5f - 64 - dy) / scale + 64 - 0.5f - a.y;
            const int x0 = static_cast<int>(std::floor(sx)), y0 = static_cast<int>(std::floor(sy));
            const float fx = sx - x0, fy = sy - y0;
            auto px = [&](int u, int v) { return u < 0 || v < 0 || u >= a.w || v >= a.h ? 0.f : a.data[v * a.w + u]; };
            const float v = (px(x0, y0) * (1 - fx) + px(x0 + 1, y0) * fx) * (1 - fy) +
                            (px(x0, y0 + 1) * (1 - fx) + px(x0 + 1, y0 + 1) * fx) * fy;
            uint8_t& c = cov[y * kKeySize + x];
            c = static_cast<uint8_t>(std::max<float>(c, std::min(255.f, v + 0.5f)));
        }
}

// Writes `text` onto `cov` (128 x 128) in bold, as large as fits, its ink in the middle.
bool draw_label(const std::wstring& text, uint8_t* cov) {
    HDC dc = CreateCompatibleDC(nullptr);
    if (!dc) return false;
    BITMAPINFO bi{};
    bi.bmiHeader = {sizeof(BITMAPINFOHEADER), kKeySize, -kKeySize, 1, 32, BI_RGB, 0, 0, 0, 0, 0};
    void* bits   = nullptr;
    HBITMAP bmp  = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bmp) return DeleteDC(dc), false;
    HGDIOBJ old_bmp = SelectObject(dc, bmp);
    HFONT font      = nullptr;
    SIZE size{};
    for (int h = 72; h >= 24; h -= 2) {
        if (font) DeleteObject(font);
        font = CreateFontW(-h, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                           ANTIALIASED_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        SelectObject(dc, font);
        GetTextExtentPoint32W(dc, text.c_str(), static_cast<int>(text.size()), &size);
        if (size.cx <= 100) break;
    }
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(255, 255, 255));
    TextOutW(dc, (kKeySize - size.cx) / 2, (kKeySize - size.cy) / 2, text.c_str(), static_cast<int>(text.size()));
    GdiFlush();
    const auto* px = static_cast<const uint8_t*>(bits);
    int x0 = kKeySize, y0 = kKeySize, x1 = -1, y1 = -1;
    for (int y = 0; y < kKeySize; ++y)
        for (int x = 0; x < kKeySize; ++x)
            if (px[(y * kKeySize + x) * 4 + 1]) x0 = std::min(x0, x), x1 = std::max(x1, x), y0 = std::min(y0, y), y1 = std::max(y1, y);
    if (x1 >= 0) {
        const int dx = (kKeySize - 1 - x1 - x0) / 2, dy = (kKeySize - 1 - y1 - y0) / 2;
        for (int y = 0; y < kKeySize; ++y)
            for (int x = 0; x < kKeySize; ++x) {
                const int sx = x - dx, sy = y - dy;
                if (sx >= 0 && sy >= 0 && sx < kKeySize && sy < kKeySize) cov[y * kKeySize + x] = px[(sy * kKeySize + sx) * 4 + 1];
            }
    }
    SelectObject(dc, old_bmp);
    DeleteObject(bmp);
    if (font) DeleteObject(font);
    DeleteDC(dc);
    return x1 >= 0;
}

// A swatchbin of a w x h picture (BGRA, premultiplied; both multiples of 8), as the game's own
// buttons. The game's own have 0x48 after the format when both sides are powers of two (its
// buttons) and 0x40 otherwise (its logos).
std::string swatchbin(int w, int h, const uint8_t* bgra) {
    const uint32_t size = static_cast<uint32_t>(w * h * 4);
    std::string f(0x53 + size, '\0');
    auto put = [&](std::size_t at, uint32_t v, int n) { std::memcpy(&f[at], &v, n); };
    std::memcpy(&f[0x00], "burG\x01\x00\x01\x00", 8);
    put(0x08, 0x53, 4), put(0x0C, 0x53 + size, 4);
    std::memcpy(&f[0x10], "BCXT\x02\x00\x01\x00", 8);
    put(0x18, 0x28, 4), put(0x1C, 0x53, 4), put(0x20, size, 4), put(0x24, size, 4);
    std::memcpy(&f[0x28], "HCXT\x30\x02\x08\x00", 8);
    f[0x3F] = 1;
    put(0x40, w, 2), put(0x42, h, 2), put(0x44, 1, 2), put(0x46, w, 2), put(0x48, h, 2), put(0x4A, 1, 2);
    std::memcpy(&f[0x4C], "\x01\x01\x00\xAD\x01\x00\x48", 7);   // one level, format 0xAD
    if ((w & (w - 1)) || (h & (h - 1))) f[0x52] = 0x40;
    for (int i = 0; i < w * h; ++i) {
        const int tile = i >> 6, j = i & 63;
        const int x = (j & 1) | ((j >> 1) & 2) | ((j >> 2) & 4), y = ((j >> 1) & 1) | ((j >> 2) & 2) | ((j >> 3) & 4);
        const int px = (tile % (w / 8)) * 8 + x, py = (tile / (w / 8)) * 8 + y;
        std::memcpy(&f[0x53 + i * 4], bgra + (py * w + px) * 4, 4);
    }
    return f;
}

std::string key_file(int vk) {
    char id[16];
    std::snprintf(id, sizeof id, "Key_%02X", vk & 0xFF);
    return id;
}

// The key's name in the ini ("Enter", "PageUp", "A"), for its picture in FH2Mouse\KeyIcons.
std::string key_name(int vk) {
    static const struct { int vk; const char* name; } names[] = {
        {VK_RETURN, "Enter"}, {VK_ESCAPE, "Escape"}, {VK_PRIOR, "PageUp"}, {VK_NEXT, "PageDown"}, {VK_SPACE, "Space"},
        {VK_TAB, "Tab"}, {VK_BACK, "Backspace"}, {VK_INSERT, "Insert"}, {VK_DELETE, "Delete"}, {VK_HOME, "Home"},
        {VK_END, "End"}, {VK_SHIFT, "Shift"}, {VK_LSHIFT, "LShift"}, {VK_RSHIFT, "RShift"}, {VK_CONTROL, "Ctrl"},
        {VK_LCONTROL, "LCtrl"}, {VK_RCONTROL, "RCtrl"}, {VK_MENU, "Alt"}, {VK_LMENU, "LAlt"}, {VK_RMENU, "RAlt"},
        {VK_OEM_COMMA, "Comma"}, {VK_OEM_PERIOD, "Period"}, {VK_OEM_MINUS, "Minus"}, {VK_OEM_PLUS, "Equals"},
        {VK_OEM_1, "Semicolon"}, {VK_OEM_2, "Slash"}, {VK_OEM_3, "Grave"}, {VK_OEM_4, "LBracket"}, {VK_OEM_5, "Backslash"},
        {VK_OEM_6, "RBracket"}, {VK_OEM_7, "Quote"}, {VK_UP, "Up"}, {VK_DOWN, "Down"}, {VK_LEFT, "Left"}, {VK_RIGHT, "Right"},
    };
    for (const auto& n : names)
        if (n.vk == vk) return n.name;
    char name[16];
    if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) std::snprintf(name, sizeof name, "%c", vk);
    else if (vk >= VK_F1 && vk <= VK_F24) std::snprintf(name, sizeof name, "F%d", vk - VK_F1 + 1);
    else if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) std::snprintf(name, sizeof name, "Num%d", vk - VK_NUMPAD0);
    else std::snprintf(name, sizeof name, "0x%02X", vk & 0xFF);
    return name;
}

// The pictures are PNGs, in the DLL (keyicons\<key>.png) and, to change one, in
// FH2Mouse\KeyIcons\<key>.png. Windows' own codecs (WIC) read them.
IWICImagingFactory* wic() {
    static IWICImagingFactory* factory = [] {
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);   // or it's on already, either way
        IWICImagingFactory* f = nullptr;
        CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&f));
        return f;
    }();
    return factory;
}

template <class T> struct Com {
    T* p = nullptr;
    ~Com() { if (p) p->Release(); }
    T** operator&() { return &p; }
    T* operator->() { return p; }
};

// A PNG (or any picture WIC reads), from a file or, with `size`, from memory, as BGRA,
// premultiplied, 128 high and as wide as it is for that (`w`: 128 to 512, a multiple of 32, so a
// quarter of it is the small picture's).
bool load_picture(const void* source, std::size_t size, std::vector<uint8_t>& bgra, int& w) {
    IWICImagingFactory* f = wic();
    Com<IWICStream> stream;
    Com<IWICBitmapDecoder> decoder;
    Com<IWICBitmapFrameDecode> frame;
    Com<IWICBitmapScaler> scaler;
    Com<IWICFormatConverter> converter;
    UINT pw = 0, ph = 0;
    if (!f) return false;
    const bool opened =
        size ? SUCCEEDED(f->CreateStream(&stream)) &&
                   SUCCEEDED(stream->InitializeFromMemory(static_cast<BYTE*>(const_cast<void*>(source)), static_cast<DWORD>(size))) &&
                   SUCCEEDED(f->CreateDecoderFromStream(stream.p, nullptr, WICDecodeMetadataCacheOnDemand, &decoder))
             : SUCCEEDED(f->CreateDecoderFromFilename(static_cast<const wchar_t*>(source), nullptr, GENERIC_READ,
                                                      WICDecodeMetadataCacheOnDemand, &decoder));
    if (!opened || FAILED(decoder->GetFrame(0, &frame)) || FAILED(frame->GetSize(&pw, &ph)) || !pw || !ph) return false;
    const double aspect = std::max(1.0, std::min(4.0, static_cast<double>(pw) / ph));
    w = static_cast<int>(std::lround(aspect * kKeySmall / 8)) * 8 * (kKeySize / kKeySmall);
    bgra.assign(static_cast<std::size_t>(w) * kKeySize * 4, 0);
    return SUCCEEDED(f->CreateBitmapScaler(&scaler)) &&
           SUCCEEDED(scaler->Initialize(frame.p, w, kKeySize, WICBitmapInterpolationModeFant)) &&
           SUCCEEDED(f->CreateFormatConverter(&converter)) &&
           SUCCEEDED(converter->Initialize(scaler.p, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0,
                                           WICBitmapPaletteTypeCustom)) &&
           SUCCEEDED(converter->CopyPixels(nullptr, w * 4, static_cast<UINT>(bgra.size()), bgra.data()));
}

// `src` (sw x sh BGRA, premultiplied) scaled to dw x dh, each pixel the average of what it covers.
std::vector<uint8_t> shrink(const std::vector<uint8_t>& src, int sw, int sh, int dw, int dh) {
    std::vector<uint8_t> out(static_cast<std::size_t>(dw) * dh * 4);
    for (int y = 0; y < dh; ++y)
        for (int x = 0; x < dw; ++x) {
            const int x0 = x * sw / dw, x1 = std::max(x0 + 1, (x + 1) * sw / dw);
            const int y0 = y * sh / dh, y1 = std::max(y0 + 1, (y + 1) * sh / dh);
            for (int ch = 0; ch < 4; ++ch) {
                int sum = 0;
                for (int v = y0; v < y1; ++v)
                    for (int u = x0; u < x1; ++u) sum += src[(static_cast<std::size_t>(v) * sw + u) * 4 + ch];
                const int n = (x1 - x0) * (y1 - y0);
                out[(static_cast<std::size_t>(y) * dw + x) * 4 + ch] = static_cast<uint8_t>((sum + n / 2) / n);
            }
        }
    return out;
}

// Every key's picture (premultiplied BGRA, kKeySize high), by texture (Key_0D): for ANNA Drone
// Mode's button help. Written before any thread starts, then only read.
struct KeyPicture {
    int width = 0;
    std::vector<uint32_t> pixels;
};
std::map<std::string, KeyPicture> g_key_pictures;

// The wide keys, by texture (Key_0D), and how many times as wide as high they are. For each,
// <name>_Sq is the same picture in a square, for where the game has a square for it.
std::vector<std::pair<std::string, float>> g_wide_keys;

float key_aspect(const std::string& file) {
    for (const auto& k : g_wide_keys)
        if (k.first == file) return k.second;
    return 1;
}

// A key picture built into the DLL (keyicons\<key>.png, src/keyicons.rc), or null.
const void* builtin_picture(const std::string& key, std::size_t* size) {
    HRSRC res = FindResourceA(g_self, ("KEY_" + key).c_str(), MAKEINTRESOURCEA(10));   // 10: RT_RCDATA
    HGLOBAL mem = res ? LoadResource(g_self, res) : nullptr;
    const void* data = mem ? LockResource(mem) : nullptr;
    *size = data ? SizeofResource(g_self, res) : 0;
    return *size ? data : nullptr;
}

// The key's picture, at 128 high (Name_Large) and 32 (Name): FH2Mouse\KeyIcons\<key>.png if
// there's one, else the DLL's own (keyicons\<key>.png), else a light key with its name or arrows
// drawn dark on it. A picture can be wider than high.
void write_key(const std::string& name, const std::string& key, const uint8_t* glyph) {
    std::vector<uint8_t> big;
    int w = kKeySize;
    const std::wstring png = g_dir + L"\\KeyIcons\\" + widen(key) + L".png";
    std::size_t size = 0;
    const void* builtin = builtin_picture(key, &size);
    if (GetFileAttributesW(png.c_str()) != INVALID_FILE_ATTRIBUTES && load_picture(png.c_str(), 0, big, w)) {
        g_custom_keys++;
    } else if (!(builtin && load_picture(builtin, size, big, w))) {
        if (GetFileAttributesW(png.c_str()) != INVALID_FILE_ATTRIBUTES)
            logf("key icons: %ls can't be read", png.c_str());
        w = kKeySize;
        big.assign(kKeySize * kKeySize * 4, 0);
        for (int i = 0; i < kKeySize * kKeySize; ++i) {
            const int a = kKeyPlate.data[i], c = 245 - 219 * glyph[i] / 255;
            const auto v = static_cast<uint8_t>(c * a / 255);
            big[i * 4] = big[i * 4 + 1] = big[i * 4 + 2] = v;
            big[i * 4 + 3] = static_cast<uint8_t>(a);
        }
    }
    const std::string dir = "UI/textures/data_bound/FH2MouseKeys/";
    auto put = [&](const std::string& file, const std::vector<uint8_t>& large, int lw) {
        const int sw     = lw * kKeySmall / kKeySize;
        const auto little = shrink(large, lw, kKeySize, sw, kKeySmall);   // not "small": rpcndr.h defines it
        const std::string a = swatchbin(sw, kKeySmall, little.data()), b = swatchbin(lw, kKeySize, large.data());
        return g_api->set_file(g_mod, (dir + file + ".swatchbin").c_str(), a.data(), a.size()) &&
               g_api->set_file(g_mod, (dir + file + "_Large.swatchbin").c_str(), b.data(), b.size());
    };
    g_key_pictures[name] = {w, std::vector<uint32_t>(reinterpret_cast<const uint32_t*>(big.data()),
                                                     reinterpret_cast<const uint32_t*>(big.data()) + big.size() / 4)};
    if (!put(name, big, w)) return;
    if (w > kKeySize) {
        // The square one: the key as wide as the square, in its middle.
        const int h = std::max(8, kKeySize * kKeySize / w);
        const auto fit = shrink(big, w, kKeySize, kKeySize, h);
        std::vector<uint8_t> square(kKeySize * kKeySize * 4, 0);
        std::memcpy(&square[static_cast<std::size_t>((kKeySize - h) / 2) * kKeySize * 4], fit.data(), fit.size());
        if (!put(name + "_Sq", square, kKeySize)) return;
        g_wide_keys.emplace_back(name, static_cast<float>(w) / kKeySize);
    }
    g_key_files.push_back(name);
}

// Every key an action can be given on the Mods page, and those the ini gives them: the pictures
// the game will load (it doesn't see files that come later), written before it starts.
void write_key_icons() {
    std::vector<int> vks;   // FH2 ANNA's among them: its HUD shows its keys (FH2MouseKeys/Key_43...)
    for (const char* c : kKeyChoices)
        if (const int vk = vk_of(c)) vks.push_back(vk);
    for (const auto& keys : g_action_keys)
        for (int vk : keys)
            if (vk) vks.push_back(vk);
    for (int i = 0; i < kActionCount; ++i) {   // what a row or a preset can give the action
        if (const int vk = vk_of(kActions[i].keys)) vks.push_back(vk);
        for (int p = 0; p < kPresetCount; ++p)
            if (const int vk = vk_of(preset_keys(p, i))) vks.push_back(vk);
    }
    std::sort(vks.begin(), vks.end());
    vks.erase(std::unique(vks.begin(), vks.end()), vks.end());
    std::vector<uint8_t> glyph(kKeySize * kKeySize);
    for (int vk : vks) {
        std::fill(glyph.begin(), glyph.end(), 0);
        if (const int arrow = arrow_of(vk); arrow >= 0) draw_art(*kKeyArrows[arrow], glyph.data(), 1, 0, 0);
        else draw_label(key_label(vk), glyph.data());
        write_key(key_file(vk), key_name(vk), glyph.data());
    }
    // Both ways at once, for the d-pad's up / down and left / right.
    std::fill(glyph.begin(), glyph.end(), 0);
    draw_art(*kKeyArrows[0], glyph.data(), 0.62f, 0, -24), draw_art(*kKeyArrows[1], glyph.data(), 0.62f, 0, 24);
    write_key("Key_UpDown", "UpDown", glyph.data());
    std::fill(glyph.begin(), glyph.end(), 0);
    draw_art(*kKeyArrows[2], glyph.data(), 0.62f, -24, 0), draw_art(*kKeyArrows[3], glyph.data(), 0.62f, 24, 0);
    write_key("Key_LeftRight", "LeftRight", glyph.data());
    logf("key icons: %d pictures, %d of them from FH2Mouse\\KeyIcons", static_cast<int>(g_key_files.size()), g_custom_keys);
}

int action_index(const char* name) {
    for (int i = 0; i < kActionCount; ++i)
        if (!std::strcmp(kActions[i].name, name)) return i;
    return -1;
}

int first_key(const char* action) {
    const int i = action ? action_index(action) : -1;
    return i < 0 ? 0 : g_action_keys[i][0];
}

// The picture for a pad button ("X", "DPadUpDown"), or "": the key of the action that presses it
// in the menus, or while driving.
std::string key_picture(const std::wstring& button, bool driving) {
    static const struct { const wchar_t* button; const char* menu[2]; const char* driving[2]; } buttons[] = {
        {L"A", {"Accept"}, {}},
        {L"B", {"Back"}, {}},
        {L"X", {"ButtonX"}, {"Interact"}},
        {L"Y", {"ButtonY"}, {}},
        {L"Menu", {"Pause"}, {"Pause"}},
        {L"View", {"ButtonView"}, {"Interact"}},
        {L"LT", {"ButtonLT"}, {}},
        {L"RT", {"ButtonRT"}, {}},
        {L"L3", {"ButtonLS"}, {}},
        {L"R3", {"ButtonRS"}, {}},
        {L"LB", {"PreviousTab"}, {}},
        {L"RB", {"NextTab"}, {}},
        {L"DPadUp", {"Up"}, {}},
        {L"DPadDown", {"Down"}, {"Telemetry"}},
        {L"DPadLeft", {"Left"}, {"RadioPrevious"}},
        {L"DPadRight", {"Right"}, {"RadioNext"}},
        {L"DPadUpDown", {"Up", "Down"}, {}},
        {L"DPadLeftRight", {"Left", "Right"}, {"RadioPrevious", "RadioNext"}},
    };
    for (const auto& b : buttons) {
        if (button != b.button) continue;
        const char* const* actions = driving ? b.driving : b.menu;
        const int vk = first_key(actions[0]);
        if (!vk) return {};
        std::string name = key_file(vk);
        if (actions[1]) {
            const int other = first_key(actions[1]);
            if (vk == VK_UP && other == VK_DOWN) name = "Key_UpDown";
            else if (vk == VK_LEFT && other == VK_RIGHT) name = "Key_LeftRight";
        }
        return std::find(g_key_files.begin(), g_key_files.end(), name) != g_key_files.end() ? name : std::string();
    }
    return {};
}

// A BitmapImage of ours, made once (and kept for good), or null.
uint8_t* key_image(const std::string& file) {
    static std::vector<std::pair<std::string, uint8_t*>> made;   // UI thread only
    for (const auto& m : made)
        if (m.first == file) return m.second;
    const std::string uri = "/ForzaUI;component/Textures/Data_Bound/FH2MouseKeys/" + file + ".png";
    auto* text = new uint8_t[0x80]();
    game<AvuiStringFn>(RVA_AVUI_STRING)(text, uri.c_str(), -1);
    InterlockedIncrement(reinterpret_cast<volatile LONG*>(text + 0x10));   // ours: never freed
    void* converter = g_base + RVA_BITMAP_CONVERTER;   // an object, as far as it can tell: its vtable
    uint8_t* image  = nullptr;
    vfn<ConvertFromFn>(&converter, 8)(&converter, &image, nullptr, text);
    if (g_cfg.debug) logf("key icon %s: %s", file.c_str(), image ? rtti_name(image) : "not made");
    made.emplace_back(file, image);
    return image;
}

constexpr uintptr_t RVA_SET_VALUE = 0x15F8A70;   // DependencyObject::SetValue(obj, property, value)
using SetValueFn = void (*)(void* obj, void* property, void* value);

// With `aspect`, the picture can be wider than high (a wide key's own, *aspect times as wide);
// without, a wide key's is its square one.
uint8_t* find_icon_for(void* element, uint8_t* name, bool driving, float* aspect = nullptr) {
    if (aspect) *aspect = 1;
    if (keys_shown() && name) {
        const wchar_t* text = at<const wchar_t*>(name, 0x28);
        const int n         = at<int>(name, 0x30);
        static const wchar_t prefix[] = L"A_BitmapImage_ControllerButton_";
        constexpr int np = sizeof prefix / sizeof prefix[0] - 1;
        if (text && n > np && n < 128 && !std::wcsncmp(text, prefix, np)) {
            std::wstring button(text + np, n - np);
            bool large = false;
            for (const wchar_t* size : {L"_Large", L"_Medium", L"_Small"}) {
                const std::size_t len = std::wcslen(size);
                if (button.size() > len && !button.compare(button.size() - len, len, size)) {
                    large = !std::wcscmp(size, L"_Large");
                    button.resize(button.size() - len);
                }
            }
            // The telemetry's help bar, made while driving: its d-pad is the driving keys'.
            std::string file;
            if (!driving && !g_in_menu.load() && !button.compare(0, 4, L"DPad")) file = key_picture(button, true);
            if (file.empty()) file = key_picture(button, driving);
            if (!file.empty()) {
                const float a = key_aspect(file);
                if (a > 1 && !aspect) file += "_Sq";
                if (uint8_t* image = key_image(large ? file + "_Large" : file)) {
                    if (aspect) *aspect = a;
                    return image;
                }
            }
        }
    }
    return o_find_resource(element, name);
}

// A wide key's picture in the help bar: its icon (+0x1D8) and the icon's shadow (+0x1E0), boxes
// as high as they are wide in its template, are made as wide as the picture is for that height.
constexpr uintptr_t HELP_ICON = 0x1D8, HELP_ICON_SHADOW = 0x1E0;
constexpr uintptr_t RVA_WIDTH_DP  = 0x36C6958;   // FrameworkElement::WidthProperty (a float)
constexpr uintptr_t RVA_HEIGHT_DP = 0x36C5C18;   // FrameworkElement::HeightProperty (+0x20 of what 0x1649925 fills)
constexpr uintptr_t RVA_BOX_FLOAT = 0x03F5990;   // ObjectPtr* box(ObjectPtr* out, const float*)
using BoxFloatFn = void** (*)(uint8_t** out, const float* v);
bool g_widths_ok = false;   // the above are as expected

void set_icon_width(uint8_t* control, float aspect) {
    void* width_dp = at<void*>(g_base, RVA_WIDTH_DP);
    void* height_dp = at<void*>(g_base, RVA_HEIGHT_DP);
    if (!g_widths_ok || !width_dp || !height_dp) return;
    for (uintptr_t off : {HELP_ICON, HELP_ICON_SHADOW}) {
        uint8_t* icon = at<uint8_t*>(control, off);
        if (!icon) continue;
        uint8_t* boxed = nullptr;
        game<DpGetValueFn>(RVA_DP_GET_VALUE)(icon, &boxed, height_dp);
        const float h = boxed && boxed != game<UnsetFn>(RVA_UNSET_VALUE)() ? at<float>(boxed, 0x28) : 0;
        release(boxed);
        if (!(h > 0 && h < 1000)) continue;   // not a number of pixels: left as it is
        const float w = h * aspect;
        uint8_t* value = nullptr;
        game<BoxFloatFn>(RVA_BOX_FLOAT)(&value, &w);
        if (value) game<SetValueFn>(RVA_SET_VALUE)(icon, width_dp, value), release(value);
    }
}

// The help bar: the menus' keys (photo mode's are those too). It's made before the focus comes
// to its page, so where the focus is says nothing.
void set_icon_width_guarded(uint8_t* control, float aspect) {
    __try {
        set_icon_width(control, aspect);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_widths_ok = false;
        logf("the help bar's icon isn't as expected: wide keys show squeezed");
    }
}

uint8_t* find_icon(void* element, uint8_t* name) {
    float aspect = 1;
    uint8_t* image = find_icon_for(element, name, false, &aspect);
    if (element) set_icon_width_guarded(static_cast<uint8_t*>(element), aspect);
    return image;
}

// Pictures pages name in their XAML ("PRESS (A) TO START") are the game's own BitmapImages, one
// for each resource, shared by everything showing it. Their UriSource (a dependency property,
// 0x36B1178, as the constructor at 0x76EBB0 sets it) is pointed at the key's texture while the
// keys are shown, and back at the pad's after; what shows them loads the new picture. They're the
// menus' keys, but large ones are the HUD's, so they're the driving keys where those have one.
constexpr uintptr_t RVA_URI_SOURCE_DP = 0x36B1178;   // BitmapImage::UriSourceProperty

struct SharedIcon {
    const wchar_t* button;
    bool large;
    uint8_t* image   = nullptr;   // the game's (a reference of ours)
    uint8_t* pad_uri = nullptr;   // its own Uri (a reference of ours)
};
std::vector<SharedIcon> g_shared_icons;   // UI thread only

uint8_t* uri_of(uint8_t* image) {
    void* dp = at<void*>(g_base, RVA_URI_SOURCE_DP);
    uint8_t* boxed = nullptr;
    if (dp && image) game<DpGetValueFn>(RVA_DP_GET_VALUE)(image, &boxed, dp);
    if (boxed && boxed == game<UnsetFn>(RVA_UNSET_VALUE)()) release(boxed), boxed = nullptr;
    return boxed;
}

bool find_shared_icons(uint8_t* root) {
    static const wchar_t* const buttons[] = {L"A", L"B", L"X", L"Y", L"Menu", L"View", L"LB", L"RB", L"DPadUp", L"DPadDown",
                                             L"DPadLeft", L"DPadRight", L"DPadUpDown", L"DPadLeftRight",
                                             L"LT", L"RT", L"L3", L"R3"};
    for (const wchar_t* b : buttons)
        for (bool large : {false, true}) {
            char name[96];
            std::snprintf(name, sizeof name, "A_BitmapImage_ControllerButton_%ls%s", b, large ? "_Large" : "");
            auto* text = new uint8_t[0x80]();   // kept: the game may hold on to what it's asked with
            game<AvuiStringFn>(RVA_AVUI_STRING)(text, name, -1);
            InterlockedIncrement(reinterpret_cast<volatile LONG*>(text + 0x10));
            SharedIcon s{b, large};
            s.image = o_find_resource(root, text);
            if (!s.image || std::strcmp(rtti_name(s.image), ".?AVBitmapImage@AVUI@@")) continue;
            if (!(s.pad_uri = uri_of(s.image))) continue;
            InterlockedIncrement(reinterpret_cast<volatile LONG*>(s.image + 0x10));
            g_shared_icons.push_back(s);
        }
    logf("key icons: %d shared pictures", static_cast<int>(g_shared_icons.size()));
    return !g_shared_icons.empty();
}

bool g_shared_ok = false;   // SetValue is as expected

// False if they can't be found yet (no window to look from).
bool swap_shared_icons() {
    static bool found = false;
    if (!g_shared_ok) return true;
    if (!found) {
        uint8_t* root = active_window();
        if (!root || !(found = find_shared_icons(root))) return false;
    }
    void* dp = at<void*>(g_base, RVA_URI_SOURCE_DP);
    static std::vector<std::pair<std::string, uint8_t*>> uris;   // our pictures' Uris, kept
    for (const SharedIcon& s : g_shared_icons) {
        uint8_t* uri = s.pad_uri;
        if (keys_shown()) {
            std::string file = s.large ? key_picture(s.button, true) : std::string();
            if (file.empty()) file = key_picture(s.button, false);
            if (!file.empty()) {
                if (key_aspect(file) > 1) file += "_Sq";   // the page has a square for it
                if (s.large) file += "_Large";
                auto it = std::find_if(uris.begin(), uris.end(), [&](const auto& u) { return u.first == file; });
                if (it == uris.end()) uris.emplace_back(file, uri_of(key_image(file))), it = uris.end() - 1;
                if (it->second) uri = it->second;
            }
        }
        if (uri) game<SetValueFn>(RVA_SET_VALUE)(s.image, dp, uri);
    }
    return true;
}

// FH2 ANNA's buttons on the HUD (D-pad down that opens it, D-pad right that chooses, and the
// autodrive prompt's cinematic camera button) are BitmapImages of its own, resources of its control
// (HUDAnnaControl) named FH2ANNA_Icon_Open, FH2ANNA_Icon_Choose and FH2ANNA_Icon_Cinematic: found from the control (searched for from the top of the window the active
// one is in) and pointed at the ANNA keys' pictures as the shared ones are. A new HUD is a new
// control: one only we hold is gone, and searched for again.
struct AnnaIcon {
    const char* name;
    const char* action;           // its key's (kActions)
    uint8_t* image   = nullptr;   // a reference of ours
    uint8_t* pad_uri = nullptr;   // ditto
};
// The cinematic camera's prompt has a picture for each pad button it can be set to (FH2ANNA_Icon_Cinematic
// is LB's, the others _<button>): the key in each, whichever is set. One an older FH2 ANNA hasn't is skipped.
AnnaIcon g_anna_icons[] = {{"FH2ANNA_Icon_Open", "AnnaOpen"}, {"FH2ANNA_Icon_Choose", "AnnaChoose"}, {"FH2ANNA_Icon_Cinematic", "AnnaCinematic"},
                           {"FH2ANNA_Icon_Cinematic_RB", "AnnaCinematic"}, {"FH2ANNA_Icon_Cinematic_A", "AnnaCinematic"},
                           {"FH2ANNA_Icon_Cinematic_B", "AnnaCinematic"}, {"FH2ANNA_Icon_Cinematic_X", "AnnaCinematic"},
                           {"FH2ANNA_Icon_Cinematic_Y", "AnnaCinematic"}, {"FH2ANNA_Icon_Cinematic_View", "AnnaCinematic"},
                           {"FH2ANNA_Icon_Cinematic_L3", "AnnaCinematic"}, {"FH2ANNA_Icon_Cinematic_R3", "AnnaCinematic"}};
uint8_t* g_anna_control = nullptr;   // a reference of ours (UI thread only)

uint8_t* find_by_type(uint8_t* v, const char* rtti, int depth, int& visited) {
    if (!v || depth > 80 || ++visited > 30000) return nullptr;
    if (!is(type_of(v), TYPE_VISUAL)) return nullptr;
    if (!std::strcmp(rtti_name(v), rtti)) return v;
    const int n = vfn<CountFn>(v, VT_CHILD_COUNT)(v);
    for (int i = 0; i < n && i < 4096; ++i)
        if (uint8_t* f = find_by_type(vfn<ChildFn>(v, VT_CHILD)(v, i), rtti, depth + 1, visited)) return f;
    return nullptr;
}

void drop_anna_icons() {
    for (AnnaIcon& s : g_anna_icons) {
        release(s.image), release(s.pad_uri);
        s.image = s.pad_uri = nullptr;
    }
    release(g_anna_control);
    g_anna_control = nullptr;
}

bool find_anna_icons() {
    uint8_t* root = active_window();
    for (int up = 0; root && up < 200; ++up) {
        uint8_t* parent = at<uint8_t*>(root, VISUAL_PARENT);
        if (!parent) break;
        root = parent;
    }
    int visited       = 0;
    uint8_t* control  = root ? find_by_type(root, ".?AVHUDAnnaControl@@", 0, visited) : nullptr;
    if (!control) return false;
    for (AnnaIcon& s : g_anna_icons) {
        auto* text = new uint8_t[0x80]();   // kept: the game may hold on to what it's asked with
        game<AvuiStringFn>(RVA_AVUI_STRING)(text, s.name, -1);
        InterlockedIncrement(reinterpret_cast<volatile LONG*>(text + 0x10));
        uint8_t* image = o_find_resource(control, text);
        if (!image || std::strcmp(rtti_name(image), ".?AVBitmapImage@AVUI@@")) continue;
        if (!(s.pad_uri = uri_of(image))) continue;
        InterlockedIncrement(reinterpret_cast<volatile LONG*>(image + 0x10));
        s.image = image;
    }
    InterlockedIncrement(reinterpret_cast<volatile LONG*>(control + 0x10));
    g_anna_control = control;
    static bool told = false;
    if (!told) told = true, logf("FH2 ANNA's HUD: its buttons %s", g_anna_icons[0].image && g_anna_icons[1].image ? "show the keys" : "aren't as expected");
    return true;
}

void anna_icons_frame(bool stale) {
    if (!g_anna || !g_shared_ok) return;
    if (g_anna_control && at<LONG>(g_anna_control, 0x10) <= 1) drop_anna_icons();   // the HUD's gone
    if (!g_anna_control) {
        static DWORD tried = 0;
        if (tried && GetTickCount() - tried < 2000) return;
        tried = GetTickCount();
        if (!find_anna_icons()) return;
        stale = true;
    }
    if (!stale) return;
    void* dp = at<void*>(g_base, RVA_URI_SOURCE_DP);
    static std::vector<std::pair<std::string, uint8_t*>> uris;   // our pictures' Uris, kept
    for (const AnnaIcon& s : g_anna_icons) {
        if (!s.image) continue;
        uint8_t* uri  = s.pad_uri;
        const int i   = action_index(s.action);
        const int vk  = i >= 0 ? g_action_keys[i][0] : 0;
        std::string file = vk ? key_file(vk) : std::string();
        if (keys_shown() && !file.empty() && std::find(g_key_files.begin(), g_key_files.end(), file) != g_key_files.end()) {
            if (key_aspect(file) > 1) file += "_Sq";   // it's a square
            auto it = std::find_if(uris.begin(), uris.end(), [&](const auto& u) { return u.first == file; });
            if (it == uris.end()) uris.emplace_back(file, uri_of(key_image(file))), it = uris.end() - 1;
            if (it->second) uri = it->second;
        }
        if (uri) game<SetValueFn>(RVA_SET_VALUE)(s.image, dp, uri);
    }
}

// The activate buttons that showed a picture lately, each with a reference of ours, to show them
// again when the device changes: they're in the HUD, not under the active window. One only we
// hold is gone from the screen, and let go.
std::vector<uint8_t*> g_activate_buttons;   // UI thread only

uint8_t* find_activate_icon(void* element, uint8_t* name) {
    auto* el = static_cast<uint8_t*>(element);
    if (el && std::find(g_activate_buttons.begin(), g_activate_buttons.end(), el) == g_activate_buttons.end()) {
        InterlockedIncrement(reinterpret_cast<volatile LONG*>(el + 0x10));
        g_activate_buttons.push_back(el);
        if (g_activate_buttons.size() > 16) release(g_activate_buttons.front()), g_activate_buttons.erase(g_activate_buttons.begin());
    }
    return find_icon_for(element, name, true);
}

void refresh_activate_buttons() {
    for (std::size_t i = 0; i < g_activate_buttons.size();) {
        uint8_t* el = g_activate_buttons[i];
        if (at<LONG>(el, 0x10) <= 1) {
            release(el);
            g_activate_buttons.erase(g_activate_buttons.begin() + static_cast<std::ptrdiff_t>(i));
            continue;
        }
        at_ref<int>(el, ACTIVATE_SHOWN) = -1;   // else it's the control it shows, and it does nothing
        game<ActivateUpdateFn>(RVA_ACTIVATE_UPDATE)(el);
        ++i;
    }
}

// The help bar's buttons on screen, shown again (through find_icon).
void refresh_icons_in(uint8_t* v, int depth, int& visited) {
    if (!v || depth > 80 || ++visited > 20000) return;
    void* t = type_of(v);
    if (!is(t, TYPE_VISUAL)) return;
    if (is(t, TYPE_HELP_BUTTON)) {
        if (const int type = help_button_type(v); type >= 0) game<IconUpdateFn>(RVA_ICON_UPDATE)(v, type);
        return;
    }
    const int n = vfn<CountFn>(v, VT_CHILD_COUNT)(v);
    for (int i = 0; i < n && i < 4096; ++i) refresh_icons_in(vfn<ChildFn>(v, VT_CHILD)(v, i), depth + 1, visited);
}

bool any_pad();

// A pad's button, trigger or stick, pressed on any of the four (XInput, read directly: not the
// game's input, which has the keys in it).
bool pad_used() {
    using GetStateFn = DWORD(WINAPI*)(DWORD, XINPUT_STATE*);
    static GetStateFn get = [] {
        HMODULE m = LoadLibraryExW(L"xinput1_4.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        return m ? reinterpret_cast<GetStateFn>(reinterpret_cast<void*>(GetProcAddress(m, "XInputGetState"))) : nullptr;
    }();
    static XINPUT_GAMEPAD last[4] = {};
    static bool connected[4]      = {};
    static DWORD tried[4]         = {};
    if (!get) return false;
    bool used = false;
    for (DWORD p = 0; p < 4; ++p) {
        // Asking a port with no pad is slow: those only every two seconds.
        if (!connected[p] && tried[p] && GetTickCount() - tried[p] < 2000) continue;
        XINPUT_STATE s{};
        if (get(p, &s) != ERROR_SUCCESS) {
            connected[p] = false, tried[p] = GetTickCount();
            continue;
        }
        const XINPUT_GAMEPAD& g = s.Gamepad;
        const XINPUT_GAMEPAD& o = last[p];
        auto pushed = [](int now, int before, int limit) { return std::abs(now) > limit && std::abs(before) <= limit; };
        if (connected[p] && ((g.wButtons & ~o.wButtons) || pushed(g.bLeftTrigger, o.bLeftTrigger, 100) ||
                             pushed(g.bRightTrigger, o.bRightTrigger, 100) || pushed(g.sThumbLX, o.sThumbLX, 16000) ||
                             pushed(g.sThumbLY, o.sThumbLY, 16000) || pushed(g.sThumbRX, o.sThumbRX, 16000) ||
                             pushed(g.sThumbRY, o.sThumbRY, 16000)))
            used = true;
        connected[p] = true, last[p] = g;
    }
    return used;
}

// A key or a mouse button pressed, with the game in front.
bool keys_used() {
    static uint8_t down[256] = {};
    HWND h           = g_hwnd.load();
    const bool front = h && GetForegroundWindow() == h;
    bool used        = false;
    for (int vk = 1; vk < 255; ++vk) {
        const bool now = front && (GetAsyncKeyState(vk) & 0x8000);
        if (now && !down[vk]) used = true;
        down[vk] = now;
    }
    return used;
}

// Once a UI frame: which of the two was used last, and the help bar shown again when it changes.
void icons_frame() {
    // which was used last: for the game's help bar, and Drone Mode's (which doesn't need our hooks)
    if (!g_icons_hooked && !g_drone) return;
    static bool started = false;
    const bool pad = pad_used(), keys = keys_used();
    if (!started) {
        started       = true;
        g_keys_last   = !any_pad();
        g_icons_stale = true;
    }
    if (keys && !g_keys_last.load()) g_keys_last = true, g_icons_stale = true;
    else if (pad && !keys && g_keys_last.load()) g_keys_last = false, g_icons_stale = true;
    if (!g_icons_hooked) return;
    const bool stale = g_icons_stale.exchange(false);
    anna_icons_frame(stale);
    if (!stale) return;
    if (g_cfg.debug) logf("key icons: %s", keys_shown() ? "keys" : "the pad's buttons");
    if (!swap_shared_icons()) g_icons_stale = true;   // again next frame
    int visited = 0;
    refresh_icons_in(active_window(), 0, visited);
    refresh_activate_buttons();
}

void hook_key_icons() {
    if (g_key_files.empty() || !g_api->hook_call) return;
    static const uint8_t update_bytes[] = {0x89, 0x54, 0x24, 0x10, 0x56, 0x57, 0x41, 0x56, 0x48, 0x83, 0xEC, 0x40};
    static const uint8_t string_bytes[] = {0x48, 0x89, 0x4C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x30};
    bool ok = !std::memcmp(g_base + RVA_ICON_UPDATE, update_bytes, sizeof update_bytes) &&
              !std::memcmp(g_base + RVA_AVUI_STRING, string_bytes, sizeof string_bytes) &&
              at<uint8_t*>(g_base, RVA_BITMAP_CONVERTER) == g_base + RVA_CAN_CONVERT &&
              at<uint8_t*>(g_base, RVA_BITMAP_CONVERTER + 8) == g_base + RVA_CONVERT_FROM;
    auto calls_find = [](uintptr_t site) {
        const uint8_t* p = g_base + site;
        return p[0] == 0xE8 && p + 5 + at<int32_t>(p, 1) == g_base + RVA_FIND_RESOURCE;
    };
    for (uintptr_t site : RVA_ICON_FINDS) ok = ok && calls_find(site);
    // What's hooked calls on to the game's own, and does only that unless the help bar's both are.
    o_find_resource = game<FindResourceFn>(RVA_FIND_RESOURCE);
    if (ok)
        for (uintptr_t site : RVA_ICON_FINDS)
            ok = ok && g_api->hook_call(g_mod, "key icons: FindResource", g_base + site, reinterpret_cast<void*>(&find_icon),
                                        reinterpret_cast<void**>(&o_find_resource));
    // The activate buttons: optional.
    static const uint8_t activate_bytes[] = {0x40, 0x55, 0x56, 0x57, 0x41, 0x56, 0x41, 0x57, 0x48, 0x8D, 0x6C, 0x24};
    if (ok && !(!std::memcmp(g_base + RVA_ACTIVATE_UPDATE, activate_bytes, sizeof activate_bytes) &&
                calls_find(RVA_ACTIVATE_FIND) &&
                g_api->hook_call(g_mod, "key icons: FindResource (activate)", g_base + RVA_ACTIVATE_FIND,
                                 reinterpret_cast<void*>(&find_activate_icon), reinterpret_cast<void**>(&o_find_resource))))
        logf("the activate button's pictures aren't as expected: the HUD's prompts show the pad's buttons");
    g_api->enable_hooks(g_mod, nullptr);
    g_icons_hooked = ok;
    static const uint8_t set_value_bytes[] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48};
    g_shared_ok = ok && !std::memcmp(g_base + RVA_SET_VALUE, set_value_bytes, sizeof set_value_bytes);
    static const uint8_t box_float_bytes[] = {0x40, 0x53, 0x48, 0x83, 0xEC, 0x30, 0x48, 0x8B, 0xD9, 0xC7, 0x44, 0x24};
    g_widths_ok = g_shared_ok && !std::memcmp(g_base + RVA_BOX_FLOAT, box_float_bytes, sizeof box_float_bytes);
    if (ok && !g_shared_ok) logf("SetValue isn't as expected: pictures pages draw themselves show the pad's buttons");
    if (!ok) logf("the help bar's pictures aren't as expected: it shows the pad's buttons");
}

// ---- tab strips ----------------------------------------------------------------------------------
// The pause menu's tabs are Buttons that can't take the focus, in a row with the LB and RB
// pictures at its ends; the current tab is the one marked IsFocused. A click on one is turned
// into the LB or RB presses that get there.

bool laid_out(uint8_t* el) {
    const uint32_t flags = at<uint32_t>(el, VISUAL_FLAGS);
    return (((flags >> 5) & 1) || ((flags >> 6) & 1)) && at<float>(el, RENDER_SIZE) > 0;
}

bool bool_property(uint8_t* el, uintptr_t dp_rva) {
    void* dp = at<void*>(g_base, dp_rva);
    if (!dp) return false;
    uint8_t* boxed = nullptr;
    game<DpGetValueFn>(RVA_DP_GET_VALUE)(el, &boxed, dp);
    const bool v = boxed && boxed != game<UnsetFn>(RVA_UNSET_VALUE)() && at<uint8_t>(boxed, 0x28);
    release(boxed);
    return v;
}

bool is_focused(uint8_t* el) { return bool_property(el, RVA_IS_FOCUSED_DP); }

// LB/RB skip disabled tabs (TileState Disabled), which the tab template shows at half opacity.
bool tab_disabled(uint8_t* tab) {
    if (vfn<CountFn>(tab, VT_CHILD_COUNT)(tab) < 1) return false;
    uint8_t* look = vfn<ChildFn>(tab, VT_CHILD)(tab, 0);
    return look && opacity(look) < 0.75f;
}

// A button in a tab row: a row of buttons, some of which (the tabs) can't take the focus. The
// LB/RB pictures at its ends can, but focusing them takes the current tab from the row.
bool in_tab_row(uint8_t* ctl) {
    if (!is(type_of(ctl), TYPE_BUTTON_BASE)) return false;
    uint8_t* row = at<uint8_t*>(ctl, VISUAL_PARENT);
    if (!row) return false;
    int buttons = 0, tabs = 0;
    const int n = vfn<CountFn>(row, VT_CHILD_COUNT)(row);
    for (int i = 0; i < n && i < 256; ++i) {
        uint8_t* c = vfn<ChildFn>(row, VT_CHILD)(row, i);
        if (!c || !is(type_of(c), TYPE_BUTTON_BASE) || !laid_out(c)) continue;
        ++buttons;
        if (!bool_property(c, RVA_FOCUSABLE_DP)) ++tabs;
    }
    return buttons >= 3 && tabs > 0;
}

bool tab_click(uint8_t* ctl) {
    uint8_t* row = at<uint8_t*>(ctl, VISUAL_PARENT);
    if (!row) return false;
    std::vector<uint8_t*> buttons;
    const int n = vfn<CountFn>(row, VT_CHILD_COUNT)(row);
    for (int i = 0; i < n && i < 256; ++i) {
        uint8_t* c = vfn<ChildFn>(row, VT_CHILD)(row, i);
        if (c && is(type_of(c), TYPE_BUTTON_BASE) && laid_out(c)) buttons.push_back(c);
    }
    if (buttons.size() < 3) return false;
    int key = 0, steps = 0;
    if (ctl == buttons.front()) {
        key = KEY_LB, steps = 1;
    } else if (ctl == buttons.back()) {
        key = KEY_RB, steps = 1;
    } else {
        int cur = -1;
        for (size_t i = 1; i + 1 < buttons.size(); ++i)
            if (is_focused(buttons[i])) cur = static_cast<int>(i);
        if (cur < 0) {
            if (g_cfg.debug) logf("tab click: no current tab (%zu buttons)", buttons.size());
            return false;
        }
        // Count only the tabs LB/RB stop at: those in the current tab's state.
        if (tab_disabled(ctl)) return true;   // a tab LB/RB can't reach: nothing to do
        int pos = 0, target = 0, here = 0;
        for (size_t i = 1; i + 1 < buttons.size(); ++i) {
            if (tab_disabled(buttons[i])) continue;
            if (static_cast<int>(i) == cur) here = pos;
            if (buttons[i] == ctl) target = pos;
            ++pos;
        }
        key   = target > here ? KEY_RB : KEY_LB;
        steps = std::abs(target - here);
    }
    if (g_cfg.debug) logf("tab click: %d x %s", steps, key == KEY_RB ? "RB" : "LB");
    for (int i = 0; i < steps; ++i) {
        send_key(key, false, i ? frames(10) : 0);   // a moment for each tab to come in
        send_key(key, true);
    }
    return true;
}

// ---- the map ------------------------------------------------------------------------------------
// The map has a cursor of its own, which the stick moves over the map and the camera follows
// once it strays too far from the middle. Here the map cursor sits under the mouse, a drag moves
// the camera (and the cursor with it), a click is A on the spot under the cursor (select an
// icon, set a waypoint), and the wheel zooms. While the mouse is in use the camera's leash is
// let out, so pointing near an edge doesn't scroll the map.

uint8_t* map_controller() {
    uint8_t* f = focused_object();
    if (!f || at<uint8_t*>(f, 0) != g_base + VT_MAP_SCENE) return nullptr;
    uint8_t* c = at<uint8_t*>(f, MAP_CONTROLLER);
    return c && at<uint8_t*>(c, 0) == g_base + VT_MAP_CONTROLLER ? c : nullptr;
}

float ui_height() {
    uint8_t* app = at<uint8_t*>(g_base, RVA_APPLICATION);
    const float h = app ? at<float>(app, 0x54) : 0.f;
    return h > 0 ? h : 1080.f;
}

float ui_width() {
    uint8_t* app = at<uint8_t*>(g_base, RVA_APPLICATION);
    const float w = app ? at<float>(app, 0x50) : 0.f;
    return w > 0 ? w : 1920.f;
}

float& f32(uint8_t* c, uintptr_t off) { return *reinterpret_cast<float*>(c + off); }

// World units per UI pixel: the map shows `zoom` from the middle to the top edge.
float map_scale(uint8_t* c) { return 2.f * f32(c, MAP_ZOOM_SHOWN) / ui_height(); }

uint8_t* g_leash_map = nullptr;   // the controller whose leash is let out (compared only)
float g_leash_saved  = 0.f;

void map_leash(uint8_t* c, bool out) {
    if (out && g_leash_map != c) {
        g_leash_map   = c;
        g_leash_saved = f32(c, MAP_LEASH);
        f32(c, MAP_LEASH) = 1000.f;
        if (g_cfg.debug) logf("map: leash %.3f let out", g_leash_saved);
    } else if (!out && g_leash_map == c && c) {
        f32(c, MAP_LEASH) = g_leash_saved;
        g_leash_map       = nullptr;
    }
}

// The map cursor to the spot under the mouse.
void map_point(uint8_t* c) {
    float pt[2];
    if (!cursor_ui(pt)) return;
    map_leash(c, true);
    const float k  = map_scale(c);
    f32(c, MAP_POS_X) = f32(c, MAP_CAM_X) + (pt[0] - ui_width() / 2) * k;
    f32(c, MAP_POS_Z) = f32(c, MAP_CAM_Z) - (pt[1] - ui_height() / 2) * k;
}

// Moves what's on the map by (dx, dy) UI pixels: the camera, where it's going and where it is.
void map_drag(uint8_t* c, float dx, float dy) {
    const float k = map_scale(c);
    for (uintptr_t off : {MAP_TARGET_X, MAP_CAM_X}) f32(c, off) -= dx * k;
    for (uintptr_t off : {MAP_TARGET_Z, MAP_CAM_Z}) f32(c, off) += dy * k;
}

bool g_map_drag = false, g_map_dragged = false;
float g_map_last[2] = {}, g_map_start[2] = {};

bool map_press() {
    uint8_t* c = map_controller();
    if (!c || !cursor_ui(g_map_start)) return false;
    g_map_drag    = true;
    g_map_dragged = false;
    g_map_last[0] = g_map_start[0], g_map_last[1] = g_map_start[1];
    return true;
}

bool on_move_map() {
    uint8_t* c = map_controller();
    if (!c) {
        g_map_drag = false;
        return false;
    }
    float pt[2];
    if (g_map_drag && cursor_ui(pt)) {
        if (std::abs(pt[0] - g_map_start[0]) + std::abs(pt[1] - g_map_start[1]) > 8) g_map_dragged = true;
        if (g_map_dragged) map_drag(c, pt[0] - g_map_last[0], pt[1] - g_map_last[1]);
        g_map_last[0] = pt[0], g_map_last[1] = pt[1];
    }
    map_point(c);
    return true;
}

bool map_release() {
    if (!g_map_drag) return false;
    g_map_drag = false;
    uint8_t* c = map_controller();
    if (g_cfg.debug && c)
        logf("map %s: cursor %.1f,%.1f target %.1f,%.1f cam %.1f,%.1f angle %.3f aspect %.3f zoom %.1f cam90 %.1f,%.1f,%.1f b8 %.3f",
             g_map_dragged ? "drag" : "click", f32(c, MAP_POS_X), f32(c, MAP_POS_Z), f32(c, MAP_TARGET_X),
             f32(c, MAP_TARGET_Z), f32(c, MAP_CAM_X), f32(c, MAP_CAM_Z), f32(c, 0xC8), f32(c, 0xBC),
             f32(c, MAP_ZOOM_SHOWN), f32(c, 0x90), f32(c, 0x94), f32(c, 0x98), f32(c, 0xB8));
    if (!c || g_map_dragged) return true;
    map_point(c);
    if (g_cfg.debug) logf("map click at (%.0f,%.0f), zoom %.1f (%.2f)", g_map_start[0], g_map_start[1], f32(c, MAP_ZOOM_SHOWN), f32(c, MAP_ZOOM));
    send_key(KEY_A, false, frames(1));
    send_key(KEY_A, true);
    return true;
}

bool map_wheel(int delta) {
    uint8_t* c = map_controller();
    if (!c) return false;
    const float lo = std::min(f32(c, MAP_ZOOM_MIN), f32(c, MAP_ZOOM_MAX));
    const float hi = std::max(f32(c, MAP_ZOOM_MIN), f32(c, MAP_ZOOM_MAX));
    f32(c, MAP_ZOOM) = std::max(lo, std::min(hi, f32(c, MAP_ZOOM) - 0.1f * delta / WHEEL_DELTA));
    if (g_cfg.debug) logf("map zoom %.2f [%.2f, %.2f]", f32(c, MAP_ZOOM), lo, hi);
    return true;
}

// Each frame: the leash back in once the mouse rests (the cursor has hidden) or the map is gone.
void map_frame() {
    if (!g_leash_map) return;
    uint8_t* c = map_controller();
    if (c != g_leash_map) {
        g_leash_map = nullptr;   // that map is closed; its controller may be gone
        return;
    }
    if (!cursor_wanted()) map_leash(c, false);
}

// ---- rows, sliders and selectors ----------------------------------------------------------------
// Lists keep the focus on a control inside each row (a Slider, a SpinnerControl, a Button), or
// on the list itself with a selection that the pad moves (the car carousels, the Stats and
// Tuning tabs and lists). A click focuses the row's control, or walks the selection to the
// item clicked with the d-pad (or LB/RB, where that's what moves it), as the pad would.

uint8_t* dp_object(uint8_t* el, uintptr_t dp_rva) {
    void* dp = at<void*>(g_base, dp_rva);
    if (!dp || !el) return nullptr;
    uint8_t* boxed = nullptr;
    game<DpGetValueFn>(RVA_DP_GET_VALUE)(el, &boxed, dp);
    uint8_t* v = boxed == game<UnsetFn>(RVA_UNSET_VALUE)() ? nullptr : boxed;
    release(boxed);   // identity only: whoever holds the value keeps it alive
    return v;
}

bool is_adjustable(uint8_t* el) {
    void* t = type_of(el);
    return is(t, TYPE_SLIDER) || is(t, TYPE_SPINNER);
}

// The control to focus for `ctl`: itself when it can take the focus, else the first control
// inside it that can (a settings row's slider or spinner).
uint8_t* focus_target_in(uint8_t* v, int depth) {
    if (!v || depth > 24) return nullptr;
    void* t = type_of(v);
    if (!is(t, TYPE_VISUAL)) return nullptr;
    if (is(t, TYPE_UIELEMENT) && laid_out(v) && control_kind(t) == 2 && !is(t, TYPE_LIST_BOX_ITEM) &&
        bool_property(v, RVA_FOCUSABLE_DP))
        return v;
    const int n = vfn<CountFn>(v, VT_CHILD_COUNT)(v);
    for (int i = 0; i < n && i < 256; ++i)
        if (uint8_t* f = focus_target_in(vfn<ChildFn>(v, VT_CHILD)(v, i), depth + 1)) return f;
    return nullptr;
}

// The game's ItemsSelector `el` is in, if any: its items (tabs, carousel cars) are selected, not
// focused; their buttons can take the focus, but A on them does the page's thing, not theirs.
uint8_t* items_selector_of(uint8_t* el) {
    for (int d = 0; el && d < 40; ++d, el = at<uint8_t*>(el, VISUAL_PARENT)) {
        if (!is(type_of(el), TYPE_VISUAL)) break;
        if (!std::strcmp(rtti_name(el), ".?AVItemsSelector@@")) return el;
    }
    return nullptr;
}

uint8_t* focus_target(uint8_t* ctl) {
    if (!ctl || is_help_button(ctl) || in_tab_row(ctl) || items_selector_of(ctl)) return nullptr;
    if (bool_property(ctl, RVA_FOCUSABLE_DP)) return ctl;
    if (uint8_t* f = focus_target_in(ctl, 0)) return f;
    // Part of something that can't take the focus (a balance slider's visible halves): the
    // row's control (there, an invisible slider holding the value).
    for (uint8_t* e = at<uint8_t*>(ctl, VISUAL_PARENT); e; e = at<uint8_t*>(e, VISUAL_PARENT)) {
        void* t = type_of(e);
        if (!is(t, TYPE_VISUAL) || is(t, TYPE_SELECTOR)) break;
        if (is(t, TYPE_LIST_BOX_ITEM)) return focus_target_in(e, 0);
    }
    return nullptr;
}

// -1 / +1: the click is on the left / right part of a slider or spinner (where its arrows are).
// The RepeatButtons (arrows) laid out inside `v`.
// With `all`, those not laid out too.
void arrows_in(uint8_t* v, std::vector<uint8_t*>& out, int depth, bool all = false) {
    if (!v || depth > 24 || out.size() > 8) return;
    void* t = type_of(v);
    if (!is(t, TYPE_VISUAL)) return;
    if (is(t, TYPE_REPEAT_BUTTON) && (all || laid_out(v))) out.push_back(v);
    const int n = vfn<CountFn>(v, VT_CHILD_COUNT)(v);
    for (int i = 0; i < n && i < 256; ++i) arrows_in(vfn<ChildFn>(v, VT_CHILD)(v, i), out, depth + 1, all);
}

int adjust_side(uint8_t* control, const float pt[2]) {
    uint8_t* root = active_window();
    float r[4];
    if (!bounds(control, root, r)) return 0;
    const float w = r[2] - r[0];
    std::vector<uint8_t*> any;
    if (is(type_of(control), TYPE_SLIDER)) arrows_in(control, any, 0, true);
    // A slider with no RepeatButtons at all is the platform's choice row (the Mods page's, and
    // Keyboard & Mouse's keys): its Track is hidden and it's drawn as a spinner, "<  value  >"
    // in the right half, so it's clicked as one (below).
    if (!any.empty()) {
        // A slider: its track's "decrease" RepeatButton runs from the track's start to the
        // value. Left of the value is down, right of it up; the label before the track, nothing.
        std::vector<uint8_t*> parts;
        arrows_in(control, parts, 0);
        for (uint8_t* part : parts) {
            float d[4];
            if (!bounds(part, root, d)) continue;
            if (pt[0] < d[0] - 24.f) return 0;
            return pt[0] < d[2] ? -1 : 1;
        }
        // At its minimum the decrease part has no width: the right half of the row is "up".
        return pt[0] > r[0] + w * 0.45f ? 1 : 0;
    }
    // A spinner: label, then "<  value  >" in the right half; the value's middle is about three
    // quarters of the way across.
    const float at = (pt[0] - r[0]) / w;
    if (at < 0.45f) return 0;
    return at < 0.75f ? -1 : 1;
}

// Holding the mouse on a slider's side repeats, like holding the d-pad.
struct Adjust {
    int key = 0;
    DWORD next = 0;
} g_adjust;

// -- selectors --

struct SelHit {
    uint8_t* root;
    float pt[2];
    uint8_t* sel  = nullptr;   // the selector
    uint8_t* item = nullptr;   // the item under the cursor (its DataContext)
    int visited   = 0;
};

void selector_hit_in(SelHit& h, uint8_t* v, uint8_t* sel, int depth) {
    if (!v || depth > 80 || ++h.visited > 20000) return;
    void* t = type_of(v);
    if (!is(t, TYPE_VISUAL) || opacity(v) <= 0.01f) return;
    if (is(t, TYPE_SELECTOR)) sel = v;
    if (sel && v != sel && is(t, TYPE_UIELEMENT)) {
        float r[4];
        if (bounds(v, h.root, r) && h.pt[0] >= r[0] && h.pt[0] < r[2] && h.pt[1] >= r[1] && h.pt[1] < r[3]) {
            uint8_t* item = dp_object(v, RVA_DATA_CONTEXT_DP);
            if (item && item != dp_object(sel, RVA_DATA_CONTEXT_DP)) h.sel = sel, h.item = item;
        }
    }
    const int n = vfn<CountFn>(v, VT_CHILD_COUNT)(v);
    for (int i = 0; i < n && i < 4096; ++i) selector_hit_in(h, vfn<ChildFn>(v, VT_CHILD)(v, i), sel, depth + 1);
}

bool selector_hit(const float pt[2], uint8_t** sel, uint8_t** item) {
    SelHit h{active_window(), {pt[0], pt[1]}};
    if (!h.root) return false;
    selector_hit_in(h, h.root, nullptr, 0);
    *sel = h.sel, *item = h.item;
    return h.sel != nullptr;
}

// Where the item's container is: the first element under `sel` whose DataContext is `item`.
bool item_center_in(uint8_t* v, uint8_t* item, uint8_t* root, float c[2], int depth) {
    if (!v || depth > 40) return false;
    void* t = type_of(v);
    if (!is(t, TYPE_VISUAL)) return false;
    if (is(t, TYPE_UIELEMENT) && dp_object(v, RVA_DATA_CONTEXT_DP) == item) {
        float r[4];
        if (!bounds(v, root, r)) return false;
        c[0] = (r[0] + r[2]) / 2, c[1] = (r[1] + r[3]) / 2;
        return true;
    }
    const int n = vfn<CountFn>(v, VT_CHILD_COUNT)(v);
    for (int i = 0; i < n && i < 4096; ++i)
        if (item_center_in(vfn<ChildFn>(v, VT_CHILD)(v, i), item, root, c, depth + 1)) return true;
    return false;
}

void tap(int key, float wait = 0) {
    send_key(key, false, wait);
    send_key(key, true);
}

struct Walk {
    bool active = false;
    uint8_t* sel = nullptr;
    uint8_t* item = nullptr;
    float pt[2] = {};
    int steps = 0, stalls = 0;
    float wait = 0;   // seconds
    uint8_t* last = nullptr;
    bool bumpers = false;   // LB/RB instead of left/right
    int dx = 0, dy = 0;     // the last direction
} g_walk;

void walk_stop(const char* why) {
    if (g_cfg.debug) logf("selection walk: %s after %d steps", why, g_walk.steps);
    g_walk.active = false;
}

void walk_step() {
    if (!g_walk.active || !g_keys.empty()) return;
    if (g_walk.wait > 0) {
        g_walk.wait -= g_ui_dt;
        return;
    }
    // Still there? The same selector under the same spot (what's in it may have scrolled).
    uint8_t *sel = nullptr, *item = nullptr;
    if (!selector_hit(g_walk.pt, &sel, &item) || sel != g_walk.sel) return walk_stop("gone");
    uint8_t* cur = dp_object(sel, RVA_SELECTED_ITEM_DP);
    if (cur == g_walk.item) return walk_stop("there");
    if (g_walk.steps && cur == g_walk.last) {
        if (++g_walk.stalls >= 3) return walk_stop("stuck");
        if (g_walk.dx && items_selector_of(g_walk.sel) == g_walk.sel)
            g_walk.bumpers = !g_walk.bumpers;   // that way didn't move it: the other
    } else {
        g_walk.stalls = 0;
    }
    float t[2], c[2];
    uint8_t* root = active_window();
    // Sideways only in the game's ItemsSelectors (tabs, carousels): in a list, left/right would
    // change the focused row's slider or option.
    const bool sideways_ok = items_selector_of(sel) == sel;
    if (cur && item_center_in(sel, g_walk.item, root, t, 0) && item_center_in(sel, cur, root, c, 0)) {
        const float dx = t[0] - c[0], dy = t[1] - c[1];
        if (sideways_ok && std::abs(dx) >= std::abs(dy)) g_walk.dx = dx > 0 ? 1 : -1, g_walk.dy = 0;
        else if (std::abs(dy) > 4.f) g_walk.dx = 0, g_walk.dy = dy > 0 ? 1 : -1;
        else return walk_stop("no way there");
    } else if (!g_walk.dx && !g_walk.dy) {
        return walk_stop("lost");
    }
    int key;
    if (g_walk.dx) key = g_walk.bumpers ? (g_walk.dx > 0 ? KEY_RB : KEY_LB) : (g_walk.dx > 0 ? KEY_RIGHT : KEY_LEFT);
    else key = g_walk.dy > 0 ? KEY_DOWN : KEY_UP;
    tap(key);
    g_walk.last = cur;
    g_walk.wait = frames(8);
    if (++g_walk.steps > 40) walk_stop("too far");
}

void walk_start(uint8_t* sel, uint8_t* item, const float pt[2]) {
    g_walk        = Walk{};
    g_walk.active = true;
    // A selector without the focus (a tab row over a list) moves with LB/RB: the d-pad goes to
    // whatever has the focus (a tuning slider would change).
    g_walk.bumpers = focused_object() != sel && items_selector_of(sel) == sel;
    g_walk.sel = sel, g_walk.item = item;
    g_walk.pt[0] = pt[0], g_walk.pt[1] = pt[1];
    walk_step();
}

// -- what counts as a menu --

const char* focus_name() {
    uint8_t* f = focused_object();
    return f ? rtti_name(f) : "";
}

// A menu page has the focus (not the driving HUD's window). The telemetry takes the focus too,
// but it's shown while driving: the driving keys drive, and close it and turn its pages.
// The game is taking driving input: its driving controls (context 0) are on for the current player. Not on a race's
// results, which are drawn in the driving HUD's window, so its focus alone doesn't tell.
bool driving_controls_on() {
    if (!o_get_digital) return true;   // can't tell: the focus decides, as before
    uint8_t* all = at<uint8_t*>(g_base, RVA_INPUTS);
    uint8_t* cur = all ? at<uint8_t*>(all, INPUTS_CURRENT) : nullptr;
    return !cur || game<ContextActiveFn>(RVA_CONTEXT_ACTIVE)(cur, kDriving);
}

bool in_menu() {
    if (drone_flying()) return false;   // the drone has the driving input, as a menu would, but it's no menu
    const char* n = focus_name();
    if (!*n || !std::strcmp(n, ".?AVTelemetry@@")) return false;
    if (std::strcmp(n, ".?AVUIWindow@@") != 0) return true;
    return !driving_controls_on();   // the HUD's window, but nothing to drive: a race's results
}

// Screens with nothing to point at, where any click is A: the title screen, videos, the wheelspin.
bool click_anywhere_screen() {
    const char* n = focus_name();
    return !std::strcmp(n, ".?AVSplash@@") || !std::strcmp(n, ".?AVVideoPlayer@@") || !std::strcmp(n, ".?AVTombola@@");
}

// ---- photo mode ---------------------------------------------------------------------------------
// The photo camera (CCamPhotoMode) reads the sticks into fields of its own each frame. After it
// does, the mouse and keyboard are added: dragging with the left button looks around (the right
// stick), W A S D move (the left stick), Q / E go down and up, the wheel zooms.

constexpr uintptr_t RVA_PHOTO_INPUT_SLOT = 0x2C6C5B0;   // CCamPhotoMode vtable slot 7: read the input
constexpr uintptr_t RVA_PHOTO_INPUT      = 0x103F660;
constexpr uintptr_t PHOTO_MOVE_X  = 0x1F0;   // the camera's stick values, -1..1: forward/back
constexpr uintptr_t PHOTO_MOVE_Y  = 0x1F4;   // right/left
constexpr uintptr_t PHOTO_RISE    = 0x1F8;   // up/down
constexpr uintptr_t PHOTO_LOOK_X  = 0x1FC;   // turning (0x204/0x208 are zoom and roll)
constexpr uintptr_t PHOTO_LOOK_Y  = 0x200;
constexpr uintptr_t PHOTO_ZOOM    = 0x204;   // zoom (0x208 is roll)

using PhotoInputFn = void (*)(void* cam);
PhotoInputFn o_photo_input = nullptr;

// Mouse movement for the camera, in UI pixels, gathered on the UI thread and used up by the camera.
std::atomic<float> g_photo_look_x{0}, g_photo_look_y{0}, g_photo_move_x{0}, g_photo_move_y{0};
std::atomic<float> g_photo_zoom{0};   // seconds left to zoom in (+) or out (-), from the wheel

std::atomic<bool> g_photo_keys{false};   // photo mode is up and the game window is in front

float key_axis(int minus, int plus) {
    return ((GetAsyncKeyState(plus) & 0x8000) ? 1.f : 0.f) - ((GetAsyncKeyState(minus) & 0x8000) ? 1.f : 0.f);
}

float stick(float pixels, float per_pixel) { return std::max(-1.f, std::min(1.f, pixels * per_pixel)); }

void photo_input(void* cam) {
    o_photo_input(cam);
    auto* c = static_cast<uint8_t*>(cam);
    // The mouse's share of a frame, worked out once a frame and given to every camera that asks:
    // in the Autoshow two read the input each frame (an idle one first), and the first would
    // take it all from the one on screen.
    static uint32_t frame = ~0u;
    static float look[2], move[2], zoom;
    if (const uint32_t now = g_input_frame.load(); !now || now != frame) {
        frame = now;
        const float lx = take(g_photo_look_x), ly = take(g_photo_look_y);
        const float mx = take(g_photo_move_x), my = take(g_photo_move_y);
        static FrameClock clock;
        const float dt = clock.tick(), k = 0.05f * kTunedDt / dt;
        look[0] = stick(lx, k), look[1] = stick(ly, k);
        move[0] = stick(mx, k), move[1] = stick(my, k);
        // The wheel's zoom, held for its time: the last frame of it only partly.
        float z = g_photo_zoom.load();
        zoom    = 0;
        if (z) {
            const float step = std::min(std::abs(z), dt);
            zoom = (z > 0 ? 1 : -1) * step / dt;
            if (!g_photo_zoom.compare_exchange_strong(z, z > 0 ? z - step : z + step)) zoom = 0;
        }
    }
    if (look[0] || look[1]) f32(c, PHOTO_LOOK_X) -= look[0], f32(c, PHOTO_LOOK_Y) -= look[1];
    if (move[0] || move[1]) f32(c, PHOTO_MOVE_X) -= move[0], f32(c, PHOTO_MOVE_Y) -= move[1];
    if (zoom) f32(c, PHOTO_ZOOM) += zoom;
    if (g_photo_keys.load()) {
        f32(c, PHOTO_MOVE_X) += key_axis('S', 'W');   // 0x1F0 is forward/back
        f32(c, PHOTO_MOVE_Y) += key_axis('A', 'D');   // 0x1F4 is right/left
        f32(c, PHOTO_RISE) += key_axis('Q', 'E');
    }
    if (g_cfg.debug) {
        static DWORD last = 0;
        bool any = false;
        for (uintptr_t o = 0x1F0; o <= 0x208; o += 4) any |= f32(c, o) != 0.f;
        if (any && GetTickCount() - last > 300) {
            last = GetTickCount();
            logf("photo camera input: 1f0 %.2f 1f4 %.2f 1f8 %.2f 1fc %.2f 200 %.2f 204 %.2f 208 %.2f (layout %d)",
                 f32(c, 0x1F0), f32(c, 0x1F4), f32(c, 0x1F8), f32(c, 0x1FC), f32(c, 0x200), f32(c, 0x204), f32(c, 0x208),
                 at<uint8_t>(c, 0x22D));
        }
    }
}

bool in_photo_mode() { return !std::strcmp(focus_name(), ".?AVPhotoModeScene@@"); }

// 0: no drag; 1: looking (left button); 2: moving (right button).
int g_photo_drag = 0;
bool g_photo_dragged = false;
float g_photo_last[2] = {}, g_photo_start[2] = {};

bool photo_press(int kind) {
    if (!in_photo_mode() || !cursor_ui(g_photo_start)) return false;
    g_photo_drag    = kind;
    g_photo_dragged = false;
    g_photo_last[0] = g_photo_start[0], g_photo_last[1] = g_photo_start[1];
    return true;
}

bool on_move_photo() {
    if (!g_photo_drag) return false;
    float pt[2];
    if (!in_photo_mode() || !cursor_ui(pt)) {
        g_photo_drag = 0;
        return false;
    }
    if (std::abs(pt[0] - g_photo_start[0]) + std::abs(pt[1] - g_photo_start[1]) > 6) g_photo_dragged = true;
    if (g_photo_dragged) {
        const float dx = pt[0] - g_photo_last[0], dy = pt[1] - g_photo_last[1];
        add(g_photo_look_x, dx), add(g_photo_look_y, dy);
    }
    g_photo_last[0] = pt[0], g_photo_last[1] = pt[1];
    return true;
}

// The button's release: true when it was a drag (and so nothing else should happen).
bool photo_release() {
    if (!g_photo_drag) return false;
    g_photo_drag = 0;
    return g_photo_dragged;
}

bool photo_wheel(int delta) {
    if (!in_photo_mode()) return false;
    add(g_photo_zoom, frames(10) * delta / WHEEL_DELTA);
    return true;
}

using GetAnalogFn = float (*)(void* input, int context, int control);
GetAnalogFn o_get_analog = nullptr;
std::atomic<float> g_cam_stick_x{0}, g_cam_stick_y{0};   // see get_analog
// The garage camera (0xAA02E0, Update(camera, input)): mode 0 orbits by the right stick, and
// +0xA8 is its distance (the game's presets: -2, -3.5, -5; further is more negative). The
// wheel zooms it: steps waiting for the camera's next update.
constexpr uintptr_t RVA_GARAGE_CAMERA = 0xAA02E0;
constexpr uintptr_t GARAGE_CAM_MODE = 0x15C, GARAGE_CAM_DISTANCE = 0xA8;
using GarageCameraFn = void (*)(void* cam, void* input);
GarageCameraFn o_garage_camera = nullptr;
std::atomic<int> g_garage_zoom{0};          // wheel notches, + in
std::atomic<DWORD> g_garage_seen{0};         // when the garage camera last updated

void garage_camera(void* cam, void* input) {
    g_garage_seen = GetTickCount();
    if (const int z = g_garage_zoom.exchange(0)) {
        auto* c = static_cast<uint8_t*>(cam);
        if (at<int>(c, GARAGE_CAM_MODE) == 0) {
            float& d = *reinterpret_cast<float*>(c + GARAGE_CAM_DISTANCE);
            d = std::max(-8.f, std::min(-1.5f, d + 0.3f * z));
            if (g_cfg.debug) logf("garage camera distance %.2f", d);
        }
    }
    o_garage_camera(cam, input);
}

bool garage_wheel(int delta) {
    if (!o_garage_camera || GetTickCount() - g_garage_seen.load() > 200) return false;
    g_garage_zoom += delta / WHEEL_DELTA;
    return true;
}

bool g_cam_drag = false;
float g_cam_last[2] = {}, g_cam_dx = 0, g_cam_dy = 0;   // UI thread

bool cam_press() {
    if (!o_get_analog || !in_menu() || !cursor_ui(g_cam_last)) return false;
    g_cam_drag = true;
    g_cam_dx = g_cam_dy = 0;
    return true;
}

bool on_move_cam() {
    if (!g_cam_drag) return false;
    float pt[2];
    if (!cursor_ui(pt)) return true;
    g_cam_dx += pt[0] - g_cam_last[0], g_cam_dy += pt[1] - g_cam_last[1];
    g_cam_last[0] = pt[0], g_cam_last[1] = pt[1];
    return true;
}

// Before the UI's update: this frame's drag as stick values ("grab and turn the car").
void cam_frame_begin() {
    const float k = 0.04f * kTunedDt / g_ui_dt;
    g_cam_stick_x = g_cam_drag ? -g_cam_dx * k : 0.f;
    g_cam_stick_y = g_cam_drag ? g_cam_dy * k : 0.f;
    g_cam_dx = g_cam_dy = 0;
}

// Which way a list scrolls: its items (the topmost elements whose DataContext is an item, not
// the list's own) overflow it sideways (the car carousels, the manufacturer grid) or not.
void item_boxes(uint8_t* v, uint8_t* own, uint8_t* root, float box[4], int& n, int depth) {
    if (!v || depth > 40 || n > 400) return;
    void* t = type_of(v);
    if (!is(t, TYPE_VISUAL)) return;
    if (is(t, TYPE_UIELEMENT) && depth > 0) {
        uint8_t* dc = dp_object(v, RVA_DATA_CONTEXT_DP);
        if (dc && dc != own) {
            float r[4];
            if (laid_out(v) && layout_bounds(v, root, r)) {
                box[0] = std::min(box[0], r[0]), box[1] = std::min(box[1], r[1]);
                box[2] = std::max(box[2], r[2]), box[3] = std::max(box[3], r[3]);
                ++n;
            }
            return;
        }
    }
    const int c = vfn<CountFn>(v, VT_CHILD_COUNT)(v);
    for (int i = 0; i < c && i < 4096; ++i) item_boxes(vfn<ChildFn>(v, VT_CHILD)(v, i), own, root, box, n, depth + 1);
}

bool scrolls_sideways(uint8_t* el) {
    // The list: the focused element if it's one, else its nearest list ancestor.
    uint8_t* sel = el;
    while (sel && is(type_of(sel), TYPE_VISUAL) && !is(type_of(sel), TYPE_SELECTOR)) sel = at<uint8_t*>(sel, VISUAL_PARENT);
    if (!sel || !is(type_of(sel), TYPE_SELECTOR)) return false;
    uint8_t* root = active_window();
    float r[4], box[4] = {1e30f, 1e30f, -1e30f, -1e30f};
    int n = 0;
    if (!layout_bounds(sel, root, r)) return false;
    item_boxes(sel, dp_object(sel, RVA_DATA_CONTEXT_DP), root, box, n, 0);
    if (n < 2) return false;
    const float over_x = std::max(0.f, r[0] - box[0]) + std::max(0.f, box[2] - r[2]);
    const float over_y = std::max(0.f, r[1] - box[1]) + std::max(0.f, box[3] - r[3]);
    return over_x > 4.f && over_x > over_y;
}

// The click: what it's on decides what the pad would have done.
void click_press() {
    float pt[2];
    if (!cursor_ui(pt)) return;
    uint8_t* ctl = hovered_control();
    if (help_click(ctl)) return;
    if (map_press()) return;
    if (ctl && in_tab_row(ctl)) {
        tab_click(ctl);
        return;
    }
    if (uint8_t* target = focus_target(ctl)) {
        const bool was = focused_object() == target;
        if (!was) focus(target);
        if (focused_object() != target) {
            if (g_cfg.debug) logf("%s didn't take the focus", rtti_name(target));
        } else if (is_adjustable(target)) {
            if (const int side = adjust_side(target, pt)) {
                g_adjust.key  = side < 0 ? KEY_LEFT : KEY_RIGHT;
                g_adjust.next = GetTickCount() + 400;
                tap(g_adjust.key);
            }
        } else {
            send_key(KEY_A, false);
            g_a_held = true;
        }
        return;
    }
    uint8_t *sel = nullptr, *item = nullptr;
    if (selector_hit(pt, &sel, &item)) {
        if (dp_object(sel, RVA_SELECTED_ITEM_DP) == item) {
            // Already selected: A, where the selector is what has the focus (a car carousel).
            // A tab row's A would be the page's (tuning: apply the setup).
            if (focused_object() == sel) {
                if (g_cfg.debug) logf("click on the selected item of %s: A", rtti_name(sel));
                send_key(KEY_A, false);
                g_a_held = true;
            }
        } else {
            if (g_cfg.debug) logf("click on an item of %s: walking the selection there", rtti_name(sel));
            walk_start(sel, item, pt);
        }
        return;
    }
    if (photo_press(1)) return;   // photo mode: drag to look around
    if (!click_anywhere_screen() && cam_press()) return;   // empty space in a menu: turn the camera
    if (click_anywhere_screen()) {
        send_key(KEY_A, false);
        g_a_held = true;
    } else if (g_cfg.debug) {
        logf("click on nothing (%s): ignored", focus_name());
    }
}

void click_frame() {
    walk_step();
    if (g_adjust.key && GetTickCount() >= g_adjust.next && g_keys.empty()) {
        tap(g_adjust.key);
        g_adjust.next = GetTickCount() + 90;
    }
}

void on_move() {
    if (on_move_cam()) return;
    if (on_move_photo()) return;
    if (on_move_map()) return;
    uint8_t* ctl = hovered_control();
    if (ctl == g_hovered) return;
    g_hovered = ctl;
    if (g_cfg.debug) {
        float r[4] = {};
        if (ctl) bounds(ctl, active_window(), r);
        uint8_t* f = focused_object();
        logf("control under the cursor (%d,%d): %s [%.0f,%.0f - %.0f,%.0f]; focus %s", g_cx.load(), g_cy.load(),
             ctl ? rtti_name(ctl) : "none", r[0], r[1], r[2], r[3], f ? rtti_name(f) : "none");
    }
    // Only what can take the focus: the pause menu's tabs can't, and asking them to takes the
    // current tab from the tab row.
    if (!g_cfg.hover_focus || g_walk.active) return;
    if (uint8_t* target = focus_target(ctl))
        if (focused_object() != target) focus(target);
}

void on_event(const Event& e) {
    if (g_cfg.debug) {
        uint8_t* f = focused_object();
        logf("event %d (%d), cursor %s, focus %s, menu %d", static_cast<int>(e.type), e.a, cursor_wanted() ? "shown" : "hidden",
             f ? rtti_name(f) : "none", menu_active() ? 1 : 0);
    }
    // The drone flying (see "Drone Mode"): left click looks at the car, right click's the way
    // out, the wheel zooms, and nothing else is clicked (a release still lets go of what it held).
    if (drone_flying()) switch (e.type) {
        case Ev::LDown: g_drone_pressed |= kDroneLookAtCar; return;
        case Ev::RDown: g_drone_pressed |= kDroneExit; return;
        case Ev::Wheel: add(g_drone_zoom, 0.15f * e.a / WHEEL_DELTA); return;
        case Ev::X1Down:
        case Ev::X2Down:
        case Ev::MDown: return;
        default: break;
        }
    switch (e.type) {
    case Ev::Move: break;   // handled once per frame
    case Ev::LDown:
        if (g_cfg.debug) dump_under_cursor();
        g_walk.active = false;   // a new click: whatever was under way stops
        click_press();
        break;
    case Ev::LUp:
        g_adjust.key = 0;
        g_cam_drag   = false;
        if (photo_release()) break;
        if (map_release()) break;
        if (g_a_held) send_key(KEY_A, true), g_a_held = false;
        break;
    case Ev::RDown:
        if (in_menu()) send_key(KEY_B, false), g_b_held = true;
        break;
    case Ev::RUp:
        if (g_b_held) send_key(KEY_B, true), g_b_held = false;
        break;
    case Ev::Wheel:
        if (map_wheel(e.a)) break;
        if (photo_wheel(e.a)) break;
        if (garage_wheel(e.a)) break;
        if (in_menu()) {
            // The car carousels (the game's ItemsSelector with the focus) go sideways.
            const bool sideways = scrolls_sideways(focused_object());
            const int key = sideways ? (e.a > 0 ? KEY_LEFT : KEY_RIGHT) : (e.a > 0 ? KEY_UP : KEY_DOWN);
            for (int n = 0, steps = std::max(1, std::abs(e.a) / WHEEL_DELTA); n < steps && n < 10; ++n) {
                send_key(key, false);
                send_key(key, true);
            }
        }
        break;
    case Ev::X1Down:
        if (in_menu()) send_key(KEY_LB, false), g_lb_held = true;
        break;
    case Ev::X1Up:
        if (g_lb_held) send_key(KEY_LB, true), g_lb_held = false;
        break;
    case Ev::X2Down:
        if (in_menu()) send_key(KEY_RB, false), g_rb_held = true;
        break;
    case Ev::MDown:
        // The right stick's click (the pad's control, key 119).
        if (in_menu() && !press_pad(KEY_RS_CLICK, true)) tap(KEY_RS_CLICK);
        break;
    case Ev::MUp:
        g_press_held = false;   // the release follows (after at least 80 ms down)
        break;
    case Ev::X2Up:
        if (g_rb_held) send_key(KEY_RB, true), g_rb_held = false;
        break;
    }
}

std::vector<Event> g_batch;   // UI thread only

void frame_inner() {
    bool moved = false;
    for (const Event& e : g_batch) {
        if (e.type == Ev::Move) {
            moved = true;
            continue;
        }
        if (moved) on_move(), moved = false;   // a click goes where the cursor was at the time
        on_event(e);
    }
    if (moved) on_move();
    click_frame();
    flush_one_key();
}

std::atomic<bool> g_broken{false};

// Debug: F9 logs every control's box in the active window, to set against a screenshot.
void dump_controls_in(uint8_t* v, uint8_t* root, int depth, int& n) {
    if (!v || depth > 80 || n > 400) return;
    void* t = type_of(v);
    if (!is(t, TYPE_VISUAL) || opacity(v) <= 0.01f) return;
    if (is(t, TYPE_UIELEMENT) && (control_kind(t) || is(t, TYPE_SELECTOR))) {
        float r[4];
        alignas(16) float matrix[16] = {};
        void* gt = nullptr;
        const bool simple = game<ToAncestorFn>(RVA_TO_ANCESTOR)(v, root, 0, &gt, matrix);
        release(static_cast<uint8_t*>(gt));
        if (bounds(v, root, r)) {
            ++n;
            logf("  %s [%.0f,%.0f - %.0f,%.0f] %s%s", rtti_name(v), r[0], r[1], r[2], r[3], simple ? "" : "(not simple) ",
                 bool_property(v, RVA_FOCUSABLE_DP) ? "focusable" : "");
        }
    }
    const int c = vfn<CountFn>(v, VT_CHILD_COUNT)(v);
    for (int i = 0; i < c && i < 4096; ++i) dump_controls_in(vfn<ChildFn>(v, VT_CHILD)(v, i), root, depth + 1, n);
}

std::atomic<uint8_t*> g_ui{nullptr};   // the UserInterface object (its +0x120: the camera tweak)

void dump_floats(const char* what, uint8_t* p, int from, int to) {
    std::string out;
    char b[48];
    for (int o = from; o < to; o += 4) {
        std::snprintf(b, sizeof b, " +%X=%.3f", o, *reinterpret_cast<float*>(p + o));
        out += b;
    }
    logf("%s %p:%s", what, p, out.c_str());
}

void debug_frame() {
    static bool f10 = false;
    HWND hw = g_hwnd.load();
    const bool now10 = hw && GetForegroundWindow() == hw && (GetAsyncKeyState(VK_F10) & 0x8000);
    if (now10 && !f10 && g_cfg.debug) {
        if (uint8_t* ui = g_ui.load())
            if (uint8_t* cam = at<uint8_t*>(ui, 0x120)) {
                logf("camera tweak %s", rtti_name(cam));
                dump_floats("  behavior", cam, 0x18, 0x80);
                if (uint8_t* t = at<uint8_t*>(cam, 0x10)) dump_floats("  target", t, 0, 0x40);
            }
    }
    f10 = now10;
    static bool down = false;
    HWND h = g_hwnd.load();
    const bool now = h && GetForegroundWindow() == h && (GetAsyncKeyState(VK_F9) & 0x8000);
    if (now && !down && g_cfg.debug) {
        int n = 0;
        logf("controls (focus %s):", focus_name());
        if (uint8_t* root = active_window()) dump_controls_in(root, root, 0, n);
    }
    down = now;
}

// ---- the decal editor ---------------------------------------------------------------------------
// Design editor / Edit layer (LiveryLayerPosition) moves, sizes, spins and skews the layer by
// two analog menu controls, 111 (right +) and 112 (up +), read with GetAnalog(input, context,
// control) (0x106F040; context 7 is the menus). On that page, with the game window in front,
// W A S D push them all the way.
constexpr uintptr_t RVA_GET_ANALOG = 0x106F040;
constexpr int kDecalX = 111, kDecalY = 112;
std::atomic<bool> g_decal_keys{false};

// The garage's camera (UICameraTweakBehavior) turns by the right stick, menu controls 113 (right
// +) and 114 (up +). A left drag on empty space in a menu turns it: this frame's stick values,
// set before the UI's update and cleared after.
constexpr int kCamX = 113, kCamY = 114;

void log_analog(const char* what, int context, int control, float v, void* from) {
    static int lc = -1, ll = -1;
    static DWORD lt = 0;
    if (context == lc && control == ll && GetTickCount() - lt < 500) return;
    lc = context, ll = control, lt = GetTickCount();
    logf("%s: context %d control %d = %.2f, asked from game+0x%llX", what, context, control, v,
         static_cast<unsigned long long>(static_cast<uint8_t*>(from) - g_base));
}

// Debug only: what asks about buttons, through the other queries.
using ButtonStateFn = int (*)(void* input, int context, int control, uint8_t a);
ButtonStateFn o_dbg_state = nullptr;
int dbg_state(void* input, int context, int control, uint8_t a) {
    const int v = o_dbg_state(input, context, control, a);
    if (v) log_analog("state", context, control, static_cast<float>(v), RETURN_ADDRESS());
    return v;
}
// The pad's buttons as 0..1 (the triggers, the handbrake): the keyboard's driving keys go in here.
float get_digital(void* input, int context, int control) {
    const float v = o_get_digital(input, context, control);
    if (v != 0.f && g_cfg.debug) log_analog("digital", context, control, v, RETURN_ADDRESS());
    return std::max(with_keys(v, input, context, control), look_value(input, context, control));
}

float get_analog(void* input, int context, int control) {
    const float v = with_keys(o_get_analog(input, context, control), input, context, control);
    if (v != 0.f && g_cfg.debug && !(context == 7 && (control == kDecalX || control == kDecalY)))
        log_analog("analog", context, control, v, RETURN_ADDRESS());
    if (context == 7 && (control == kCamX || control == kCamY)) {
        const float d = control == kCamX ? g_cam_stick_x.load() : g_cam_stick_y.load();
        if (d) return std::max(-1.f, std::min(1.f, v + d));
    }
    if (context != 7 || (control != kDecalX && control != kDecalY) || !g_decal_keys.load()) return v;
    const float k = control == kDecalX ? key_axis('A', 'D') : key_axis('S', 'W');
    return k ? std::max(-1.f, std::min(1.f, v + k)) : v;
}

void photo_frame() {
    HWND h = g_hwnd.load();
    const bool front = h && GetForegroundWindow() == h;
    g_photo_keys = front && in_photo_mode();
    g_decal_keys = front && !std::strcmp(focus_name(), ".?AVLiveryLayerPosition@@");
}

// Once a UI frame, with the game window in front: which keys are held, and what was pressed.
// The menu's keys press the pad's buttons; the arrows repeat while held, as the d-pad does.
// "Controller disconnected": when the profile's user has no pad (the one it played with went),
// the game shows its ControllerPairing page through ShowPairing(manager, show, ...) (0xB620C0,
// from 0x21D26F and 0x228BD4 / 0x228C65), and only a pad's A paired to the profile takes it away.
// With the keys on and no pad there, the keyboard plays on instead: the page isn't shown. Once
// a pad is plugged in it is, so that pad's A pairs it to the profile again.
constexpr uintptr_t RVA_SHOW_PAIRING = 0xB620C0;

bool any_pad() {
    uint8_t* inputs = at<uint8_t*>(g_base, RVA_INPUTS);
    if (!inputs) return false;
    for (int i = 0; i < 8; ++i)
        if (at<void*>(inputs, i * 0x1300 + INPUTS_DEVICE)) return true;
    return false;
}
using ShowPairingFn = void (*)(void* manager, uint8_t show, uint8_t flag);
ShowPairingFn o_show_pairing = nullptr;

// The game loses its input then too: each frame it gives the UserInterface the input of the
// user's pad (SetInput, vtable slot at 0x2ECFC68, 0x14237E0: its +0x88), null without one, and
// with none the menus take nothing and the game pauses itself again as soon as it's resumed.
// With the keys on, the UI keeps the input it had (the player's, in the game's own array of
// them), which the keys feed.
constexpr uintptr_t RVA_UI_SET_INPUT_SLOT = 0x2ECFC68;
constexpr uintptr_t RVA_UI_SET_INPUT      = 0x14237E0;
using SetInputFn = void (*)(void* ui, void* input);
SetInputFn o_ui_set_input = nullptr;

void ui_set_input(void* ui, void* input) {
    static void* last = nullptr;
    if (input) last = input;
    else if (g_keyboard) input = last;
    o_ui_set_input(ui, input);
}

// And the HUD pauses the game while the user's pad isn't connected: each frame (0x964810) it
// checks the user's input's "connected" (+4) and, without it, calls Post(hud, its pause event)
// at 0x9648FA. Signed in without a pad, that input isn't the one the keys feed (and keep
// connected), so with the keys on and no pad there the call is skipped.
constexpr uintptr_t RVA_PAD_LOST_PAUSE = 0x9648FA;   // the call
using PostFn = uintptr_t (*)(void* self, void* event);
PostFn o_pad_lost_pause = nullptr;

uintptr_t pad_lost_pause(void* self, void* event) {
    return g_keyboard && !any_pad() ? 0 : o_pad_lost_pause(self, event);
}

// Photo mode (P while driving, the Autoshow's "Take photo") starts only for a profile with a pad:
// it asks the profile for its input with CGamerProfile's vtable +0x58 (0x124A9F0), which is null
// for a profile no pad was ever paired to, and then asks for a pad instead (which show_pairing
// keeps away), so nothing happens. The other ask (+0x50, 0x124A7D0) answers the current input
// then (the inputs' +0xAB28). With the keys on and no pad, this one answers it too: the input the
// keys feed.
constexpr uintptr_t RVA_PROFILE_INPUT = 0x124A9F0;
using ProfileInputFn = void* (*)(void* profile, uint8_t flag);
ProfileInputFn o_profile_input = nullptr;

void* profile_input(void* profile, uint8_t flag) {
    void* input = o_profile_input(profile, flag);
    if (input || !g_keyboard || any_pad()) return input;
    uint8_t* inputs = at<uint8_t*>(g_base, RVA_INPUTS);
    return inputs ? at<void*>(inputs, INPUTS_CURRENT) : nullptr;
}

void show_pairing(void* manager, uint8_t show, uint8_t flag) {
    if (show && g_keyboard && !any_pad()) {
        static DWORD told = 0;
        if (GetTickCount() - told > 10000) logf("controller disconnected: playing on with the keyboard"), told = GetTickCount();
        return;
    }
    o_show_pairing(manager, show, flag);
}

void keyboard_frame() {
    const bool menu = in_menu();
    g_in_menu       = menu;   // free look needs it too
    if (!g_keyboard) return;
    HWND h           = g_hwnd.load();
    const bool front = h && GetForegroundWindow() == h;
    static float repeat[kActionCount] = {};   // seconds to the next repeat
    static bool down[kActionCount]    = {};   // its key is down, wherever we are
    const bool flying                 = drone_flying();
    for (int i = 0; i < kActionCount; ++i) {
        const Action& a = kActions[i];
        bool now        = false;
        if (front)
            for (int vk : g_action_keys[i])
                now = now || (vk && (GetAsyncKeyState(vk) & 0x8000) && (a.menu || is_anna_action(a.name) || !anna_key(vk)));
        // A press counts where it's made: Esc that closes the map doesn't pause the drive it
        // leads back to, though a key held into the drive (W) does drive.
        // A key that holds a pad button holds it only from a press made here, until it's let go
        // or we're elsewhere: else held Esc (to quit Azimuth) pauses, the menu takes Start away,
        // the drive gives it back, and the pause menu opens and closes over and over.
        const bool pressed = now && !down[i];
        down[i]            = now;
        const bool here    = a.menu == menu;
        static bool armed[kActionCount] = {};
        static DWORD until[kActionCount] = {};   // held at least till then (min_ms)
        if (flying || is_drone_action(a.name)) {   // the drone's keys: only while it flies, and nothing else then
            armed[i] = false, until[i] = 0;
            g_held[i] = flying && now && is_drone_action(a.name);
            if (flying && pressed) g_drone_pressed |= drone_press(a.name);
            continue;
        }
        armed[i] = now && here && (armed[i] || pressed);
        if (pressed && here && a.min_ms) until[i] = GetTickCount() + a.min_ms;
        if (until[i] && static_cast<LONG>(until[i] - GetTickCount()) <= 0) until[i] = 0;
        g_held[i] = (now && here && (!a.pad || armed[i])) || (here && until[i]);
        if (!here || !now) continue;
        if (pressed) {
            if (g_cfg.debug) logf("key %s", a.name);
            if (a.control >= 0) key_press(a.control);
        }
        if (!a.key || (a.pad && o_input_update && g_player.load())) continue;   // held by input_update
        // Without that, the pad's presses, as clicks make them, repeating while held as the d-pad
        // does (slower than a press takes, so it can be the pad's again).
        const bool arrow = a.key == KEY_UP || a.key == KEY_DOWN || a.key == KEY_LEFT || a.key == KEY_RIGHT;
        if (pressed) {
            repeat[i] = 0.4f;
            if (!press_pad(a.key)) tap(a.key);
        } else if (arrow && (repeat[i] -= g_ui_dt) <= 0) {
            repeat[i] = 0.15f;
            if (!press_pad(a.key)) tap(a.key);
        }
    }
}

void map_frame_guarded() {
    __try {
        keyboard_frame();
        look_frame();
        icons_frame();
        photo_frame();
        debug_frame();
        map_frame();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

bool frame_guarded() {
    __try {
        frame_inner();
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void detour_update(void* self, float dt) {
    g_ui = static_cast<uint8_t*>(self);
    static FrameClock clock;
    g_ui_dt = clock.tick();
    static int counted = 0;
    static float total = 0;
    if (counted < 600 && ++counted > 300 && (total += g_ui_dt, counted == 600))
        logf("the menus run at %.0f frames a second%s", 300 / total, g_cfg.frame_rate > 0 ? " (FrameRate is set)" : "");
    press_frame();
    g_last_frame = GetTickCount();
    g_framed     = true;
    if (g_cfg.enabled && !g_broken.load()) {
        poll_mouse();
        map_frame_guarded();
        {
            std::lock_guard<std::mutex> lock(g_mx);
            g_batch.swap(g_events);
        }
        if ((!g_batch.empty() || !g_keys.empty() || g_walk.active || g_adjust.key) && !frame_guarded()) {
            g_broken = true;
            logf("stopped: the game's UI code didn't behave as expected (exception)");
        }
        g_batch.clear();
        cam_frame_begin();
        plugin_buttons();
    }
    o_update(self, dt);
}

// ---- start-up -----------------------------------------------------------------------------------

bool code_matches() {
    for (const Expect& e : kExpected) {
        const uint8_t* p = g_base + e.rva;
        if (std::memcmp(p, e.bytes, e.len) == 0) continue;
        char hex[3 * sizeof e.bytes + 1] = {};
        for (std::size_t i = 0; i < sizeof e.bytes; ++i) std::snprintf(hex + 3 * i, 4, "%02X ", p[i]);
        hex[3 * sizeof e.bytes - 1] = 0;
        logf("game code at +0x%llX isn't as expected (%s); not starting", static_cast<unsigned long long>(e.rva), hex);
        return false;
    }
    if (at<uint8_t*>(g_base, RVA_UI_UPDATE_SLOT) != g_base + RVA_UI_UPDATE) {
        logf("UserInterface::Update isn't where expected; not starting");
        return false;
    }
    return true;
}

// ---- Drone Mode ---------------------------------------------------------------------------------
// ANNA Drone Mode (fh2-mods\ANNADroneMode.dll, FH2 ANNA's menu > Drone Mode) flies the camera on
// the pad, which it reads itself. Its interface asks us for more input once a frame while it
// flies: the Drone keys (and the radio's), and the mouse (see "free look": moving it turns, the
// wheel zooms, left click looks at the car, right click's the way out). While it flies, the
// driving and menu keys do nothing (keyboard_frame), and the game's no menu (in_menu).

// The drone's camera thread, once a frame while it flies.
void drone_input(DroneModeInput* in, float) {
    if (!in || in->size < sizeof(DroneModeInput)) return;
    static const int fwd = action_index("DroneForward"), back = action_index("DroneBack"), left = action_index("DroneLeft"),
                     right = action_index("DroneRight"), up = action_index("DroneUp"), down = action_index("DroneDown"),
                     fast = action_index("DroneFast");
    auto held = [](int i) { return i >= 0 && g_held[i].load() ? 1.f : 0.f; };
    in->forward       = held(fwd) - held(back);
    in->right         = held(right) - held(left);
    in->up            = held(up) - held(down);
    in->fast          = held(fast) != 0;
    const float per   = kLookPerPixel * g_cfg.look_percent.load() / 50.f;
    in->yaw           = take(g_drone_look_x) * per;
    in->pitch         = -take(g_drone_look_y) * per;   // the mouse up looks up
    in->zoom          = take(g_drone_zoom);
    in->pressed       = g_drone_pressed.exchange(0);
}

// Its button help: the keys' pictures while the keyboard or mouse was used last (its thread).
uint32_t drone_keys_shown() {
    if (!g_keyboard || !g_key_icons_on || !g_keys_last.load()) return 0;
    return g_keys_version.load() | 1;   // never 0
}

int drone_pictures(int item, DroneModePicture* out, int max) {
    int n = 0;
    auto key = [&](const char* action) {
        const int vk = first_key(action);
        if (!vk || n >= max) return;
        const auto it = g_key_pictures.find(key_file(vk));
        if (it == g_key_pictures.end() || !it->second.width) return;
        out[n++] = {kDronePictureKey, it->second.width, kKeySize, it->second.pixels.data()};
    };
    auto mouse = [&](int kind) {
        if (n < max) out[n++] = {kind, 0, 0, nullptr};
    };
    switch (item) {
    case kDroneHelpMove: key("DroneForward"), key("DroneLeft"), key("DroneBack"), key("DroneRight"); break;
    case kDroneHelpLook: mouse(kDronePictureMouse); break;
    case kDroneHelpDownUp: key("DroneDown"), key("DroneUp"); break;
    case kDroneHelpFaster: key("DroneFast"); break;
    case kDroneHelpZoom: mouse(kDronePictureWheel); break;
    case kDroneHelpRadio: key("RadioPrevious"), key("RadioNext"); break;
    case kDroneHelpToCar: key("DroneToCar"); break;
    case kDroneHelpLookAtCar: key("DroneLookAtCar"), mouse(kDronePictureLeft); break;
    case kDroneHelpHide: key("DroneHelp"); break;
    case kDroneHelpExit: key("DroneExit"), mouse(kDronePictureRight); break;
    default: break;
    }
    return n;
}

const DroneModeHelpKeys kDroneHelpKeys = {sizeof(DroneModeHelpKeys), drone_keys_shown, drone_pictures};

// Once every mod has started: the drone's interface, if it's there.
void hook_drone() {
    g_drone = static_cast<const DroneModeApi*>(g_api->get_interface("ANNADroneMode", 1));
    if (!g_drone) return;
    if (g_drone->size < sizeof(DroneModeApi)) {
        g_drone = nullptr;
        return;
    }
    g_drone->set_input(drone_input);
    if (!g_key_pictures.empty()) g_drone->set_help_keys(&kDroneHelpKeys);
    logf("ANNA Drone Mode is here: the Drone keys and the mouse fly it");
}

// ---- FnFNitro ----------------------------------------------------------------------------------
// FnFNitro (fh2-mods\FnFNitro.dll) boosts while the pad's nitro button is down: its NitroButton
// (LB unless its ini says otherwise), read with XInputGetState on ports 0 to 3. With it there,
// FH2Mouse answers its XInputGetState (its import, nobody else's): while the Nitro key is held,
// port 0 has that button down, with or without a pad. The game never sees it.
using XInputGetStateFn = DWORD(WINAPI*)(DWORD port, XINPUT_STATE* state);
XInputGetStateFn o_nitro_xinput = nullptr;
int g_nitro_action = -1;

// FnFNitro's NitroButton, as a pad button's bit (0x10000 / 0x20000: LT / RT); read again each second.
DWORD nitro_button() {
    static DWORD bit = XINPUT_GAMEPAD_LEFT_SHOULDER, read = 0;
    if (read && GetTickCount() - read < 1000) return bit;
    read = GetTickCount();
    static const struct { const wchar_t* name; DWORD bit; } names[] = {
        {L"LB", XINPUT_GAMEPAD_LEFT_SHOULDER}, {L"RB", XINPUT_GAMEPAD_RIGHT_SHOULDER}, {L"A", XINPUT_GAMEPAD_A},
        {L"B", XINPUT_GAMEPAD_B}, {L"X", XINPUT_GAMEPAD_X}, {L"Y", XINPUT_GAMEPAD_Y}, {L"LS", XINPUT_GAMEPAD_LEFT_THUMB},
        {L"RS", XINPUT_GAMEPAD_RIGHT_THUMB}, {L"DPadUp", XINPUT_GAMEPAD_DPAD_UP}, {L"DPadDown", XINPUT_GAMEPAD_DPAD_DOWN},
        {L"DPadLeft", XINPUT_GAMEPAD_DPAD_LEFT}, {L"DPadRight", XINPUT_GAMEPAD_DPAD_RIGHT}, {L"Back", XINPUT_GAMEPAD_BACK},
        {L"Start", XINPUT_GAMEPAD_START}, {L"LT", 0x10000}, {L"RT", 0x20000},
    };
    wchar_t name[32] = {};
    GetPrivateProfileStringW(L"Settings", L"NitroButton", L"LB", name, 32,
                             (std::wstring(g_api->azimuth_dir) + L"\\fh2-mods\\FnFNitro.ini").c_str());
    bit = XINPUT_GAMEPAD_LEFT_SHOULDER;   // as FnFNitro: unknown names are LB
    for (const auto& n : names)
        if (!_wcsicmp(name, n.name)) bit = n.bit;
    return bit;
}

DWORD WINAPI nitro_xinput(DWORD port, XINPUT_STATE* state) {
    DWORD r = o_nitro_xinput(port, state);
    if (port != 0 || !state || !g_keyboard || g_nitro_action < 0 || !g_held[g_nitro_action].load()) return r;
    if (r != ERROR_SUCCESS) std::memset(state, 0, sizeof *state), r = ERROR_SUCCESS;
    const DWORD bit = nitro_button();
    if (bit == 0x10000) state->Gamepad.bLeftTrigger = 255;
    else if (bit == 0x20000) state->Gamepad.bRightTrigger = 255;
    else state->Gamepad.wButtons |= static_cast<WORD>(bit);
    return r;
}

// A mod's import of XInputGetState (by name, or xinput1_4's ordinal 2), or null.
void** xinput_slot(HMODULE m) {
    auto* base = reinterpret_cast<uint8_t*>(m);
    auto* dos  = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    auto* nt   = reinterpret_cast<IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    const IMAGE_DATA_DIRECTORY& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress) return nullptr;
    for (auto* d = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + dir.VirtualAddress); d->Name; ++d) {
        if (_strnicmp(reinterpret_cast<char*>(base + d->Name), "xinput", 6)) continue;
        auto* names = reinterpret_cast<IMAGE_THUNK_DATA64*>(base + (d->OriginalFirstThunk ? d->OriginalFirstThunk : d->FirstThunk));
        auto* slots = reinterpret_cast<IMAGE_THUNK_DATA64*>(base + d->FirstThunk);
        for (; names->u1.AddressOfData; ++names, ++slots) {
            const bool by_ordinal = IMAGE_SNAP_BY_ORDINAL64(names->u1.Ordinal);
            if ((by_ordinal && IMAGE_ORDINAL64(names->u1.Ordinal) == 2) ||
                (!by_ordinal && !std::strcmp(reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + names->u1.AddressOfData)->Name,
                                             "XInputGetState")))
                return reinterpret_cast<void**>(&slots->u1.Function);
        }
    }
    return nullptr;
}

void hook_nitro() {
    for (int i = 0; i < kActionCount; ++i)
        if (!std::strcmp(kActions[i].name, "Nitro")) g_nitro_action = i;
    HMODULE m = GetModuleHandleW(L"FnFNitro.dll");
    if (!m || !g_keyboard) return;
    void** slot = xinput_slot(m);
    if (slot && g_api->hook_pointer(g_mod, "FnFNitro: XInputGetState", slot, reinterpret_cast<void*>(&nitro_xinput),
                                    reinterpret_cast<void**>(&o_nitro_xinput)))
        g_api->enable_hooks(g_mod, nullptr), logf("FnFNitro is here: the Nitro key boosts");
    else
        logf("FnFNitro is here, but its pad reading isn't as expected: no Nitro key");
}

// ---- HonkHonk ----------------------------------------------------------------------------------
// HonkHonk (fh2-mods\HonkHonk.dll) honks while the pad's left stick is clicked in, read with its
// own XInputGetState (xinput1_4's ordinal 2). FH2MP moves it to the right stick when Extra
// Options doesn't use that one, by changing HonkHonk's code. FH2Mouse answers that import as it
// does FnFNitro's: while the Horn key is held, port 0 has both sticks clicked in, so it honks on
// whichever it's on, and the game doesn't see it.
XInputGetStateFn o_horn_xinput = nullptr;
int g_horn_action = -1;

DWORD WINAPI horn_xinput(DWORD port, XINPUT_STATE* state) {
    DWORD r = o_horn_xinput(port, state);
    if (port != 0 || !state || !g_keyboard || g_horn_action < 0 || !g_held[g_horn_action].load()) return r;
    if (r != ERROR_SUCCESS) std::memset(state, 0, sizeof *state), r = ERROR_SUCCESS;
    state->Gamepad.wButtons |= XINPUT_GAMEPAD_LEFT_THUMB | XINPUT_GAMEPAD_RIGHT_THUMB;
    return r;
}

void hook_horn() {
    for (int i = 0; i < kActionCount; ++i)
        if (!std::strcmp(kActions[i].name, "Horn")) g_horn_action = i;
    HMODULE m = GetModuleHandleW(L"HonkHonk.dll");
    if (!m || !g_keyboard) return;
    void** slot = xinput_slot(m);
    if (slot && g_api->hook_pointer(g_mod, "HonkHonk: XInputGetState", slot, reinterpret_cast<void*>(&horn_xinput),
                                    reinterpret_cast<void**>(&o_horn_xinput)))
        g_api->enable_hooks(g_mod, nullptr), logf("HonkHonk is here: the Horn key honks");
    else
        logf("HonkHonk is here, but its pad reading isn't as expected: no Horn key");
}

// ---- Pause > Settings > Keyboard & Mouse ---------------------------------------------------------
// The keys' rows (and the mouse's) get a tile of their own on the Settings page, next to Controls,
// instead of being among every mod's rows on the platform's MODS page.
//
// - The pages' XAML is edited as the game reads it, as Extra Options edits the pages it changes:
//   the page loader (0x16EBB37) gets the file's bytes with 0xAABEC0(reader, Ptr* out), a resource
//   object with the text at +0x28 and its length at +0x30. That function is hooked; for the page
//   loader's call (or Extra Options', which takes that call over and calls it from its own DLL)
//   a resource of ours with the edited text is handed back instead, as Extra Options makes them.
// - A_PauseOptions.xaml: Controls becomes a square tile, and ours goes beside it, in the other
//   half of its place (GridX 2), so the grid keeps its size.
// - The tile is wired as the platform wires its MODS tile (PauseOptionsMenu's 0xDC8A60 wires the
//   tiles, with 0xD9FC10(page, "PART_Button_X", &pmf)), and opens the page as the MODS tile does:
//   it runs the HUD tile's handler (0xDC6C40), and the HUD page's scene record (0xB3A3E0, built
//   twice) is pointed at the platform's Mods page, A_ModOptions.
// - That page, read just after our tile was pressed, keeps only this mod's rows (the platform
//   writes " (FH2 Mouse & Keyboard)" at the end of their descriptions) and gets our title; read at
//   any other time, it loses them. Its rows are bound to the platform's properties, so a change
//   comes to keyboard_row_changed as before.
constexpr uintptr_t RVA_GET_RESOURCE      = 0x0AABEC0;   // Ptr* (reader, Ptr* out): the reader's data, add-ref'd
constexpr uintptr_t RVA_PAGE_XAML_RETURN  = 0x16EBB3C;   // after the page loader's call to it
constexpr uintptr_t RVA_GAME_ALLOCATE     = 0x03229C0;
constexpr uintptr_t RVA_GAME_FREE         = 0x031C7B0;
constexpr uintptr_t RVA_OBJECT_INIT       = 0x143EB80;
constexpr uintptr_t RVA_OBJECT_CLEANUP    = 0x1442050;
constexpr uintptr_t RVA_PAUSE_WIRE_TILES  = 0x0DC8A60;   // PauseOptionsMenu: wire the tiles (page)
constexpr uintptr_t RVA_WIRE_BUTTON       = 0x0D9FC10;   // (page, const char* name, const Pmf*)
constexpr uintptr_t RVA_HUD_TILE          = 0x0DC6C40;   // PauseOptionsMenu::OnHud(page)
constexpr uintptr_t RVA_HUD_SCENE_RECORD  = 0x0B3A3E0;   // SceneRecord* build(SceneRecord* out)

constexpr char kModName[]    = "FH2 Mouse & Keyboard";
constexpr char kOurSuffix[]  = " (FH2 Mouse &amp; Keyboard)\"";   // the end of our rows' descriptions
// The tile's title, in the game's language now. The Settings page's XAML isn't read as UTF-8
// (the platform's Mods page is), so what isn't ASCII goes as character references (&#x9375;).
std::string tile_title() {
    const std::string s = xml_escape(lang::tr("KEYBOARD & MOUSE"));
    const int n         = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(n > 0 ? n : 0, L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    std::string o;
    for (std::size_t i = 0; i < w.size(); ++i) {
        uint32_t c = w[i];
        if (c >= 0xD800 && c < 0xDC00 && i + 1 < w.size()) c = 0x10000 + ((c - 0xD800) << 10) + (w[++i] - 0xDC00);
        char ref[16];
        if (c < 0x80) o += static_cast<char>(c);
        else o.append(ref, static_cast<std::size_t>(std::snprintf(ref, sizeof ref, "&#x%X;", c)));
    }
    return o;
}

struct XamlResource {
    void** vtable;
    uint8_t header[0x20];   // the refcount at +0x10
    char* bytes;
    int length;
    int capacity;
};
static_assert(offsetof(XamlResource, bytes) == 0x28, "the resource's text");

using GetResourceFn = void** (*)(void* reader, void** out);
using AllocateFn    = void* (*)(std::size_t);
using ObjectFn      = void (*)(void* obj);
struct Pmf {
    void* fn;
    int32_t adjust, pad;
};
using WireTilesFn  = void (*)(void* page);
using WireButtonFn = void (*)(void* page, const char* name, const Pmf* pmf);
using SceneRecFn   = uint8_t* (*)(uint8_t* out);

GetResourceFn o_get_resource = nullptr;
WireTilesFn o_wire_tiles     = nullptr;
SceneRecFn o_hud_scene       = nullptr;
uintptr_t g_image_end        = 0;              // the game image's end
std::atomic<bool> g_kbm_tile{false};           // the Settings page was given the tile
std::atomic<bool> g_kbm_failed{false};         // ... or couldn't be: the rows stay on the MODS pages
std::atomic<uint64_t> g_kbm_asked{0};          // GetTickCount64() when it was pressed
std::atomic<int> g_kbm_builds{0};              // scene records still to point at the Mods page
std::atomic<uint64_t> g_kbm_opening{0};        // ... pointed: the Mods page that's read next is ours

// Our resources' vtables: the original's, with a destructor that frees what we allocated.
std::mutex g_vt_mu;
struct VtCopy {
    void** from;
    void* slots[7];
};
std::vector<VtCopy*> g_vts;   // guarded by g_vt_mu, never freed

void* xaml_resource_delete(XamlResource* r, unsigned flags) {
    game<ObjectFn>(RVA_OBJECT_CLEANUP)(r);
    if (flags & 1) game<ObjectFn>(RVA_GAME_FREE)(r);
    return r;
}

void** our_vtable(void** from) {
    std::scoped_lock lk{g_vt_mu};
    for (VtCopy* v : g_vts)
        if (v->from == from) return v->slots + 1;
    auto* v = new VtCopy{from, {}};
    std::memcpy(v->slots, from - 1, sizeof v->slots);   // [-1] is the RTTI locator
    v->slots[1] = reinterpret_cast<void*>(&xaml_resource_delete);
    g_vts.push_back(v);
    return v->slots + 1;
}

bool readable_range(const void* p, std::size_t n) {
    MEMORY_BASIC_INFORMATION mbi{};
    if (!p || VirtualQuery(p, &mbi, sizeof mbi) != sizeof mbi || mbi.State != MEM_COMMIT) return false;
    if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return false;
    return static_cast<const char*>(p) + n <= static_cast<const char*>(mbi.BaseAddress) + mbi.RegionSize;
}

// The resource's text, if it's one (and a page we change, by its first bytes).
bool resource_text(void* obj, const char** bytes, int* length) {
    __try {
        auto* r = static_cast<XamlResource*>(obj);
        if (!readable_range(r, sizeof *r) || !r->bytes || r->length <= 0 || r->length > (8 << 20)) return false;
        if (!readable_range(r->bytes, static_cast<std::size_t>(r->length))) return false;
        *bytes  = r->bytes;
        *length = r->length;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// *out becomes a resource of ours with `text`, the original released.
bool replace_resource(void** out, const std::string& text) {
    auto* original = static_cast<XamlResource*>(*out);
    const std::size_t capacity = text.size() + 1;
    auto* r = static_cast<XamlResource*>(game<AllocateFn>(RVA_GAME_ALLOCATE)(sizeof(XamlResource) + capacity));
    if (!r) return false;
    char* bytes = reinterpret_cast<char*>(r + 1);
    std::memcpy(bytes, text.data(), text.size());
    bytes[text.size()] = 0;
    __try {
        game<ObjectFn>(RVA_OBJECT_INIT)(r);
        r->vtable   = our_vtable(original->vtable);
        r->bytes    = bytes;
        r->length   = static_cast<int>(text.size());
        r->capacity = static_cast<int>(capacity);
        *out        = r;
        release(reinterpret_cast<uint8_t*>(original));
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// The tiles' pictures: ours a keyboard and a mouse, drawn as the platform draws its MODS tab's
// (Paths in the light grey of the game's icons); Controls the game's own wide picture, its right
// half (the pad) in a square.
const char kTileResources[] = R"X(
            <local:TileMenuDataItem x:Key="FH2Mouse_MenuDataItem_Kbm" Heading="FH2MOUSE_TILE_TITLE" Icon="" />
            <ControlTemplate x:Key="FH2Mouse_Template_Kbm" TargetType="{x:Type Button}">
                <Grid Style="{StaticResource A_Style_FrameworkElement_DefaultTransition}" local:UIPageTransitionManager.TransitionIndex="{Binding (local:UIPageTransitionManager.TransitionIndex), RelativeSource={RelativeSource TemplatedParent}}">
                    <Grid Style="{StaticResource A_Style_Grid_HiddenWhenLocked}">
                        <local:FZMaterial Style="{StaticResource A_Style_FZMaterial_TileShadow}" />
                    </Grid>
                    <Grid Style="{StaticResource A_Style_Grid_MotionBlur}">
                        <local:FZMaterial Style="{StaticResource A_Style_FZMaterial_TileBackground}" />
                        <Grid ClipToBounds="True">
                            <Path Data="M18,140 L190,140 L190,250 L18,250 Z M232,148 A22,22 0 0 1 254,170 L254,226 A22,22 0 0 1 210,226 L210,170 A22,22 0 0 1 232,148 Z M232,150 L232,186" Stroke="#FFDCDCDC" StrokeThickness="12" />
                            <Path Data="M38,160 L56,160 L56,178 L38,178 Z M66,160 L84,160 L84,178 L66,178 Z M94,160 L112,160 L112,178 L94,178 Z M122,160 L140,160 L140,178 L122,178 Z M150,160 L168,160 L168,178 L150,178 Z M38,186 L56,186 L56,204 L38,204 Z M66,186 L84,186 L84,204 L66,204 Z M94,186 L112,186 L112,204 L94,204 Z M122,186 L140,186 L140,204 L122,204 Z M150,186 L168,186 L168,204 L150,204 Z M38,212 L56,212 L56,230 L38,230 Z M66,212 L140,212 L140,230 L66,230 Z M150,212 L168,212 L168,230 L150,230 Z" Fill="#FFDCDCDC" />
                        </Grid>
                        <Grid Margin="{StaticResource A_Thickness_TilePadding}">
                            <local:FZTextBlock Text="{Binding DataContext.Heading, RelativeSource={RelativeSource TemplatedParent}}" Style="{StaticResource A_Style_FZTextBlock_TileTitle}" HorizontalAlignment="Left" TextAlignment="Left" MaxWidth="{Binding Width, RelativeSource={RelativeSource TemplatedParent}}" />
                        </Grid>
                        <local:FZMaterial Style="{StaticResource A_Style_FZMaterial_TileSelectionBar}" />
                    </Grid>
                </Grid>
            </ControlTemplate>
            <ControlTemplate x:Key="FH2Mouse_Template_Controls" TargetType="{x:Type Button}">
                <Grid Style="{StaticResource A_Style_FrameworkElement_DefaultTransition}" local:UIPageTransitionManager.TransitionIndex="{Binding (local:UIPageTransitionManager.TransitionIndex), RelativeSource={RelativeSource TemplatedParent}}">
                    <Grid Style="{StaticResource A_Style_Grid_HiddenWhenLocked}">
                        <local:FZMaterial Style="{StaticResource A_Style_FZMaterial_TileShadow}" />
                    </Grid>
                    <Grid Style="{StaticResource A_Style_Grid_MotionBlur}">
                        <local:FZMaterial Style="{StaticResource A_Style_FZMaterial_TileBackground}" />
                        <Grid ClipToBounds="True">
                            <local:FZMaterial Style="{StaticResource A_Style_FZMaterial_LockableImage}" ImageSource="{Binding DataContext.Icon, RelativeSource={RelativeSource TemplatedParent}}" Width="558" Height="276" HorizontalAlignment="Right" VerticalAlignment="Center" StretchDirection="Both" />
                        </Grid>
                        <Grid Margin="{StaticResource A_Thickness_TilePadding}">
                            <local:FZTextBlock Text="{Binding DataContext.Heading, RelativeSource={RelativeSource TemplatedParent}}" Style="{StaticResource A_Style_FZTextBlock_TileTitle}" HorizontalAlignment="Left" TextAlignment="Left" MaxWidth="{Binding Width, RelativeSource={RelativeSource TemplatedParent}}" />
                            <local:FZMaterial Style="{StaticResource A_Style_Tile_LockIcon}" />
                        </Grid>
                        <local:FZMaterial Style="{StaticResource A_Style_FZMaterial_TileSelectionBar}" />
                    </Grid>
                </Grid>
            </ControlTemplate>
)X";

const char kTileButton[] = R"X(
                                <Button x:Name="PART_Button_FH2Mouse"
                                        local:TileMenuNavigationManager.TileState="Enabled"
                                        DataContext="{StaticResource FH2Mouse_MenuDataItem_Kbm}"
                                        local:TileMenuNavigationManager.GridX="2"
                                        local:TileMenuNavigationManager.GridY="0"
                                        Style="{DynamicResource A_Style_Button_Tile_1x1}"
                                        Canvas.Left="{StaticResource A_Double_TileCoord_X_2}"
                                        Canvas.Top="{StaticResource A_Double_TileCoord_Y_0}"
                                        Template="{StaticResource FH2Mouse_Template_Kbm}"
                                        local:UIPageTransitionManager.TransitionIndex="1"
                                        local:BindingProxy.C="DisableMotionBlur"/>)X";

bool replace_once(std::string& s, const std::string& from, const std::string& to) {
    const std::size_t at = s.find(from);
    if (at == std::string::npos) return false;
    s.replace(at, from.size(), to);
    return true;
}

// A_PauseOptions.xaml: Controls in a square, our tile beside it.
bool add_settings_tile(std::string& page) {
    const std::size_t named = page.find("x:Name=\"PART_Button_Controls\"");
    const std::size_t item  = page.find("x:Key=\"A_MenuDataItem_TeamCredits\"");
    if (named == std::string::npos || item == std::string::npos || page.find("PART_Button_FH2Mouse") != std::string::npos ||
        page.find("TileMenuNavigationManager.GridX=\"2\"") != std::string::npos)
        return false;
    const std::size_t a = page.rfind("<Button", named), b = page.find("/>", named);
    const std::size_t item_end = page.find('\n', item);
    if (a == std::string::npos || b == std::string::npos || item_end == std::string::npos || item_end > a) return false;
    std::string el = page.substr(a, b + 2 - a);
    if (!replace_once(el, "A_Style_Button_Tile_2x1", "A_Style_Button_Tile_1x1") ||
        !replace_once(el, "A_ControlTemplate_HorizontalImageTile", "FH2Mouse_Template_Controls"))
        return false;
    page.replace(a, b + 2 - a, el + kTileButton);
    std::string resources = kTileResources;
    replace_once(resources, "FH2MOUSE_TILE_TITLE", tile_title());
    page.insert(item_end + 1, resources);
    return true;
}

// ---- the rows on screen follow a preset --------------------------------------------------------
// The platform tells the page only of the row that was changed, and a preset changes the key
// rows too. So our rows' property descriptors (the platform's, looked up as the page's bindings
// look them up, with AVUI::Type::FindProperty, which the platform hooks) get a setter of ours
// around the platform's: after the platform's setter (and so our on_change) it raises
// PropertyChanged(page, descriptor) (0xCF9C60) for the rows refresh_row asked for, and the page
// reads them again.
constexpr uintptr_t RVA_FIND_PROPERTY = 0x157FF60;   // AVUI::Type::FindProperty(type, String* name)
constexpr uintptr_t RVA_NOTIFY_PROP   = 0x0CF9C60;   // raise PropertyChanged(obj, descriptor)
constexpr uintptr_t DESC_SETTER       = 0x30;        // a property descriptor's setter (desc, obj, Ptr value)
using FindPropertyFn = uint8_t* (*)(void* type, void* name);
using NotifyPropFn   = void (*)(void* obj, void* desc);
using RowSetFn       = void (*)(void* desc, void* obj, void* value);
RowSetFn o_row_set   = nullptr;   // the platform's setter
bool g_refresh_ok    = false;     // NotifyProp is as expected
std::vector<std::pair<std::string, std::string>> g_open_props;   // the page being opened: our keys and properties
std::mutex g_desc_mu;
std::vector<std::pair<std::string, uint8_t*>> g_descs;   // our keys' descriptors (guarded by g_desc_mu)
std::vector<uint8_t*> g_refresh;                          // to tell the page of (UI thread)

void refresh_row(const char* key) {
    std::scoped_lock lk{g_desc_mu};
    for (const auto& [k, d] : g_descs)
        if (k == key) g_refresh.push_back(d);
}

void notify_guarded(void* obj, void* desc) {
    __try {
        game<NotifyPropFn>(RVA_NOTIFY_PROP)(obj, desc);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

void row_set(void* desc, void* obj, void* value) {
    o_row_set(desc, obj, value);
    std::vector<uint8_t*> now;
    now.swap(g_refresh);
    if (obj)
        for (uint8_t* d : now) notify_guarded(obj, d);
}

uint8_t* find_property_guarded(void* type, void* name) {
    __try {
        return game<FindPropertyFn>(RVA_FIND_PROPERTY)(type, name);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

// The descriptor's setter becomes ours (once); false if it isn't the platform's row setter.
bool wrap_setter(uint8_t* d) {
    __try {
        void** setter = reinterpret_cast<void**>(d + DESC_SETTER);
        if (*setter == reinterpret_cast<void*>(&row_set)) return true;
        if (!*setter || (o_row_set && *setter != reinterpret_cast<void*>(o_row_set))) return false;
        o_row_set = reinterpret_cast<RowSetFn>(*setter);
        *setter   = reinterpret_cast<void*>(&row_set);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Our rows' descriptors, for the page being opened (each looked up once: they're for good).
void find_descriptors() {
    if (!g_refresh_ok) return;
    static uint8_t no_type[0x100] = {};   // no properties of its own and no base: only the platform's are found
    int found = 0;
    for (const auto& [key, prop] : g_open_props) {
        {
            std::scoped_lock lk{g_desc_mu};
            if (std::any_of(g_descs.begin(), g_descs.end(), [&](const auto& e) { return e.first == key; })) {
                ++found;
                continue;
            }
        }
        auto* name = new uint8_t[0x80]();   // kept, as the game may keep what it's asked with
        game<AvuiStringFn>(RVA_AVUI_STRING)(name, prop.c_str(), -1);
        InterlockedIncrement(reinterpret_cast<volatile LONG*>(name + 0x10));
        uint8_t* d = find_property_guarded(no_type, name);
        if (!d || !wrap_setter(d)) continue;
        std::scoped_lock lk{g_desc_mu};
        g_descs.emplace_back(key, d);
        ++found;
    }
    if (found < static_cast<int>(g_open_props.size()))
        logf("only %d of the page's %d rows can be shown changing: a preset's keys show the next time it opens", found,
             static_cast<int>(g_open_props.size()));
}

// The Mods page (A_ModOptions.xaml, "ALL SETTINGS"): only our rows (`ours`), or all but them.
bool filter_mods_page(std::string& page, bool ours) {
    const std::string list_at = "<local:CommonListBox x:Name=\"OptionsList\"", list_end = "</local:CommonListBox>";
    const std::string item = "<local:TileMenuDataItem x:Key=\"FH2Mod_Item_", tag = "Tag=\"{StaticResource FH2Mod_Item_";
    const std::string first = " x:Name=\"FirstItem\"";
    if (page.find("Content=\"ALL SETTINGS\"") == std::string::npos) return false;
    // our rows, by number
    std::vector<std::string> mine;
    std::vector<std::pair<std::string, std::string>> props;   // our rows' keys and properties
    for (std::size_t p = page.find(item); p != std::string::npos; p = page.find(item, p + 1)) {
        const std::size_t n = p + item.size(), q = page.find('"', n), e = page.find("/>", n);
        if (q == std::string::npos || e == std::string::npos) return false;
        const std::string el = page.substr(n, e - n);
        if (el.find(kOurSuffix) == std::string::npos) continue;
        mine.push_back(page.substr(n, q - n));
        const std::size_t h = el.find("Heading=\""), he = h == std::string::npos ? h : el.find('"', h + 9);
        if (he == std::string::npos) continue;
        const std::string heading = el.substr(h + 9, he - h - 9);
        for (const auto& [label, key] : g_row_names)
            if (label == heading) props.emplace_back(key, "FH2Mod_" + mine.back());
    }
    if (mine.empty()) return false;
    std::size_t l = page.find(list_at);
    l = l == std::string::npos ? l : page.find('>', l);
    const std::size_t le = l == std::string::npos ? l : page.find(list_end, l);
    if (le == std::string::npos) return false;
    std::string kept;
    int rows = 0;
    bool any_other = false;
    std::size_t p = l + 1;
    while (p < le) {
        std::size_t e = page.find('\n', p);
        e = e == std::string::npos || e > le ? le : e + 1;
        std::string line = page.substr(p, e - p);
        p = e;
        const std::size_t t = line.find(tag);
        if (t != std::string::npos) {
            const std::size_t n = t + tag.size(), q = line.find('}', n);
            if (q == std::string::npos) return false;
            const bool is_mine = std::find(mine.begin(), mine.end(), line.substr(n, q - n)) != mine.end();
            any_other = any_other || !is_mine;
            if (is_mine != ours) continue;
            while (replace_once(line, first, "")) {}
            if (rows++ == 0) {   // the page's first row is named FirstItem
                const std::size_t close = line.rfind("/>");
                if (close == std::string::npos) return false;
                line.insert(close, first.substr(1) + " ");
            }
        }
        kept += line;
    }
    if (!rows || (!ours && !any_other)) return false;
    page.replace(l + 1, le - (l + 1), kept);
    if (ours) {
        g_open_props = props;
        replace_once(page, "Content=\"ALL SETTINGS\"", "Content=\"" + tile_title() + "\"");
        while (replace_once(page, kOurSuffix, "\"")) {}   // the descriptions needn't say whose they are
    }
    return true;
}

// MODS > Installed mods ("INSTALLED MODS"): each mod's view is a list of its rows; ours lose
// theirs, as they're on their own page. The platform finds what A does on a row by its place
// in the list, so ours go only from the end of a list (they're last in our mod's).
bool filter_installed_page(std::string& page) {
    const std::string item = "<local:TileMenuDataItem x:Key=\"", tag = "Tag=\"{StaticResource ";
    const std::string list_at = "<local:CommonListBox", list_end = "</local:CommonListBox>";
    if (page.find("Content=\"INSTALLED MODS\"") == std::string::npos) return false;
    std::vector<std::string> mine;   // our rows' items, by key
    for (std::size_t p = page.find(item); p != std::string::npos; p = page.find(item, p + 1)) {
        const std::size_t n = p + item.size(), q = page.find('"', n), e = page.find("/>", n);
        if (q == std::string::npos || e == std::string::npos) return false;
        if (page.substr(n, e - n).find(kOurSuffix) != std::string::npos) mine.push_back(page.substr(n, q - n));
    }
    if (mine.empty()) return false;
    bool changed = false;
    for (std::size_t l = page.find(list_at); l != std::string::npos; l = page.find(list_at, l + 1)) {
        const std::size_t open = page.find('>', l), le = open == std::string::npos ? open : page.find(list_end, open);
        if (le == std::string::npos) return false;
        // the rows, each a line: from the end, ours while there are any
        std::size_t cut = le, p = le;
        while (p > open + 1) {
            const std::size_t start = page.rfind('\n', p - 2) + 1;
            const std::string line  = page.substr(start, p - start);
            const std::size_t t     = line.find(tag);
            if (t != std::string::npos) {
                const std::size_t n = t + tag.size(), q = line.find('}', n);
                if (q == std::string::npos || std::find(mine.begin(), mine.end(), line.substr(n, q - n)) == mine.end() ||
                    line.find("x:Name=") != std::string::npos)
                    break;
                cut = start;
            } else if (line.find_first_not_of(" \t\r\n") != std::string::npos) {
                break;   // something else (the list's own tag)
            }
            p = start;
        }
        if (cut == le) continue;
        page.erase(cut, le - cut);
        page.insert(cut, "                                ");
        changed = true;
    }
    return changed;
}

bool has(const char* bytes, int length, const char* what) {
    const std::size_t n = std::strlen(what);
    const char* end     = bytes + length;
    return std::search(bytes, end, what, what + n) != end;
}

void edit_page(void** out) {
    const char* bytes = nullptr;
    int length        = 0;
    if (!resource_text(*out, &bytes, &length)) return;
    // Only the two pages: their class is near the top.
    const int head = std::min(length, 2048);
    const bool settings = has(bytes, head, "x:Class=\"ForzaUI.A_PauseOptions\"");
    const bool mods     = !settings && has(bytes, head, "x:Class =\"ForzaUI.A_HudOptions\"");   // the platform's (see its settings.cpp)
    if (!settings && !mods) return;
    std::string text(bytes, static_cast<std::size_t>(length));
    if (settings) {
        if (!add_settings_tile(text) || !replace_resource(out, text)) {
            if (!g_kbm_failed.exchange(true)) logf("the Settings page isn't as expected: the keys stay on the MODS page");
            return;
        }
        if (!g_kbm_tile.exchange(true)) logf("Pause > Settings: Keyboard & Mouse tile added");   // the page is read each time it shows
        return;
    }
    // The Settings page may not have been read yet (it is when it first shows); the rows leave the
    // MODS pages unless it couldn't be given the tile.
    if (g_kbm_failed.load()) return;
    const uint64_t opening = g_kbm_opening.exchange(0);
    const bool ours        = opening && GetTickCount64() - opening < 5000;
    if (!ours && filter_installed_page(text)) {
        replace_resource(out, text);
    } else if (filter_mods_page(text, ours)) {
        if (replace_resource(out, text) && ours) find_descriptors();
    } else if (ours) {
        logf("the Mods page isn't as expected: it shows every mod's rows");
    }
}

void** d_get_resource(void* reader, void** out) {
    const auto from = reinterpret_cast<uintptr_t>(RETURN_ADDRESS());
    void** r = o_get_resource(reader, out);
    // The page loader's call, or Extra Options' (from its own DLL): nothing else outside the game calls it.
    const bool in_game = from >= reinterpret_cast<uintptr_t>(g_base) && from < g_image_end;
    if (r == out && out && *out && (!in_game || from == reinterpret_cast<uintptr_t>(g_base) + RVA_PAGE_XAML_RETURN))
        edit_page(out);
    return r;
}

// L"Anthem\A_HudOptions" -> L"Anthem\A_ModOptions", in place (as the platform's MODS tile does)
bool point_at_mods_page(uint8_t* rec) {
    __try {
        const uint64_t size = *reinterpret_cast<uint64_t*>(rec + 0x38);
        const uint64_t cap  = *reinterpret_cast<uint64_t*>(rec + 0x40);
        wchar_t* p = cap > 7 ? *reinterpret_cast<wchar_t**>(rec + 0x28) : reinterpret_cast<wchar_t*>(rec + 0x28);
        if (size != 19 || !readable_range(p, 40) || std::wcsncmp(p, L"Anthem\\A_HudOptions", 19) != 0) return false;
        std::wmemcpy(p + 7, L"A_ModOptions", 12);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

uint8_t* d_hud_scene(uint8_t* out) {
    uint8_t* r = o_hud_scene(out);
    if (g_kbm_builds.load() > 0 && GetTickCount64() - g_kbm_asked.load() < 1500) {
        --g_kbm_builds;
        if (point_at_mods_page(r)) g_kbm_opening = GetTickCount64();
        else logf("the HUD page's scene record isn't as expected: the tile opened the HUD page");
    }
    return r;
}

void kbm_tile_pressed(void* page) {
    g_kbm_builds = 2;
    g_kbm_asked  = GetTickCount64();
    game<ObjectFn>(RVA_HUD_TILE)(page);
}

void wire_kbm_tile(void* page) {
    static const Pmf pmf{reinterpret_cast<void*>(&kbm_tile_pressed), 0, 0};
    __try {
        game<WireButtonFn>(RVA_WIRE_BUTTON)(page, "PART_Button_FH2Mouse", &pmf);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

void d_wire_tiles(void* page) {
    g_kbm_builds = 0;
    o_wire_tiles(page);
    if (g_kbm_tile.load()) wire_kbm_tile(page);
}

void hook_settings_tile() {
    if (!g_kbm_rows) return;
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(g_base);
    const auto* nt  = reinterpret_cast<const IMAGE_NT_HEADERS64*>(g_base + dos->e_lfanew);
    g_image_end     = reinterpret_cast<uintptr_t>(g_base) + nt->OptionalHeader.SizeOfImage;
    // What it calls (what it hooks, the platform checks against the exe on disk: the platform hooks them too).
    static const Expect checks[] = {
        {RVA_WIRE_BUTTON, {0x48, 0x8B, 0xC4, 0x57, 0x48, 0x81, 0xEC, 0x90, 0x00, 0x00, 0x00, 0x48}},
        {RVA_HUD_TILE, {0x40, 0x53, 0x48, 0x83, 0xEC, 0x30, 0x48, 0x8B, 0xD9, 0x48, 0x8B, 0x89}},
        {RVA_GAME_ALLOCATE, {0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x8B, 0x15}},
        {RVA_GAME_FREE, {0x48, 0x85, 0xC9, 0x74, 0x1D, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B}},
        {RVA_OBJECT_INIT, {0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8D}},
        {RVA_OBJECT_CLEANUP, {0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8D, 0x05, 0xB3, 0xD0, 0x9F}},
    };
    static const uint8_t notify_bytes[] = {0x40, 0x57, 0x48, 0x83, 0xEC, 0x60, 0x48, 0xC7, 0x44, 0x24, 0x20, 0xFE};
    static const uint8_t string_bytes[] = {0x48, 0x89, 0x4C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x30};
    g_refresh_ok = !std::memcmp(g_base + RVA_NOTIFY_PROP, notify_bytes, sizeof notify_bytes) &&
                   !std::memcmp(g_base + RVA_AVUI_STRING, string_bytes, sizeof string_bytes);
    if (!g_refresh_ok) logf("PropertyChanged isn't as expected: a preset's keys show the next time the page opens");
    for (const Expect& e : checks)
        if (std::memcmp(g_base + e.rva, e.bytes, e.len)) {
            logf("the Settings page's code isn't as expected (game+0x%llX): the keys stay on the MODS page",
                 static_cast<unsigned long long>(e.rva));
            return;
        }
    FH2Hook* hooks[] = {
        g_api->hook_function(g_mod, "Settings tile: wire the tiles", g_base + RVA_PAUSE_WIRE_TILES,
                             reinterpret_cast<void*>(&d_wire_tiles), reinterpret_cast<void**>(&o_wire_tiles)),
        g_api->hook_function(g_mod, "Settings tile: HUD page scene record", g_base + RVA_HUD_SCENE_RECORD,
                             reinterpret_cast<void*>(&d_hud_scene), reinterpret_cast<void**>(&o_hud_scene)),
        g_api->hook_function(g_mod, "Settings tile: page XAML", g_base + RVA_GET_RESOURCE,
                             reinterpret_cast<void*>(&d_get_resource), reinterpret_cast<void**>(&o_get_resource)),
    };
    for (FH2Hook* h : hooks)
        if (!h) {
            for (FH2Hook* k : hooks)
                if (k) g_api->disable_hooks(g_mod, k);
            logf("couldn't hook the Settings page: the keys stay on the MODS page");
            return;
        }
    for (FH2Hook* h : hooks) g_api->enable_hooks(g_mod, h);   // the XAML last: the rest is ready for it
}

DWORD WINAPI start(LPVOID) {
    g_base = static_cast<uint8_t*>(g_api->wait_for_code(0xFFFFFFFFu));
    if (!g_base || !code_matches()) return 0;
    lang::set_base(g_base);   // the game's language from now on (see lang.h)
    void** slot = reinterpret_cast<void**>(g_base + RVA_UI_UPDATE_SLOT);
    if (!g_api->hook_pointer(g_mod, "UserInterface::Update", slot, reinterpret_cast<void*>(&detour_update),
                             reinterpret_cast<void**>(&o_update)) ||
        !g_api->enable_hooks(g_mod, nullptr)) {
        logf("couldn't hook UserInterface::Update; not starting");
        return 0;
    }
    // Pad-level presses for the help bar: optional, keys are sent without it.
    static const uint8_t pressed_bytes[] = {0x44, 0x88, 0x4C, 0x24, 0x20, 0x55, 0x57, 0x41, 0x54, 0x41, 0x57, 0x48};
    if (!std::memcmp(g_base + RVA_WAS_PRESSED, pressed_bytes, sizeof pressed_bytes) &&
        g_api->hook_function(g_mod, "input WasPressed", g_base + RVA_WAS_PRESSED, reinterpret_cast<void*>(&was_pressed),
                             reinterpret_cast<void**>(&o_was_pressed)))
        g_api->enable_hooks(g_mod, nullptr);
    else
        o_was_pressed = nullptr, logf("the input check isn't as expected: help bar clicks send keys only");
    // The decal editor's W A S D: optional too.
    static const uint8_t analog_bytes[] = {0x40, 0x53, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x68};
    if (!std::memcmp(g_base + RVA_GET_ANALOG, analog_bytes, sizeof analog_bytes) &&
        g_api->hook_function(g_mod, "input GetAnalog", g_base + RVA_GET_ANALOG, reinterpret_cast<void*>(&get_analog),
                             reinterpret_cast<void**>(&o_get_analog)))
        g_api->enable_hooks(g_mod, nullptr);
    else
        o_get_analog = nullptr, logf("the analog input isn't as expected: no W A S D for decals");
    // The keyboard's driving keys: GetAnalog (above), GetDigital and WasPressed, in context 0.
    static const uint8_t digital_bytes[] = {0x40, 0x53, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x58};
    static const uint8_t active_bytes[]  = {0x40, 0x53, 0x48, 0x83, 0xEC, 0x40, 0x48, 0x83, 0x79, 0x58, 0x00, 0x8B};
    if (!std::memcmp(g_base + RVA_CONTEXT_ACTIVE, active_bytes, sizeof active_bytes) && o_get_analog && o_was_pressed &&
        !std::memcmp(g_base + RVA_GET_DIGITAL, digital_bytes, sizeof digital_bytes) &&
        g_api->hook_function(g_mod, "input GetDigital", g_base + RVA_GET_DIGITAL, reinterpret_cast<void*>(&get_digital),
                             reinterpret_cast<void**>(&o_get_digital)))
        g_api->enable_hooks(g_mod, nullptr);
    else
        o_get_digital = nullptr, logf("the game's input isn't as expected: no keys for driving");
    // A prompt taking the press: optional (without it, the HUD's other prompts miss the key's).
    static const uint8_t consume_bytes[] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18};
    if (o_get_digital && !std::memcmp(g_base + RVA_CONSUME, consume_bytes, sizeof consume_bytes) &&
        g_api->hook_function(g_mod, "input Consume", g_base + RVA_CONSUME, reinterpret_cast<void*>(&consume),
                             reinterpret_cast<void**>(&o_consume)))
        g_api->enable_hooks(g_mod, nullptr);
    else
        o_consume = nullptr, logf("the input's consume isn't as expected: the keys' prompts go to the first that asks");
    // Signing in without a pad: pressing A on the title screen signs in the user of the pad that
    // pressed it (0xE7EBC0) and, with no pad, gives up ("Sign in needed") instead of asking the
    // account picker (0xD08BD0, which takes no pad, as the pairing page's use of it shows, and
    // under Azimuth answers with the signed-in player). With the keys on, it asks: the jump that
    // gives up (je at 0xE7ECC4, after test bl, bl: "there's a pad") goes.
    if (g_keyboard && g_api->api_version >= 7) {
        static const uint8_t give_up[] = {0x0F, 0x84, 0x9B, 0x01, 0x00, 0x00};
        static const uint8_t ask[]     = {0x66, 0x0F, 0x1F, 0x44, 0x00, 0x00};   // nop
        if (FH2Hook* p = g_api->patch_bytes(g_mod, "sign in without a pad", g_base + 0xE7ECC4, give_up, ask, sizeof ask))
            g_api->enable_hooks(g_mod, p);
        else
            logf("signing in isn't as expected: without a pad, sign in needs one");
    }
    // No "Controller disconnected" while the keyboard plays: optional.
    static const uint8_t pairing_bytes[] = {0x48, 0x8B, 0xC4, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x8D, 0x68, 0xC8};
    if (!std::memcmp(g_base + RVA_SHOW_PAIRING, pairing_bytes, sizeof pairing_bytes) &&
        g_api->hook_function(g_mod, "show controller pairing", g_base + RVA_SHOW_PAIRING, reinterpret_cast<void*>(&show_pairing),
                             reinterpret_cast<void**>(&o_show_pairing)))
        g_api->enable_hooks(g_mod, nullptr);
    else
        o_show_pairing = nullptr, logf("the controller pairing page isn't as expected: it shows when a pad goes");
    // Photo mode without a pad: optional.
    static const uint8_t profile_input_bytes[] = {0x40, 0x57, 0x48, 0x83, 0xEC, 0x30, 0x48, 0xC7, 0x44, 0x24, 0x20, 0xFE,
                                                  0xFF, 0xFF, 0xFF, 0x48, 0x89, 0x5C, 0x24, 0x48, 0x0F, 0xB6, 0xDA};
    if (o_show_pairing && !std::memcmp(g_base + RVA_PROFILE_INPUT, profile_input_bytes, sizeof profile_input_bytes) &&
        g_api->hook_function(g_mod, "profile's input", g_base + RVA_PROFILE_INPUT, reinterpret_cast<void*>(&profile_input),
                             reinterpret_cast<void**>(&o_profile_input)))
        g_api->enable_hooks(g_mod, nullptr);
    else
        logf("the profile's input isn't as expected: without a pad, photo mode doesn't start");
    // The bytes before the call: cmp byte [rbx+4], 0 / jne / lea rdx, [rdi+0x28] / mov rcx, rdi.
    static const uint8_t pause_bytes[] = {0x80, 0x7B, 0x04, 0x00, 0x75, 0x0C, 0x48, 0x8D, 0x57, 0x28, 0x48, 0x8B, 0xCF, 0xE8};
    if (o_show_pairing && !std::memcmp(g_base + RVA_PAD_LOST_PAUSE - 13, pause_bytes, sizeof pause_bytes) &&
        g_api->hook_call(g_mod, "HUD: pause while the pad is gone", g_base + RVA_PAD_LOST_PAUSE, reinterpret_cast<void*>(&pad_lost_pause),
                         reinterpret_cast<void**>(&o_pad_lost_pause)))
        g_api->enable_hooks(g_mod, nullptr);
    else
        logf("the HUD's pause isn't as expected: without a pad, the game may pause again when resumed");
    static const uint8_t set_input_bytes[] = {0x48, 0x89, 0x91, 0x88, 0x00, 0x00, 0x00, 0xC3};
    void** set_input_slot = reinterpret_cast<void**>(g_base + RVA_UI_SET_INPUT_SLOT);
    if (o_show_pairing && *set_input_slot == g_base + RVA_UI_SET_INPUT &&
        !std::memcmp(g_base + RVA_UI_SET_INPUT, set_input_bytes, sizeof set_input_bytes) &&
        g_api->hook_pointer(g_mod, "UserInterface::SetInput", set_input_slot, reinterpret_cast<void*>(&ui_set_input),
                            reinterpret_cast<void**>(&o_ui_set_input)))
        g_api->enable_hooks(g_mod, nullptr);
    else
        logf("the UI's input isn't as expected: when a pad goes, the game waits for one");
    // The keys that hold the pad's own buttons (the radio): optional. Not checked here: FH2 ANNA may
    // have hooked it already (the platform checks the game's bytes, and shares the function).
    if (o_get_digital &&
        g_api->hook_function(g_mod, "input update", g_base + RVA_INPUT_UPDATE, reinterpret_cast<void*>(&input_update),
                             reinterpret_cast<void**>(&o_input_update)))
        g_api->enable_hooks(g_mod, nullptr);
    else
        logf("the game's input update isn't as expected: no keys for the radio");
    if (g_cfg.debug) {   // which game code asks about which button (for finding new ones)
        if (g_api->hook_function(g_mod, "input ButtonState (debug)", g_base + 0x106E690, reinterpret_cast<void*>(&dbg_state),
                                 reinterpret_cast<void**>(&o_dbg_state)))
            g_api->enable_hooks(g_mod, nullptr);
    }
    // The garage camera's wheel zoom: optional.
    static const uint8_t garage_bytes[] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x48, 0x89};
    if (!std::memcmp(g_base + RVA_GARAGE_CAMERA, garage_bytes, sizeof garage_bytes) &&
        g_api->hook_function(g_mod, "garage camera", g_base + RVA_GARAGE_CAMERA, reinterpret_cast<void*>(&garage_camera),
                             reinterpret_cast<void**>(&o_garage_camera)))
        g_api->enable_hooks(g_mod, nullptr);
    else
        o_garage_camera = nullptr, logf("the garage camera isn't as expected: no wheel zoom there");
    // Photo mode's camera: optional, the rest works without it. The function, not CCamPhotoMode's
    // vtable slot: the Autoshow's photo mode camera (CUIPhotoModeCamera) has it in its own vtable.
    static const uint8_t photo_bytes[] = {0x40, 0x53, 0x48, 0x83, 0xEC, 0x40, 0x80, 0xB9, 0x2C, 0x02, 0x00, 0x00};
    if (at<uint8_t*>(g_base, RVA_PHOTO_INPUT_SLOT) == g_base + RVA_PHOTO_INPUT &&
        !std::memcmp(g_base + RVA_PHOTO_INPUT, photo_bytes, sizeof photo_bytes) &&
        g_api->hook_function(g_mod, "photo mode camera input", g_base + RVA_PHOTO_INPUT, reinterpret_cast<void*>(&photo_input),
                             reinterpret_cast<void**>(&o_photo_input)))
        g_api->enable_hooks(g_mod, nullptr);
    else
        logf("photo mode's camera isn't as expected: no mouse there");
    hook_nitro();
    hook_horn();
    hook_key_icons();   // last: it enables what it hooked only when all of it is
    hook_settings_tile();   // after every enable_hooks(g_mod, nullptr): it enables its own only if all of them are made
    // FH2 ANNA (its interface, once every mod has started): the keys it takes, the keys don't do.
    if (g_api->api_version >= 2 && g_api->wait_for_mods(60000)) {
        if ((g_anna = static_cast<const FH2AnnaApi*>(g_api->get_interface("FH2ANNA", 1)))) logf("FH2 ANNA is here: the ANNA keys work it");
        hook_drone();
    }
    g_arrow = LoadCursorW(nullptr, IDC_ARROW);
    if (HANDLE t = CreateThread(nullptr, 0, window_thread, nullptr, 0, nullptr)) CloseHandle(t);
    if (HANDLE t = CreateThread(nullptr, 0, hook_thread, nullptr, 0, nullptr)) CloseHandle(t);
    logf("ready (hover selects: %s, hides after %lu s)", g_cfg.hover_focus ? "on" : "off", g_cfg.hide_ms / 1000);
    return 0;
}

} // namespace

FH2MOD_EXPORT int fh2mod_init(const FH2ModApi* api) {
    if (api->api_version < 3) return 1;
    g_api = api;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&fh2mod_init), &g_self);
    lang::init(std::wstring(api->azimuth_dir) + L"\\FH2Mouse\\FH2Mouse.ini");   // the rows' language: the one seen last
    FH2ModInfo info{};
    info.size             = sizeof info;
    info.name             = kModName;   // the Mods page ends our rows' descriptions with it (kOurSuffix)
    info.version          = FH2MOUSE_VERSION;
    info.github_repo      = "qiangqiang101/FH2MouseKeyboard";
    info.platform_version = FH2MOD_PLATFORM_VERSION;
    info.check_updates    = 1;
    info.description      = lang::tr("Drive with the keyboard, point at things in the menus and click them, drag the map, turn the garage camera, place decals with the keyboard, and fly the photo mode camera, all without picking up a controller.");
    g_mod                 = api->register_mod(&info);
    if (!g_mod) return 1;
    load_settings();
    load_extra_bindings();
    if (!g_cfg.enabled) {
        api->log(g_mod, "off (FH2Mouse\\FH2Mouse.ini, Enabled=0)");
        return 0;
    }
    add_keyboard_rows();
    if (g_keyboard) write_key_icons();   // before the game reads its files
    if (HANDLE t = CreateThread(nullptr, 0, start, nullptr, 0, nullptr)) CloseHandle(t);
    return 0;
}
