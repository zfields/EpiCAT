#include <M2M_LM75A.h>
#include <Notecard.h>
#ifdef ARDUINO_ARCH_ESP32
#include <NotecardAuxiliaryWiFi.h>
#endif // ARDUINO_ARCH_ESP32

#define NDEBUG

#define productUID "<com.your-company.your-product>"
#define usbSerial Serial
#define txRxPinsSerial Serial1

#define MAX_SAFE_TEMP_C    30
#define MAX_STORAGE_TEMP_C 25

using namespace blues;

M2M_LM75A lm75a;
Notecard notecard;
NotecardAuxiliaryWiFi aux_wifi(notecard);

static size_t polling_period_s;

void logNoteF (const char * format_, ...) {
    char log[256];
    va_list args;
    va_start(args, format_);
    vsnprintf(log, sizeof(log), format_, args);

    J *cmd = notecard.newCommand("note.add");
    if (cmd) {
        JAddStringToObject(cmd, "file", "log.qo");
        J *body = JAddObjectToObject(cmd, "body");
        if (body) {
            JAddStringToObject(body, "text", log);
            notecard.sendRequest(cmd);
        } else {
            JDelete(cmd);
        }
    }

    va_end(args);
}

int acquireGPSLocation (size_t timeout_s_ = 95) {
    int result;
    const char *gps_mode;
    size_t gps_time_s;

    // Get Current Configuration
    {
        J *rsp = notecard.requestAndResponse(notecard.newRequest("card.location.mode"));
        if (rsp) {
            gps_mode = JGetString(rsp, "mode");
            NoteDeleteResponse(rsp);
        } else {
            result = -1;  // Unable to communicate with Notecard
            return result;
        }
    }

    // Halt periodic tracking, before active sync is performed
    {
        J *cmd = notecard.newCommand("card.location.mode");
        JAddStringToObject(cmd, "mode", "off");
        notecard.sendRequest(cmd);
    }

    // Gather timestamp of previous location information
    {
        J *rsp = notecard.requestAndResponse(notecard.newRequest("card.location"));
        if (rsp) {
            gps_time_s = JGetInt(rsp, "time");
            logNoteF("INFO: Previous GPS time (seconds): %u", gps_time_s);
            NoteDeleteResponse(rsp);
        } else {
            result = -1;  // Unable to communicate with Notecard
            return result;
        }
    }

    // Activate GPS
    {
        J *cmd = notecard.newCommand("card.location.mode");
        JAddStringToObject(cmd, "mode", "continuous");
        notecard.sendRequest(cmd);
    }

    // Block while resolving GPS location
    for (const size_t start_ms = ::millis();;) {
        if (timeout_s_) {
            if (::millis() >= (start_ms + (timeout_s_ * 1000))) {
                logNoteF("WARNING: GPS user timeout expired!");
                result = -3;  // User timeout has expired
                break;
            }
        }

        // Check if GPS has acquired location information
        J *rsp = notecard.requestAndResponse(notecard.newRequest("card.location"));
        if (rsp) {
            const size_t current_gps_time_s = JGetInt(rsp, "time");
            if (current_gps_time_s != gps_time_s) {
                logNoteF("INFO: Current GPS time (seconds): %u", current_gps_time_s);
                result = 0;  // GPS has fixed on position
                break;
            }
            if (JGetObjectItem(rsp, "stop")) {
                logNoteF("WARNING: GPS internal timeout expired!");
                result = -2;  // Notecard has signaled polling should stop,
                              // most likely caused by internal GPS timeout
                break;
            }
            NoteDeleteResponse(rsp);
        } else {
            result = -1;  // Unable to communicate with Notecard
            break;
        }
        ::delay(250);
    }

    // Restore Previous Configuration
    {
        J *cmd = notecard.newCommand("card.location.mode");
        JAddStringToObject(cmd, "mode", gps_mode);
        notecard.sendRequest(cmd);
    }

    // Clean up and exit
    return result;
}

