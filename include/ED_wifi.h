
#pragma once

#include "esp_wifi.h"
#include <esp_err.h>
#include <esp_event_base.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include <cstring>
#include <secrets.h>
#include "esp_timer.h"
#include <optional>
#include "esp_log.h"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <esp_http_server.h>
#include <vector>
#include <functional>
#include "ED_nvs.h"


/* The event group allows multiple bits for each event, but we only care about two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1
#define ED_MAX_SSID_PWD_SIZE 19
// #define EXAMPLE_H2E_IDENTIFIER 0 // Define the EXAMPLE_H2E_IDENTIFIER constan

/*
template for the wifi credential which need to be loaded at boot. Actual values to be defined in secrets.h
Specify in sequence SSID, password,  U or u or 0 [unconnectable] , any other char for [connectable]
#define WIFI_CREDENTIALS   \
    {"SSID1", "PW1", "C"},     \
    {"SSID2", "PW2". "C"},     \
    {"SSID3", "PW3", "U"}
*/

namespace ED_wifi
{

    // A memory-efficient class for ESP32.
    // It avoids dynamic allocation and complex libraries.
    class MacAddress
    {
    public:
        // Default constructor (initializes to all zeros)
        MacAddress()
        {
            memset(_mac_addr, 0, 6);
        }

        // Constructor to initialize with a C-style array
        MacAddress(const uint8_t mac[6])
        {
            memcpy(_mac_addr, mac, 6);
        }

        // For read-only access on a const object
        const uint8_t& operator[](size_t index) const
        {
            return _mac_addr[index];
        }
        // Get a pointer to the internal array
        const uint8_t* get() const
        {
            return _mac_addr;
        }
        // returns a pointer to the internal class allowing modifications.
        uint8_t* set()
        {
            return _mac_addr;
        }
        // Formats the MAC address into a provided buffer.
        // The buffer must be at least 18 characters long.
        char* toString(char* buffer, size_t buffer_size) const
        {
            if (buffer && buffer_size >= 18)
            {
                snprintf(buffer, buffer_size, "%02X:%02X:%02X:%02X:%02X:%02X",
                         _mac_addr[0], _mac_addr[1], _mac_addr[2],
                         _mac_addr[3], _mac_addr[4], _mac_addr[5]);
            }
            return buffer;
        }

    private:
        uint8_t _mac_addr[6];
    };

    class WiFiService
    {
    public:
        class APCredential
        {
        public:
            /**
             * @brief type of Access Point, used to tell whether an AP can be connected (credentials available)
             */
            enum APType : bool
            {
                AP_CONNECTABLE   = 1, // a wifi AP to which you can connect
                AP_UNCONNECTABLE = 0  // a wifi AP to which you cannot connect and can just interfere with own ops.
            };
            /**
             * @brief compares AP based on the strength of their signal
             * @param a
             * @param b
             * @return positive is the signal of a is stronger than the one of b
             */
            static int                   compare_rssi_desc(const void* a, const void* b);
            static std::optional<APType> toAPType(char value);

            char     ssid[ED_MAX_SSID_PWD_SIZE];     // the SSID of the Access Point
            char     password[ED_MAX_SSID_PWD_SIZE]; // the password to access the SSID
            APType   type;                           // the type of AP: Connectable or Unconnectable
            int8_t   RSSI;                           // the latest measured value of the signal strength in dB
            uint8_t  chann;                          // the channel of the SSID
            uint32_t lastSeen;                       // timestamp in seconds of the last time the SSID was detected

            APCredential(const char* s, const char* p, bool canConnect);
            /**
             * @brief checks the target SSID identifier matches the current one
             * @param targetSsid
             * @return true if matches
             */
            bool matches(const char* targetSsid) const;
            APCredential();
            /**
             * @brief type of Codec operation which needs to be completed
             */
            enum class CodecOperation
            {
                OP_JOIN,
                OP_SPLIT
            }; // used to specify the CODEC operation when joining or splitting data pairs (strings) in/from a singly codec string
            /**
             * @brief codec the credential to a string or vice versa
             * @param op the operation to perform
             * @param packedCred the string to be packed or unpacked
             * @param target the source credential to be encoded or the decoded one.
             */
            static esp_err_t packCredential(CodecOperation op, std::string packedCred, APCredential* target);
        };

    public:
        /**
         * @brief stores the credential to access Wifi Access Points and the latest signal strength measured by the ESP
         * this class is designed to be able to track also external SSID to help diagnose interference issues on the internal wifi network
         * using the ESP as sensor for their signal strength and band at their location.
         */
        class APCredentialManager
        {
        private:

            static constexpr size_t maxTrackedSSIDs = 10;
            APCredentialManager()                   = delete; // meant to be only static
            inline static bool initialized          = false;
            /**
             * @brief loads boilerplate credentials stored in the WIFI_CREDENTIALS define stored in a support header file.
             */
            static void loadDefaultAPs();
            enum class CodecOperation
            {
                Join,
                Split
            }; // used to specify the CODEC operation when joining or splitting data pairs (strings) in/from a singly codec string

        public:
            static constexpr StringLiteral NVS_AREA_NAME = "Config_WiFi";
            /**
             * @brief removes a given SSID from the registered and tracked SSID
             * @param ssid
             * @return
             */
            static bool remove(const char* ssid);
            /**
             * @brief finds a given SSID among the registered and tracked SSID, and updates the associated measurement of strength of the signal and channel used
             * @param ssid
             * @param strength the new value os RSSI (strength of signal)
             * @return pointer to the updated APcredential instance
             */
            // static const APCredential *findAndUpdateInfo(char *ssid, int strength, uint8_t chann) ;