void setup()
{
#ifdef NDEBUG
    // Provide visual signal when the Host MCU is powered
    ::pinMode(LED_BUILTIN, OUTPUT);
    ::digitalWrite(LED_BUILTIN, HIGH);
#endif

    // Configure temperature threshold for Ephinephrine
    // https://community.kidswithfoodallergies.org/blog/researchers-review-effects-of-heat-cold-on-epinephrine
    lm75a.begin();
    lm75a.setOsTripTemperature(MAX_SAFE_TEMP_C);         // Max safe temperature for travel
    lm75a.setHysterisisTemperature(MAX_STORAGE_TEMP_C);  // Max safe temperature for storage

#ifndef NDEBUG
    // Initialize Debug Output
    usbSerial.begin(115200);
    static const size_t MAX_SERIAL_WAIT_MS = 5000;
    size_t begin_serial_wait_ms = ::millis();
    while (!usbSerial && (MAX_SERIAL_WAIT_MS > (::millis() - begin_serial_wait_ms)))
    {
        ; // wait for debug serial port to connect. Needed for native USB
    }
    notecard.setDebugOutputStream(usbSerial);
#endif

    // Initialize Notecard
    notecard.begin();
    aux_wifi.begin();

    // Configure Notecard to synchronize with Notehub periodically, as well as
    // adjust the frequency based on the battery level
    {
        J *req = notecard.newRequest("hub.set");
        JAddStringToObject(req, "sn", "EpiCAT");
        JAddStringToObject(req, "product", productUID);
        JAddStringToObject(req, "mode", "periodic");
        JAddStringToObject(req, "vinbound", "usb:60;high:120;normal:240;low:480;dead:0");
        JAddStringToObject(req, "voutbound", "usb:30;high:60;normal:90;low:120;dead:0");
        notecard.sendRequest(req);
    }

    // Optimize voltage variable behaviors for LiPo battery
    {
        J *req = notecard.newRequest("card.voltage");
        JAddStringToObject(req, "mode", "lipo");
        notecard.sendRequest(req);
    }

    // Ensure the accelerometer has been activated
    {
        J *req = notecard.newRequest("card.motion.mode");
        JAddBoolToObject(req, "start", true);
        notecard.sendRequest(req);
    }

    // Configure GPS heartbeat to verify last location reported
    {
        J *req = notecard.newRequest("card.location.track");
        JAddBoolToObject(req, "start", true);
        JAddBoolToObject(req, "sync", true);
        JAddBoolToObject(req, "heartbeat", true);
        JAddNumberToObject(req, "hours", 12);
        notecard.sendRequest(req);
    }

    // Configure auxiliary GPIO for input (interrupt trigger)
    {
        J *req = notecard.newRequest("card.aux");
        JAddStringToObject(req, "mode", "gpio");
        J *usage = JAddArrayToObject(req, "usage");
        JAddItemToArray(usage, JCreateString("count-pullup")); // Aux 1
        JAddItemToArray(usage, JCreateString(""));             // Aux 2
        JAddItemToArray(usage, JCreateString(""));             // Aux 3
        JAddItemToArray(usage, JCreateString(""));             // Aux 4
        notecard.sendRequest(req);
    }

    // Establish a template to optimize queue size and data usage
    {
        J *req = notecard.newRequest("note.template");
        JAddStringToObject(req, "file", "status.qo");
        J *body = JAddObjectToObject(req, "body");
        JAddBoolToObject(body, "alert", TBOOL);
        JAddBoolToObject(body, "low_batt", TBOOL);
        JAddNumberToObject(body, "temp", TFLOAT16);
        JAddNumberToObject(body, "voltage", TFLOAT16);
        notecard.sendRequest(req);
    }

    // Sample battery voltage
    float voltage = 0.0f;
    bool low_battery = false;
    {
        J *rsp = notecard.requestAndResponse(notecard.newRequest("card.voltage"));
        if (rsp) {
            voltage = JGetNumber(rsp, "value");
            const char * power_mode = JGetString(rsp, "mode");
            low_battery = (power_mode && !strcmp(power_mode,"low"));
            NoteDeleteResponse(rsp);
        }
    }

    // Sample temperature sensor
    const float temperature = lm75a.getTemperature();
    static const bool ACTIVE_ALARM = (MAX_SAFE_TEMP_C < temperature);
    static const bool TEMP_WARNING = (MAX_STORAGE_TEMP_C < temperature);

    // Update polling interval based upon temperature
    if (TEMP_WARNING) {
        polling_period_s = 180;  // Sample temperature every three (3) minutes
                                 // when storage threshold has been exceeded
    } else {
        polling_period_s = 1800;  // Check status every half hour
    }

    // Select location service (Wi-Fi or GPS) based upon alarm state
    if (ACTIVE_ALARM) {
        // Halt periodic tracking when alarm is active
        {
            J *cmd = notecard.newCommand("card.location.mode");
            JAddStringToObject(cmd, "mode", "off");
            notecard.sendRequest(cmd);
        }

        // Update Current Location (via GPS)
        if (int gps_error = acquireGPSLocation()) {
            logNoteF("ERROR: Failed to acquire GPS signal! <%d>", gps_error);
            // Failed to acquire GPS try Wi-Fi instead
            aux_wifi.updateTriangulationData(true, false);
            aux_wifi.logCachedSsids();  // Log SSIDs used in calculation
        }
    } else {
        aux_wifi.updateTriangulationData(true);  // Update Current Location (via Wi-Fi)
        aux_wifi.logCachedSsids();  // Log SSIDs used in calculation

        // Configure GPS to update only when device is in motion
        {
            J *req = notecard.newRequest("card.location.mode");
            JAddStringToObject(req, "mode", "periodic");
            JAddNumberToObject(req, "seconds", 900);  // Fifteen (15) minutes
            notecard.sendRequest(req);
        }
    }

    // Send results to Notehub
    {
        J *req = notecard.newRequest("note.add");
        JAddStringToObject(req, "file", "status.qo");
        J *body = JAddObjectToObject(req, "body");
        if (ACTIVE_ALARM) {
            JAddBoolToObject(req, "sync", true);
            JAddBoolToObject(body, "alert", true);
        }
        if (low_battery) {
            JAddBoolToObject(body, "low_batt", true);
        }
        JAddNumberToObject(body, "temp", temperature);
        JAddNumberToObject(body, "voltage", voltage);
        notecard.sendRequest(req);
    }
}

void loop()
{
    // Request sleep from loop to safeguard against transmission failure,
    // and ensure sleep request is honored so power usage is minimized.
    {
        // Create a "command" instead of a "request", because the host
        // MCU is going to power down and cannot receive a response.
        J *cmd = notecard.newCommand("card.attn");
        JAddStringToObject(cmd, "mode", "rearm,auxgpio,sleep");
        JAddNumberToObject(cmd, "seconds", polling_period_s);
        notecard.sendRequest(cmd);
    }

    // Wait 3s before retrying
    ::delay(3000);
}