            /**
             * @brief finds a given SSID among the registered and tracked SSID, and updates the associated measurement of strength of the signal and channel used
             * @param ssid
             * @param strength the new value os RSSI (strength of signal)
             * @param chann the detected channel
             * @param index suggested index for the right item in the array. Used to avoid rescans.
             * @return pointer to the updated APcredential instance, nullpointer if out of index
             */
            static const APCredential* findAndUpdateInfo(char* ssid, int strength, uint8_t chann, int8_t index = -1);
            /**
             * @brief gets the current number of Access Points whose credentials are marked as usable for connection
             * @return
             */
            static size_t getConnCredentialsQty() { return count; }
            /**
             * @brief
             * werg
             * werb
             *
             * @param index
             * @return const char*
             */
            static const char* getSSID(size_t index)
            {
                if (index < count)
                    return credentials[index].ssid;
                else
                    return nullptr;
            }

            /**
             * @brief processes the  AP detected during a WiFi scan matching the tracked list and updating their data
             * @param number number of detected AP
             * @param ap_records records of detected AP
             */
            static void updateDetectedAPs(uint16_t number, wifi_ap_record_t* ap_records);
            /**
             * @brief switches to the next active AP, sorted by detected strength of signal. when the list is exhaustes, returns nullptr
             * @return true is there is at least one reachable connectable network
             */
            static bool                setNextActiveAP();
            static const APCredential* curAP;

            /**
             * @brief adds a new set of SSID/pwd to access a Wifi Network.
             * if the SSID already exists, the password is updated.
             * @param ssid
             * @param password
             * @param canConnect if true, the credentials are considered for connection. otherwise, just for monitoring (password redundant)
             * @return false is no space available and insert would be required. remove a credential and retry.
             */
            static bool addOrUpdate(const char* ssid, const char* password, bool canConnect);
            /**
             * @brief packs/unpacks a SSID/Password credential for storage as a value in a key:value NVS storage
             * @param mode
             * @param ssid
             * @param password
             * @param codecStr packed string (input or output)
             * @return
             */
            static bool      packCredential(CodecOperation mode, std::string& ssid, std::string& password, std::string& codecStr);
            static esp_err_t addOrUpdateToNVS(const char* ssid, const char* password);

        private:
            static constexpr const char* NVS_STORAGE_KEY = "WFC";
            // array of pointers to known active AP accepting connection, with valid rcredentials, sorted by strength of signal
            // the list ends at the first nullptr
            static const APCredential* activeSSIDs[maxTrackedSSIDs + 1];
            static APCredential        credentials[maxTrackedSSIDs];
            static size_t              count;
            /**
             * @brief retrieves the stored information for a given SSID
             * @param ssid
             * @return nullptr if not found
             */
            static const APCredential* retrieve(const char* ssid);

            /**
             * @brief loads from NVS wifi credential received after flashing firmware using the web interface.
             * the password will overwrite the one loaded from flashed firmware is SSID matches.
             * @return
             */
            static esp_err_t loadFromNVS();

            // number of SSID current registered at the credential manager
        };

        class WebInterfaace
        {
        private:
            WebInterfaace();

        public:
            static void init();

            static esp_err_t root_get_handler(httpd_req_t* req);
            static esp_err_t set_ap_post_handler(httpd_req_t* req);
            static esp_err_t httpd_resp_send_500(httpd_req_t* req);
        };
        explicit WiFiService() = delete; // meant to be used as singleton, no instances.
        ~WiFiService();

        static esp_err_t launch();
        /**
         * @brief allows subscription to the funtions to be called when the ESP got an IP
         * @param callback
         */
        static void subscribeToIPReady(std::function<void()> callback);

    private:
        static std::vector<std::function<void()>> ipReadyCallbacks;      // callback subscribers which need launching on IP ready
        static void                               runGotIPsubscribers(); // processes the list of method which subscribed the IP assigned event.
        static inline esp_netif_t*                sta_netif = nullptr;
        static char                               station_ID[18]; // the network host ID of the station
        static inline MacAddress                  station_mac;    // the MAC of the device
        // static char _SSID[2][ED_MAX_SSID_PWD_SIZE]; // instance value as we suppose there might be two station configurations, 4G and 5G to handle
        // static char _PWD[2][ED_MAX_SSID_PWD_SIZE];
        static const uint8_t ReconnectDelay_ms = 2000; // dealy before the next wifi reconnection session is launched
        /**
         * @brief initializes MAC and std station ID for wifi operations
         */
        static esp_err_t setHostName();
        /**
         * @brief retries to connect to Access Point after a period working as STA as a fallback for repeated failure to connect AP
         * @param arg
         */
        static void          retry_sta_mode_task(void* arg);
        static void          sta_retry_callback(TimerHandle_t xTimer);
        static void          init_sta_retry_timer();
        static TimerHandle_t staRetryTimer;
        static esp_err_t     wifi_conn_AP();
        static const char*   wifi_reason_to_string(uint8_t reason);
        static void          event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);
        // scans the available networks, check if they are matching the ones registered in APCredentialManager and updates their data of RSSI
        static void scan_wifi_networks();
        /**
         * @brief connecte to the next detected AP network.
         * notice that at every calls switches to the next network, until the list of available network is exhausted
         * @return
         */
        static esp_err_t     wifi_conn_STA();
        static int           s_retry_num;
        static TimerHandle_t staRetryDelayed;
        /**
         * @brief callback used to retry wifi connection after a set delay
         * @param xTimer
         */
        static void reconnectCallback(TimerHandle_t xTimer);
    };

} // end namespace